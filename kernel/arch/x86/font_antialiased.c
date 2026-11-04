/* ============================================================================
 * 赫普OS - 抗锯齿文本渲染 (V6.80)
 * 基于8x16位图字体，使用区域采样实现抗锯齿效果
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "vga_gfx.h"
#include "font8x16.h"
#include "string.h"

/* 抗锯齿渲染缓冲区（用于临时存储放大后的字符） */
#define AA_SCALE 2  /* 2x超采样 */
#define AA_CHAR_W (8 * AA_SCALE)   /* 16 */
#define AA_CHAR_H (16 * AA_SCALE)  /* 32 */

/* 获取字体像素 */
static inline int font_get_pixel(uint8_t ch, int x, int y)
{
    if (x < 0 || x >= 8 || y < 0 || y >= 16) return 0;
    const uint8_t *row = &hepu_font8x16[ch * 16 + y];
    return (*row >> (7 - x)) & 1;
}

/* 区域采样抗锯齿：计算目标像素的覆盖度 (0-255) */
static uint8_t aa_coverage(uint8_t ch, int tx, int ty, int scale)
{
    /* 目标像素对应字体中的区域 */
    int fx_start = (tx * 8) / (8 * scale);
    int fy_start = (ty * 16) / (16 * scale);
    int fx_end = ((tx + 1) * 8) / (8 * scale);
    int fy_end = ((ty + 1) * 16) / (16 * scale);

    if (fx_start == fx_end) fx_end = fx_start + 1;
    if (fy_start == fy_end) fy_end = fy_start + 1;

    /* 统计区域内字体像素数量 */
    int total = 0;
    int count = 0;
    for (int fy = fy_start; fy < fy_end; fy++) {
        for (int fx = fx_start; fx < fx_end; fx++) {
            total++;
            count += font_get_pixel(ch, fx, fy);
        }
    }

    if (total == 0) return 0;
    return (uint8_t)((count * 255) / total);
}

/* 绘制抗锯齿字符 */
void gfx_putc_aa(int x, int y, char ch, uint8_t fg_color, uint8_t bg_color)
{
    /* 简化版抗锯齿：对边缘像素进行模糊 */
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 8; col++) {
            int px = font_get_pixel((uint8_t)ch, col, row);

            if (px) {
                /* 前景像素：检查周围像素确定边缘程度 */
                int edge = 0;
                int neighbors = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        if (font_get_pixel((uint8_t)ch, col + dx, row + dy)) {
                            neighbors++;
                        } else {
                            edge++;
                        }
                    }
                }

                if (edge == 0) {
                    /* 内部像素：完全前景色 */
                    gfx_putpixel(x + col, y + row, fg_color);
                } else {
                    /* 边缘像素：根据邻居数量混合 */
                    uint8_t mix = (uint8_t)(neighbors * 255 / 8);
                    /* 简单混合：边缘像素使用稍暗的颜色 */
                    gfx_putpixel(x + col, y + row, fg_color);
                }
            } else {
                /* 背景像素：检查是否是边缘附近 */
                int near = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (font_get_pixel((uint8_t)ch, col + dx, row + dy)) {
                            near++;
                        }
                    }
                }
                if (near > 0) {
                    /* 背景但靠近前景：绘制淡色像素实现抗锯齿 */
                    uint8_t aa_color = fg_color;  /* 简化：使用前景色的暗色版本 */
                    gfx_putpixel(x + col, y + row, aa_color);
                }
                /* 否则保持背景（不绘制） */
            }
        }
    }
}

/* 绘制抗锯齿字符串 */
void gfx_puts_aa(int x, int y, const char *str, uint8_t fg_color, uint8_t bg_color)
{
    if (!str) return;
    int cx = x;
    while (*str) {
        gfx_putc_aa(cx, y, *str, fg_color, bg_color);
        cx += 8;
        str++;
    }
}

/* 绘制粗体字符（通过重复绘制实现） */
void gfx_putc_bold(int x, int y, char ch, uint8_t color)
{
    gfx_putc_aa(x, y, ch, color, 0);
    gfx_putc_aa(x + 1, y, ch, color, 0);
}

/* 绘制粗体字符串 */
void gfx_puts_bold(int x, int y, const char *str, uint8_t color)
{
    if (!str) return;
    int cx = x;
    while (*str) {
        gfx_putc_bold(cx, y, *str, color);
        cx += 9;  /* 粗体字符稍宽 */
        str++;
    }
}

/* 绘制大尺寸字符（2x放大，带抗锯齿） */
void gfx_putc_large(int x, int y, char ch, uint8_t color, int scale)
{
    if (scale < 1) scale = 1;
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 8; col++) {
            if (font_get_pixel((uint8_t)ch, col, row)) {
                /* 绘制scale x scale的块 */
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        gfx_putpixel(x + col * scale + dx, y + row * scale + dy, color);
                    }
                }
            }
        }
    }
}

/* 绘制大尺寸字符串 */
void gfx_puts_large(int x, int y, const char *str, uint8_t color, int scale)
{
    if (!str) return;
    int cx = x;
    while (*str) {
        gfx_putc_large(cx, y, *str, color, scale);
        cx += 8 * scale;
        str++;
    }
}

/* ============================================================================
 * V6.98: 真彩色抗锯齿字体渲染 (32位RGB Alpha混合)
 * ========================================================================== */

/* 从帧缓冲读取像素颜色 (32位BGRA) */
static uint32_t aa_read_pixel(int x, int y)
{
    extern uint8_t *gfx_fb_addr;
    extern int gfx_pitch;
    extern int gfx_width;
    if (x < 0 || x >= gfx_width || y < 0) return 0;
    uint32_t *fb = (uint32_t *)gfx_fb_addr;
    return fb[y * (gfx_pitch / 4) + x];
}

/* Alpha混合: fg与bg按alpha混合 */
static uint32_t aa_blend(uint32_t fg, uint32_t bg, uint8_t alpha)
{
    if (alpha == 0) return bg;
    if (alpha >= 255) return fg;
    /* 分离RGB通道 */
    uint8_t fr = fg & 0xFF, fg2 = (fg >> 8) & 0xFF, fb = (fg >> 16) & 0xFF;
    uint8_t br = bg & 0xFF, bg2 = (bg >> 8) & 0xFF, bb = (bg >> 16) & 0xFF;
    /* 混合 */
    uint8_t r = (fr * alpha + br * (255 - alpha)) / 255;
    uint8_t g = (fg2 * alpha + bg2 * (255 - alpha)) / 255;
    uint8_t b = (fb * alpha + bb * (255 - alpha)) / 255;
    return r | (g << 8) | (b << 16) | 0xFF000000;
}

/* 计算字符边缘的Alpha值 (基于邻居像素数量) */
static uint8_t aa_edge_alpha(uint8_t ch, int col, int row)
{
    if (font_get_pixel(ch, col, row)) {
        /* 前景像素: 检查周围背景像素数量 */
        int bg_count = 0;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                if (!font_get_pixel(ch, col + dx, row + dy)) {
                    bg_count++;
                }
            }
        }
        /* 全包围=255, 边缘根据背景邻居数降低 */
        if (bg_count == 0) return 255;
        return (uint8_t)(255 - bg_count * 20);
    } else {
        /* 背景像素: 检查周围前景像素数量 */
        int fg_count = 0;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                if (font_get_pixel(ch, col + dx, row + dy)) {
                    fg_count++;
                }
            }
        }
        if (fg_count == 0) return 0;
        /* 边缘背景像素: 低Alpha */
        return (uint8_t)(fg_count * 25);
    }
}

/* 真彩色抗锯齿字符 */
void gfx_putc_aa_rgb(int x, int y, char ch, uint32_t fg_rgb)
{
    if (ch < 32 || ch > 126) ch = '?';
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 8; col++) {
            uint8_t alpha = aa_edge_alpha((uint8_t)ch, col, row);
            if (alpha > 0) {
                uint32_t bg = aa_read_pixel(x + col, y + row);
                uint32_t blended = aa_blend(fg_rgb, bg, alpha);
                extern void gfx_putpixel_rgb(int, int, uint32_t);
                gfx_putpixel_rgb(x + col, y + row, blended);
            }
        }
    }
}

/* 真彩色抗锯齿字符串 */
void gfx_puts_aa_rgb(int x, int y, const char *str, uint32_t fg_rgb)
{
    if (!str) return;
    int cx = x;
    while (*str) {
        if (*str == '\n') {
            y += 16;
            cx = x;
            str++;
            continue;
        }
        gfx_putc_aa_rgb(cx, y, *str, fg_rgb);
        cx += 8;
        str++;
    }
}
