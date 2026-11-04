/* ============================================================================
 * HepuBoot - GOP绘图库
 * 用于方案A/B启动画面绘制，接口与内核boot_gfx对齐
 * ========================================================================== */

#ifndef HEPBOOT_GOP_H
#define HEPBOOT_GOP_H

#include <efi.h>
#include <efilib.h>

/* 颜色定义 (32bpp BGRA) */
#define GOP_COLOR_BG       0x00120A0A
#define GOP_COLOR_WHITE    0x00FFFFFF
#define GOP_COLOR_GREEN    0x0000FF00
#define GOP_COLOR_RED      0x00FF0000
#define GOP_COLOR_BLUE     0x00FF9E4A
#define GOP_COLOR_GRAY     0x00808080
#define GOP_COLOR_DARKGRAY 0x00404040

/* 初始化GOP绘图 */
void gop_init(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop);

/* 基础绘图 */
void gop_clear(UINT32 color);
void gop_fill_rect(int x, int y, int w, int h, UINT32 color);
void gop_draw_pixel(int x, int y, UINT32 color);

/* 文字绘制（8x16点阵） */
void gop_draw_char(int x, int y, char c, UINT32 color);
void gop_draw_string(int x, int y, const char *s, UINT32 color);
void gop_printf(int x, int y, UINT32 color, const char *fmt, ...);

/* 进度条 */
void gop_progress_bar(int x, int y, int w, int h, int percent, UINT32 color);

/* 获取分辨率 */
UINT32 gop_get_width(void);
UINT32 gop_get_height(void);

#endif /* HEPBOOT_GOP_H */
