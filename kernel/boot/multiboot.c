/* ============================================================================
 * 赫普OS - Multiboot信息解析 (V0.99新增)
 * 实现：完整解析Multiboot信息结构，内存映射、命令行、启动模块、帧缓冲
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "multiboot.h"
#include "vga.h"
#include "string.h"
#include "kstdio.h"

/* Multiboot信息结构（标准定义） */
typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint8_t  color_info[6];
} __attribute__((packed)) multiboot_info_t;

/* 内存映射条目 */
typedef struct {
    uint32_t size;
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
} __attribute__((packed)) multiboot_mmap_entry_t;

/* 启动模块 */
typedef struct {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;
    uint32_t pad;
} multiboot_module_t;

static multiboot_info_t *mb_info = NULL;
static uint32_t mb_magic = 0;

/* V6.52: HepuBoot安全/恢复模式标志 */
static int mb_safe_mode = 0;
static int mb_recovery_mode = 0;

/* 内存映射缓存 */
#define MB_MMAP_MAX 16
static mb_memory_region_t mb_mmap[MB_MMAP_MAX];
static uint32_t mb_mmap_count = 0;

/* 初始化Multiboot解析 */
void multiboot_init(uint32_t magic, void *info)
{
    mb_magic = magic;
    mb_info = (multiboot_info_t *)info;
    mb_mmap_count = 0;

    /* V6.60: 串口调试输出 */
    {
        extern void serial_puts(uint16_t, const char *);
        extern void serial_puthex(uint16_t, uint32_t);
        serial_puts(0x3F8, "[MB-DBG] magic=0x");
        serial_puthex(0x3F8, magic);
        serial_puts(0x3F8, " info=0x");
        serial_puthex(0x3F8, (uint32_t)(uintptr_t)info);
        if (mb_info) {
            serial_puts(0x3F8, " flags=0x");
            serial_puthex(0x3F8, mb_info->flags);
            serial_puts(0x3F8, " mem_upper=");
            serial_puthex(0x3F8, mb_info->mem_upper);
            serial_puts(0x3F8, " mmap_addr=0x");
            serial_puthex(0x3F8, mb_info->mmap_addr);
            serial_puts(0x3F8, " mmap_len=");
            serial_puthex(0x3F8, mb_info->mmap_length);
        }
        serial_puts(0x3F8, "\n");
    }

    vga_printf("[MB] Multiboot magic: 0x%x %s\n", magic,
               (magic == 0x2BADB002) ? "(has 效)" : "(invalid !)");

    if (!mb_info) {
        vga_puts("[MB] no Multiboot info structure \n");
        return;
    }

    /* 解析内存映射 */
    if (mb_info->flags & (1 << 6)) {
        multiboot_mmap_entry_t *mmap = (multiboot_mmap_entry_t *)mb_info->mmap_addr;
        uint32_t length = mb_info->mmap_length;
        uint32_t offset = 0;

        while (offset < length && mb_mmap_count < MB_MMAP_MAX) {
            mb_mmap[mb_mmap_count].base = (uint32_t)mmap->base_addr;
            mb_mmap[mb_mmap_count].length = (uint32_t)mmap->length;
            mb_mmap[mb_mmap_count].type = mmap->type;
            mb_mmap_count++;

            const char *type_name = "reserved";
            if (mmap->type == 1) type_name = "usable";
            else if (mmap->type == 2) type_name = "reserved";
            else if (mmap->type == 3) type_name = "ACPI";
            else if (mmap->type == 4) type_name = "NVS";

            vga_printf("[MB] mem region: 0x%x-0x%x(%dKB) %s\n",
                       (uint32_t)mmap->base_addr,
                       (uint32_t)(mmap->base_addr + mmap->length),
                       (uint32_t)(mmap->length / 1024), type_name);

            offset += mmap->size + 4;
            mmap = (multiboot_mmap_entry_t *)((uint8_t *)mmap + mmap->size + 4);
        }
    }

    /* V6.60: 串口输出内存映射解析结果 */
    {
        extern void serial_puts(uint16_t, const char *);
        extern void serial_puthex(uint16_t, uint32_t);
        serial_puts(0x3F8, "[MB-DBG] mmap_count=");
        serial_puthex(0x3F8, mb_mmap_count);
        serial_puts(0x3F8, "\n");
        for (uint32_t i = 0; i < mb_mmap_count; i++) {
            serial_puts(0x3F8, "[MB-DBG] region ");
            serial_puthex(0x3F8, i);
            serial_puts(0x3F8, ": base=0x");
            serial_puthex(0x3F8, mb_mmap[i].base);
            serial_puts(0x3F8, " len=0x");
            serial_puthex(0x3F8, mb_mmap[i].length);
            serial_puts(0x3F8, " type=");
            serial_puthex(0x3F8, mb_mmap[i].type);
            serial_puts(0x3F8, "\n");
        }
    }

    /* V2.40: 解析 VBE framebuffer 信息 (flags bit 12) */
    if (mb_info->flags & (1 << 12)) {
        /* V6.50: 添加串口输出, 方便调试GRUB VBE模式 */
        extern void serial_puts(uint16_t, const char *);
        char vbe_dbg[128];
        snprintf(vbe_dbg, sizeof(vbe_dbg), "[MB-VBE] %dx%d bpp=%d pitch=%d type=%d fb=0x%x\n",
                   mb_info->framebuffer_width, mb_info->framebuffer_height,
                   mb_info->framebuffer_bpp, mb_info->framebuffer_pitch,
                   mb_info->framebuffer_type,
                   (uint32_t)mb_info->framebuffer_addr);
        serial_puts(0x3F8, vbe_dbg);
        vga_printf("[MB] VBE framebuffer: %dx%d bpp=%d pitch=%d type=%d fb=0x%x\n",
                   mb_info->framebuffer_width, mb_info->framebuffer_height,
                   mb_info->framebuffer_bpp, mb_info->framebuffer_pitch,
                   mb_info->framebuffer_type,
                   (uint32_t)mb_info->framebuffer_addr);
        /* V6.50: 支持 8bpp/24bpp/32bpp (GRUB常用模式) */
        if ((mb_info->framebuffer_bpp == 8 && mb_info->framebuffer_type == 0) ||
            (mb_info->framebuffer_bpp == 24 && mb_info->framebuffer_type == 1) ||
            (mb_info->framebuffer_bpp == 32 && mb_info->framebuffer_type == 1)) {
            extern void gfx_set_vbe_info(int, int, uint8_t*, int, int);
            gfx_set_vbe_info(
                (int)mb_info->framebuffer_width,
                (int)mb_info->framebuffer_height,
                (uint8_t *)(uint32_t)mb_info->framebuffer_addr,
                (int)mb_info->framebuffer_pitch,
                (int)mb_info->framebuffer_bpp);
            /* V6.96: 从color_info字段读取刷新率 (type=1时为保留字段) */
            extern int gfx_refresh_rate;
            gfx_refresh_rate = mb_info->color_info[0] | (mb_info->color_info[1] << 8);
            if (gfx_refresh_rate < 30 || gfx_refresh_rate > 240) gfx_refresh_rate = 60;
            vga_printf("[MB] VBE LFB enabled (%dbpp, %dHz)\n",
                       mb_info->framebuffer_bpp, gfx_refresh_rate);
            serial_puts(0x3F8, "[MB-VBE] LFB enabled\n");
        } else {
            vga_printf("[MB] VBE mode unsupported (bpp=%d type=%d), fallback VGA\n",
                       mb_info->framebuffer_bpp, mb_info->framebuffer_type);
            serial_puts(0x3F8, "[MB-VBE] unsupported mode, fallback VGA\n");
        }
    } else {
        /* V6.50: 没有VBE信息时也输出调试 */
        extern void serial_puts(uint16_t, const char *);
        serial_puts(0x3F8, "[MB-VBE] no framebuffer info, fallback VGA\n");
    }

    /* 解析命令行 */
    if (mb_info->flags & (1 << 2)) {
        char *cmdline = (char *)mb_info->cmdline;
        if (cmdline && cmdline[0]) {
            vga_printf("[MB] cmdline: %s\n", cmdline);
            /* V6.52: 解析 hepu.safe=1 和 hepu.recovery=1 */
            if (strstr(cmdline, "hepu.safe=1")) {
                mb_safe_mode = 1;
                vga_printf("[MB] SAFE MODE enabled\n");
            }
            if (strstr(cmdline, "hepu.recovery=1")) {
                mb_recovery_mode = 1;
                vga_printf("[MB] RECOVERY MODE enabled\n");
            }
        }
    }

    /* 解析启动模块 */
    if (mb_info->flags & (1 << 3)) {
        vga_printf("[MB] boot modules: %d \n", mb_info->mods_count);
        multiboot_module_t *mods = (multiboot_module_t *)mb_info->mods_addr;
        for (uint32_t i = 0; i < mb_info->mods_count; i++) {
            vga_printf("[MB] module [%d]: 0x%x-0x%x\n", i,
                       mods[i].mod_start, mods[i].mod_end);
        }
    }

    /* 帧缓冲信息 */
    if (mb_info->flags & (1 << 12)) {
        vga_printf("[MB] framebuffer: %dx%d %dbpp pitch=%d\n",
                   mb_info->framebuffer_width,
                   mb_info->framebuffer_height,
                   mb_info->framebuffer_bpp,
                   mb_info->framebuffer_pitch);
    }

    /* 引导加载器名称 */
    if (mb_info->flags & (1 << 9)) {
        char *loader = (char *)mb_info->boot_loader_name;
        if (loader) vga_printf("[MB] bootloader: %s\n", loader);
    }
}

/* 获取总内存（KB） */
uint32_t multiboot_get_memory_kb(void)
{
    if (!mb_info) return 0;
    if (mb_info->flags & (1 << 0)) {
        return mb_info->mem_lower + mb_info->mem_upper;
    }
    /* 从内存映射计算 */
    uint32_t total = 0;
    for (uint32_t i = 0; i < mb_mmap_count; i++) {
        if (mb_mmap[i].type == 1) {  /* 可用内存 */
            total += mb_mmap[i].length / 1024;
        }
    }
    return total;
}

/* 获取内存映射 */
uint32_t multiboot_get_mmap(mb_memory_region_t **regions)
{
    if (regions) *regions = mb_mmap;
    return mb_mmap_count;
}

/* 获取命令行 */
const char *multiboot_get_cmdline(void)
{
    if (!mb_info || !(mb_info->flags & (1 << 2))) return NULL;
    return (const char *)mb_info->cmdline;
}

/* 检查命令行参数是否存在 */
int multiboot_has_param(const char *key)
{
    const char *cmdline = multiboot_get_cmdline();
    if (!cmdline || !key) return 0;
    return (strstr(cmdline, key) != NULL);
}

/* 获取启动模块数量 */
uint32_t multiboot_get_module_count(void)
{
    if (!mb_info || !(mb_info->flags & (1 << 3))) return 0;
    return mb_info->mods_count;
}

/* 获取启动模块信息 */
int multiboot_get_module(uint32_t index, uint32_t *start, uint32_t *end)
{
    if (!mb_info || !(mb_info->flags & (1 << 3))) return -1;
    if (index >= mb_info->mods_count) return -1;
    multiboot_module_t *mods = (multiboot_module_t *)mb_info->mods_addr;
    if (start) *start = mods[index].mod_start;
    if (end) *end = mods[index].mod_end;
    return 0;
}

/* 获取帧缓冲信息 */
int multiboot_get_framebuffer(mb_framebuffer_t *fb)
{
    if (!mb_info || !(mb_info->flags & (1 << 12)) || !fb) return -1;
    fb->addr = (uint32_t)mb_info->framebuffer_addr;
    fb->width = mb_info->framebuffer_width;
    fb->height = mb_info->framebuffer_height;
    fb->pitch = mb_info->framebuffer_pitch;
    fb->bpp = mb_info->framebuffer_bpp;
    fb->type = mb_info->framebuffer_type;
    return 0;
}

/* V6.52: 获取安全/恢复模式状态 */
int multiboot_is_safe_mode(void)
{
    return mb_safe_mode;
}

int multiboot_is_recovery_mode(void)
{
    return mb_recovery_mode;
}

/* V6.52: 强制设置恢复模式（用于引导失败计数触发） */
void multiboot_force_recovery_mode(void)
{
    mb_recovery_mode = 1;
}
