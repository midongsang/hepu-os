/* ============================================================================
 * HepuOS 可插拔图形子系统 - VGA文本后端
 * 仅保留文本模式，用于内核最早期应急控制台
 * 废弃bank图形模式，只提取VGA的极简文本调试能力
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "graphics.h"
#include "serial.h"

/* IO端口输出 */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* VGA文本缓冲区 */
#define VGA_TEXT_ADDR 0xB8000
#define VGA_TEXT_WIDTH 80
#define VGA_TEXT_HEIGHT 25

static volatile uint16_t *vga_buffer = (uint16_t *)VGA_TEXT_ADDR;
static int vga_cursor_x = 0;
static int vga_cursor_y = 0;
static int vga_initialized = 0;

/* VGA颜色 */
#define VGA_COLOR_BLACK     0x00
#define VGA_COLOR_BLUE      0x01
#define VGA_COLOR_GREEN     0x02
#define VGA_COLOR_CYAN      0x03
#define VGA_COLOR_RED       0x04
#define VGA_COLOR_MAGENTA   0x05
#define VGA_COLOR_BROWN     0x06
#define VGA_COLOR_LGRAY     0x07
#define VGA_COLOR_DGRAY     0x08
#define VGA_COLOR_LBLUE     0x09
#define VGA_COLOR_LGREEN    0x0A
#define VGA_COLOR_LCYAN     0x0B
#define VGA_COLOR_LRED      0x0C
#define VGA_COLOR_LMAGENTA  0x0D
#define VGA_COLOR_YELLOW    0x0E
#define VGA_COLOR_WHITE     0x0F

/* 将32位颜色转换为VGA 4位颜色 */
static uint8_t vga_color_from_rgb(gfx_color_t color)
{
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    /* 简单的颜色量化 */
    if (r > 128 && g > 128 && b > 128) return VGA_COLOR_WHITE;
    if (r > 128 && g > 128) return VGA_COLOR_YELLOW;
    if (r > 128 && b > 128) return VGA_COLOR_LMAGENTA;
    if (g > 128 && b > 128) return VGA_COLOR_LCYAN;
    if (r > 128) return VGA_COLOR_LRED;
    if (g > 128) return VGA_COLOR_LGREEN;
    if (b > 128) return VGA_COLOR_LBLUE;
    if (r > 64 || g > 64 || b > 64) return VGA_COLOR_LGRAY;
    return VGA_COLOR_BLACK;
}

/* 移动光标 */
static void vga_update_cursor(void)
{
    uint16_t pos = vga_cursor_y * VGA_TEXT_WIDTH + vga_cursor_x;
    /* VGA光标寄存器 */
    outb(0x3D4, 14);
    outb(0x3D5, (pos >> 8) & 0xFF);
    outb(0x3D4, 15);
    outb(0x3D5, pos & 0xFF);
}

/* 滚动屏幕 */
static void vga_scroll(void)
{
    for (int i = 0; i < (VGA_TEXT_HEIGHT - 1) * VGA_TEXT_WIDTH; i++) {
        vga_buffer[i] = vga_buffer[i + VGA_TEXT_WIDTH];
    }
    for (int i = (VGA_TEXT_HEIGHT - 1) * VGA_TEXT_WIDTH; i < VGA_TEXT_HEIGHT * VGA_TEXT_WIDTH; i++) {
        vga_buffer[i] = 0x0F00 | ' ';
    }
    vga_cursor_y = VGA_TEXT_HEIGHT - 1;
}

/* 输出一个字符到VGA文本 */
static void vga_putchar(char c, uint8_t fg, uint8_t bg)
{
    if (c == '\n') {
        vga_cursor_x = 0;
        vga_cursor_y++;
    } else if (c == '\r') {
        vga_cursor_x = 0;
    } else if (c == '\b') {
        if (vga_cursor_x > 0) vga_cursor_x--;
        vga_buffer[vga_cursor_y * VGA_TEXT_WIDTH + vga_cursor_x] = (bg << 12) | (fg << 8) | ' ';
    } else if (c == '\t') {
        vga_cursor_x = (vga_cursor_x + 8) & ~7;
    } else {
        vga_buffer[vga_cursor_y * VGA_TEXT_WIDTH + vga_cursor_x] = (bg << 12) | (fg << 8) | (uint8_t)c;
        vga_cursor_x++;
    }

    if (vga_cursor_x >= VGA_TEXT_WIDTH) {
        vga_cursor_x = 0;
        vga_cursor_y++;
    }
    if (vga_cursor_y >= VGA_TEXT_HEIGHT) {
        vga_scroll();
    }

    vga_update_cursor();
}

/* ============================================================================
 * 后端接口实现
 * ========================================================================== */

static int vga_init(void)
{
    if (vga_initialized) return 0;

    serial_puts(0x3F8, "[GFX-VGA] initializing VGA text backend...\n");

    /* 清屏 */
    for (int i = 0; i < VGA_TEXT_WIDTH * VGA_TEXT_HEIGHT; i++) {
        vga_buffer[i] = 0x0F00 | ' ';
    }
    vga_cursor_x = 0;
    vga_cursor_y = 0;
    vga_update_cursor();

    vga_initialized = 1;
    serial_puts(0x3F8, "[GFX-VGA] text mode 80x25 ready\n");

    return 0;
}

static void vga_shutdown(void)
{
    vga_initialized = 0;
    serial_puts(0x3F8, "[GFX-VGA] shutdown\n");
}

static int vga_set_mode(uint32_t w, uint32_t h, uint32_t bpp)
{
    /* VGA文本模式固定80x25，不支持分辨率切换 */
    return 0;
}

static void vga_get_mode(uint32_t *w, uint32_t *h, uint32_t *bpp)
{
    if (w) *w = VGA_TEXT_WIDTH;
    if (h) *h = VGA_TEXT_HEIGHT;
    if (bpp) *bpp = 4; /* 4位颜色 */
}

static void vga_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, gfx_color_t color)
{
    /* VGA文本模式下，用字符块填充 */
    uint8_t fg = vga_color_from_rgb(color);
    uint8_t bg = fg;

    for (uint32_t cy = y; cy < y + h && cy < VGA_TEXT_HEIGHT; cy++) {
        for (uint32_t cx = x; cx < x + w && cx < VGA_TEXT_WIDTH; cx++) {
            vga_buffer[cy * VGA_TEXT_WIDTH + cx] = (bg << 12) | (fg << 8) | ' ';
        }
    }
}

static void vga_put_pixel(uint32_t x, uint32_t y, gfx_color_t color)
{
    /* VGA文本模式不支持像素绘制，忽略 */
}

static void vga_draw_char(uint32_t x, uint32_t y, char c, gfx_color_t fg, gfx_color_t bg)
{
    if (x >= VGA_TEXT_WIDTH || y >= VGA_TEXT_HEIGHT) return;

    uint8_t vga_fg = vga_color_from_rgb(fg);
    uint8_t vga_bg = vga_color_from_rgb(bg);

    vga_buffer[y * VGA_TEXT_WIDTH + x] = (vga_bg << 12) | (vga_fg << 8) | (uint8_t)c;
}

static void vga_blit(void* src, uint32_t sx, uint32_t sy, uint32_t w, uint32_t h, uint32_t dx, uint32_t dy)
{
    /* VGA文本模式不支持Blit，忽略 */
}

static void* vga_get_fb_vaddr(void)
{
    return (void*)VGA_TEXT_ADDR;
}

static uint32_t vga_get_fb_paddr(void)
{
    return VGA_TEXT_ADDR;
}

static void vga_present(void)
{
    /* VGA文本模式直接写入显存，无需刷新 */
}

static int vga_has_double_buffer(void)
{
    return 0;
}

/* ============================================================================
 * 后端注册
 * ========================================================================== */

static gfx_backend_t vga_backend = {
    .type = GFX_BACKEND_VGA,
    .name = "VGA Text",
    .init = vga_init,
    .shutdown = vga_shutdown,
    .set_mode = vga_set_mode,
    .get_mode = vga_get_mode,
    .fill_rect = vga_fill_rect,
    .put_pixel = vga_put_pixel,
    .draw_char = vga_draw_char,
    .blit = vga_blit,
    .get_fb_vaddr = vga_get_fb_vaddr,
    .get_fb_paddr = vga_get_fb_paddr,
    .present = vga_present,
    .has_double_buffer = vga_has_double_buffer
};

void gfx_vga_register(void)
{
    extern int gfx_register_backend(gfx_backend_t *backend);
    gfx_register_backend(&vga_backend);
}

/* VGA应急文本输出（供内核最早期使用） */
void gfx_vga_early_write(const char *s)
{
    while (*s) {
        vga_putchar(*s++, VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    }
}
