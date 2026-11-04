/* ============================================================================
 * 赫普OS - 真实VGA驱动（从drivers_real.c拆分）
 * ========================================================================== */

#include <stdint.h>
#include "drivers_real.h"
#include "vga.h"
#include "string.h"
#include "constants.h"

extern drivers_real_t drv;

/* I/O端口操作 */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

int drv_r_vga_set_mode(vga_mode_t mode)
{
    drv.vga.mode = mode;
    switch (mode) {
        case VGA_MODE_TEXT_80x25:
            drv.vga.width = 80; drv.vga.height = 25; drv.vga.bpp = 4;
            drv.vga.text_cols = 80; drv.vga.text_rows = 25;
            break;
        case VGA_MODE_TEXT_80x50:
            drv.vga.width = 80; drv.vga.height = 50; drv.vga.bpp = 4;
            drv.vga.text_cols = 80; drv.vga.text_rows = 50;
            break;
        case VGA_MODE_GRAPHICS_320x200:
            drv.vga.width = 320; drv.vga.height = 200; drv.vga.bpp = 8;
            break;
        case VGA_MODE_GRAPHICS_640x480:
            drv.vga.width = 640; drv.vga.height = 480; drv.vga.bpp = 4;
            break;
        case VGA_MODE_GRAPHICS_800x600:
            drv.vga.width = 800; drv.vga.height = 600; drv.vga.bpp = 8;
            break;
        case VGA_MODE_GRAPHICS_1024x768:
            drv.vga.width = 1024; drv.vga.height = 768; drv.vga.bpp = 16;
            break;
        case VGA_MODE_GRAPHICS_1280x1024:
            drv.vga.width = 1280; drv.vga.height = 1024; drv.vga.bpp = 24;
            break;
    }
    return 0;
}

int drv_r_vga_get_mode(vga_info_t *info)
{
    if (!info) return -1;
    memcpy(info, &drv.vga, sizeof(vga_info_t));
    return 0;
}

int drv_r_vga_putc(int x, int y, char c, int color)
{
    if (x < 0 || x >= 80 || y < 0 || y >= 25) return -1;
    volatile uint16_t *vga_mem = (volatile uint16_t *)VGA_TEXT_FRAMEBUFFER;
    vga_mem[y * 80 + x] = (uint16_t)c | ((uint16_t)color << 8);
    return 0;
}

int drv_r_vga_puts(int x, int y, const char *s, int color)
{
    if (!s) return -1;
    int i = 0;
    while (s[i] && x + i < 80) {
        drv_r_vga_putc(x + i, y, s[i], color);
        i++;
    }
    return i;
}

int drv_r_vga_clear(int color)
{
    volatile uint16_t *vga_mem = (volatile uint16_t *)VGA_TEXT_FRAMEBUFFER;
    for (int i = 0; i < 80 * 25; i++) {
        vga_mem[i] = (uint16_t)' ' | ((uint16_t)color << 8);
    }
    return 0;
}

int drv_r_vga_set_cursor(int x, int y)
{
    if (x < 0 || x >= 80 || y < 0 || y >= 25) return -1;
    drv.vga.cursor_x = x;
    drv.vga.cursor_y = y;
    uint16_t pos = y * 80 + x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
    return 0;
}

int drv_r_vga_get_cursor(int *x, int *y)
{
    if (x) *x = drv.vga.cursor_x;
    if (y) *y = drv.vga.cursor_y;
    return 0;
}

int drv_r_vga_scroll(int lines)
{
    volatile uint16_t *vga_mem = (volatile uint16_t *)VGA_TEXT_FRAMEBUFFER;
    for (int y = lines; y < 25; y++) {
        for (int x = 0; x < 80; x++) {
            vga_mem[(y - lines) * 80 + x] = vga_mem[y * 80 + x];
        }
    }
    for (int y = 25 - lines; y < 25; y++) {
        for (int x = 0; x < 80; x++) {
            vga_mem[y * 80 + x] = (uint16_t)' ' | VGA_ATTR_LIGHT_GREY;
        }
    }
    return 0;
}

int drv_r_vga_set_palette(int index, int r, int g, int b)
{
    (void)index; (void)r; (void)g; (void)b;
    return 0;
}

/* 键盘 */
