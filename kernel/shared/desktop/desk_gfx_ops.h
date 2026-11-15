#ifndef HEPU_DESK_GFX_OPS_H
#define HEPU_DESK_GFX_OPS_H

#include <stdint.h>

/* ============================================================================
 * desk_gfx_ops_t —— "32 位共用 64 位桌面"的关键一张表
 *
 * 事实: 64 位 hpdesk.c 用了 30 个不同的 gfx64_* 函数(281 处调用),
 *       hs_apps.c 又用 5 个。它们全部是"无 ctx 的全局函数"。
 *
 * 所以共用桌面不需要重写桌面, 只需要:
 *   1) 把 gfx64_* 的函数地址装进这张表;
 *   2) 桌面代码顶部用宏把 gfx64_xxx 重定向成 g_desk_ops->xxx;
 *   3) 32 位提供同签名的实现, 填进这张表。
 *
 * 桌面源码一个字不用改 —— 改的是编译期绑定到哪套后端。
 *
 * 签名必须与 gfx64.c 完全一致, 否则 64 位那套填不进来。
 * ========================================================================== */

typedef struct desk_gfx_ops {
    /* --- 像素与矩形 --- */
    void     (*put_pixel)(int x, int y, uint32_t color);
    void     (*fill_rect)(int x, int y, int w, int h, uint32_t color);
    void     (*draw_rect)(int x, int y, int w, int h, uint32_t color);

    /* --- 圆角 --- */
    void     (*fill_rounded_rect)(int x, int y, int w, int h, int radius, uint32_t color);
    void     (*draw_rounded_rect)(int x, int y, int w, int h, int radius, uint32_t color);
    void     (*fill_round_rect_aa)(int x, int y, int w, int h, int radius, uint32_t color);
    void     (*fill_round_rect_argb)(void *buf, int bw, int bh, int x, int y,
                                     int w, int h, int radius, uint32_t color);

    /* --- 圆与弧 --- */
    void     (*fill_circle)(int cx, int cy, int r, uint32_t color);
    void     (*draw_circle)(int cx, int cy, int r, uint32_t color);
    void     (*draw_arc)(int cx, int cy, int r, int a0, int a1, uint32_t color);
    void     (*fill_circle_gradient)(int cx, int cy, int r, uint32_t c1, uint32_t c2);

    /* --- 线 / 多边形 --- */
    void     (*draw_line_aa)(int x0, int y0, int x1, int y1, uint32_t color);
    void     (*fill_polygon)(const int *pts, int n, uint32_t color);

    /* --- 位图 --- */
    void     (*blend_image)(void *src, int sw, int sh, int dx, int dy, int opacity);
    void     (*blend_image_sub)(void *src, int sw, int sh, int sx, int sy,
                                int dx, int dy, int w, int h, int opacity);
    void     (*blend_pixel)(int x, int y, uint32_t fg, int alpha);
    void     (*blit_rgba)(int x, int y, int w, int h, const void *src);
    void     (*blur_region)(int x, int y, int w, int h, int blur_radius,
                            uint32_t overlay_color, int overlay_alpha);

    /* --- 裁剪 / 双缓冲状态机 --- */
    void     (*clip_set)(int x, int y, int w, int h);
    void     (*clip_disable)(void);
    void     (*target_begin)(void *buf, int w, int h, int ox, int oy);
    void     (*target_end)(void);
    void     (*set_target_back)(void);
    void     (*set_target_front)(void);

    /* --- 提交 --- */
    void     (*present)(void);
    void     (*present_region)(int x, int y, int w, int h);

    /* --- 查询 --- */
    uint8_t *(*get_framebuffer)(void);
    uint32_t (*get_pixel)(int x, int y);
    int      (*get_width)(void);
    int      (*get_height)(void);

    /* --- 文字 (由字体层提供, 两套各自实现) ---
     * 签名对齐 hpdesk.c 里已有的 font64_draw_string(fb, fw, fh, x, y, str, color, size) */
    void     (*draw_string)(void *fb, int fw, int fh,
                            int x, int y, const char *str,
                            uint32_t color, uint16_t size);
    int      (*string_width)(const char *str, uint16_t size);
} desk_gfx_ops_t;

/* 当前生效的后端。启动时由各自架构填一次。 */
extern const desk_gfx_ops_t *g_desk_ops;

#endif /* HEPU_DESK_GFX_OPS_H */
