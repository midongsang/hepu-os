/* ============================================================================
 * HepuOS 可插拔图形子系统 - GOP后端
 * UEFI GOP帧缓冲后端，基于现有vga_gfx.c实现
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "graphics.h"
#include "vga_gfx.h"
#include "serial.h"
#include "font8x16.h"

/* 外部变量（来自vga_gfx.c） */
extern int gfx_width;
extern int gfx_height;
extern int gfx_bpp;
extern uint8_t *gfx_fb_addr;
extern uint8_t *framebuffer;
extern int gfx_refresh_rate;

/* 后端状态 */
static int gop_initialized = 0;
static uint32_t gop_width = 0;
static uint32_t gop_height = 0;
static uint32_t gop_bpp = 0;

/* 帧缓冲物理地址（从multiboot获取） */
uint32_t gfx_fb_phys = 0x80000000; /* QEMU默认地址 */

/* ============================================================================
 * 后端接口实现
 * ========================================================================== */

static int gop_init(void)
{
    if (gop_initialized) return 0;

    serial_puts(0x3F8, "[GFX-GOP] initializing GOP backend...\n");

    /* 获取当前图形模式信息 */
    gop_width = gfx_width;
    gop_height = gfx_height;
    gop_bpp = gfx_bpp;

    if (gop_width == 0 || gop_height == 0) {
        serial_puts(0x3F8, "[GFX-GOP] no active graphics mode\n");
        return -1;
    }

    gop_initialized = 1;

    serial_puts(0x3F8, "[GFX-GOP] mode: ");
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puthex(0x3F8, gop_width);
    serial_puts(0x3F8, "x");
    serial_puthex(0x3F8, gop_height);
    serial_puts(0x3F8, "@");
    serial_puthex(0x3F8, gop_bpp);
    serial_puts(0x3F8, "bpp\n");

    return 0;
}

static void gop_shutdown(void)
{
    gop_initialized = 0;
    serial_puts(0x3F8, "[GFX-GOP] shutdown\n");
}

static int gop_set_mode(uint32_t w, uint32_t h, uint32_t bpp)
{
    /* GOP模式在引导时已设置，运行时不支持动态切换 */
    gop_width = gfx_width;
    gop_height = gfx_height;
    gop_bpp = gfx_bpp;
    return 0;
}

static void gop_get_mode(uint32_t *w, uint32_t *h, uint32_t *bpp)
{
    if (w) *w = gop_width;
    if (h) *h = gop_height;
    if (bpp) *bpp = gop_bpp;
}

static void gop_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, gfx_color_t color)
{
    gfx_fillrect_rgb((int)x, (int)y, (int)w, (int)h, color);
}

static void gop_put_pixel(uint32_t x, uint32_t y, gfx_color_t color)
{
    gfx_putpixel_rgb((int)x, (int)y, color);
}

static void gop_draw_char(uint32_t x, uint32_t y, char c, gfx_color_t fg, gfx_color_t bg)
{
    if ((uint8_t)c > 127) c = '?';

    const uint8_t *glyph = &hepu_font8x16[(uint8_t)c * 16];

    for (int row = 0; row < 16; row++) {
        uint8_t line = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (line & (0x80 >> col)) {
                gfx_putpixel_rgb((int)(x + col), (int)(y + row), fg);
            } else if (bg != 0x00000000) {
                /* 透明背景不绘制 */
                gfx_putpixel_rgb((int)(x + col), (int)(y + row), bg);
            }
        }
    }
}

static void gop_blit(void* src, uint32_t sx, uint32_t sy, uint32_t w, uint32_t h, uint32_t dx, uint32_t dy)
{
    /* 软件Blit实现 */
    uint32_t *src_buf = (uint32_t *)src;
    uint32_t *dst_buf = (uint32_t *)framebuffer;
    int pitch = gfx_width;

    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            if (dx + x < gop_width && dy + y < gop_height) {
                dst_buf[(dy + y) * pitch + (dx + x)] = src_buf[(sy + y) * w + (sx + x)];
            }
        }
    }
}

static void* gop_get_fb_vaddr(void)
{
    return framebuffer;
}

static uint32_t gop_get_fb_paddr(void)
{
    return gfx_fb_phys;
}

static void gop_present(void)
{
    extern void gfx_flip(void);
    gfx_flip();
}

static int gop_has_double_buffer(void)
{
    return (framebuffer != NULL) ? 1 : 0;
}

/* ============================================================================
 * 后端注册
 * ========================================================================== */

static gfx_backend_t gop_backend = {
    .type = GFX_BACKEND_GOP,
    .name = "UEFI GOP",
    .init = gop_init,
    .shutdown = gop_shutdown,
    .set_mode = gop_set_mode,
    .get_mode = gop_get_mode,
    .fill_rect = gop_fill_rect,
    .put_pixel = gop_put_pixel,
    .draw_char = gop_draw_char,
    .blit = gop_blit,
    .get_fb_vaddr = gop_get_fb_vaddr,
    .get_fb_paddr = gop_get_fb_paddr,
    .present = gop_present,
    .has_double_buffer = gop_has_double_buffer
};

void gfx_gop_register(void)
{
    extern int gfx_register_backend(gfx_backend_t *backend);
    gfx_register_backend(&gop_backend);
}
