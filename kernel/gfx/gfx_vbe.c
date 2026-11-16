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
static uint32_t vbe_pitch_prev = 0;
static uint32_t vbe_width_prev = 0;
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

/* P1-1: 矩形填充按行批量。
 *
 * 原来是逐像素调 vbe_putpixel —— 每次调用都要过一遍边界判断和 bpp 分支。
 * 1920x1080 整屏填充 = 2,073,600 次函数调用，这是 32 位桌面刷新最贵的一笔，
 * 也让"多一次 8.3MB 的 present 拷贝"显得更吃亏。
 *
 * 32bpp 下每行就是 w 个 uint32，用 rep stosl 一次填完：整屏从 207 万次调用
 * 降到 1080 次行填充。24/16bpp 保留原路径 —— 那两个色深不是热点，改成
 * 逐字节/逐字填充反而容易在边界上出错。
 *
 * 顺带补了原实现缺的裁剪：原来只有 "i < vbe_width" 作为循环条件，x 超出屏幕
 * 时会空转 w 次；现在先把 w/h 裁到屏幕内，再填。
 *
 * 注意 color 不做 alpha 补全：vbe_putpixel 在 32bpp 下也是直接写 color，
 * 这里必须保持一致，否则同一个颜色经两条路径画出来会不同。 */
static void vbe_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, gfx_color_t color)
{
    if (!vbe_framebuffer || w == 0 || h == 0) return;
    if (x >= vbe_width || y >= vbe_height) return;

    if (x + w > vbe_width)  w = vbe_width - x;
    if (y + h > vbe_height) h = vbe_height - y;

    if (vbe_bpp == 32) {
        uint32_t *base = (uint32_t *)vbe_framebuffer;
        uint32_t stride = vbe_pitch / 4;
        for (uint32_t j = 0; j < h; j++) {
            uint32_t *row = base + (uint32_t)(y + j) * stride + x;
            uint32_t n = w;
            __asm__ volatile("cld; rep stosl"
                             : "+D"(row), "+c"(n)
                             : "a"((uint32_t)color)
                             : "memory");
        }
        return;
    }

    for (uint32_t j = y; j < y + h; j++) {
        for (uint32_t i = x; i < x + w; i++) {
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

/* 把绘制目标整帧提交到硬件 LFB。
 *
 * 原来这里是空函数: 绘制全部直写 LFB, "present" 无事可做。代价是单缓冲 ——
 * 拖动窗口会留残影, 而且"画进缓冲"和"显示出来"是同一件事, 没有任何中间
 * 状态可供观测 (P3-13 排查时就因此看不到"到底画到哪了")。
 *
 * 现在约定: 三条绘制路径(gfx_* 后端 / gfx_write_pixel_rgb 老 API /
 * hpdesk 的 gfx64_*) 全部写 gfx_draw_target(= back_buffer), 由这里一次
 * 提交到 vbe_fb_phys(= LFB)。语义与 64 位 lfb_db_present 对齐。 */
static void vbe_present(void)
{
    if (vbe_fb_phys < 0x10000000u) return;

    extern uint8_t *gfx_draw_target;
    extern int gfx_pitch, gfx_height;
    if (!gfx_draw_target) return;

    /* 尺寸必须取全局 gfx_pitch/gfx_height, 不能用 vbe_pitch/vbe_height:
     * 后者是后端自己字段, 实测残留在 1024x768@pitch4096, 而真实模式是
     * 1920x1080@pitch7680 —— 用后者只提交前 3MB(约 402 行), 桌面下半截
     * 永远是黑的。 */
    uint32_t pitch  = (uint32_t)gfx_pitch;
    uint32_t height = (uint32_t)gfx_height;
    if (!pitch || !height) return;

    uint8_t *dst = (uint8_t *)(uintptr_t)vbe_fb_phys;
    const uint8_t *src = gfx_draw_target;
    if (src == dst) return;   /* 已经是直写模式, 无需提交 */

    uint32_t bytes = pitch * height;
    uint32_t nw = bytes / 4u;
    uint32_t *d32 = (uint32_t *)dst;
    const uint32_t *s32 = (const uint32_t *)src;
    for (uint32_t i = 0; i < nw; i++) d32[i] = s32[i];
    for (uint32_t i = nw * 4u; i < bytes; i++) dst[i] = src[i];
}

/* 局部提交: 只把 [x,y,w,h) 这块从绘制目标拷到 LFB。
 *
 * hpdesk 调它的场景是光标擦旧/铺新(6724/6725)和拖动窗口(6774) —— 都是
 * 几十像素见方的小矩形。整帧提交是 8.29MB/32.6M 周期, 而一个小矩形是
 * 几百字节, 差三个数量级。没有这条路径, 32 位的双缓冲就只能"每帧全拷",
 * 光标一动就卡。 */
void vbe_present_region(int x, int y, int w, int h)
{
    if (vbe_fb_phys < 0x10000000u) return;
    if (w <= 0 || h <= 0) return;

    extern uint8_t *gfx_draw_target;
    extern int gfx_pitch, gfx_height, gfx_width;
    if (!gfx_draw_target) return;

    /* 裁剪到屏幕: 光标在边缘时矩形会出界 */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > gfx_width)  w = gfx_width - x;
    if (y + h > gfx_height) h = gfx_height - y;
    if (w <= 0 || h <= 0) return;

    uint32_t stride = (uint32_t)gfx_pitch;
    uint8_t *dst = (uint8_t *)(uintptr_t)vbe_fb_phys;
    const uint8_t *src = gfx_draw_target;
    uint32_t row_bytes = (uint32_t)w * 4u;   /* 当前只有 32bpp 走这条 */

    for (int j = 0; j < h; j++) {
        uint8_t *d = dst + (uint32_t)(y + j) * stride + (uint32_t)x * 4u;
        const uint8_t *s2 = src + (uint32_t)(y + j) * stride + (uint32_t)x * 4u;
        uint32_t nw = row_bytes / 4u;
        uint32_t *d32 = (uint32_t *)d;
        const uint32_t *s32 = (const uint32_t *)s2;
        for (uint32_t i = 0; i < nw; i++) d32[i] = s32[i];
    }
}

static void* vbe_get_fb_vaddr(void)
{
    return vbe_framebuffer;
}

static uint32_t vbe_get_fb_paddr(void)
{
    return vbe_fb_phys;
}

/* 供 gfx_flip 调用: 有 VBE 在管提交就返回 0, 由通用路径接管则返回 -1 */
/* 供 gfx64_present_region 调用 */
int gfx_backend_present_region(int x, int y, int w, int h)
{
    if (vbe_fb_phys < 0x10000000u) return -1;
    extern void vbe_present_region(int, int, int, int);
    vbe_present_region(x, y, w, h);
    return 0;
}

int gfx_backend_present(void)
{
    if (vbe_fb_phys < 0x10000000u) return -1;
    vbe_present();
    return 0;
}

static int vbe_has_double_buffer(void)
{
    /* 现在有: 绘制落 gfx_draw_target, 由 vbe_present 一次提交。
     * 前提是 gfx_draw_target 指向一块与 LFB 不同的内存(back_buffer)。 */
    extern uint8_t *gfx_draw_target;
    return (gfx_draw_target && vbe_fb_phys >= 0x10000000u) ? 1 : 0;
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

/* D-7: VBE 后端在初始化时把 gfx_* 快照进 vbe_width/height/bpp/pitch/framebuffer
 * 这组静态量。32 位真彩色路径是「先注册后端, 再由 bochs dispi 改模式」, 于是
 * 后端内部还停在 320x200@8bpp: vbe_pitch=320 而实际每行 2560 字节,
 * vbe_putpixel 写成 fb[y*80 + x] —— y 被压缩 8 倍, 整屏变成横条纹点阵。
 * 设完模式必须回来同步一次。 */
void gfx_vbe_sync_mode(void)
{
    extern int gfx_width;
    extern int gfx_height;
    extern int gfx_bpp;
    extern uint8_t *gfx_fb_addr;

    vbe_width  = (uint32_t)gfx_width;
    vbe_height = (uint32_t)gfx_height;
    vbe_bpp    = (uint32_t)gfx_bpp;
    vbe_pitch  = vbe_width * (vbe_bpp / 8);
    /* 绘制目标与硬件基址拆开了: 原来两者都 = gfx_fb_addr, 于是"往哪画"
     * 和"往哪提交"绑定成一个值, 双缓冲在这种绑定下不可能成立。
     * vbe_framebuffer 是绘制目标(可以是 back_buffer), vbe_fb_phys 才是 LFB。 */
    extern uint8_t *gfx_draw_target;
    vbe_framebuffer = gfx_draw_target ? gfx_draw_target : gfx_fb_addr;
    vbe_fb_phys = (uint32_t)(uintptr_t)gfx_fb_addr;

    /* 只在值真的变了才打印, 因为这个函数会在每次渲染前被调用 */
    if (vbe_pitch != vbe_pitch_prev || vbe_width != vbe_width_prev) {
        vbe_pitch_prev = vbe_pitch;
        vbe_width_prev = vbe_width;
        serial_puts(0x3F8, "[GFX-VBE] sync: ");
        serial_puthex(0x3F8, vbe_width);
        serial_puts(0x3F8, "x");
        serial_puthex(0x3F8, vbe_height);
        serial_puts(0x3F8, "@");
        serial_puthex(0x3F8, vbe_bpp);
        serial_puts(0x3F8, "bpp pitch=");
        serial_puthex(0x3F8, vbe_pitch);
        serial_puts(0x3F8, "\n");
    }
}

void gfx_vbe_register(void)
{
    extern int gfx_register_backend(gfx_backend_t *backend);
    gfx_register_backend(&vbe_backend);
    serial_puts(0x3F8, "[GFX-VBE] backend registered\n");
}
