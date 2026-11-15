/* ============================================================================
 * desk_surface.c —— 把后端图元包装成桌面接口的绘制表面
 *
 * 分层:
 *   桌面实现 (desk_backend) 只看到 desk_surface_t 的函数指针
 *          ↓
 *   desk_surface.c (本文件) 把指针接到 gfx64_* / font64_*
 *          ↓
 *   gfx64.c (64 位)  |  gfx64_compat*.c (32 位)    —— 唯一知道像素怎么落下去的地方
 *
 * 放在 shared/ 里是因为 gfx64_* 在 32/64 两边同名同签名 (32 位由 compat 提供),
 * 所以这一层不需要任何架构分支。改动这一层两边同时生效。
 *
 * 硬判据(GRAPHICS_BOUNDARY.md): 本文件不得出现 outb/inb/端口/物理地址 —— 它只
 * 调抽象图元, 不碰硬件。
 * ========================================================================== */

#include "desk_api.h"

/* --- 后端原语(32/64 同名) ------------------------------------------ */
extern void gfx64_put_pixel(int x, int y, uint32_t color);
extern void gfx64_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void gfx64_draw_rect(int x, int y, int w, int h, uint32_t color);
extern void gfx64_fill_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
extern void gfx64_draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
extern void gfx64_fill_circle(int cx, int cy, int r, uint32_t color);
extern void gfx64_draw_line_aa(int x0, int y0, int x1, int y1, uint32_t color);
extern void gfx64_fill_polygon(const int *xs, const int *ys, int n, uint32_t color);
extern void gfx64_blend_image(void *src, int sw, int sh, int dx, int dy, int opacity);
extern void gfx64_present(void);
extern uint8_t *gfx64_get_framebuffer(void);
extern void font64_draw_string(void *fb, int fw, int fh, int x, int y,
                               const char *str, uint32_t color, uint16_t size);
extern int font64_get_string_width(const char *str, uint16_t size);

extern void gfx64_get_info(int *width, int *height, int *bpp);

/* --- 包装 ----------------------------------------------------------- */

static void s_put_pixel(desk_surface_t *s, int x, int y, uint32_t rgb)
{
    (void)s; gfx64_put_pixel(x, y, rgb);
}

static void s_fill_rect(desk_surface_t *s, int x, int y, int w, int h, uint32_t rgb)
{
    (void)s; gfx64_fill_rect(x, y, w, h, rgb);
}

static void s_draw_rect(desk_surface_t *s, int x, int y, int w, int h, uint32_t rgb)
{
    (void)s; gfx64_draw_rect(x, y, w, h, rgb);
}

static void s_fill_rounded_rect(desk_surface_t *s, int x, int y, int w, int h,
                                int r, uint32_t rgb)
{
    (void)s; gfx64_fill_rounded_rect(x, y, w, h, r, rgb);
}

static void s_draw_rounded_rect(desk_surface_t *s, int x, int y, int w, int h,
                                int r, uint32_t rgb)
{
    (void)s; gfx64_draw_rounded_rect(x, y, w, h, r, rgb);
}

static void s_fill_circle(desk_surface_t *s, int cx, int cy, int r, uint32_t rgb)
{
    (void)s; gfx64_fill_circle(cx, cy, r, rgb);
}

static void s_draw_line_aa(desk_surface_t *s, int x0, int y0, int x1, int y1, uint32_t rgb)
{
    (void)s; gfx64_draw_line_aa(x0, y0, x1, y1, rgb);
}

/* 接口给的是交错数组 pts[2*i], pts[2*i+1]; 后端要两个数组。
 * 多边形顶点数实际很小(箭头 3-4 点, 角标 3 点), 32 上限足够。 */
static void s_fill_polygon(desk_surface_t *s, const int *pts, int n, uint32_t rgb)
{
    int xs[32], ys[32];
    (void)s;
    if (n <= 0) return;
    if (n > 32) n = 32;
    for (int i = 0; i < n; i++) {
        xs[i] = pts[i * 2];
        ys[i] = pts[i * 2 + 1];
    }
    gfx64_fill_polygon(xs, ys, n, rgb);
}

static void s_blend_image(desk_surface_t *s, int x, int y,
                          const uint8_t *src, int sw, int sh, int alpha)
{
    (void)s;
    gfx64_blend_image((void *)src, sw, sh, x, y, alpha);
}

static void s_draw_text(desk_surface_t *s, int x, int y, const char *utf8,
                        uint32_t rgb, int px)
{
    font64_draw_string(s->fb, s->width, s->height, x, y, utf8, rgb, (uint16_t)px);
}

static int s_text_width(desk_surface_t *s, const char *utf8, int px)
{
    (void)s;
    return font64_get_string_width(utf8, (uint16_t)px);
}

static void s_present(desk_surface_t *s)
{
    (void)s; gfx64_present();
}

/* --- 取一个填好的表面 ----------------------------------------------- */

/* 只在第一次调用时读 gfx_width/height 这类全局; 之后一直复用同一份。
 * 分辨率在运行中不再变(Display 页切换分辨率会走重启), 所以不必每次重读。 */
int desk_surface_get(desk_surface_t *s, uint32_t scale_pct)
{
    if (!s) return -1;

    int w = 0, h = 0, bpp = 0;
    gfx64_get_info(&w, &h, &bpp);
    s->fb        = gfx64_get_framebuffer();
    s->width     = w;
    s->height    = h;
    s->bpp       = bpp;
    s->pitch     = w * (bpp / 8);
    s->scale_pct = scale_pct;
    s->ctx       = 0;

    s->put_pixel          = s_put_pixel;
    s->fill_rect          = s_fill_rect;
    s->draw_rect          = s_draw_rect;
    s->fill_rounded_rect  = s_fill_rounded_rect;
    s->draw_rounded_rect  = s_draw_rounded_rect;
    s->fill_circle        = s_fill_circle;
    s->draw_line_aa       = s_draw_line_aa;
    s->fill_polygon       = s_fill_polygon;
    s->blend_image        = s_blend_image;
    s->draw_text          = s_draw_text;
    s->text_width         = s_text_width;
    s->present            = s_present;

    return (s->fb && s->width > 0 && s->height > 0) ? 0 : -1;
}
