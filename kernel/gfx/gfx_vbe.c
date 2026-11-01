/* ============================================================================
 * HepuOS 可插拔图形子系统 - VBE后端
 * VESA BIOS Extensions后端，支持传统BIOS高分辨率图形模式
 * 参考: VBE 3.0 Specification
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "graphics.h"
#include "serial.h"
#include "font8x16.h"

/* VBE信息块 */
typedef struct {
    char     vbe_signature[4];  /* "VESA" */
    uint16_t vbe_version;       /* 0x0300 = VBE 3.0 */
    uint32_t oem_string_ptr;    /* 远指针 */
    uint32_t capabilities;      /* 能力标志 */
    uint32_t video_mode_ptr;    /* 模式列表远指针 */
    uint16_t total_memory;      /* 64KB块数 */
    uint16_t oem_software_rev;
    uint32_t oem_vendor_name_ptr;
    uint32_t oem_product_name_ptr;
    uint32_t oem_product_rev_ptr;
    uint8_t  reserved[222];
    uint8_t  oem_data[256];
} __attribute__((packed)) vbe_info_block_t;

/* VBE模式信息块 */
typedef struct {
    uint16_t mode_attributes;
    uint8_t  win_a_attributes;
    uint8_t  win_b_attributes;
    uint16_t win_granularity;
    uint16_t win_size;
    uint16_t win_a_segment;
    uint16_t win_b_segment;
    uint32_t win_func_ptr;
    uint16_t bytes_per_scanline;
    uint16_t x_resolution;
    uint16_t y_resolution;
    uint8_t  x_char_size;
    uint8_t  y_char_size;
    uint8_t  number_of_planes;
    uint8_t  bits_per_pixel;
    uint8_t  number_of_banks;
    uint8_t  memory_model;
    uint8_t  bank_size;
    uint8_t  number_of_image_pages;
    uint8_t  reserved1;
    uint8_t  red_mask_size;
    uint8_t  red_field_position;
    uint8_t  green_mask_size;
    uint8_t  green_field_position;
    uint8_t  blue_mask_size;
    uint8_t  blue_field_position;
    uint8_t  reserved_mask_size;
    uint8_t  reserved_field_position;
    uint8_t  direct_color_mode_info;
    uint32_t phys_base_ptr;       /* 线性帧缓冲物理地址 */
    uint32_t reserved2;
    uint16_t reserved3;
    /* VBE 2.0+扩展 */
    uint16_t lin_bytes_per_scanline;
    uint8_t  bnk_number_of_image_pages;
    uint8_t  lin_number_of_image_pages;
    uint8_t  lin_red_mask_size;
    uint8_t  lin_red_field_position;
    uint8_t  lin_green_mask_size;
    uint8_t  lin_green_field_position;
    uint8_t  lin_blue_mask_size;
    uint8_t  lin_blue_field_position;
    uint8_t  lin_reserved_mask_size;
    uint8_t  lin_reserved_field_position;
    uint32_t max_pixel_clock;
    uint8_t  reserved4[189];
} __attribute__((packed)) vbe_mode_info_t;

/* VBE模式属性 */
#define VBE_MODE_ATTR_SUPPORTED     0x0001
#define VBE_MODE_ATTR_TTY           0x0004
#define VBE_MODE_ATTR_COLOR         0x0008
#define VBE_MODE_ATTR_GRAPHICS      0x0010
#define VBE_MODE_ATTR_NOT_VGA       0x0020
#define VBE_MODE_ATTR_NOT_BANKED    0x0040
#define VBE_MODE_ATTR_LINEAR        0x0080

/* VBE常用模式 */
#define VBE_MODE_640x480_32    0x112
#define VBE_MODE_800x600_32    0x115
#define VBE_MODE_1024x768_32   0x118
#define VBE_MODE_1280x1024_32  0x11B
#define VBE_MODE_1920x1080_32  0x14B

/* 后端状态 */
static int vbe_initialized = 0;
static uint32_t vbe_width = 0;
static uint32_t vbe_height = 0;
static uint32_t vbe_bpp = 0;
static uint32_t vbe_pitch = 0;
static uint8_t *vbe_framebuffer = NULL;
static uint32_t vbe_fb_phys = 0;
static uint16_t vbe_current_mode = 0;

/* VBE模式表（优先使用线性帧缓冲的32位模式） */
static const uint16_t vbe_preferred_modes[] = {
    VBE_MODE_1920x1080_32,
    VBE_MODE_1280x1024_32,
    VBE_MODE_1024x768_32,
    VBE_MODE_800x600_32,
    VBE_MODE_640x480_32,
    0xFFFF  /* 结束标记 */
};

/* ============================================================================
 * VBE BIOS调用（实模式下通过BIOS中断0x10）
 * 注意：这些函数需要在实模式下调用，保护模式下需要使用VBE PMI
 * 当前实现假设引导加载程序已设置好VBE模式并传递了帧缓冲信息
 * ========================================================================== */

/* 设置VBE模式（由引导加载程序调用，内核中仅记录信息） */
static int vbe_set_mode(uint16_t mode)
{
    /* 保护模式下无法直接调用BIOS中断
     * 实际模式设置由引导加载程序完成
     * 这里仅记录模式号 */
    vbe_current_mode = mode;
    serial_puts(0x3F8, "[GFX-VBE] mode set to 0x");
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puthex(0x3F8, mode);
    serial_puts(0x3F8, "\n");
    return 0;
}

/* 从multiboot/引导参数获取VBE帧缓冲信息 */
static int vbe_detect_from_boot_params(void)
{
    /* 检查是否有VBE帧缓冲信息（由引导加载程序传递）
     * 当前使用默认值，实际实现应从引导参数读取 */
    extern int gfx_width;
    extern int gfx_height;
    extern int gfx_bpp;
    extern uint8_t *gfx_fb_addr;

    if (gfx_width > 0 && gfx_height > 0 && gfx_fb_addr != NULL) {
        vbe_width = (uint32_t)gfx_width;
        vbe_height = (uint32_t)gfx_height;
        vbe_bpp = (uint32_t)gfx_bpp;
        vbe_pitch = vbe_width * (vbe_bpp / 8);
        vbe_framebuffer = gfx_fb_addr;
        vbe_fb_phys = 0xFD000000; /* 默认VBE帧缓冲物理地址 */

        serial_puts(0x3F8, "[GFX-VBE] detected from boot params: ");
        serial_puthex(0x3F8, vbe_width);
        serial_puts(0x3F8, "x");
        serial_puthex(0x3F8, vbe_height);
        serial_puts(0x3F8, "@");
        serial_puthex(0x3F8, vbe_bpp);
        serial_puts(0x3F8, "bpp\n");
        return 0;
    }

    serial_puts(0x3F8, "[GFX-VBE] no VBE info from boot params\n");
    return -1;
}

/* ============================================================================
 * 后端接口实现
 * ========================================================================== */

static int vbe_init(void)
{
    if (vbe_initialized) return 0;

    serial_puts(0x3F8, "[GFX-VBE] initializing VBE backend...\n");

    /* 从引导参数检测VBE信息 */
    if (vbe_detect_from_boot_params() != 0) {
        serial_puts(0x3F8, "[GFX-VBE] init failed: no VBE info\n");
        return -1;
    }

    vbe_initialized = 1;

    serial_puts(0x3F8, "[GFX-VBE] backend initialized (");
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puthex(0x3F8, vbe_width);
    serial_puts(0x3F8, "x");
    serial_puthex(0x3F8, vbe_height);
    serial_puts(0x3F8, "@");
    serial_puthex(0x3F8, vbe_bpp);
    serial_puts(0x3F8, "bpp, pitch=");
    serial_puthex(0x3F8, vbe_pitch);
    serial_puts(0x3F8, ")\n");

    return 0;
}

static int vbe_set_mode_api(uint32_t w, uint32_t h, uint32_t bpp)
{
    /* VBE模式切换需要BIOS调用，保护模式下不支持动态切换
     * 记录请求的模式，实际切换由引导加载程序完成 */
    vbe_width = w;
    vbe_height = h;
    vbe_bpp = bpp;
    vbe_pitch = w * (bpp / 8);
    return 0;
}

static void vbe_putpixel(uint32_t x, uint32_t y, gfx_color_t color)
{
    if (!vbe_framebuffer || x >= vbe_width || y >= vbe_height) return;

    if (vbe_bpp == 32) {
        uint32_t *fb = (uint32_t *)vbe_framebuffer;
        fb[y * (vbe_pitch / 4) + x] = color;
    } else if (vbe_bpp == 24) {
        uint8_t *fb = vbe_framebuffer + y * vbe_pitch + x * 3;
        fb[0] = color & 0xFF;
        fb[1] = (color >> 8) & 0xFF;
        fb[2] = (color >> 16) & 0xFF;
    } else if (vbe_bpp == 16) {
        uint16_t *fb = (uint16_t *)vbe_framebuffer;
        uint16_t r = (color >> 19) & 0x1F;
        uint16_t g = (color >> 10) & 0x3F;
        uint16_t b = (color >> 3) & 0x1F;
        fb[y * (vbe_pitch / 2) + x] = (r << 11) | (g << 5) | b;
    }
}

static void vbe_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, gfx_color_t color)
{
    if (!vbe_framebuffer) return;

    for (uint32_t j = y; j < y + h && j < vbe_height; j++) {
        for (uint32_t i = x; i < x + w && i < vbe_width; i++) {
            vbe_putpixel(i, j, color);
        }
    }
}

static void vbe_draw_char(uint32_t x, uint32_t y, char c, gfx_color_t fg, gfx_color_t bg)
{
    if (!vbe_framebuffer) return;
    if ((uint8_t)c > 127) c = '?';

    /* 使用8x16字体 */
    const uint8_t *glyph = &hepu_font8x16[(uint8_t)c * 16];

    for (int row = 0; row < 16; row++) {
        uint8_t line = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (line & (0x80 >> col)) {
                vbe_putpixel(x + col, y + row, fg);
            } else if (bg != 0x00000000) {
                vbe_putpixel(x + col, y + row, bg);
            }
        }
    }
}

static void vbe_clear(gfx_color_t color)
{
    if (!vbe_framebuffer) return;
    vbe_fill_rect(0, 0, vbe_width, vbe_height, color);
}

static void vbe_present(void)
{
    /* VBE直接写入帧缓冲，无需额外操作 */
}

static void* vbe_get_fb_vaddr(void)
{
    return vbe_framebuffer;
}

static uint32_t vbe_get_fb_paddr(void)
{
    return vbe_fb_phys;
}

static int vbe_has_double_buffer(void)
{
    return 0; /* VBE默认单缓冲 */
}

static void vbe_get_mode(uint32_t *w, uint32_t *h, uint32_t *bpp)
{
    if (w) *w = vbe_width;
    if (h) *h = vbe_height;
    if (bpp) *bpp = vbe_bpp;
}

/* ============================================================================
 * 后端注册
 * ========================================================================== */

static gfx_backend_t vbe_backend = {
    .type = GFX_BACKEND_VBE,
    .name = "VBE",
    .init = vbe_init,
    .shutdown = NULL,
    .set_mode = vbe_set_mode_api,
    .get_mode = vbe_get_mode,
    .fill_rect = vbe_fill_rect,
    .put_pixel = vbe_putpixel,
    .draw_char = vbe_draw_char,
    .blit = NULL,
    .get_fb_vaddr = vbe_get_fb_vaddr,
    .get_fb_paddr = vbe_get_fb_paddr,
    .present = vbe_present,
    .has_double_buffer = vbe_has_double_buffer,
};

void gfx_vbe_register(void)
{
    extern int gfx_register_backend(gfx_backend_t *backend);
    gfx_register_backend(&vbe_backend);
    serial_puts(0x3F8, "[GFX-VBE] backend registered\n");
}
