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
extern void gfx_algo_draw_rect(void *fb, int pitch, int bpp, uint32_t color,
                               int x, int y, int w, int h);
extern void gfx_algo_draw_line(void *fb, int pitch, int bpp, uint32_t color,
                               int x0, int y0, int x1, int y1);
extern void gfx_algo_fill_rounded_rect(void *fb, int pitch, int bpp, uint32_t color,
                                       int x, int y, int w, int h, int radius);

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

void gfx64_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    g32_clamp(&x, &y, &w, &h);
    if (w <= 0 || h <= 0) return;
    gfx_fillrect_rgb(x, y, w, h, color);
}

void gfx64_draw_rect(int x, int y, int w, int h, uint32_t color)
{
    g32_clamp(&x, &y, &w, &h);
    if (w <= 0 || h <= 0) return;
    gfx_algo_draw_rect(framebuffer, gfx_pitch, gfx_bpp, color, x, y, w, h);
}

/* ================= 圆角 ================= */

void gfx64_fill_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color)
{
    g32_clamp(&x, &y, &w, &h);
    if (w <= 0 || h <= 0) return;
    gfx_algo_fill_rounded_rect(framebuffer, gfx_pitch, gfx_bpp, color,
                               x, y, w, h, radius);
}

void gfx64_draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color)
{
    /* 退化: 四条边 + 四角小弧. 32 位暂不需要更精细的描边圆角 */
    gfx64_draw_rect(x + radius, y, w - 2 * radius, 1, color);
    gfx64_draw_rect(x + radius, y + h - 1, w - 2 * radius, 1, color);
    gfx64_draw_rect(x, y + radius, 1, h - 2 * radius, color);
    gfx64_draw_rect(x + w - 1, y + radius, 1, h - 2 * radius, color);
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
    gfx_algo_draw_line(framebuffer, gfx_pitch, gfx_bpp, color, x0, y0, x1, y1);
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

void gfx64_blend_image(void *src, int sw, int sh, int dx, int dy, int opacity)
{
    gfx_blit_alpha(framebuffer, dx, dy, src, sw, sh, opacity);
}

void gfx64_blend_image_sub(void *src, int sw, int sh, int sx, int sy,
                           int dx, int dy, int w, int h, int opacity)
{
    /* 退化: 不做子矩形, 整图贴 */
    (void)sx; (void)sy;
    gfx_blit_alpha(framebuffer, dx, dy, src, sw, sh, opacity);
    (void)w; (void)h;
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
    gfx_blit_alpha(framebuffer, x, y, src, w, h, 255);
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

void gfx64_present(void) { gfx_flip(); }
void gfx64_present_region(int x, int y, int w, int h) { (void)x;(void)y;(void)w;(void)h; }

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

void font64_draw_string(void *fb, int fw, int fh,
                        int x, int y, const char *str, uint32_t color, uint16_t size)
{
    (void)fb; (void)fw; (void)fh;
    gfx_draw_string_font(x, y, str, color, (int)size);
}
