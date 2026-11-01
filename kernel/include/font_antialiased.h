#ifndef FONT_ANTIALIASED_H
#define FONT_ANTIALIASED_H

#include <stdint.h>

/* 抗锯齿文本渲染 */
void gfx_putc_aa(int x, int y, char ch, uint8_t fg_color, uint8_t bg_color);
void gfx_puts_aa(int x, int y, const char *str, uint8_t fg_color, uint8_t bg_color);

/* 粗体文本 */
void gfx_putc_bold(int x, int y, char ch, uint8_t color);
void gfx_puts_bold(int x, int y, const char *str, uint8_t color);

/* 大尺寸文本（scale倍放大） */
void gfx_putc_large(int x, int y, char ch, uint8_t color, int scale);
void gfx_puts_large(int x, int y, const char *str, uint8_t color, int scale);

/* V6.98: 真彩色抗锯齿文本 (32位RGB Alpha混合) */
void gfx_putc_aa_rgb(int x, int y, char ch, uint32_t fg_rgb);
void gfx_puts_aa_rgb(int x, int y, const char *str, uint32_t fg_rgb);

#endif /* FONT_ANTIALIASED_H */
