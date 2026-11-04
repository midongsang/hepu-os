/* ============================================================================
 * HepuOS 可插拔图形子系统 - 核心管理
 * 融合VGA/VBE/virtio-gpu优点，全新自研设计
 *
 * 功能：后端探测、注册、切换、显存分区管理
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "graphics.h"
#include "vga.h"
#include "kheap.h"
#include "serial.h"

/* 最大支持的后端数量 */
#define GFX_MAX_BACKENDS 8

/* 已注册的后端 */
static gfx_backend_t *registered_backends[GFX_MAX_BACKENDS];
static int backend_count = 0;

/* 当前激活的后端 */
static gfx_backend_t *active_backend = NULL;

/* 图形子系统配置 */
static gfx_config_t gfx_config;
static int gfx_initialized = 0;

/* 显存分区管理 */
#define GFX_VRAM_MAX_PARTITIONS 8
static uint32_t vram_partitions[GFX_VRAM_MAX_PARTITIONS];
static int vram_partition_count = 0;
static uint8_t *vram_base = NULL;
static uint32_t vram_total_size = 0;
static uint32_t vram_used = 0;

/* 简单的显存分配器（首次适配） */
typedef struct {
    uint32_t offset;
    uint32_t size;
    int used;
} vram_block_t;

#define VRAM_MAX_BLOCKS 64
static vram_block_t vram_blocks[VRAM_MAX_BLOCKS];
static int vram_block_count = 0;

/* ============================================================================
 * VGA应急文本输出（内核最早期，内存管理器未就绪时可用）
 * ========================================================================== */

static uint16_t *vga_text_buffer = (uint16_t *)0xB8000;
static int vga_text_x = 0;
static int vga_text_y = 0;
static const int VGA_TEXT_WIDTH = 80;
static const int VGA_TEXT_HEIGHT = 25;

void gfx_vga_early_putchar(char c)
{
    if (c == '\n') {
        vga_text_x = 0;
        vga_text_y++;
    } else if (c == '\r') {
        vga_text_x = 0;
    } else if (c == '\b') {
        if (vga_text_x > 0) vga_text_x--;
        vga_text_buffer[vga_text_y * VGA_TEXT_WIDTH + vga_text_x] = 0x0F00 | ' ';
    } else {
        uint16_t attr = 0x0F00; /* 白底黑字 */
        vga_text_buffer[vga_text_y * VGA_TEXT_WIDTH + vga_text_x] = attr | (uint8_t)c;
        vga_text_x++;
    }

    if (vga_text_x >= VGA_TEXT_WIDTH) {
        vga_text_x = 0;
        vga_text_y++;
    }

    if (vga_text_y >= VGA_TEXT_HEIGHT) {
        /* 向上滚动 */
        for (int i = 0; i < (VGA_TEXT_HEIGHT - 1) * VGA_TEXT_WIDTH; i++) {
            vga_text_buffer[i] = vga_text_buffer[i + VGA_TEXT_WIDTH];
        }
        for (int i = (VGA_TEXT_HEIGHT - 1) * VGA_TEXT_WIDTH; i < VGA_TEXT_HEIGHT * VGA_TEXT_WIDTH; i++) {
            vga_text_buffer[i] = 0x0F00 | ' ';
        }
        vga_text_y = VGA_TEXT_HEIGHT - 1;
    }
}

void gfx_vga_early_puts(const char *s)
{
    while (*s) {
        gfx_vga_early_putchar(*s++);
    }
}

/* ============================================================================
 * 后端注册与管理
 * ========================================================================== */

int gfx_register_backend(gfx_backend_t *backend)
{
    if (!backend || backend_count >= GFX_MAX_BACKENDS) {
        return -1;
    }

    /* 检查是否已注册 */
    for (int i = 0; i < backend_count; i++) {
        if (registered_backends[i]->type == backend->type) {
            return -2; /* 已存在 */
        }
    }

    registered_backends[backend_count++] = backend;
    serial_puts(0x3F8, "[GFX] registered backend: ");
    serial_puts(0x3F8, backend->name);
    serial_putc(0x3F8, '\n');

    return 0;
}

gfx_backend_t* gfx_get_active_backend(void)
{
    return active_backend;
}

gfx_backend_type_t gfx_get_backend_type(void)
{
    if (active_backend) return active_backend->type;
    return GFX_BACKEND_NONE;
}

/* 按优先级查找后端：virtio > GOP > VBE > VGA */
static gfx_backend_t* gfx_find_backend_by_priority(void)
{
    gfx_backend_type_t priority[] = {
        GFX_BACKEND_VIRTIO,
        GFX_BACKEND_GOP,
        GFX_BACKEND_VBE,
        GFX_BACKEND_VGA
    };

    for (int p = 0; p < 4; p++) {
        for (int i = 0; i < backend_count; i++) {
            if (registered_backends[i]->type == priority[p]) {
                return registered_backends[i];
            }
        }
    }

    return NULL;
}

int gfx_probe_backend(void)
{
    serial_puts(0x3F8, "[GFX] probing backends...\n");

    /* V7.09: 检查内核启动参数 graphics=xxx */
    extern int boot_get_graphics_backend(void);
    int cmdline_backend = boot_get_graphics_backend();
    if (cmdline_backend > 0) {
        gfx_config.forced_backend = (gfx_backend_type_t)cmdline_backend;
        serial_puts(0x3F8, "[GFX] using cmdline backend: ");
        extern void serial_puthex(uint16_t, uint32_t);
        serial_puthex(0x3F8, (uint32_t)cmdline_backend);
        serial_puts(0x3F8, "\n");
    }

    /* 如果用户指定了后端，直接使用 */
    if (gfx_config.forced_backend != GFX_BACKEND_NONE) {
        for (int i = 0; i < backend_count; i++) {
            if (registered_backends[i]->type == gfx_config.forced_backend) {
                active_backend = registered_backends[i];
                serial_puts(0x3F8, "[GFX] using forced backend: ");
                serial_puts(0x3F8, active_backend->name);
                serial_putc(0x3F8, '\n');
                return 0;
            }
        }
        serial_puts(0x3F8, "[GFX] forced backend not found, falling back to auto\n");
    }

    /* 自动探测，按优先级尝试初始化每个后端 */
    gfx_backend_type_t priority[] = {
        GFX_BACKEND_VIRTIO,
        GFX_BACKEND_GOP,
        GFX_BACKEND_VBE,
        GFX_BACKEND_VGA
    };

    for (int p = 0; p < 4; p++) {
        for (int i = 0; i < backend_count; i++) {
            if (registered_backends[i]->type == priority[p]) {
                /* 尝试初始化后端 */
                if (registered_backends[i]->init) {
                    int ret = registered_backends[i]->init();
                    if (ret == 0) {
                        active_backend = registered_backends[i];
                        serial_puts(0x3F8, "[GFX] selected backend: ");
                        serial_puts(0x3F8, active_backend->name);
                        serial_putc(0x3F8, '\n');
                        return 0;
                    } else {
                        serial_puts(0x3F8, "[GFX] backend ");
                        serial_puts(0x3F8, registered_backends[i]->name);
                        serial_puts(0x3F8, " init failed, trying next\n");
                    }
                } else {
                    /* 没有init函数，直接选中 */
                    active_backend = registered_backends[i];
                    serial_puts(0x3F8, "[GFX] selected backend: ");
                    serial_puts(0x3F8, active_backend->name);
                    serial_putc(0x3F8, '\n');
                    return 0;
                }
            }
        }
    }

    serial_puts(0x3F8, "[GFX] no backend found!\n");
    return -1;
}

int gfx_switch_backend(gfx_backend_type_t type)
{
    /* 关闭当前后端 */
    if (active_backend && active_backend->shutdown) {
        active_backend->shutdown();
    }

    /* 查找新后端 */
    for (int i = 0; i < backend_count; i++) {
        if (registered_backends[i]->type == type) {
            active_backend = registered_backends[i];
            if (active_backend->init) {
                return active_backend->init();
            }
            return 0;
        }
    }

    return -1;
}

/* ============================================================================
 * 显存分区管理（用户可自定义大小）
 * ========================================================================== */

int gfx_set_vram_partition(uint32_t index, uint32_t size)
{
    if (index >= GFX_VRAM_MAX_PARTITIONS) {
        return -1;
    }

    if ((int)index >= vram_partition_count) {
        vram_partition_count = (int)index + 1;
    }

    vram_partitions[index] = size;
    serial_puts(0x3F8, "[GFX] vram partition ");
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puthex(0x3F8, index);
    serial_puts(0x3F8, " size=");
    serial_puthex(0x3F8, size);
    serial_putc(0x3F8, '\n');

    return 0;
}

void gfx_get_vram_info(gfx_vram_info_t *info)
{
    if (!info) return;

    memset(info, 0, sizeof(gfx_vram_info_t));
    info->total_size = vram_total_size;
    info->used_size = vram_used;
    info->free_size = vram_total_size - vram_used;
    info->partition_count = vram_partition_count;

    for (int i = 0; i < vram_partition_count && i < 8; i++) {
        info->partition_sizes[i] = vram_partitions[i];
    }
}

static int gfx_init_vram_allocator(uint32_t base_addr, uint32_t size)
{
    vram_base = (uint8_t *)base_addr;
    vram_total_size = size;
    vram_used = 0;
    vram_block_count = 0;

    /* 初始化第一个空闲块 */
    vram_blocks[0].offset = 0;
    vram_blocks[0].size = size;
    vram_blocks[0].used = 0;
    vram_block_count = 1;

    serial_puts(0x3F8, "[GFX] vram allocator init: ");
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puthex(0x3F8, size);
    serial_puts(0x3F8, " bytes at 0x");
    serial_puthex(0x3F8, base_addr);
    serial_putc(0x3F8, '\n');

    return 0;
}

void* gfx_vram_alloc(uint32_t size)
{
    if (!vram_base || size == 0) return NULL;

    /* 对齐到4KB */
    size = (size + 4095) & ~4095;

    /* 首次适配 */
    for (int i = 0; i < vram_block_count; i++) {
        if (!vram_blocks[i].used && vram_blocks[i].size >= size) {
            /* 如果剩余空间足够，拆分块 */
            if (vram_blocks[i].size > size + 4096 && vram_block_count < VRAM_MAX_BLOCKS) {
                for (int j = vram_block_count; j > i + 1; j--) {
                    vram_blocks[j] = vram_blocks[j - 1];
                }
                vram_blocks[i + 1].offset = vram_blocks[i].offset + size;
                vram_blocks[i + 1].size = vram_blocks[i].size - size;
                vram_blocks[i + 1].used = 0;
                vram_block_count++;
                vram_blocks[i].size = size;
            }

            vram_blocks[i].used = 1;
            vram_used += size;

            return vram_base + vram_blocks[i].offset;
        }
    }

    serial_puts(0x3F8, "[GFX] vram_alloc failed: out of memory\n");
    return NULL;
}

void gfx_vram_free(void *ptr)
{
    if (!ptr || !vram_base) return;

    uint32_t offset = (uint8_t *)ptr - vram_base;

    for (int i = 0; i < vram_block_count; i++) {
        if (vram_blocks[i].used && vram_blocks[i].offset == offset) {
            vram_blocks[i].used = 0;
            vram_used -= vram_blocks[i].size;

            /* 合并相邻空闲块 */
            if (i + 1 < vram_block_count && !vram_blocks[i + 1].used) {
                vram_blocks[i].size += vram_blocks[i + 1].size;
                for (int j = i + 1; j < vram_block_count - 1; j++) {
                    vram_blocks[j] = vram_blocks[j + 1];
                }
                vram_block_count--;
            }
            if (i > 0 && !vram_blocks[i - 1].used) {
                vram_blocks[i - 1].size += vram_blocks[i].size;
                for (int j = i; j < vram_block_count - 1; j++) {
                    vram_blocks[j] = vram_blocks[j + 1];
                }
                vram_block_count--;
            }

            return;
        }
    }
}

/* ============================================================================
 * 字符串绘制
 * ========================================================================== */

void gfx_draw_string(uint32_t x, uint32_t y, const char *str, gfx_color_t fg, gfx_color_t bg)
{
    if (!str || !active_backend || !active_backend->draw_char) return;

    uint32_t cx = x;
    while (*str) {
        if (*str == '\n') {
            y += 16;
            cx = x;
            str++;
            continue;
        }
        active_backend->draw_char(cx, y, *str, fg, bg);
        cx += 8;
        str++;
    }
}

/* ============================================================================
 * 初始化与关闭
 * ========================================================================== */

int gfx_init(gfx_config_t *config)
{
    if (gfx_initialized) {
        return 0;
    }

    serial_puts(0x3F8, "[GFX] initializing graphics subsystem...\n");

    /* 保存配置 */
    if (config) {
        memcpy(&gfx_config, config, sizeof(gfx_config_t));
    } else {
        memset(&gfx_config, 0, sizeof(gfx_config_t));
        gfx_config.default_width = 1024;
        gfx_config.default_height = 768;
        gfx_config.default_bpp = 32;
        gfx_config.double_buffer = 1;
    }

    /* 探测后端（会自动尝试初始化） */
    if (gfx_probe_backend() != 0) {
        serial_puts(0x3F8, "[GFX] no backend available, using VGA text fallback\n");
        return -1;
    }

    /* 后端已在probe中初始化，这里不再重复调用init */

    /* 设置默认模式 */
    if (active_backend->set_mode) {
        active_backend->set_mode(
            gfx_config.default_width,
            gfx_config.default_height,
            gfx_config.default_bpp
        );
    }

    /* 初始化显存分配器（如果后端提供了帧缓冲） */
    if (active_backend->get_fb_vaddr && active_backend->get_fb_paddr) {
        void *fb = active_backend->get_fb_vaddr();
        uint32_t fb_size = gfx_config.default_width * gfx_config.default_height * 4;
        if (fb && fb_size > 0) {
            /* 帧缓冲之后的空间作为显存分区 */
            uint32_t vram_start = (uint32_t)fb + fb_size;
            uint32_t vram_size = gfx_config.vram_partition_size;
            if (vram_size == 0) vram_size = 4 * 1024 * 1024; /* 默认4MB */
            gfx_init_vram_allocator(vram_start, vram_size);
        }
    }

    gfx_initialized = 1;

    serial_puts(0x3F8, "[GFX] graphics subsystem initialized\n");
    return 0;
}

void gfx_shutdown(void)
{
    if (active_backend && active_backend->shutdown) {
        active_backend->shutdown();
    }
    active_backend = NULL;
    gfx_initialized = 0;
    serial_puts(0x3F8, "[GFX] graphics subsystem shutdown\n");
}

/* ============================================================================
 * 多显示器支持（V7.09）
 * ========================================================================== */

static gfx_monitor_t gfx_monitors[GFX_MAX_MONITORS];
static int gfx_monitor_count = 0;
static int gfx_primary_monitor = 0;

int gfx_enum_monitors(gfx_monitor_t *monitors, int max_count)
{
    if (!monitors || max_count <= 0) return 0;

    /* 初始化显示器列表（首次调用时） */
    if (gfx_monitor_count == 0) {
        /* 主显示器：使用当前后端的分辨率 */
        gfx_monitor_t *m = &gfx_monitors[0];
        memset(m, 0, sizeof(gfx_monitor_t));
        m->index = 0;
        strncpy(m->name, "Primary Display", 31);
        m->connected = 1;
        m->primary = 1;
        m->enabled = 1;

        /* 从当前后端获取分辨率 */
        if (active_backend && active_backend->get_mode) {
            active_backend->get_mode(&m->width, &m->height, &m->bpp);
        }
        if (m->width == 0) {
            m->width = 1024;
            m->height = 768;
            m->bpp = 32;
        }
        m->refresh_rate = 60;
        m->physical_width = 340;  /* 约15.6寸 */
        m->physical_height = 190;

        if (active_backend && active_backend->get_fb_paddr) {
            m->framebuffer = active_backend->get_fb_paddr();
        }

        /* 添加默认支持的模式列表 */
        m->mode_count = 0;
        static const uint32_t default_modes[][3] = {
            {640, 480, 60},
            {800, 600, 60},
            {1024, 768, 60},
            {1280, 720, 60},
            {1280, 1024, 60},
            {1366, 768, 60},
            {1440, 900, 60},
            {1600, 900, 60},
            {1920, 1080, 60},
            {1920, 1080, 75},
            {2560, 1440, 60},
        };
        for (int mi = 0; mi < 11 && m->mode_count < GFX_MAX_MODES_PER_MONITOR; mi++) {
            m->modes[m->mode_count].width = default_modes[mi][0];
            m->modes[m->mode_count].height = default_modes[mi][1];
            m->modes[m->mode_count].bpp = 32;
            m->modes[m->mode_count].refresh_rate = default_modes[mi][2];
            m->mode_count++;
        }

        gfx_monitor_count = 1;

        serial_puts(0x3F8, "[GFX] monitor 0: ");
        extern void serial_puthex(uint16_t, uint32_t);
        serial_puthex(0x3F8, m->width);
        serial_puts(0x3F8, "x");
        serial_puthex(0x3F8, m->height);
        serial_puts(0x3F8, "@");
        serial_puthex(0x3F8, m->bpp);
        serial_puts(0x3F8, "bpp\n");
    }

    /* 复制到输出缓冲区 */
    int count = gfx_monitor_count < max_count ? gfx_monitor_count : max_count;
    for (int i = 0; i < count; i++) {
        memcpy(&monitors[i], &gfx_monitors[i], sizeof(gfx_monitor_t));
    }

    return count;
}

gfx_monitor_t* gfx_get_primary_monitor(void)
{
    if (gfx_monitor_count == 0) {
        gfx_enum_monitors(gfx_monitors, GFX_MAX_MONITORS);
    }
    return &gfx_monitors[gfx_primary_monitor];
}

int gfx_set_primary_monitor(uint32_t index)
{
    if (index >= (uint32_t)gfx_monitor_count) return -1;
    if (!gfx_monitors[index].connected || !gfx_monitors[index].enabled) return -1;

    gfx_monitors[gfx_primary_monitor].primary = 0;
    gfx_monitors[index].primary = 1;
    gfx_primary_monitor = (int)index;

    serial_puts(0x3F8, "[GFX] primary monitor set to ");
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puthex(0x3F8, index);
    serial_puts(0x3F8, "\n");

    return 0;
}

int gfx_set_monitor_mode(uint32_t index, uint32_t width, uint32_t height, uint32_t bpp)
{
    if (index >= (uint32_t)gfx_monitor_count) return -1;
    if (!gfx_monitors[index].connected) return -1;

    /* 更新显示器信息 */
    gfx_monitors[index].width = width;
    gfx_monitors[index].height = height;
    gfx_monitors[index].bpp = bpp;

    /* 如果是主显示器，通知后端切换模式 */
    if (index == (uint32_t)gfx_primary_monitor && active_backend && active_backend->set_mode) {
        active_backend->set_mode(width, height, bpp);
    }

    serial_puts(0x3F8, "[GFX] monitor ");
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puthex(0x3F8, index);
    serial_puts(0x3F8, " mode set to ");
    serial_puthex(0x3F8, width);
    serial_puts(0x3F8, "x");
    serial_puthex(0x3F8, height);
    serial_puts(0x3F8, "@");
    serial_puthex(0x3F8, bpp);
    serial_puts(0x3F8, "bpp\n");

    return 0;
}

int gfx_enable_monitor(uint32_t index, int enable)
{
    if (index >= (uint32_t)gfx_monitor_count) return -1;
    if (!gfx_monitors[index].connected) return -1;

    gfx_monitors[index].enabled = enable ? 1 : 0;

    serial_puts(0x3F8, "[GFX] monitor ");
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puthex(0x3F8, index);
    serial_puts(0x3F8, enable ? " enabled\n" : " disabled\n");

    return 0;
}

int gfx_get_monitor_count(void)
{
    if (gfx_monitor_count == 0) {
        gfx_enum_monitors(gfx_monitors, GFX_MAX_MONITORS);
    }
    return gfx_monitor_count;
}

/* ============================================================================
 * V7.29: EDID解析和多显示器完善
 * ========================================================================== */

/* 解析EDID厂商ID（3个大写字母） */
static void edid_decode_manufacturer(uint16_t id, char *out)
{
    /* EDID厂商ID是5位编码的3个字母 */
    out[0] = 'A' + ((id >> 10) & 0x1F) - 1;
    out[1] = 'A' + ((id >> 5) & 0x1F) - 1;
    out[2] = 'A' + (id & 0x1F) - 1;
    out[3] = '\0';
}

/* 解析EDID数据 */
int gfx_edid_parse(const uint8_t *edid_data, uint32_t size, gfx_monitor_t *monitor)
{
    if (!edid_data || !monitor || size < 128) return -1;

    /* 验证EDID头 */
    if (edid_data[0] != 0x00 || edid_data[1] != 0xFF ||
        edid_data[2] != 0xFF || edid_data[3] != 0xFF ||
        edid_data[4] != 0xFF || edid_data[5] != 0xFF ||
        edid_data[6] != 0xFF || edid_data[7] != 0x00) {
        return -2;
    }

    /* 验证校验和 */
    uint8_t checksum = 0;
    for (uint32_t i = 0; i < 128; i++) {
        checksum += edid_data[i];
    }
    if (checksum != 0) {
        serial_puts(0x3F8, "[GFX-EDID] checksum mismatch\n");
        /* 校验和不匹配但继续解析（某些显示器EDID有问题） */
    }

    /* 解析厂商ID */
    uint16_t manuf_id = (edid_data[8] << 8) | edid_data[9];
    edid_decode_manufacturer(manuf_id, monitor->manufacturer);

    /* 解析产品代码 */
    uint16_t product_code = (edid_data[10] << 8) | edid_data[11];

    /* 解析序列号 */
    uint32_t serial = edid_data[12] | (edid_data[13] << 8) |
                      (edid_data[14] << 16) | (edid_data[15] << 24);
    snprintf(monitor->serial, sizeof(monitor->serial), "%08X", serial);

    /* 解析物理尺寸（cm转mm） */
    monitor->physical_width = edid_data[21] * 10;   /* cm -> mm */
    monitor->physical_height = edid_data[22] * 10;

    /* 解析版本 */
    uint8_t version = edid_data[18];
    uint8_t revision = edid_data[19];

    /* 构建显示器名称 */
    snprintf(monitor->name, sizeof(monitor->name), "%s-%04X",
             monitor->manufacturer, product_code);

    /* 解析标准时序（第38-53字节，8个时序） */
    monitor->mode_count = 0;
    for (int i = 0; i < 8 && monitor->mode_count < GFX_MAX_MODES_PER_MONITOR; i++) {
        uint8_t t = edid_data[38 + i];
        if (t == 0x01 || t == 0x00) continue;  /* 未使用 */

        /* 标准时序：X分辨率 = (t[7:6]+8)*8, 纵横比由t[5:4]决定 */
        uint32_t x = ((t >> 6) + 8) * 8;
        uint32_t ratio = (t >> 4) & 0x3;
        uint32_t y;
        switch (ratio) {
            case 0: y = x * 10 / 16; break;  /* 16:10 */
            case 1: y = x * 3 / 4; break;    /* 4:3 */
            case 2: y = x * 4 / 5; break;    /* 5:4 */
            case 3: y = x * 9 / 16; break;   /* 16:9 */
            default: y = x * 3 / 4; break;
        }

        /* 刷新率 = t[3:0] + 60 Hz */
        uint32_t refresh = (t & 0x0F) + 60;

        /* 检查是否已存在相同模式 */
        int exists = 0;
        for (uint32_t j = 0; j < monitor->mode_count; j++) {
            if (monitor->modes[j].width == x &&
                monitor->modes[j].height == y &&
                monitor->modes[j].refresh_rate == refresh) {
                exists = 1;
                break;
            }
        }
        if (!exists) {
            monitor->modes[monitor->mode_count].width = x;
            monitor->modes[monitor->mode_count].height = y;
            monitor->modes[monitor->mode_count].bpp = 32;
            monitor->modes[monitor->mode_count].refresh_rate = refresh;
            monitor->mode_count++;
        }
    }

    /* 解析详细时序描述符（第54-71, 72-89, 90-107, 108-125字节） */
    for (int d = 0; d < 4; d++) {
        uint8_t *desc = (uint8_t*)&edid_data[54 + d * 18];
        uint16_t pixel_clock = desc[0] | (desc[1] << 8);
        if (pixel_clock == 0) continue;  /* 不是时序描述符 */

        uint32_t h_active = desc[2] | ((desc[4] & 0xF0) << 4);
        uint32_t v_active = desc[5] | ((desc[7] & 0xF0) << 4);
        uint32_t h_blank = desc[3] | ((desc[4] & 0x0F) << 8);
        uint32_t v_blank = desc[6] | ((desc[7] & 0x0F) << 8);

        if (h_active == 0 || v_active == 0) continue;

        /* 计算刷新率 */
        uint32_t h_total = h_active + h_blank;
        uint32_t v_total = v_active + v_blank;
        uint32_t refresh = 0;
        if (h_total > 0 && v_total > 0 && pixel_clock > 0) {
            refresh = (pixel_clock * 10000) / (h_total * v_total);  /* 0.1Hz精度 */
            refresh = (refresh + 5) / 10;  /* 四舍五入到Hz */
        }

        /* 检查是否已存在 */
        int exists = 0;
        for (uint32_t j = 0; j < monitor->mode_count; j++) {
            if (monitor->modes[j].width == h_active &&
                monitor->modes[j].height == v_active) {
                exists = 1;
                break;
            }
        }
        if (!exists && monitor->mode_count < GFX_MAX_MODES_PER_MONITOR) {
            monitor->modes[monitor->mode_count].width = h_active;
            monitor->modes[monitor->mode_count].height = v_active;
            monitor->modes[monitor->mode_count].bpp = 32;
            monitor->modes[monitor->mode_count].refresh_rate = refresh > 0 ? refresh : 60;
            monitor->mode_count++;
        }
    }

    monitor->edid_valid = 1;

    serial_puts(0x3F8, "[GFX-EDID] parsed: ");
    serial_puts(0x3F8, monitor->name);
    serial_puts(0x3F8, " serial=");
    serial_puts(0x3F8, monitor->serial);
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puts(0x3F8, " modes=");
    serial_puthex(0x3F8, monitor->mode_count);
    serial_puts(0x3F8, " size=");
    serial_puthex(0x3F8, monitor->physical_width);
    serial_puts(0x3F8, "x");
    serial_puthex(0x3F8, monitor->physical_height);
    serial_puts(0x3F8, "mm EDID v");
    serial_puthex(0x3F8, version);
    serial_puts(0x3F8, ".");
    serial_puthex(0x3F8, revision);
    serial_puts(0x3F8, "\n");

    return 0;
}

/* 检测显示器热插拔 */
int gfx_monitor_hotplug_detect(void)
{
    /* 简化版：重新枚举显示器，检测连接状态变化 */
    int changed = 0;
    gfx_monitor_t old_monitors[GFX_MAX_MONITORS];
    int old_count = gfx_monitor_count;

    /* 保存旧状态 */
    for (int i = 0; i < old_count && i < GFX_MAX_MONITORS; i++) {
        memcpy(&old_monitors[i], &gfx_monitors[i], sizeof(gfx_monitor_t));
    }

    /* 强制重新枚举 */
    gfx_monitor_count = 0;
    gfx_enum_monitors(gfx_monitors, GFX_MAX_MONITORS);

    /* 比较连接状态变化 */
    int new_count = gfx_monitor_count;
    for (int i = 0; i < new_count && i < GFX_MAX_MONITORS; i++) {
        if (i < old_count) {
            if (old_monitors[i].connected != gfx_monitors[i].connected) {
                changed = 1;
                serial_puts(0x3F8, "[GFX-HPD] monitor ");
                extern void serial_puthex(uint16_t, uint32_t);
                serial_puthex(0x3F8, i);
                serial_puts(0x3F8, gfx_monitors[i].connected ? " connected\n" : " disconnected\n");
            }
        } else if (gfx_monitors[i].connected) {
            changed = 1;
            serial_puts(0x3F8, "[GFX-HPD] new monitor ");
            extern void serial_puthex(uint16_t, uint32_t);
            serial_puthex(0x3F8, i);
            serial_puts(0x3F8, " connected\n");
        }
    }

    return changed;
}

/* 获取显示器支持的模式列表 */
int gfx_get_monitor_modes(uint32_t index, gfx_mode_t *modes, int max_modes)
{
    if (index >= (uint32_t)gfx_monitor_count || !modes || max_modes <= 0) return 0;
    if (!gfx_monitors[index].connected) return 0;

    int count = gfx_monitors[index].mode_count;
    if (count > max_modes) count = max_modes;

    for (int i = 0; i < count; i++) {
        memcpy(&modes[i], &gfx_monitors[index].modes[i], sizeof(gfx_mode_t));
    }

    return count;
}
