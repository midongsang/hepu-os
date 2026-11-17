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

/* 放大绘制：scale=1 等价于 gop_draw_string。用于 1024x768 这类高分辨率下
 * 让 8x16 的点阵字不至于小到看不清。放大是整数 nearest-neighbor，必然有锯齿 ——
 * 引导界面的标题用 logo 位图(带 alpha)承担，正文用这个就够了。 */
void gop_draw_char_scaled(int x, int y, char c, UINT32 color, int scale);
void gop_draw_string_scaled(int x, int y, const char *s, UINT32 color, int scale);
int  gop_string_width_scaled(const char *s, int scale);

/* 8bit alpha 位图贴图：按 alpha 混合到现有像素，所以边缘是抗锯齿的。
 * alpha 只做单色渲染(color 提供 RGB)，够画单色 logo 用。 */
void gop_draw_alpha_bitmap(int x, int y, const unsigned char *alpha,
                           int w, int h, UINT32 color);

/* 半透明填充：a=0 全透明, 255 全不透明。用来画菜单背板 */
void gop_fill_rect_blend(int x, int y, int w, int h, UINT32 color, UINT8 a);
void gop_printf(int x, int y, UINT32 color, const char *fmt, ...);

/* 进度条 */
void gop_progress_bar(int x, int y, int w, int h, int percent, UINT32 color);

/* 获取分辨率 */
UINT32 gop_get_width(void);
UINT32 gop_get_height(void);

#endif /* HEPBOOT_GOP_H */
