/* ============================================================================
 * HepuOS 可插拔图形子系统 - 抗锯齿字体渲染
 * 基于统一API，支持32位RGB Alpha混合
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "graphics.h"
#include "font8x16.h"

/* 8x16字体的边缘Alpha值（简化版，基于字体位图的邻域分析） */
static uint8_t aa_edge_alpha(uint8_t ch, int col, int row)
{
    if (ch < 32 || ch > 126) ch = '?';

    const uint8_t *glyph = &hepu_font8x16[ch * 16];
    uint8_t line = glyph[row];
    int pixel_set = (line & (0x80 >> col)) ? 1 : 0;

    if (!pixel_set) {
        /* 检查是否是边缘（相邻像素有设置） */
        int neighbors = 0;
        if (col > 0 && (line & (0x80 >> (col - 1)))) neighbors++;
        if (col < 7 && (line & (0x80 >> (col + 1)))) neighbors++;
        if (row > 0 && (glyph[row - 1] & (0x80 >> col))) neighbors++;
        if (row < 15 && (glyph[row + 1] & (0x80 >> col))) neighbors++;

        if (neighbors > 0) {
            /* 边缘像素，根据邻居数量设置Alpha */
            return (uint8_t)(40 + neighbors * 30);
        }
        return 0;
    }

    /* 已设置的像素，检查是否是边缘 */
    int edge_neighbors = 0;
    if (col > 0 && !(line & (0x80 >> (col - 1)))) edge_neighbors++;
    if (col < 7 && !(line & (0x80 >> (col + 1)))) edge_neighbors++;
    if (row > 0 && !(glyph[row - 1] & (0x80 >> col))) edge_neighbors++;
    if (row < 15 && !(glyph[row + 1] & (0x80 >> col))) edge_neighbors++;

    if (edge_neighbors > 0) {
        /* 边缘像素，Alpha稍低 */
        return (uint8_t)(200 - edge_neighbors * 15);
    }

    /* 内部像素，完全不透明 */
    return 255;
}

/* Alpha混合：fg * alpha + bg * (1 - alpha) */
static uint32_t aa_blend(uint32_t fg, uint32_t bg, uint8_t alpha)
{
    if (alpha >= 255) return fg;
    if (alpha == 0) return bg;

    uint32_t fr = (fg >> 16) & 0xFF;
    uint32_t fg_ = (fg >> 8) & 0xFF;
    uint32_t fb = fg & 0xFF;
    uint32_t br = (bg >> 16) & 0xFF;
    uint32_t bg_ = (bg >> 8) & 0xFF;
    uint32_t bb = bg & 0xFF;

    uint32_t inv_alpha = 255 - alpha;
    uint32_t r = (fr * alpha + br * inv_alpha) / 255;
    uint32_t g = (fg_ * alpha + bg_ * inv_alpha) / 255;
    uint32_t b = (fb * alpha + bb * inv_alpha) / 255;

    return (r << 16) | (g << 8) | b;
}

/* 读取帧缓冲像素 */
static uint32_t aa_read_pixel(int x, int y)
{
    void *fb = gfx_get_fb_vaddr();
    if (!fb) return 0;

    uint32_t w = 0, h = 0, bpp = 0;
    gfx_get_mode(&w, &h, &bpp);
    if (x < 0 || y < 0 || x >= (int)w || y >= (int)h) return 0;

    if (bpp == 32 || bpp == 24) {
        uint32_t *buf = (uint32_t *)fb;
        return buf[y * w + x];
    }
    return 0;
}

/* 统一API抗锯齿字符绘制 */
void gfx_draw_char_aa(uint32_t x, uint32_t y, char ch, gfx_color_t fg)
{
    if (ch < 32 || ch > 126) ch = '?';

    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 8; col++) {
            uint8_t alpha = aa_edge_alpha((uint8_t)ch, col, row);
            if (alpha > 0) {
                uint32_t bg = aa_read_pixel((int)x + col, (int)y + row);
                uint32_t blended = aa_blend(fg, bg, alpha);
                gfx_put_pixel(x + col, y + row, blended);
            }
        }
    }
}

/* 统一API抗锯齿字符串绘制 */
void gfx_draw_string_aa(uint32_t x, uint32_t y, const char *str, gfx_color_t fg)
{
    if (!str) return;
    uint32_t cx = x;
    while (*str) {
        if (*str == '\n') {
            y += 16;
            cx = x;
            str++;
            continue;
        }
        gfx_draw_char_aa(cx, y, *str, fg);
        cx += 8;
        str++;
    }
}
