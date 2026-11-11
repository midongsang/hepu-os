/* ============================================================================
 * 赫普OS - 共享图形算法库（32位/64位通用）
 * 与CPU架构无关的纯算法：填充、画线、alpha混合、渐变、颜色操作
 * 底层像素访问通过 gfx_algo_backend_t 操作表，由各架构实现
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "gfx_shared.h"

/* 简单像素访问回调（用于算法库，各架构提供实现） */
typedef struct {
    void (*put_pixel)(int x, int y, uint32_t color);
    uint32_t (*get_pixel)(int x, int y);
    int width;
    int height;
} gfx_algo_backend_t;

/* ============================================================================
 * 颜色操作
 * ========================================================================== */

/* 颜色插值：t=0返回c1，t=256返回c2 */
uint32_t gfx_algo_color_lerp(uint32_t c1, uint32_t c2, int t)
{
    if (t <= 0) return c1;
    if (t >= 256) return c2;
    int it = 256 - t;
    uint8_t r = ((c1 >> 16) & 0xFF) * it / 256 + ((c2 >> 16) & 0xFF) * t / 256;
    uint8_t g = ((c1 >> 8) & 0xFF) * it / 256 + ((c2 >> 8) & 0xFF) * t / 256;
    uint8_t b = (c1 & 0xFF) * it / 256 + (c2 & 0xFF) * t / 256;
    return (r << 16) | (g << 8) | b;
}

/* alpha混合：fg覆盖在bg上，alpha=0全透明，255不透明 */
uint32_t gfx_algo_alpha_blend(uint32_t fg, uint32_t bg, uint8_t alpha)
{
    if (alpha == 0) return bg;
    if (alpha >= 255) return fg;
    int a = alpha;
    int ia = 255 - alpha;
    uint8_t r = ((fg >> 16) & 0xFF) * a / 255 + ((bg >> 16) & 0xFF) * ia / 255;
    uint8_t g = ((fg >> 8) & 0xFF) * a / 255 + ((bg >> 8) & 0xFF) * ia / 255;
    uint8_t b = (fg & 0xFF) * a / 255 + (bg & 0xFF) * ia / 255;
    return (r << 16) | (g << 8) | b;
}

/* ============================================================================
 * 基础绘制（通过后端操作表）
 * ========================================================================== */

/* 填充矩形 */
void gfx_algo_fill_rect(gfx_algo_backend_t *ops, int x, int y, int w, int h, uint32_t color)
{
    if (!ops || !ops->put_pixel) return;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            ops->put_pixel(x + i, y + j, color);
        }
    }
}

/* 填充矩形（带裁剪） */
void gfx_algo_fill_rect_clip(gfx_algo_backend_t *ops, int x, int y, int w, int h,
                              uint32_t color, int clip_w, int clip_h)
{
    if (!ops || !ops->put_pixel) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > clip_w ? clip_w : x + w;
    int y1 = y + h > clip_h ? clip_h : y + h;
    for (int j = y0; j < y1; j++) {
        for (int i = x0; i < x1; i++) {
            ops->put_pixel(i, j, color);
        }
    }
}

/* 水平线 */
void gfx_algo_draw_hline(gfx_algo_backend_t *ops, int x, int y, int w, uint32_t color)
{
    if (!ops || !ops->put_pixel || w <= 0) return;
    for (int i = 0; i < w; i++) {
        ops->put_pixel(x + i, y, color);
    }
}

/* 垂直线 */
void gfx_algo_draw_vline(gfx_algo_backend_t *ops, int x, int y, int h, uint32_t color)
{
    if (!ops || !ops->put_pixel || h <= 0) return;
    for (int i = 0; i < h; i++) {
        ops->put_pixel(x, y + i, color);
    }
}

/* 任意直线（Bresenham算法） */
void gfx_algo_draw_line(gfx_algo_backend_t *ops, int x0, int y0, int x1, int y1, uint32_t color)
{
    if (!ops || !ops->put_pixel) return;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (1) {
        ops->put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

/* 矩形边框 */
void gfx_algo_draw_rect(gfx_algo_backend_t *ops, int x, int y, int w, int h, uint32_t color)
{
    gfx_algo_draw_hline(ops, x, y, w, color);
    gfx_algo_draw_hline(ops, x, y + h - 1, w, color);
    gfx_algo_draw_vline(ops, x, y, h, color);
    gfx_algo_draw_vline(ops, x + w - 1, y, h, color);
}

/* 圆角矩形填充（简化版，四角用直线代替） */
void gfx_algo_fill_rounded_rect(gfx_algo_backend_t *ops, int x, int y, int w, int h,
                                 int radius, uint32_t color)
{
    if (radius <= 0) {
        gfx_algo_fill_rect(ops, x, y, w, h, color);
        return;
    }
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    /* 中间矩形 */
    gfx_algo_fill_rect(ops, x + radius, y, w - 2 * radius, h, color);
    /* 左右矩形 */
    gfx_algo_fill_rect(ops, x, y + radius, radius, h - 2 * radius, color);
    gfx_algo_fill_rect(ops, x + w - radius, y + radius, radius, h - 2 * radius, color);
    /* 四角用近似（画圆） */
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy <= radius * radius) {
                ops->put_pixel(x + radius + dx, y + radius + dy, color);
                ops->put_pixel(x + w - radius - 1 + dx, y + radius + dy, color);
                ops->put_pixel(x + radius + dx, y + h - radius - 1 + dy, color);
                ops->put_pixel(x + w - radius - 1 + dx, y + h - radius - 1 + dy, color);
            }
        }
    }
}

/* ============================================================================
 * 渐变绘制
 * ========================================================================== */

/* 垂直渐变矩形 */
void gfx_algo_draw_vgradient(gfx_algo_backend_t *ops, int x, int y, int w, int h,
                              uint32_t color_top, uint32_t color_bottom)
{
    if (!ops || !ops->put_pixel || h <= 0) return;
    for (int j = 0; j < h; j++) {
        uint32_t c = gfx_algo_color_lerp(color_top, color_bottom, j * 256 / h);
        for (int i = 0; i < w; i++) {
            ops->put_pixel(x + i, y + j, c);
        }
    }
}

/* 水平渐变矩形 */
void gfx_algo_draw_hgradient(gfx_algo_backend_t *ops, int x, int y, int w, int h,
                              uint32_t color_left, uint32_t color_right)
{
    if (!ops || !ops->put_pixel || w <= 0) return;
    for (int i = 0; i < w; i++) {
        uint32_t c = gfx_algo_color_lerp(color_left, color_right, i * 256 / w);
        for (int j = 0; j < h; j++) {
            ops->put_pixel(x + i, y + j, c);
        }
    }
}

/* 对角线渐变（左上到右下） */
void gfx_algo_draw_dgradient(gfx_algo_backend_t *ops, int x, int y, int w, int h,
                              uint32_t c1, uint32_t c2)
{
    if (!ops || !ops->put_pixel) return;
    int total = w + h;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            int t = (i + j) * 256 / total;
            ops->put_pixel(x + i, y + j, gfx_algo_color_lerp(c1, c2, t));
        }
    }
}

/* ============================================================================
 * 字体渲染辅助（alpha混合位图）
 * ========================================================================== */

/* 绘制8位alpha位图（用于TrueType字体渲染） */
void gfx_algo_draw_alpha_bitmap(gfx_algo_backend_t *ops, int x, int y,
                                 const uint8_t *bitmap, int w, int h,
                                 uint32_t fg_color)
{
    if (!ops || !ops->put_pixel || !bitmap) return;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            uint8_t alpha = bitmap[j * w + i];
            if (alpha > 0) {
                int px = x + i;
                int py = y + j;
                if (alpha >= 250) {
                    ops->put_pixel(px, py, fg_color);
                } else {
                    uint32_t bg = ops->get_pixel ? ops->get_pixel(px, py) : 0;
                    ops->put_pixel(px, py, gfx_algo_alpha_blend(fg_color, bg, alpha));
                }
            }
        }
    }
}

/* 绘制8x16位图字体（二值） */
void gfx_algo_draw_font8x16(gfx_algo_backend_t *ops, int x, int y, char c,
                             const uint8_t *font_data, uint32_t color)
{
    if (!ops || !ops->put_pixel || !font_data) return;
    /* V0.55.1: c 是 char(有符号), "c >= 128" 恒假, 负数已由 c<0 拦下 */
    if (c < 0) return;
    const uint8_t *glyph = font_data + (uint8_t)c * 16;
    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                ops->put_pixel(x + col, y + row, color);
            }
        }
    }
}

/* ============================================================================
 * 几何辅助
 * ========================================================================== */

/* 点在矩形内 */
int gfx_algo_point_in_rect(int px, int py, int rx, int ry, int rw, int rh)
{
    return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

/* 矩形相交 */
int gfx_algo_rect_intersect(int x1, int y1, int w1, int h1,
                             int x2, int y2, int w2, int h2)
{
    return x1 < x2 + w2 && x1 + w1 > x2 && y1 < y2 + h2 && y1 + h1 > y2;
}

/* 夹紧值 */
int gfx_algo_clamp(int val, int min, int max)
{
    if (val < min) return min;
    if (val > max) return max;
    return val;
}
