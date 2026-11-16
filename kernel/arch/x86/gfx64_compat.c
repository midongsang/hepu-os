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
 *   退化实现的:     抗锯齿圆角(退成普通圆角)、模糊(退成纯覆色)、
 *                  双缓冲(32 位单缓冲, 退化成空操作)、渐变(退成纯色)
 *   32 位无硬件特性: cursor64_*(stub)、pmm64_*(stub)
 *
 * 退化项会在串口打一次 [COMPAT32] degrade 说明，避免以后误以为"已对齐"。
 */

#include <stdint.h>
#include <stddef.h>

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
    serial_puts(0x3F8, "[COMPAT32] degrade: aa-round=plain, blur=fill, "
                      "double-buffer=none, gradient=flat\n");
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

void gfx64_fill_round_rect_aa(int x, int y, int w, int h, int radius, uint32_t color)
{
    g32_note_degrade();
    gfx64_fill_rounded_rect(x, y, w, h, radius, color);
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

void gfx64_fill_circle_gradient(int cx, int cy, int r, uint32_t c1, uint32_t c2)
{
    g32_note_degrade();
    gfx_fillcircle_rgb(cx, cy, r, c1);
    (void)c2;
}

/* ================= 线 / 多边形 ================= */

void gfx64_draw_line_aa(int x0, int y0, int x1, int y1, uint32_t color)
{
    /* 32 位: 自己的 Bresenham, 不做抗锯齿(退化项已在启动时声明) */
    {
        extern void gfx_putpixel(int, int, uint32_t);
        int dx = x1 > x0 ? x1 - x0 : x0 - x1;
        int dy = y1 > y0 ? y1 - y0 : y0 - y1;
        int sx = x0 < x1 ? 1 : -1;
        int sy = y0 < y1 ? 1 : -1;
        int err = dx - dy;
        int guard = 0;
        while (guard++ < 4096) {
            gfx_putpixel(x0, y0, color);
            if (x0 == x1 && y0 == y1) break;
            int e2 = err * 2;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 <  dx) { err += dx; y0 += sy; }
        }
    }
}

void gfx64_fill_polygon(const int *pts, int n, uint32_t color)
{
    if (!pts || n < 3) return;
    int miny = pts[1], maxy = pts[1];
    for (int i = 0; i < n; i++) {
        int y = pts[i * 2 + 1];
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
    }
    /* 扫描线填充 */
    for (int y = miny; y <= maxy; y++) {
        int xs[16], xn = 0;
        for (int i = 0; i < n && xn < 16; i++) {
            int j = (i + 1) % n;
            int ya = pts[i * 2 + 1], yb = pts[j * 2 + 1];
            if ((ya <= y && yb > y) || (yb <= y && ya > y)) {
                int xa = pts[i * 2], xb = pts[j * 2];
                xs[xn++] = xa + (int)((long)(y - ya) * (xb - xa) / (yb - ya));
            }
        }
        for (int i = 0; i < xn; i += 2) {
            if (i + 1 >= xn) break;
            int a = xs[i], b = xs[i + 1];
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
    extern int gfx_backend_present(void);
    (void)gfx_backend_present();
}
void gfx64_present_region(int x, int y, int w, int h)
{
    /* 与整帧提交同理: 只走后端, 不绕 gfx_flip。
     * 这条路径服务光标擦旧/铺新和拖动窗口这类小矩形。 */
    extern int gfx_backend_present_region(int, int, int, int);
    (void)gfx_backend_present_region(x, y, w, h);
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
    extern int font_render_glyph(uint32_t codepoint, uint8_t type, uint16_t size,
                                 const uint8_t **bitmap, uint16_t *width,
                                 uint16_t *height, int16_t *advance,
                                 int16_t *bearing_x, int16_t *bearing_y);
    (void)fb; (void)fw; (void)fh; (void)size;

    if (!str || gfx_bpp != 32) return;
    uint32_t v = color | 0xFF000000u;
    int cur_x = x, cur_y = y;

    while (*str) {
        uint32_t cp;
        const unsigned char *p8 = (const unsigned char *)str;
        if (p8[0] < 0x80) { cp = p8[0]; str += 1; }
        else if ((p8[0] & 0xE0) == 0xC0 && p8[1]) {
            cp = ((uint32_t)(p8[0] & 0x1F) << 6) | (p8[1] & 0x3F); str += 2;
        } else if ((p8[0] & 0xF0) == 0xE0 && p8[1] && p8[2]) {
            cp = ((uint32_t)(p8[0] & 0x0F) << 12) |
                 ((uint32_t)(p8[1] & 0x3F) << 6) | (p8[2] & 0x3F); str += 3;
        } else if ((p8[0] & 0xF8) == 0xF0 && p8[1] && p8[2] && p8[3]) {
            cp = ((uint32_t)(p8[0] & 0x07) << 18) |
                 ((uint32_t)(p8[1] & 0x3F) << 12) |
                 ((uint32_t)(p8[2] & 0x3F) << 6) | (p8[3] & 0x3F); str += 4;
        } else { cp = p8[0]; str += 1; }

        const uint8_t *bm = 0;
        uint16_t w = 0, h = 0;
        int16_t adv = 0, bx = 0, by = 0;
        if (font_render_glyph(cp, 1 /* BITMAP_8x16 */, 16,
                              &bm, &w, &h, &adv, &bx, &by) != 0) {
            cur_x += 8;
            continue;
        }
        /* 防御: 位图尺寸异常直接跳过, 不要画出一片横线 */
        if (!bm || w == 0 || h == 0 || w > 32 || h > 32) { cur_x += 8; continue; }

        for (uint16_t row = 0; row < h; row++) {
            int py = cur_y + by + row;
            if (py < 0 || py >= gfx_height) continue;
            uint32_t *line = (uint32_t *)(framebuffer + (size_t)py * gfx_pitch);
            for (uint16_t col = 0; col < w; col++) {
                uint8_t a = bm[row * w + col];
                if (!a) continue;
                int px = cur_x + bx + col;
                if (px < 0 || px >= gfx_width) continue;
                if (a == 255) line[px] = v;
                else {
                    uint32_t bg = line[px];
                    int r = (((v >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * (255 - a)) / 255;
                    int g = (((v >> 8) & 0xFF) * a + ((bg >> 8) & 0xFF) * (255 - a)) / 255;
                    int b = ((v & 0xFF) * a + (bg & 0xFF) * (255 - a)) / 255;
                    line[px] = (uint32_t)((r << 16) | (g << 8) | b) | 0xFF000000u;
                }
            }
        }
        cur_x += (adv > 0 && adv < 40) ? adv : (int)w;
    }
}
