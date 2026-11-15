/* 64 位后端: 把 gfx64_* 装进 g_desk_ops。
 *
 * 这张表是"32 位共用 64 位桌面"的接线板 —— 桌面代码调用 g_desk_ops->fill_rect(),
 * 64 位这边指到已验证的 gfx64_fill_rect(), 32 位那边指到自己的实现。
 * 桌面源码一份, 后端两套。 */

#include "../../shared/desktop/desk_gfx_ops.h"

/* gfx64 侧声明 (与 gfx64.c 签名逐字一致) */
extern void     gfx64_put_pixel(int x, int y, uint32_t color);
extern void     gfx64_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void     gfx64_draw_rect(int x, int y, int w, int h, uint32_t color);
extern void     gfx64_fill_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
extern void     gfx64_draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
extern void     gfx64_fill_round_rect_aa(int x, int y, int w, int h, int radius, uint32_t color);
extern void     gfx64_fill_round_rect_argb(void *buf, int bw, int bh, int x, int y,
                                           int w, int h, int radius, uint32_t color);
extern void     gfx64_fill_circle(int cx, int cy, int r, uint32_t color);
extern void     gfx64_draw_circle(int cx, int cy, int r, uint32_t color);
extern void     gfx64_draw_arc(int cx, int cy, int r, int a0, int a1, uint32_t color);
extern void     gfx64_fill_circle_gradient(int cx, int cy, int r, uint32_t c1, uint32_t c2);
extern void     gfx64_draw_line_aa(int x0, int y0, int x1, int y1, uint32_t color);
extern void     gfx64_fill_polygon(const int *pts, int n, uint32_t color);
extern void     gfx64_blend_image(void *src, int sw, int sh, int dx, int dy, int opacity);
extern void     gfx64_blend_image_sub(void *src, int sw, int sh, int sx, int sy,
                                      int dx, int dy, int w, int h, int opacity);
extern void     gfx64_blend_pixel(int x, int y, uint32_t fg, int alpha);
extern void     gfx64_blit_rgba(int x, int y, int w, int h, const void *src);
extern void     gfx64_blur_region(int x, int y, int w, int h, int blur_radius,
                                  uint32_t overlay_color, int overlay_alpha);
extern void     gfx64_clip_set(int x, int y, int w, int h);
extern void     gfx64_clip_disable(void);
extern void     gfx64_target_begin(void *buf, int w, int h, int ox, int oy);
extern void     gfx64_target_end(void);
extern void     gfx64_set_target_back(void);
extern void     gfx64_set_target_front(void);
extern void     gfx64_present(void);
extern void     gfx64_present_region(int x, int y, int w, int h);
extern uint8_t *gfx64_get_framebuffer(void);
extern uint32_t gfx64_get_pixel(int x, int y);
extern int      gfx64_get_width(void);
extern int      gfx64_get_height(void);
extern void     font64_draw_string(void *fb, int fw, int fh,
                                   int x, int y, const char *str,
                                   uint32_t color, uint16_t size);


static const desk_gfx_ops_t g_ops64 = {
    .put_pixel = gfx64_put_pixel,
    .fill_rect = gfx64_fill_rect,
    .draw_rect = gfx64_draw_rect,
    .fill_rounded_rect = gfx64_fill_rounded_rect,
    .draw_rounded_rect = gfx64_draw_rounded_rect,
    .fill_round_rect_aa = gfx64_fill_round_rect_aa,
    .fill_round_rect_argb = gfx64_fill_round_rect_argb,
    .fill_circle = gfx64_fill_circle,
    .draw_circle = gfx64_draw_circle,
    .draw_arc = gfx64_draw_arc,
    .fill_circle_gradient = gfx64_fill_circle_gradient,
    .draw_line_aa = gfx64_draw_line_aa,
    .fill_polygon = gfx64_fill_polygon,
    .blend_image = gfx64_blend_image,
    .blend_image_sub = gfx64_blend_image_sub,
    .blend_pixel = gfx64_blend_pixel,
    .blit_rgba = gfx64_blit_rgba,
    .blur_region = gfx64_blur_region,
    .clip_set = gfx64_clip_set,
    .clip_disable = gfx64_clip_disable,
    .target_begin = gfx64_target_begin,
    .target_end = gfx64_target_end,
    .set_target_back = gfx64_set_target_back,
    .set_target_front = gfx64_set_target_front,
    .present = gfx64_present,
    .present_region = gfx64_present_region,
    .get_framebuffer = gfx64_get_framebuffer,
    .get_pixel = gfx64_get_pixel,
    .get_width = gfx64_get_width,
    .get_height = gfx64_get_height,
    .draw_string = font64_draw_string,
    .string_width = 0,   /* 字体层暂未暴露测宽函数 */
};

const desk_gfx_ops_t *g_desk_ops = &g_ops64;

/* 文字直接接 hpdesk 已在用的字体层 */
