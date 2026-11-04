/* ============================================================================
 * 赫普OS - 启动画面绘图库
 * 独立于vga_gfx，纯帧缓冲操作，用于方案A/B启动画面
 * ========================================================================== */

#include <stdint.h>
#include <stdarg.h>
#include "boot_gfx.h"
#include "font8x16.h"
#include "string.h"
#include "paging.h"

boot_gfx_t boot_gfx = {0};

/* 日志滚动状态 */
static int log_x = 0;
static int log_y = 0;
static int log_max_lines = 0;
static int log_current_line = 0;

/* 启动时间（毫秒） */
static uint32_t boot_start_ms = 0;
extern uint32_t sched_ticks_total;  /* 100Hz tick计数 */

/* ============================================================================
 * 初始化
 * ========================================================================== */

void boot_gfx_init(uint32_t phys_addr, uint32_t width, uint32_t height,
                   uint32_t pitch, uint32_t bpp)
{
    boot_gfx.phys = phys_addr;
    boot_gfx.width = width;
    boot_gfx.height = height;
    boot_gfx.pitch = pitch;
    boot_gfx.bpp = bpp;
    boot_gfx.initialized = 1;

    /* 映射帧缓冲到内核虚拟地址
     * V6.86真机兼容: 真机GOP帧缓冲可能在任何PCI MMIO地址，
     * 直接使用物理地址（恒等映射），高地址由vga_gfx_init额外映射 */
    boot_gfx.fb = (uint32_t *)phys_addr;

    /* 记录启动时间 */
    boot_start_ms = sched_ticks_total * 10;  /* 100Hz → 10ms/tick */

    /* 清屏为深色背景 */
    boot_clear(BOOT_COLOR_BG);
}

/* ============================================================================
 * 基础绘图
 * ========================================================================== */

void boot_clear(uint32_t color)
{
    if (!boot_gfx.initialized) return;
    boot_fill_rect(0, 0, boot_gfx.width, boot_gfx.height, color);
}

void boot_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    if (!boot_gfx.initialized) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)boot_gfx.width) w = boot_gfx.width - x;
    if (y + h > (int)boot_gfx.height) h = boot_gfx.height - y;
    if (w <= 0 || h <= 0) return;

    for (int row = 0; row < h; row++) {
        uint32_t *ptr = boot_gfx.fb + (y + row) * (boot_gfx.pitch / 4) + x;
        for (int col = 0; col < w; col++) {
            ptr[col] = color;
        }
    }
}

void boot_draw_pixel(int x, int y, uint32_t color)
{
    if (!boot_gfx.initialized) return;
    if (x < 0 || x >= (int)boot_gfx.width || y < 0 || y >= (int)boot_gfx.height) return;
    boot_gfx.fb[y * (boot_gfx.pitch / 4) + x] = color;
}

/* ============================================================================
 * 文字绘制（8x16点阵字体）
 * ========================================================================== */

void boot_draw_char(int x, int y, char c, uint32_t color)
{
    if (!boot_gfx.initialized) return;
    unsigned char uc = (unsigned char)c;

    const unsigned char *glyph = &hepu_font8x16[uc * 16];
    for (int row = 0; row < 16; row++) {
        unsigned char bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                boot_draw_pixel(x + col, y + row, color);
            }
        }
    }
}

void boot_draw_string(int x, int y, const char *s, uint32_t color)
{
    if (!s) return;
    int cx = x;
    while (*s) {
        if (*s == '\n') {
            cx = x;
            y += 16;
        } else {
            boot_draw_char(cx, y, *s, color);
            cx += 8;
        }
        s++;
    }
}

/* 简单的格式化输出（支持%d %s %x %c %%） */
static void boot_num_to_str(uint32_t n, char *buf, int base, int uppercase)
{
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[32];
    int i = 0;
    if (n == 0) { buf[0] = '0'; buf[1] = 0; return; }
    while (n > 0) {
        tmp[i++] = digits[n % base];
        n /= base;
    }
    for (int j = 0; j < i; j++) buf[j] = tmp[i-1-j];
    buf[i] = 0;
}

void boot_printf(int x, int y, uint32_t color, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[256];
    int bi = 0;
    const char *p = fmt;
    while (*p && bi < 250) {
        if (*p == '%') {
            p++;
            char numbuf[32];
            switch (*p) {
                case 'd': {
                    int v = va_arg(ap, int);
                    if (v < 0) { buf[bi++] = '-'; v = -v; }
                    boot_num_to_str(v, numbuf, 10, 0);
                    for (char *q = numbuf; *q && bi < 250; q++) buf[bi++] = *q;
                    break;
                }
                case 'u': {
                    uint32_t v = va_arg(ap, uint32_t);
                    boot_num_to_str(v, numbuf, 10, 0);
                    for (char *q = numbuf; *q && bi < 250; q++) buf[bi++] = *q;
                    break;
                }
                case 'x': case 'X': {
                    uint32_t v = va_arg(ap, uint32_t);
                    boot_num_to_str(v, numbuf, 16, *p == 'X');
                    for (char *q = numbuf; *q && bi < 250; q++) buf[bi++] = *q;
                    break;
                }
                case 's': {
                    const char *s = va_arg(ap, const char *);
                    if (s) while (*s && bi < 250) buf[bi++] = *s++;
                    break;
                }
                case 'c': {
                    buf[bi++] = (char)va_arg(ap, int);
                    break;
                }
                case '%': {
                    buf[bi++] = '%';
                    break;
                }
                default:
                    buf[bi++] = '%';
                    buf[bi++] = *p;
            }
        } else {
            buf[bi++] = *p;
        }
        p++;
    }
    buf[bi] = 0;
    va_end(ap);
    boot_draw_string(x, y, buf, color);
}

/* ============================================================================
 * 进度条
 * ========================================================================== */

void boot_progress_bar(int x, int y, int w, int h, int percent, uint32_t color)
{
    if (!boot_gfx.initialized) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    /* 背景框 */
    boot_fill_rect(x - 1, y - 1, w + 2, h + 2, BOOT_COLOR_DARKGRAY);
    boot_fill_rect(x, y, w, h, 0x00202020);

    /* 进度填充 */
    int fill_w = (w * percent) / 100;
    if (fill_w > 0) {
        boot_fill_rect(x, y, fill_w, h, color);
    }
}

/* ============================================================================
 * 日志滚动输出（方案A用）
 * ========================================================================== */

void boot_log_init(int x, int y, int max_lines)
{
    log_x = x;
    log_y = y;
    log_max_lines = max_lines;
    log_current_line = 0;
}

void boot_log(const char *fmt, ...)
{
    if (!boot_gfx.initialized) return;

    va_list ap;
    va_start(ap, fmt);
    char buf[256];
    int bi = 0;
    const char *p = fmt;
    while (*p && bi < 250) {
        if (*p == '%') {
            p++;
            char numbuf[32];
            switch (*p) {
                case 'd': {
                    int v = va_arg(ap, int);
                    if (v < 0) { buf[bi++] = '-'; v = -v; }
                    boot_num_to_str(v, numbuf, 10, 0);
                    for (char *q = numbuf; *q && bi < 250; q++) buf[bi++] = *q;
                    break;
                }
                case 'u': {
                    uint32_t v = va_arg(ap, uint32_t);
                    boot_num_to_str(v, numbuf, 10, 0);
                    for (char *q = numbuf; *q && bi < 250; q++) buf[bi++] = *q;
                    break;
                }
                case 'x': case 'X': {
                    uint32_t v = va_arg(ap, uint32_t);
                    boot_num_to_str(v, numbuf, 16, *p == 'X');
                    for (char *q = numbuf; *q && bi < 250; q++) buf[bi++] = *q;
                    break;
                }
                case 's': {
                    const char *s = va_arg(ap, const char *);
                    if (s) while (*s && bi < 250) buf[bi++] = *s++;
                    break;
                }
                case 'c':
                    buf[bi++] = (char)va_arg(ap, int);
                    break;
                case '%':
                    buf[bi++] = '%';
                    break;
                default:
                    buf[bi++] = '%';
                    buf[bi++] = *p;
            }
        } else {
            buf[bi++] = *p;
        }
        p++;
    }
    buf[bi] = 0;
    va_end(ap);

    /* 滚动处理 */
    if (log_current_line >= log_max_lines) {
        /* 向上滚动一行 */
        int line_h = 16;
        uint32_t *src = boot_gfx.fb + (log_y + line_h) * (boot_gfx.pitch / 4) + log_x;
        uint32_t *dst = boot_gfx.fb + log_y * (boot_gfx.pitch / 4) + log_x;
        int copy_lines = log_max_lines - 1;
        int copy_width = boot_gfx.width - log_x;
        for (int i = 0; i < copy_lines * line_h; i++) {
            for (int j = 0; j < copy_width; j++) {
                dst[i * (boot_gfx.pitch / 4) + j] = src[i * (boot_gfx.pitch / 4) + j];
            }
        }
        /* 清除最后一行 */
        boot_fill_rect(log_x, log_y + (log_max_lines - 1) * line_h,
                       copy_width, line_h, BOOT_COLOR_BG);
        log_current_line = log_max_lines - 1;
    }

    /* 绘制当前行 */
    int y = log_y + log_current_line * 16;
    boot_draw_string(log_x, y, buf, BOOT_COLOR_WHITE);
    log_current_line++;
}

void boot_log_flush(void)
{
    /* 帧缓冲是直接写的，无需flush */
}

/* ============================================================================
 * 启动耗时
 * ========================================================================== */

uint32_t boot_get_time_ms(void)
{
    return sched_ticks_total * 10;  /* 100Hz → 10ms/tick */
}

void boot_log_time(const char *label)
{
    uint32_t now = boot_get_time_ms();
    uint32_t elapsed = now - boot_start_ms;
    boot_log("[BOOT] %s: %u ms", label, elapsed);
}
