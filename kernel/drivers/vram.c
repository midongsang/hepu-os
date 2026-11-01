/* ============================================================================
 * 赫普OS - 显存分配器（VRAM Allocator）
 * 基于《赫普OS内存、显存与存储资源管理详解》2.2节实现
 *
 * 功能：
 * - 管理VGA文本模式帧缓冲（0xB8000-0xBFFFF，32KB）
 * - 管理VGA图形模式帧缓冲（0xA0000-0xAFFFF，64KB）
 * - 支持注册扩展显存区域（VBE/LFB）
 * - 位图式分配，支持4KB/64KB对齐
 * - 统计信息和自测
 *
 * 设计说明：
 * 显存与系统内存不同：
 * 1. 显存地址是固定的物理地址（不是通过mm_alloc_page分配）
 * 2. 显存访问需要通过MMIO或DMA（参见文档2.3节）
 * 3. 显存分配器只管理地址空间，不实际分配物理内存
 * 4. 多个显存区域独立管理（文本模式和图形模式不重叠）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "vram.h"
#include "vga.h"
#include "string.h"

#define VRAM_MAX_REGIONS  4
#define VRAM_PAGE_SIZE     4096

/* 显存区域 */
typedef struct {
    uint32_t phys_addr;    /* 物理起始地址 */
    uint32_t size;         /* 大小（字节） */
    int      type;         /* 区域类型 */
    uint8_t  *bitmap;      /* 分配位图（每一位代表4KB） */
    uint32_t bitmap_size;  /* 位图大小（字节） */
    uint32_t used_pages;   /* 已使用页数 */
    int      initialized;  /* 是否初始化 */
} vram_region_t;

static vram_region_t vram_regions[VRAM_MAX_REGIONS];
static int vram_region_count = 0;
static int vram_initialized = 0;

/* 静态位图存储（VGA文本32KB=8页，VGA图形64KB=16页，扩展显存最多256KB=64页） */
static uint8_t vram_bitmap_storage[8 + 16 + 64 + 64];  /* 最多4个区域 */

/* 注册显存区域 */
int vram_register_region(uint32_t phys_addr, uint32_t size, int type)
{
    if (vram_region_count >= VRAM_MAX_REGIONS) return -1;
    if (size == 0) return -2;

    vram_region_t *r = &vram_regions[vram_region_count];
    r->phys_addr = phys_addr;
    r->size = size;
    r->type = type;
    r->bitmap_size = (size + VRAM_PAGE_SIZE - 1) / VRAM_PAGE_SIZE / 8 + 1;

    /* 分配位图（从静态存储中分配） */
    static uint32_t bitmap_offset = 0;
    if (bitmap_offset + r->bitmap_size > sizeof(vram_bitmap_storage)) return -3;
    r->bitmap = &vram_bitmap_storage[bitmap_offset];
    bitmap_offset += r->bitmap_size;

    memset(r->bitmap, 0, r->bitmap_size);
    r->used_pages = 0;
    r->initialized = 1;

    vram_region_count++;
    return 0;
}

/* 初始化显存分配器 */
void vram_init(void)
{
    if (vram_initialized) return;

    /* 注册VGA文本模式帧缓冲：0xB8000-0xBFFFF (32KB) */
    vram_register_region(0xB8000, 32 * 1024, VRAM_REGION_VGA_TEXT);

    /* 注册VGA图形模式帧缓冲：0xA0000-0xAFFFF (64KB) */
    vram_register_region(0xA0000, 64 * 1024, VRAM_REGION_VGA_GFX);

    vram_initialized = 1;
}

/* 在指定区域内分配显存 */
static uint32_t vram_alloc_in_region(vram_region_t *r, uint32_t size, uint32_t flags)
{
    if (!r || !r->initialized || size == 0) return 0;

    uint32_t pages = (size + VRAM_PAGE_SIZE - 1) / VRAM_PAGE_SIZE;
    uint32_t total_pages = (r->size + VRAM_PAGE_SIZE - 1) / VRAM_PAGE_SIZE;

    /* 对齐要求 */
    uint32_t align_pages = 1;
    if (flags & VRAM_ALIGN_64K) align_pages = 16;  /* 64KB = 16页 */
    else if (flags & VRAM_ALIGN_4K) align_pages = 1;

    /* 计算第一个对齐的起始页号（考虑区域物理地址可能不对齐） */
    uint32_t start_page = 0;
    if (align_pages > 1) {
        uint32_t phys_page = r->phys_addr / VRAM_PAGE_SIZE;
        uint32_t offset = phys_page % align_pages;
        if (offset != 0) start_page = align_pages - offset;
    }

    /* 线性搜索连续空闲页 */
    for (uint32_t start = start_page; start + pages <= total_pages; start += align_pages) {
        /* 检查连续pages页是否都空闲 */
        uint32_t found = 1;
        for (uint32_t i = 0; i < pages; i++) {
            uint32_t page = start + i;
            uint32_t idx = page / 8;
            uint32_t bit = page % 8;
            if (r->bitmap[idx] & (1 << bit)) {
                found = 0;
                start = page;  /* 跳过已检查的区域 */
                break;
            }
        }
        if (found) {
            /* 标记为已占用 */
            for (uint32_t i = 0; i < pages; i++) {
                uint32_t page = start + i;
                uint32_t idx = page / 8;
                uint32_t bit = page % 8;
                r->bitmap[idx] |= (1 << bit);
            }
            r->used_pages += pages;
            return r->phys_addr + start * VRAM_PAGE_SIZE;
        }
    }
    return 0;  /* 内存不足 */
}

/* 分配显存，返回物理地址，失败返回0 */
uint32_t vram_alloc(uint32_t size, uint32_t flags)
{
    if (!vram_initialized) return 0;

    /* 优先从图形模式显存分配（文本模式显存通常由VGA驱动独占） */
    for (int i = 0; i < vram_region_count; i++) {
        if (vram_regions[i].type == VRAM_REGION_VGA_GFX) {
            uint32_t addr = vram_alloc_in_region(&vram_regions[i], size, flags);
            if (addr) return addr;
        }
    }

    /* 然后尝试扩展显存 */
    for (int i = 0; i < vram_region_count; i++) {
        if (vram_regions[i].type == VRAM_REGION_EXTENDED) {
            uint32_t addr = vram_alloc_in_region(&vram_regions[i], size, flags);
            if (addr) return addr;
        }
    }

    /* 最后尝试文本模式显存（通常不应该分配给应用） */
    for (int i = 0; i < vram_region_count; i++) {
        if (vram_regions[i].type == VRAM_REGION_VGA_TEXT) {
            uint32_t addr = vram_alloc_in_region(&vram_regions[i], size, flags);
            if (addr) return addr;
        }
    }

    return 0;  /* 所有区域都不足 */
}

/* 释放显存 */
void vram_free(uint32_t addr, uint32_t size)
{
    if (!vram_initialized || addr == 0 || size == 0) return;

    /* 查找地址所属的区域 */
    for (int i = 0; i < vram_region_count; i++) {
        vram_region_t *r = &vram_regions[i];
        if (!r->initialized) continue;
        if (addr >= r->phys_addr && addr < r->phys_addr + r->size) {
            uint32_t start_page = (addr - r->phys_addr) / VRAM_PAGE_SIZE;
            uint32_t pages = (size + VRAM_PAGE_SIZE - 1) / VRAM_PAGE_SIZE;
            uint32_t total_pages = (r->size + VRAM_PAGE_SIZE - 1) / VRAM_PAGE_SIZE;

            for (uint32_t j = 0; j < pages && start_page + j < total_pages; j++) {
                uint32_t page = start_page + j;
                uint32_t idx = page / 8;
                uint32_t bit = page % 8;
                if (r->bitmap[idx] & (1 << bit)) {
                    r->bitmap[idx] &= ~(1 << bit);
                    r->used_pages--;
                }
            }
            return;
        }
    }
}

/* 统计信息 */
void vram_stats(uint32_t *total, uint32_t *used, uint32_t *free)
{
    uint32_t t = 0, u = 0;
    for (int i = 0; i < vram_region_count; i++) {
        if (vram_regions[i].initialized) {
            t += vram_regions[i].size;
            u += vram_regions[i].used_pages * VRAM_PAGE_SIZE;
        }
    }
    if (total) *total = t;
    if (used) *used = u;
    if (free) *free = t - u;
}

/* 打印显存区域信息 */
void vram_dump(void)
{
    vga_printf("[VRAM] %d region domains:\n", vram_region_count);
    for (int i = 0; i < vram_region_count; i++) {
        vram_region_t *r = &vram_regions[i];
        if (!r->initialized) continue;
        const char *type_name = "unknown";
        switch (r->type) {
            case VRAM_REGION_VGA_TEXT: type_name = "VGA text"; break;
            case VRAM_REGION_VGA_GFX:  type_name = "VGA gfx"; break;
            case VRAM_REGION_EXTENDED: type_name = "extended"; break;
        }
        uint32_t total_pages = (r->size + VRAM_PAGE_SIZE - 1) / VRAM_PAGE_SIZE;
        vga_printf(" [%d] 0x%x-0x%x (%dKB) %s, used=%d/%d pages\n",
                   i, r->phys_addr, r->phys_addr + r->size,
                   r->size / 1024, type_name,
                   r->used_pages, total_pages);
    }
}

/* ============================================================================
 * 显存分配器自测
 * ========================================================================== */
void vram_selftest(void)
{
    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);
    #define VLOG(s) serial_puts(0x3F8, s)

    VLOG("[VRAM] selftest: start\n");
    int pass = 0, fail = 0;

    /* 测试1：初始化 */
    vram_init();
    uint32_t total, used, free;
    vram_stats(&total, &used, &free);
    /* VGA文本32KB + VGA图形64KB = 96KB */
    if (total == 96 * 1024 && used == 0 && free == 96 * 1024) {
        VLOG("[VRAM] test1 init: PASS (total=96KB)\n");
        pass++;
    } else {
        VLOG("[VRAM] test1 init: FAIL (total=");
        serial_puthex(0x3F8, total);
        VLOG(")\n");
        fail++;
    }

    /* 测试2：分配4KB */
    uint32_t addr1 = vram_alloc(4096, 0);
    if (addr1 != 0 && (addr1 & 0xFFF) == 0) {
        VLOG("[VRAM] test2 alloc 4KB: PASS (addr=0x");
        serial_puthex(0x3F8, addr1);
        VLOG(")\n");
        pass++;
    } else {
        VLOG("[VRAM] test2 alloc 4KB: FAIL\n");
        fail++;
    }

    /* 测试3：分配16KB（连续） */
    uint32_t addr2 = vram_alloc(16 * 1024, 0);
    if (addr2 != 0 && (addr2 & 0xFFF) == 0) {
        VLOG("[VRAM] test3 alloc 16KB: PASS (addr=0x");
        serial_puthex(0x3F8, addr2);
        VLOG(")\n");
        pass++;
    } else {
        VLOG("[VRAM] test3 alloc 16KB: FAIL\n");
        fail++;
    }

    /* 测试4：统计更新 */
    vram_stats(&total, &used, &free);
    if (used == 20 * 1024 && free == 76 * 1024) {
        VLOG("[VRAM] test4 stats update: PASS (used=20KB, free=76KB)\n");
        pass++;
    } else {
        VLOG("[VRAM] test4 stats update: FAIL (used=");
        serial_puthex(0x3F8, used);
        VLOG(", free=");
        serial_puthex(0x3F8, free);
        VLOG(")\n");
        fail++;
    }

    /* 测试5：释放4KB */
    vram_free(addr1, 4096);
    vram_stats(&total, &used, &free);
    if (used == 16 * 1024) {
        VLOG("[VRAM] test5 free 4KB: PASS (used=16KB)\n");
        pass++;
    } else {
        VLOG("[VRAM] test5 free 4KB: FAIL (used=");
        serial_puthex(0x3F8, used);
        VLOG(")\n");
        fail++;
    }

    /* 测试6：重新分配4KB（应该能重用释放的空间） */
    uint32_t addr3 = vram_alloc(4096, 0);
    if (addr3 != 0) {
        VLOG("[VRAM] test6 realloc after free: PASS (addr=0x");
        serial_puthex(0x3F8, addr3);
        VLOG(")\n");
        pass++;
    } else {
        VLOG("[VRAM] test6 realloc after free: FAIL\n");
        fail++;
    }

    /* 测试7：64KB对齐分配（需要先注册扩展显存区域，VGA显存太小无法满足64KB对齐） */
    vram_register_region(0xE00000, 256 * 1024, VRAM_REGION_EXTENDED);
    uint32_t addr4 = vram_alloc(4096, VRAM_ALIGN_64K);
    if (addr4 != 0 && (addr4 & 0xFFFF) == 0) {
        VLOG("[VRAM] test7 alloc 64K-aligned: PASS (addr=0x");
        serial_puthex(0x3F8, addr4);
        VLOG(")\n");
        pass++;
    } else {
        VLOG("[VRAM] test7 alloc 64K-aligned: FAIL (addr=0x");
        serial_puthex(0x3F8, addr4);
        VLOG(")\n");
        fail++;
    }

    /* 清理 */
    vram_free(addr2, 16 * 1024);
    vram_free(addr3, 4096);
    vram_free(addr4, 4096);

    VLOG("[VRAM] selftest: done (pass=");
    serial_puthex(0x3F8, pass);
    VLOG(", fail=");
    serial_puthex(0x3F8, fail);
    VLOG(")\n");

    #undef VLOG
}
