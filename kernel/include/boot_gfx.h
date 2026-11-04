#ifndef BOOT_GFX_H
#define BOOT_GFX_H

#include <stdint.h>

/* 启动画面绘图库
 * 用于方案A（开发调试版）和方案B（成品简约版）的启动画面绘制
 * 独立于vga_gfx，不依赖VGA寄存器，纯帧缓冲操作
 */

/* 颜色定义 (32bpp BGRA格式，小端存储) */
#define BOOT_COLOR_BG       0x00120A0A  /* #0A0A12 深蓝黑 */
#define BOOT_COLOR_WHITE    0x00FFFFFF
#define BOOT_COLOR_GREEN    0x0000FF00
#define BOOT_COLOR_RED      0x00FF0000
#define BOOT_COLOR_BLUE     0x00FF9E4A  /* #4A9EFF 进度条蓝 */
#define BOOT_COLOR_GRAY     0x00808080
#define BOOT_COLOR_DARKGRAY 0x00404040

/* 帧缓冲信息 */
typedef struct {
    uint32_t *fb;        /* 帧缓冲虚拟地址 */
    uint32_t phys;       /* 帧缓冲物理地址 */
    uint32_t width;      /* 宽度 */
    uint32_t height;     /* 高度 */
    uint32_t pitch;      /* 每行字节数 */
    uint32_t bpp;        /* 色深 */
    int initialized;     /* 是否已初始化 */
} boot_gfx_t;

extern boot_gfx_t boot_gfx;

/* 初始化 */
void boot_gfx_init(uint32_t phys_addr, uint32_t width, uint32_t height,
                   uint32_t pitch, uint32_t bpp);

/* 基础绘图 */
void boot_clear(uint32_t color);
void boot_fill_rect(int x, int y, int w, int h, uint32_t color);
void boot_draw_pixel(int x, int y, uint32_t color);

/* 文字绘制（8x16点阵字体） */
void boot_draw_char(int x, int y, char c, uint32_t color);
void boot_draw_string(int x, int y, const char *s, uint32_t color);
void boot_printf(int x, int y, uint32_t color, const char *fmt, ...);

/* 进度条 */
void boot_progress_bar(int x, int y, int w, int h, int percent, uint32_t color);

/* 日志滚动输出（方案A用） */
void boot_log_init(int x, int y, int max_lines);
void boot_log(const char *fmt, ...);
void boot_log_flush(void);

/* 启动耗时 */
uint32_t boot_get_time_ms(void);
void boot_log_time(const char *label);

#endif /* BOOT_GFX_H */
