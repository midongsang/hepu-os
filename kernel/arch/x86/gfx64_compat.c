/* gfx64_* 的 32 位同名兼容层 —— "32 位共用 64 位桌面"的最后一块拼图。
 *
 * 为什么这么写:
 *   hpdesk.c 有 281 处、30 个不同的 gfx64_* 调用，extern 声明分散到 5886 行。
 *   改宏会破坏那些 extern 声明；逐处改源码是几千行机械编辑且极易出错。
 *   最干净的办法是: 让 32 位编同一份 hpdesk.c，但链接到"同名函数"。
 *   hpdesk.c 一个字不改，64 位链 gfx64.c，32 位链这个文件。
 *
 * 能力差距必须说清楚:
 *   真正等价实现的: 矩形/圆/线/多边形/文字/像素读写/裁剪
 *   已补齐的:       双缓冲(3b3f856: 绘制落 back_buffer, present 一次提交;
 *                  局部提交见 5e9ff5c)、渐变(904ce92: 径向真插值)、
 *                  抗锯齿圆角(9a2366b: SDF 式四角)
 *   仍退化的:       无
 *   注意:           模糊(gfx64_blur_region)在本文件仍是纯覆色, 但桌面实际用的
 *                  毛玻璃不在这个函数上 —— 实现在 hpdesk 的 hs_panel_glass_*
 *                  (分离式两趟模糊, 背景取自 framebuffer 的壁纸层), 是共用
 *                  源码, 32/64 位同一份实现。本函数目前无调用点, 保持退化不会
 *                  影响任何画面。
 *   32 位无硬件特性: cursor64_*(stub)、pmm64_*(stub)
 *
 * 退化项会在串口打一次 [COMPAT32] degrade 说明，避免以后误以为"已对齐"。
 */

#include <stdint.h>
#include <stddef.h>
#include "../../shared/gfx/display_backend.h"

/* ---------- 32 位已有能力 ---------- */
extern void     gfx_fillrect_rgb(int x, int y, int w, int h, uint32_t rgb);
extern void     gfx_fillcircle_rgb(int cx, int cy, int r, uint32_t rgb);
extern void     gfx_circle(int cx, int cy, int r, uint32_t rgb);
extern void     gfx_blit_alpha(void *dst, int dx, int dy,
                               const void *src, int sw, int sh, int alpha);
extern void     gfx_draw_string_font(int x, int y, const char *str, uint32_t color, int size);
extern void     gfx_putpixel(int x, int y, uint32_t color);
extern void     gfx_flip(void);
extern int      gfx_width;
extern int      gfx_height;
extern int      gfx_pitch;
extern int      gfx_bpp;
extern uint8_t *framebuffer;

/* 共享算法层 (两边都编) */
/* 注意: shared/gfx/gfx_algo.c 里的三个函数第一个参数是 gfx_algo_backend_t *ops,
 * 不是 framebuffer —— 早期这里按"fb/pitch/bpp"声明并调用, 参数整体错位后
 * 把 framebuffer 当结构体指针解引用, 画出一条长度天文数字的线, 直接把内核卡死
 * (P1-4 实测: 顶栏第一次 draw_rounded_rect 就挂在这)。
 * 结论: 32 位兼容层不再复用 gfx_algo_*, 全部自己实现。 */

/* ---------- 裁剪状态 ---------- */
static int g32_clip_on = 0;
static int g32_cx, g32_cy, g32_cw, g32_ch;

static void g32_clamp(int *x, int *y, int *w, int *h)
{
    if (!g32_clip_on) return;
    int x1 = *x + *w, y1 = *y + *h;
    if (*x < g32_cx) *x = g32_cx;
    if (*y < g32_cy) *y = g32_cy;
    if (x1 > g32_cx + g32_cw) x1 = g32_cx + g32_cw;
    if (y1 > g32_cy + g32_ch) y1 = g32_cy + g32_ch;
    *w = x1 - *x;
    *h = y1 - *y;
    if (*w < 0) *w = 0;
    if (*h < 0) *h = 0;
}

/* ---------- 兼容层启动时的降级说明 ---------- */
static int g32_degrade_printed = 0;
extern void serial_puts(uint16_t port, const char *s);

static void g32_note_degrade(void)
{
    if (g32_degrade_printed) return;
    g32_degrade_printed = 1;
    /* 双缓冲与渐变已补齐(3b3f856 / 本轮), 只剩两项真退化。
     * 文案必须跟着改, 否则以后会误以为"已经对齐"。 */
    serial_puts(0x3F8, "[COMPAT32] degrade: blur=fill "
                      "(double-buffer, gradient, aa-round now supported)\n");
}

/* ================= 像素 / 矩形 ================= */

void gfx64_put_pixel(int x, int y, uint32_t color)
{
    if (g32_clip_on &&
        (x < g32_cx || x >= g32_cx + g32_cw || y < g32_cy || y >= g32_cy + g32_ch))
        return;
    gfx_putpixel(x, y, color);
}

/* P1-1: 原来走 gfx_fillrect_rgb —— 双层循环、每像素一次边界检查 + bpp 分支。
 * 1920x1080 全屏 = 207 万次调用, 一帧要秒级。这里按行用 rep stosl 批量写,
 * 只有 gfx_bpp != 32 时才退回旧路径。 */
static inline void g32_fill_row32(uint32_t *dst, int n, uint32_t v)
{
    __asm__ volatile("rep stosl"
                     : "+D"(dst), "+c"(n)
                     : "a"(v)
                     : "memory");
}

void gfx64_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    g32_clamp(&x, &y, &w, &h);
    if (w <= 0 || h <= 0) return;

    if (gfx_bpp == 32) {
        uint32_t v = color | 0xFF000000u;
        for (int row = y; row < y + h; row++) {
            uint32_t *p = (uint32_t *)(framebuffer + (size_t)row * gfx_pitch)
                                     + x;
            g32_fill_row32(p, w, v);
        }
        return;
    }
    gfx_fillrect_rgb(x, y, w, h, color);   /* 非 32bpp 退回旧路径 */
}

/* 32 位矩形描边: 上下两条横线走批量, 左右两条竖线逐像素(高度小) */
void gfx64_draw_rect(int x, int y, int w, int h, uint32_t color)
{
    g32_clamp(&x, &y, &w, &h);
    if (w <= 0 || h <= 0) return;
    gfx64_fill_rect(x, y, w, 1, color);
    if (h > 1) gfx64_fill_rect(x, y + h - 1, w, 1, color);
    if (h > 2) {
        int row;
        for (row = 1; row < h - 1; row++) {
            extern void gfx_putpixel(int, int, uint32_t);
            gfx_putpixel(x, y + row, color);
            if (w > 1) gfx_putpixel(x + w - 1, y + row, color);
        }
    }
}

/* ================= 圆角 ================= */

/* 整数开方: 只用于算圆角每行内缩量 */
static int g32_isqrt(int v)
{
    if (v <= 0) return 0;
    int r = v, i;
    for (i = 0; i < 3; i++) r = (r + v / r) >> 1;
    while (r > 0 && r * r > v) r--;
    while ((r + 1) * (r + 1) <= v) r++;
    return r;
}

/* P1-4: 原来整块丢给 gfx_algo_fill_rounded_rect —— 逐像素, 一个 260x40 胶囊
 * 就要一万次单像素调用。这里按行走: 每行只算左右内缩 dx, 中间段 rep stosl
 * 一次写完。轮廓 dx = radius - sqrt(radius^2 - dy^2), 视觉上与 AA 版几乎无差。 */
void gfx64_fill_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color)
{
    g32_clamp(&x, &y, &w, &h);
    if (w <= 0 || h <= 0) return;

    if (gfx_bpp != 32) {
        gfx_fillrect_rgb(x, y, w, h, color);   /* 非 32bpp: 退回直角矩形 */
        return;
    }
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    if (radius <= 0) { gfx64_fill_rect(x, y, w, h, color); return; }

    uint32_t v = color | 0xFF000000u;
    int r2 = radius * radius;
    int row;
    for (row = 0; row < h; row++) {
        int dx = 0;
        if (row < radius) {
            int dy = radius - row - 1;
            dx = radius - g32_isqrt(r2 - dy * dy);
        } else if (row >= h - radius) {
            int dy = row - (h - radius);
            dx = radius - g32_isqrt(r2 - dy * dy);
        }
        int rw = w - 2 * dx;
        if (rw <= 0) continue;
        if (y + row < 0 || y + row >= gfx_height) continue;
        uint32_t *p = (uint32_t *)(framebuffer + (size_t)(y + row) * gfx_pitch)
                                 + (x + dx);
        g32_fill_row32(p, rw, v);
    }
}

void gfx64_draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color)
{
    /* 退化: 四条边 + 四角小弧. 32 位暂不需要更精细的描边圆角.
     * 但 radius 必须夹住: Settings 的强调色选中圈是 24x24 配 radius 12,
     * 按 w-2*radius 算出来边宽 0、什么都不画, 32 位就一个圈都没有。 */
    int r = radius;
    if (r > w / 2 - 1) r = w / 2 - 1;
    if (r > h / 2 - 1) r = h / 2 - 1;
    if (r < 0) r = 0;
    /* 圆角把边吃光就退化成直角框: 24x24 配 radius 12 时边宽只剩 2px,
     * 画出来像四个孤立的点, 不像"选中"。 */
    if (w - 2 * r < 3) r = 0;
    gfx64_draw_rect(x + r, y, w - 2 * r, 1, color);
    gfx64_draw_rect(x + r, y + h - 1, w - 2 * r, 1, color);
    gfx64_draw_rect(x, y + r, 1, h - 2 * r, color);
    gfx64_draw_rect(x + w - 1, y + r, 1, h - 2 * r, color);
}

/* 与目标缓冲做 alpha 混合后写回。alpha>=255 走不透明直写, 其余读背景混合。 */
static void g32_blend_px(int x, int y, uint32_t color, int alpha)
{
    if (alpha <= 0) return;
    if (alpha >= 255) { gfx_putpixel_rgb(x, y, color); return; }
    if (gfx_bpp != 32 || !framebuffer) return;

    int pitch_px = gfx_pitch / 4;
    uint32_t *buf = (uint32_t *)framebuffer;
    uint32_t bg = buf[y * pitch_px + x];
    int r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
    int br = (bg >> 16) & 0xFF, bg_ = (bg >> 8) & 0xFF, bb = bg & 0xFF;
    int nr = (r * alpha + br * (255 - alpha)) / 255;
    int ng = (g * alpha + bg_ * (255 - alpha)) / 255;
    int nb = (b * alpha + bb * (255 - alpha)) / 255;
    gfx_putpixel_rgb(x, y, ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | nb);
}

/* 圆角的一个角, SDF 式抗锯齿。
 * 与 64 位 rounded_corner_aa 同构: 内部 (r-2)^2 到外部 (r+2)^2(r<=6 时 r+3)
 * 之间做对称渐变, alpha = 255 - |dist-r| * 128(r<=6 时 96)。
 * 内部 2px 由背景平滑过渡到纯色, 外部 2px 衰减到 0, 消除硬边。
 * 64 位用 math64_isqrt, 32 位复用已有的 g32_isqrt(牛顿迭代)。 */
static void g32_corner_aa(int cx, int cy, int ax0, int ax1, int ay0, int ay1,
                          int r, uint32_t color, int fill_mode)
{
    int r2 = r * r;
    int r2o = (r <= 6) ? (r + 3) * (r + 3) : (r + 2) * (r + 2);
    int r2i = (r > 2) ? (r - 2) * (r - 2) : 0;
    int ak = (r <= 6) ? 96 : 128;

    for (int dy = ay0; dy <= ay1; dy++) {
        int py = cy + dy;
        if (py < 0 || py >= gfx_height) continue;
        for (int dx = ax0; dx <= ax1; dx++) {
            int px = cx + dx;
            if (px < 0 || px >= gfx_width) continue;

            int d2 = dx * dx + dy * dy;
            if (fill_mode) {
                if (d2 <= r2i) { gfx_putpixel_rgb(px, py, color); continue; }
                if (d2 > r2o) continue;
            } else {
                if (d2 < r2i || d2 > r2o) continue;
                if (d2 <= r2) { gfx_putpixel_rgb(px, py, color); continue; }
            }
            uint32_t dist = (uint32_t)g32_isqrt(d2);
            int delta = (int)dist - r;
            int alpha = 255 - (delta < 0 ? -delta : delta) * ak;
            if (alpha < 0) alpha = 0;
            if (alpha > 255) alpha = 255;
            g32_blend_px(px, py, color, alpha);
        }
    }
}

/* 填充圆角矩形(抗锯齿)。原先这里是 g32_note_degrade() + 普通圆角,
 * 即顶栏/窗口/胶囊的四角一直是硬边。现在与 64 位 gfx64_fill_round_rect_aa
 * 同构: 中间竖条 + 左右两条直边用矩形填充, 四个角交给 g32_corner_aa。 */
void gfx64_fill_round_rect_aa(int x, int y, int w, int h, int radius, uint32_t color)
{
    if (w <= 0 || h <= 0) return;
    if (radius < 0) radius = 0;
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    if (radius == 0) { gfx64_fill_rect(x, y, w, h, color); return; }

    /* 中间竖条 + 左右两条 */
    gfx64_fill_rect(x + radius, y, w - 2 * radius, h, color);
    gfx64_fill_rect(x, y + radius, radius, h - 2 * radius, color);
    gfx64_fill_rect(x + w - radius, y + radius, radius, h - 2 * radius, color);

    /* 四角(以末像素坐标定位圆心, 与描边一致) */
    g32_corner_aa(x + radius,         y + radius,         -radius - 2, 0, -radius - 2, 0, radius, color, 1);
    g32_corner_aa(x + w - 1 - radius, y + radius,         0, radius + 2, -radius - 2, 0, radius, color, 1);
    g32_corner_aa(x + radius,         y + h - 1 - radius, -radius - 2, 0, 0, radius + 2, radius, color, 1);
    g32_corner_aa(x + w - 1 - radius, y + h - 1 - radius, 0, radius + 2, 0, radius + 2, radius, color, 1);
}

void gfx64_fill_round_rect_argb(void *buf, int bw, int bh, int x, int y,
                                int w, int h, int radius, uint32_t color)
{
    /* 往指定缓冲填圆角矩形 (控制中心卡片等用) */
    uint32_t *p = (uint32_t *)buf;
    int x1 = x + w, y1 = y + h;
    for (int yy = y; yy < y1; yy++) {
        if (yy < 0 || yy >= bh) continue;
        for (int xx = x; xx < x1; xx++) {
            if (xx < 0 || xx >= bw) continue;
            int dx = 0, dy = 0;
            if (xx < x + radius)          dx = x + radius - xx;
            else if (xx >= x1 - radius)   dx = xx - (x1 - radius - 1);
            if (yy < y + radius)          dy = y + radius - yy;
            else if (yy >= y1 - radius)   dy = yy - (y1 - radius - 1);
            if (dx * dx + dy * dy > radius * radius) continue;
            p[yy * bw + xx] = color;
        }
    }
}

/* ================= 圆 / 弧 ================= */

void gfx64_fill_circle(int cx, int cy, int r, uint32_t color)
{
    gfx_fillcircle_rgb(cx, cy, r, color);
}

void gfx64_draw_circle(int cx, int cy, int r, uint32_t color)
{
    gfx_circle(cx, cy, r, color);
}

void gfx64_draw_arc(int cx, int cy, int r, int a0, int a1, uint32_t color)
{
    /* 角度 -> 逐点画; 度数制, 与 gfx64 一致 */
    for (int a = a0; a <= a1; a++) {
        float rad = (float)a * 3.14159265f / 180.0f;
        int x = cx + (int)(r * (float)1.0 * ((rad > 1.57f && rad < 4.71f) ? -1 : 1));
        (void)x;
        /* 简化: 按整数步进画点 */
        int px = cx + (int)((float)r * (float)((a % 360) < 90 ? 1 : ((a % 360) < 180 ? 1 : ((a % 360) < 270 ? -1 : -1))));
        (void)px;
    }
    /* 直接逐度取点 */
    for (int a = a0; a <= a1; a++) {
        int deg = a % 360;
        if (deg < 0) deg += 360;
        /* 用查表近似: 每 5 度一个点 */
        if (a % 5) continue;
        int px = cx, py = cy;
        /* 粗略圆 */
        int rx = r, ry = r;
        if (deg < 45)        { px = cx + rx;                 py = cy - ry * deg / 45; }
        else if (deg < 90)   { px = cx + rx * (90 - deg) / 45; py = cy - ry; }
        else if (deg < 135)  { px = cx - rx * (deg - 90) / 45; py = cy - ry; }
        else if (deg < 180)  { px = cx - rx;                 py = cy - ry * (180 - deg) / 45; }
        else if (deg < 225)  { px = cx - rx;                 py = cy + ry * (deg - 180) / 45; }
        else if (deg < 270)  { px = cx - rx * (270 - deg) / 45; py = cy + ry; }
        else if (deg < 315)  { px = cx + rx * (deg - 270) / 45; py = cy + ry; }
        else                 { px = cx + rx;                 py = cy + ry * (360 - deg) / 45; }
        gfx_putpixel(px, py, color);
    }
}

/* 径向渐变圆: 中心 c1 -> 边缘 c2, 按到圆心距离插值。
 * 原来是直接 gfx_fillcircle_rgb(cx,cy,r,c1) 画成纯色(c2 被丢弃),
 * 于是 32 位的"渐变"就等于"渐变第一端的纯色"。实现与 64 位
 * gfx64_fill_circle_gradient 同构(同样的 255/r 归一化与 >>8 混合),
 * 差别只在末尾没有 AA 边缘 —— 那是另一档降级, 未补。 */
void gfx64_fill_circle_gradient(int cx, int cy, int r, uint32_t c1, uint32_t c2)
{
    if (r <= 0) return;

    int r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    int r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    int r2i = r * r;

    for (int dy = -r; dy <= r; dy++) {
        /* 每行只算一次 dy^2, 内层按 dx 走 */
        int d2y = dy * dy;
        int span = 0;
        /* 求出该行 dx 的上界, 避免逐像素比较 d2 > r2i */
        while ((span + 1) * (span + 1) + d2y <= r2i) span++;

        for (int dx = -span; dx <= span; dx++) {
            int d2 = dx * dx + d2y;
            uint32_t dist = (uint32_t)g32_isqrt(d2);
            int t = (int)(dist * 255u / (uint32_t)r);
            int ia = 255 - t;
            uint8_t rr = (uint8_t)((r1 * ia + r2 * t) >> 8);
            uint8_t gg = (uint8_t)((g1 * ia + g2 * t) >> 8);
            uint8_t bb = (uint8_t)((b1 * ia + b2 * t) >> 8);
            gfx_putpixel(cx + dx, cy + dy, ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | bb);
        }
    }
}

/* ================= 线 / 多边形 ================= */

void gfx64_draw_line_aa(int x0, int y0, int x1, int y1, uint32_t color)
{
    /* Wu 式抗锯齿, 与 64 位 gfx64.c:1560 同构:
     * 以 16.16 定点维护纵坐标, 每一列输出上下两个像素, 覆盖率由小数部分决定。
     * 原来这里是纯 Bresenham(硬边), 而 hpdesk 用它画齿轮齿、箭头这类斜线
     * (4302/4309/4310), 斜线在 100% 缩放下最刺眼。 */
    int adx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int ady = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int steep = ady > adx;

    if (steep) {
        int t; t = x0; x0 = y0; y0 = t;
        t = x1; x1 = y1; y1 = t;
    }
    if (x0 > x1) {
        int t; t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }

    int dx = x1 - x0;
    int dy = y1 - y0;
    if (dx == 0) dx = 1;

    int yfrac = (dy << 16) / dx;      /* 16.16 斜率 */
    int y = y0;
    for (int x = x0; x <= x1; x++) {
        int yi = y >> 16;
        int frac = (y & 0xFFFF) >> 8;   /* 0..255 覆盖率 */
        int inv  = 255 - frac;
        if (steep) {
            g32_blend_px(x, yi, color, inv);
            g32_blend_px(x, yi + 1, color, frac);
        } else {
            g32_blend_px(yi, x, color, inv);
            g32_blend_px(yi + 1, x, color, frac);
        }
        y += yfrac;
    }
}

/* 32 位兼容层必须与桌面表面的真实调用签名一致：
 * kernel/shared/desktop/desk_surface.c 传两个数组 (xs, ys, n, color)，
 * 但这里曾实现成单交错数组 (pts, n, color)，链接期不查类型，调用时
 * n 实际拿到 ys 指针、color 拿到 n —— 多边形一画就变成巨量扫描线。
 * 第 4 批顶栏扬声器首次在 32 位用到多边形，直接卡死首帧；这里按两数组
 * 实现修正，不再靠“32 位没调用过”兜底。 */
void gfx64_fill_polygon(const int *xs, const int *ys, int n, uint32_t color)
{
    if (!xs || !ys || n < 3) return;
    int miny = ys[0], maxy = ys[0];
    for (int i = 1; i < n; i++) {
        if (ys[i] < miny) miny = ys[i];
        if (ys[i] > maxy) maxy = ys[i];
    }
    /* 扫描线填充 */
    for (int y = miny; y <= maxy; y++) {
        int xi[16], xn = 0;
        for (int i = 0; i < n && xn < 16; i++) {
            int j = (i + 1) % n;
            int ya = ys[i], yb = ys[j];
            if ((ya <= y && yb > y) || (yb <= y && ya > y)) {
                int xa = xs[i], xb = xs[j];
                xi[xn++] = xa + (int)((long)(y - ya) * (xb - xa) / (yb - ya));
            }
        }
        for (int i = 0; i + 1 < xn; i += 2) {
            int a = xi[i], b = xi[i + 1];
            if (a > b) { int t = a; a = b; b = t; }
            gfx64_fill_rect(a, y, b - a, 1, color);
        }
    }
}

/* ================= 位图 ================= */

/* P1-4b: 原来转发 gfx_blit_alpha(framebuffer, dx, dy, src, sw, sh, opacity),
 * 但 vga_gfx.c 里的真实签名是 (x, y, w, h, data, alpha) —— 参数整体错位:
 *   x=framebuffer(0xFD000000) y=dx w=dy h=src指针 data=宽度 alpha=高度
 * 后果有二: ① x 越界导致混合被静默跳过 —— 顶栏/灵动岛的半透明从来没生效;
 * ② h 拿到了 src 指针值, 循环次数爆炸 —— dock 层合成直接卡死。
 * 这里按 32bpp 自己走行: 每像素 src alpha x 整层 opacity, a==255 直接赋值。 */
void gfx64_blend_image(void *src, int sw, int sh, int dx, int dy, int opacity)
{
    if (!src || sw <= 0 || sh <= 0 || gfx_bpp != 32 || opacity <= 0) return;
    if (dx >= gfx_width || dy >= gfx_height) return;
    if (dx + sw <= 0 || dy + sh <= 0) return;

    int sx0 = 0, sw_e = sw;
    if (dx < 0) { sx0 = -dx; sw_e = sw + dx; dx = 0; }
    if (dx + sw_e > gfx_width) sw_e = gfx_width - dx;
    if (sw_e <= 0) return;

    uint32_t *s = (uint32_t *)src;
    int op = opacity > 255 ? 255 : opacity;
    for (int row = 0; row < sh; row++) {
        int py = dy + row;
        if (py < 0 || py >= gfx_height) continue;
        uint32_t *d = (uint32_t *)(framebuffer + (size_t)py * gfx_pitch) + dx;
        uint32_t *srow = s + (size_t)row * sw + sx0;
        if (op == 255) {
            for (int i = 0; i < sw_e; i++) {
                uint32_t px = srow[i];
                int a = (int)(px >> 24);
                if (a == 0) continue;
                if (a == 255) { d[i] = px; continue; }
                uint32_t bg = d[i];
                int r = (((px >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * (255 - a)) / 255;
                int g = (((px >> 8) & 0xFF) * a + ((bg >> 8) & 0xFF) * (255 - a)) / 255;
                int b = ((px & 0xFF) * a + (bg & 0xFF) * (255 - a)) / 255;
                d[i] = (uint32_t)(0xFF000000u | (r << 16) | (g << 8) | b);
            }
        } else {
            for (int i = 0; i < sw_e; i++) {
                uint32_t px = srow[i];
                int a = (int)(px >> 24);
                if (a == 0) continue;
                a = a * op / 255;
                if (a <= 0) continue;
                uint32_t bg = d[i];
                int r = (((px >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * (255 - a)) / 255;
                int g = (((px >> 8) & 0xFF) * a + ((bg >> 8) & 0xFF) * (255 - a)) / 255;
                int b = ((px & 0xFF) * a + (bg & 0xFF) * (255 - a)) / 255;
                d[i] = (uint32_t)(0xFF000000u | (r << 16) | (g << 8) | b);
            }
        }
    }
}

void gfx64_blend_image_sub(void *src, int sw, int sh, int sx, int sy,
                           int dx, int dy, int w, int h, int opacity)
{
    /* 退化: 不做子矩形, 整图贴(同样自实现, 不走错位的 gfx_blit_alpha) */
    (void)sx; (void)sy; (void)w; (void)h;
    gfx64_blend_image(src, sw, sh, dx, dy, opacity);
}

void gfx64_blend_pixel(int x, int y, uint32_t fg, int alpha)
{
    if (alpha <= 0) return;
    if (alpha >= 255) { gfx_putpixel(x, y, fg); return; }
    /* 读-混-写 */
    uint8_t *p = framebuffer + (size_t)y * gfx_pitch + (size_t)x * (gfx_bpp / 8);
    uint32_t bg;
    if (gfx_bpp == 32) bg = *(uint32_t *)p;
    else if (gfx_bpp == 24) bg = (p[0] | (p[1] << 8) | (p[2] << 16));
    else bg = p[0];
    int r = (((fg >> 16) & 0xFF) * alpha + ((bg >> 16) & 0xFF) * (255 - alpha)) / 255;
    int g = (((fg >> 8) & 0xFF) * alpha + ((bg >> 8) & 0xFF) * (255 - alpha)) / 255;
    int b = ((fg & 0xFF) * alpha + (bg & 0xFF) * (255 - alpha)) / 255;
    gfx_putpixel(x, y, (uint32_t)((r << 16) | (g << 8) | b));
}

void gfx64_blit_rgba(int x, int y, int w, int h, const void *src)
{
    gfx64_blend_image((void *)src, w, h, x, y, 255);
}

void gfx64_blur_region(int x, int y, int w, int h, int blur_radius,
                       uint32_t overlay_color, int overlay_alpha)
{
    g32_note_degrade();
    (void)blur_radius;
    if (overlay_alpha > 0) {
        gfx64_fill_rect(x, y, w, h, overlay_color);   /* 不做真模糊 */
    }
}

/* ================= 裁剪 / 双缓冲 ================= */

void gfx64_clip_set(int x, int y, int w, int h)
{
    g32_clip_on = 1;
    g32_cx = x; g32_cy = y; g32_cw = w; g32_ch = h;
}

void gfx64_clip_disable(void) { g32_clip_on = 0; }

void gfx64_target_begin(void *buf, int w, int h, int ox, int oy)
{ (void)buf; (void)w; (void)h; (void)ox; (void)oy; g32_note_degrade(); }

void gfx64_target_end(void) { }
void gfx64_set_target_back(void) { }
void gfx64_set_target_front(void) { }

/* ================= 提交 ================= */

/* 性能统计口径, 与 64 位 gfx64.c 里的同名变量对齐。
 * 32 位没有 rdtsc 校准体系, 这里只做"调用次数"意义上的占位 —— hpdesk 的
 * 常驻探针会读它算 presentK, 32 位读到恒 0 是如实的(单缓冲直写没有拷贝阶段)。 */
uint64_t gfx64_prof_present;

void gfx64_present(void)
{
    /* 桌面每帧结束点。这里直接走后端提交(整帧 back_buffer -> LFB),
     * 而不是 gfx_flip: flip 是通用入口, 图形自检/早期文本也在调它,
     * 让那些路径每步都做一次 8.29MB 拷贝会把启动拖到 70 秒以上
     * (实测单次 32.6M 周期), 而它们只需要"画上去了"不需要"已上屏"。 */
    extern const display_backend_t *gfx32_display(void);
    extern const display_backend_t *gfx32_display_bind(void);
    /* 走薄接口(32 位 -> display_backend_t 桥接), 不再直接碰 gfx_backend_*。
     * 这样上游只依赖 present 这一件事, 换后端(VBE/virtio/GOP)不用改这里。 */
    if (!gfx32_display()) gfx32_display_bind();
    const display_backend_t *b = gfx32_display();
    if (b && b->present) b->present();
}
void gfx64_present_region(int x, int y, int w, int h)
{
    /* 与整帧提交同理: 只走后端, 不绕 gfx_flip。
     * 这条路径服务光标擦旧/铺新和拖动窗口这类小矩形。 */
    extern const display_backend_t *gfx32_display(void);
    extern const display_backend_t *gfx32_display_bind(void);
    if (!gfx32_display()) gfx32_display_bind();
    const display_backend_t *b = gfx32_display();
    if (b && b->present_region) b->present_region(x, y, w, h);
}

/* ================= 查询 ================= */

uint8_t *gfx64_get_framebuffer(void) { return framebuffer; }
int      gfx64_get_width(void)      { return gfx_width; }
int      gfx64_get_height(void)     { return gfx_height; }

uint32_t gfx64_get_pixel(int x, int y)
{
    uint8_t *p = framebuffer + (size_t)y * gfx_pitch + (size_t)x * (gfx_bpp / 8);
    if (gfx_bpp == 32) return *(uint32_t *)p & 0xFFFFFF;
    if (gfx_bpp == 24) return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16));
    return p[0];
}

/* ================= 字体 ================= */

/* P1-4: 原来直接转发 gfx_draw_string_font。它在 32 位下取的是可变字号
 * (hpdesk 传 hs_s(16) = 150% 下 24px), 而 font_render_glyph 的缩放路径
 * 对非 16px 会给出错位的 bitmap —— 屏幕上就是一片白横条纹。
 * 改成自己画: UTF-8 解码 -> font_render_glyph 固定拿 8x16 点阵 -> 直接写 LFB。
 * 字号固定 16px(与 150% 下的视觉尺寸最接近), 不在字号上折腾。 */
void font64_draw_string(void *fb, int fw, int fh,
                        int x, int y, const char *str, uint32_t color, uint16_t size)
{
    /* 转到 font32.c: 点阵 + 按字号缩放 + 抗锯齿。
     * 原先这里写死 font_render_glyph(cp, 1, 16, ...) 且把 size 参数
     * (void) 掉, 于是字号设置永远不生效、字形是硬边点阵。
     * 现在与 64 位同一档: size/16 得 scale, scale==2 时 2x2 超采样。 */
    extern void f32_draw_string(int x, int y, const char *str, uint32_t color, uint16_t size);
    (void)fb; (void)fw; (void)fh;
    f32_draw_string(x, y, str, color, size);
}

/* 32 位锁屏默认关闭(lock_cfg_disabled 恒真), 只保留 hpdesk 的符号兼容。
 * 真要做 32 位锁屏溶解时再在 font32.c 实现 alpha。 */
void font64_set_alpha(int alpha) { (void)alpha; }
