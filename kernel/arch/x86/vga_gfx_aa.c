/* ============================================================================
 * 赫普OS - 抗锯齿字体渲染
 * 支持32bpp线性帧缓冲下的alpha混合文本渲染
 * ========================================================================== */

#include <stdint.h>
#include "font_aa16.h"
#include "vga.h"

/* 外部图形状态 (由vga_gfx.c定义) */
extern int gfx_width;
extern int gfx_height;
extern int gfx_pitch;
extern int gfx_bpp;
extern uint8_t *framebuffer;  /* 双缓冲后台缓冲区 */

/* VGA调色板 (16色标准CGA) */
static const uint32_t vga_palette_rgb[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

/* 调色板索引转RGB */
static inline uint32_t color_to_rgb(uint8_t color)
{
    if (color < 16) return vga_palette_rgb[color];
    return 0xFFFFFF;
}

/* 在32bpp帧缓冲上绘制抗锯齿字符 */
static void draw_aa_char_32bpp(int x, int y, char c, uint8_t color)
{
    if (c < AA_FONT16_FIRST || c >= AA_FONT16_FIRST + AA_FONT16_COUNT) return;
    const aa_glyph_t *g = &aa_font16[c - AA_FONT16_FIRST];
    if (!g->data || g->width == 0 || g->height == 0) return;

    uint32_t fg = color_to_rgb(color);
    uint8_t fr = (fg >> 16) & 0xFF;
    uint8_t fg2 = (fg >> 8) & 0xFF;
    uint8_t fb = fg & 0xFF;

    uint32_t *fb_ptr = (uint32_t *)framebuffer;
    int pitch_px = gfx_pitch / 4;

    for (int row = 0; row < g->height; row++) {
        int py = y + row + g->yoffset;
        if (py < 0 || py >= gfx_height) continue;
        for (int col = 0; col < g->width; col++) {
            int px = x + col + g->xoffset;
            if (px < 0 || px >= gfx_width) continue;
            uint8_t alpha = g->data[row * g->width + col];
            if (alpha == 0) continue;
            if (alpha == 255) {
                fb_ptr[py * pitch_px + px] = fg | 0xFF000000;
            } else {
                /* alpha混合: result = fg * alpha + bg * (255-alpha) */
                uint32_t bg = fb_ptr[py * pitch_px + px];
                uint8_t br = (bg >> 16) & 0xFF;
                uint8_t bg2 = (bg >> 8) & 0xFF;
                uint8_t bb = bg & 0xFF;
                uint8_t r = (fr * alpha + br * (255 - alpha)) / 255;
                uint8_t g2 = (fg2 * alpha + bg2 * (255 - alpha)) / 255;
                uint8_t b = (fb * alpha + bb * (255 - alpha)) / 255;
                fb_ptr[py * pitch_px + px] = (r << 16) | (g2 << 8) | b | 0xFF000000;
            }
        }
    }
}

/* 在8bpp帧缓冲上绘制抗锯齿字符 (简化: 阈值化) */
static void draw_aa_char_8bpp(int x, int y, char c, uint8_t color)
{
    if (c < AA_FONT16_FIRST || c >= AA_FONT16_FIRST + AA_FONT16_COUNT) return;
    const aa_glyph_t *g = &aa_font16[c - AA_FONT16_FIRST];
    if (!g->data || g->width == 0 || g->height == 0) return;

    for (int row = 0; row < g->height; row++) {
        int py = y + row + g->yoffset;
        if (py < 0 || py >= gfx_height) continue;
        for (int col = 0; col < g->width; col++) {
            int px = x + col + g->xoffset;
            if (px < 0 || px >= gfx_width) continue;
            uint8_t alpha = g->data[row * g->width + col];
            if (alpha > 128) {
                framebuffer[py * gfx_pitch + px] = color;
            }
        }
    }
}

/* 绘制抗锯齿字符 */
void gfx_aa_putc(int x, int y, char c, uint8_t color)
{
    if (gfx_bpp == 32) {
        draw_aa_char_32bpp(x, y, c, color);
    } else {
        draw_aa_char_8bpp(x, y, c, color);
    }
}

/* 绘制抗锯齿字符串 */
void gfx_aa_puts(int x, int y, const char *s, uint8_t color)
{
    int cx = x;
    while (*s) {
        if (*s == '\n') {
            y += AA_FONT16_HEIGHT + 2;
            cx = x;
            s++;
            continue;
        }
        gfx_aa_putc(cx, y, *s, color);
        /* 计算字符宽度 (使用字形宽度+间距) */
        if (*s >= AA_FONT16_FIRST && *s < AA_FONT16_FIRST + AA_FONT16_COUNT) {
            const aa_glyph_t *g = &aa_font16[*s - AA_FONT16_FIRST];
            cx += g->width + 1;
        } else {
            cx += 8;
        }
        s++;
    }
}

/* 获取抗锯齿字符串宽度 */
int gfx_aa_text_width(const char *s)
{
    int w = 0;
    while (*s) {
        if (*s >= AA_FONT16_FIRST && *s < AA_FONT16_FIRST + AA_FONT16_COUNT) {
            const aa_glyph_t *g = &aa_font16[*s - AA_FONT16_FIRST];
            w += g->width + 1;
        } else {
            w += 8;
        }
        s++;
    }
    return w;
}
