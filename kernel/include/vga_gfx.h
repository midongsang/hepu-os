#ifndef VGA_GFX_H
#define VGA_GFX_H

#include <stdint.h>

/* VGA 图形模式: 默认 320x200 256色 (VBE 可扩展) */
#define GFX_WIDTH  320   /* 默认宽度 */
#define GFX_HEIGHT 200   /* 默认高度 */
#define GFX_FB     0xA0000 /* VGA 模式帧缓冲 */

/* V2.40: 运行时分辨率变量 (VBE LFB 可覆盖默认值) */
extern int gfx_width;
extern int gfx_height;
extern int gfx_pitch;
extern int gfx_bpp;
extern uint8_t *gfx_fb_addr;
extern int gfx_refresh_rate;  /* V6.96: 刷新率(Hz) */

/* V2.41: UI 缩放因子 (相对于 320x200, 640x480=2.0) */
extern int gfx_scale_x;
extern int gfx_scale_y;

/* 按缩放因子换算坐标/尺寸 */
static inline int gfx_sx(int v) { return (v * gfx_scale_x) / 100; }
static inline int gfx_sy(int v) { return (v * gfx_scale_y) / 100; }

/* 颜色索引 (256色调色板) */
#define GFX_COLOR_BLACK    0
#define GFX_COLOR_BLUE     1
#define GFX_COLOR_GREEN    2
#define GFX_COLOR_CYAN     3
#define GFX_COLOR_RED      4
#define GFX_COLOR_MAGENTA  5
#define GFX_COLOR_BROWN    6
#define GFX_COLOR_LIGHTGREY 7
#define GFX_COLOR_DARKGREY 8
#define GFX_COLOR_LIGHTBLUE 9
#define GFX_COLOR_LIGHTGREEN 10
#define GFX_COLOR_LIGHTCYAN 11
#define GFX_COLOR_LIGHTRED 12
#define GFX_COLOR_LIGHTMAGENTA 13
#define GFX_COLOR_YELLOW   14
#define GFX_COLOR_WHITE    15

/* 初始化图形模式 (自动检测 VBE LFB, 否则用 VGA 0x13) */
void vga_gfx_init(void);

/* V2.40: 设置 VBE framebuffer 信息 (启动时调用) */
void gfx_set_vbe_info(int width, int height, uint8_t *fb_addr, int pitch, int bpp);
void gfx_force_vga13(void);
void vga_dump_regs(void);


/* 切换回文本模式 */
void vga_gfx_text_mode(void);

/* 画点 */
void gfx_putpixel(int x, int y, uint8_t color);

/* 获取点颜色 */
uint8_t gfx_getpixel(int x, int y);

/* 画水平线 */
void gfx_hline(int x, int y, int w, uint8_t color);

/* 画垂直线 */
void gfx_vline(int x, int y, int h, uint8_t color);

/* 画矩形边框 */
void gfx_rect(int x, int y, int w, int h, uint8_t color);

/* 填充矩形 */
void gfx_fillrect(int x, int y, int w, int h, uint8_t color);

/* 画直线 (Bresenham) */
void gfx_line(int x0, int y0, int x1, int y1, uint8_t color);

/* 清屏 */
void gfx_clear(uint8_t color);

/* 绘制字符 (8x8字体) */
void gfx_putchar(int x, int y, char c, uint8_t color);

/* 绘制字符串 */
void gfx_puts(int x, int y, const char *s, uint8_t color);

/* V2.35: 放大字符 (开机 logo 大字) */
void gfx_putchar_big(int x, int y, char c, uint8_t color, int scale);
void gfx_puts_big(int x, int y, const char *s, uint8_t color, int scale);

/* 设置调色板 */
void gfx_set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

/* 获取帧缓冲区指针 */
uint8_t *gfx_get_framebuffer(void);

/* 垂直同步等待 + 双缓冲提交 (消除闪烁) */
void gfx_wait_vsync(void);
void gfx_flip(void);

/* V2.61: 直接填黑显存 (休眠灭屏, 不经 back_buffer) */
void gfx_black_screen(void);

/* V6.50: 扩展绘图API */
void gfx_circle(int cx, int cy, int r, uint8_t color);
void gfx_fillcircle(int cx, int cy, int r, uint8_t color);
void gfx_ellipse(int cx, int cy, int rx, int ry, uint8_t color);
void gfx_blit(int x, int y, int w, int h, const uint8_t *data);
void gfx_blit_alpha(int x, int y, int w, int h, const uint8_t *data, uint8_t alpha);
void gfx_triangle(int x1, int y1, int x2, int y2, int x3, int y3, uint8_t color);
void gfx_gradient_h(int x, int y, int w, int h, uint8_t c1, uint8_t c2);
void gfx_gradient_v(int x, int y, int w, int h, uint8_t c1, uint8_t c2);

/* V6.90: 32位真彩色绘图API (用于现代桌面UI) */
void gfx_putpixel_rgb(int x, int y, uint32_t rgb);
void gfx_fillrect_rgb(int x, int y, int w, int h, uint32_t rgb);
void gfx_hline_rgb(int x, int y, int w, uint32_t rgb);
void gfx_vline_rgb(int x, int y, int h, uint32_t rgb);
void gfx_gradient_v_rgb(int x, int y, int w, int h, uint32_t c1, uint32_t c2);
void gfx_fillcircle_rgb(int cx, int cy, int r, uint32_t rgb);
void gfx_rounded_rect_rgb(int x, int y, int w, int h, int radius, uint32_t rgb);
void gfx_rect_rgb(int x, int y, int w, int h, uint32_t rgb);
void gfx_line_rgb(int x0, int y0, int x1, int y1, uint32_t rgb);
void gfx_ellipse_rgb(int cx, int cy, int rx, int ry, uint32_t rgb);
void gfx_triangle_rgb(int x1, int y1, int x2, int y2, int x3, int y3, uint32_t rgb);
uint32_t gfx_blend_rgb(uint32_t c1, uint32_t c2, int alpha);

#endif /* VGA_GFX_H */
void gfx_fill_test(uint8_t color);

/* V2.99: 全局反色 (浅色主题) */
void gfx_set_invert(int on);
