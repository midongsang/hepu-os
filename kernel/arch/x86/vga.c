/* ============================================================================
 * 赫普OS - VGA 文本模式驱动
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "vga.h"

/* VGA 端口操作 (与 vga_gfx.c 同源) */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

#define VGA_BUFFER 0xB8000
#define VGA_WIDTH  80
#define VGA_HEIGHT 25

static uint16_t *vga_buffer = (uint16_t *)VGA_BUFFER;
static size_t   vga_row = 0;
static size_t   vga_col = 0;
static uint8_t  vga_color = 0x07;

/* 颜色组合 */
uint8_t vga_make_color(enum vga_color fg, enum vga_color bg)
{
    return fg | (bg << 4);
}

/* 生成字符单元 */
uint16_t vga_make_entry(char c, uint8_t color)
{
    return (uint16_t)c | ((uint16_t)color << 8);
}

void vga_init(void)
{
    vga_row = 0;
    vga_col = 0;
    vga_color = vga_make_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_clear();
}

void vga_clear(void)
{
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = vga_make_entry(' ', vga_color);
        }
    }
    vga_row = 0;
    vga_col = 0;
}

void vga_set_color(uint8_t fg, uint8_t bg)
{
    vga_color = vga_make_color(fg, bg);
}

/* V3.82: 光标定位(供文本模式 splash/进度条动画重画用), 同步硬件光标 */
void vga_gotoxy(size_t row, size_t col)
{
    if (row >= VGA_HEIGHT) row = VGA_HEIGHT - 1;
    if (col >= VGA_WIDTH) col = VGA_WIDTH - 1;
    vga_row = row;
    vga_col = col;
    uint16_t pos = (uint16_t)(row * VGA_WIDTH + col);
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)pos);
}

static void vga_scroll(void)
{
    if (vga_row >= VGA_HEIGHT) {
        /* 上移一行 */
        for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
            for (size_t x = 0; x < VGA_WIDTH; x++) {
                vga_buffer[y * VGA_WIDTH + x] =
                    vga_buffer[(y + 1) * VGA_WIDTH + x];
            }
        }
        /* 清空最后一行 */
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] =
                vga_make_entry(' ', vga_color);
        }
        vga_row = VGA_HEIGHT - 1;
    }
}

/* V2.18: VGA 硬件光标位置同步(0x3D4/0x3D5).
 * 修复"闪烁光标没有显示到输入命令位置": 之前不更新硬件光标,
 * 闪烁下划线停留在 BIOS 遗留位置, 与输入位置脱节. */
static inline void outb_vga(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void vga_update_cursor(void)
{
    uint16_t pos = (uint16_t)(vga_row * VGA_WIDTH + vga_col);
    outb_vga(0x3D4, 0x0F);
    outb_vga(0x3D5, (uint8_t)(pos & 0xFF));
    outb_vga(0x3D4, 0x0E);
    outb_vga(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_putc(char c)
{
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
        vga_scroll();
        vga_update_cursor();
        return;
    }
    if (c == '\r') {
        vga_col = 0;
        vga_update_cursor();
        return;
    }
    if (c == '\t') {
        vga_col = (vga_col + 8) & ~7;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            vga_row++;
            vga_scroll();
        }
        vga_update_cursor();
        return;
    }
    /* V2.18修复: 退格(0x08). 原来被当作普通字符写入显存→显示为方块█,
     * 且光标前移→退格后残留"█ █"方块、光标位置错乱.
     * 现在: 光标后退一格并清除该位置字符. */
    if (c == 0x08) {
        if (vga_col > 0) {
            vga_col--;
        } else if (vga_row > 0) {
            vga_row--;
            vga_col = VGA_WIDTH - 1;
        } else {
            vga_update_cursor();
            return;
        }
        vga_buffer[vga_row * VGA_WIDTH + vga_col] =
            vga_make_entry(' ', vga_color);
        vga_update_cursor();
        return;
    }

    vga_buffer[vga_row * VGA_WIDTH + vga_col] =
        vga_make_entry(c, vga_color);

    if (++vga_col >= VGA_WIDTH) {
        vga_col = 0;
        vga_row++;
        vga_scroll();
    }
    vga_update_cursor();
}

void vga_puts(const char *str)
{
    while (*str) {
        unsigned char c = (unsigned char)*str;
        if (c >= 0xE0 && c <= 0xEF) {
            /* V4.22-E67: 3 字节 UTF-8 → CJK 管线 */
            extern int cjk_puts_utf8(const char **);
            if (cjk_puts_utf8(&str)) continue;
        }
        vga_putc(*str++);
    }
}

/* 简易 printf，支持 %s %d %x %c */
void vga_printf(const char *fmt, ...)
{
    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            /* V2.33: 解析标志 (左对齐/零填充) */
            int left_align = 0;
            int zero_pad = 0;
            while (*fmt == '-' || *fmt == '0' || *fmt == '+' || *fmt == ' ') {
                if (*fmt == '-') left_align = 1;
                else if (*fmt == '0') zero_pad = 1;
                /* '+' 和 ' ' 忽略 */
                fmt++;
            }
            /* 解析宽度 */
            int width = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
            /* 解析精度 (可选) */
            int precision = -1;
            if (*fmt == '.') {
                fmt++;
                precision = 0;
                while (*fmt >= '0' && *fmt <= '9') {
                    precision = precision * 10 + (*fmt - '0');
                    fmt++;
                }
            }

            char out_buf[64];
            int out_len = 0;

            switch (*fmt) {
                case 's': {
                    const char *s = __builtin_va_arg(args, const char *);
                    if (!s) s = "(null)";
                    /* V4.23-E68: %s 直接 vga_puts——走 UTF-8/CJK 管线
                     * (原逐字节 vga_putc 会把多字节 UTF-8 拆成乱码字节) */
                    vga_puts(s);
                    out_len = 0;
                    break;
                }
                case 'd': {
                    int d = __builtin_va_arg(args, int);
                    char nb[16]; int np = 0; int neg = 0;
                    if (d < 0) { neg = 1; d = -d; }
                    if (d == 0) nb[np++] = '0';
                    while (d > 0) { nb[np++] = '0' + (d % 10); d /= 10; }
                    if (neg) nb[np++] = '-';
                    out_len = np;
                    for (int i = 0; i < np; i++) out_buf[i] = nb[np - 1 - i];
                    break;
                }
                case 'u': {
                    unsigned int u = __builtin_va_arg(args, unsigned int);
                    char nb[16]; int np = 0;
                    if (u == 0) nb[np++] = '0';
                    while (u > 0) { nb[np++] = '0' + (u % 10); u /= 10; }
                    out_len = np;
                    for (int i = 0; i < np; i++) out_buf[i] = nb[np - 1 - i];
                    break;
                }
                case 'x': {
                    unsigned int x = __builtin_va_arg(args, unsigned int);
                    char nb[16]; int np = 0;
                    const char *hex = "0123456789ABCDEF";
                    if (x == 0) nb[np++] = '0';
                    while (x > 0) { nb[np++] = hex[x & 0xF]; x >>= 4; }
                    out_len = np;
                    for (int i = 0; i < np; i++) out_buf[i] = nb[np - 1 - i];
                    break;
                }
                case 'c': {
                    char c = (char)__builtin_va_arg(args, int);
                    out_buf[0] = c; out_len = 1;
                    break;
                }
                case '%':
                    vga_putc('%');
                    fmt++;
                    continue;
                default:
                    vga_putc('%');
                    if (*fmt) vga_putc(*fmt);
                    fmt++;
                    continue;
            }

            /* V2.33: 按宽度对齐输出 */
            char pad = (zero_pad && !left_align) ? '0' : ' ';
            if (left_align) {
                for (int i = 0; i < out_len; i++) vga_putc(out_buf[i]);
                for (int i = out_len; i < width; i++) vga_putc(pad);
            } else {
                for (int i = out_len; i < width; i++) vga_putc(pad);
                for (int i = 0; i < out_len; i++) vga_putc(out_buf[i]);
            }
        } else {
            vga_putc(*fmt);
        }
        fmt++;
    }

    __builtin_va_end(args);
}

/* 写缓冲区内容到屏幕（V1.01新增，替代不存在的幻觉API） */
void vga_write(const void *buf, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    for (uint32_t i = 0; i < len; i++) {
        vga_putc((char)p[i]);
    }
}
