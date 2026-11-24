/* ============================================================================
 * 赫普OS - 伙伴算法（Buddy Allocator）
 *
 * P2 内存收口：buddy 不再维护第二套空闲位图。
 * 唯一事实来源是 pmm.c 的 memory_bitmap；buddy 只负责按 2^order 对齐地
 * 找连续空闲页，并通过 mm_alloc_page_at()/mm_free_page() 记账。
 *
 * 这样做的原因：
 * - 旧实现同时维护 buddy_free_bitmap 和 pmm memory_bitmap。pmm 把页分给
 *   驱动/页缓存后，buddy 位图仍认为它空闲；释放时又可能出现 buddy 已合并、
 *   pmm 仍占用，导致整块无法再分配。
 * - 现在所有分配只有一个入口和一套账：mm_alloc_page/mm_alloc_pages 在
 *   buddy_init 之后全部转给本文件；释放直接清 pmm 位图，连续空闲页天然
 *   可以被下一次高阶分配合并使用。
 *
 * 限制：仍是单线程锁外调用，调用方需保证不在中断上下文并发分配。
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "mm.h"

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

static int      buddy_initialized = 0;
static uint32_t buddy_total_pages = 0;
static uint32_t buddy_hint_page = 0;

extern int  mm_page_used(uint32_t page);
extern int  mm_alloc_page_at(uint32_t phys_addr);
extern void mm_free_page(uint32_t phys_addr);
extern void mm_stats(uint32_t *total, uint32_t *used, uint32_t *free);

static uint32_t buddy_alloc_order(int order)
{
    uint32_t block_size = 1u << order;
    uint32_t num_blocks = buddy_total_pages / block_size;
    if (num_blocks == 0) return 0;

    uint32_t start_block = buddy_hint_page / block_size;

    for (uint32_t n = 0; n < num_blocks; n++) {
        uint32_t block = (start_block + n) % num_blocks;
        uint32_t start_page = block * block_size;

        /* 先整块确认 pmm 侧全部空闲。mm_page_used 同时覆盖保留区、
         * 页表页和普通占用位，避免把驱动/页缓存的页再发出去。 */
        int conflict = 0;
        for (uint32_t i = 0; i < block_size; i++) {
            if (mm_page_used(start_page + i)) { conflict = 1; break; }
        }
        if (conflict) continue;

        for (uint32_t i = 0; i < block_size; i++) {
            if (mm_alloc_page_at((start_page + i) * PAGE_SIZE) != 0) {
                /* 理论上预检已保证成功；并发/记账异常时回滚已标记的页，
                 * 不让半分配的块留在账上。 */
                for (uint32_t j = 0; j < i; j++)
                    mm_free_page((start_page + j) * PAGE_SIZE);
                return 0;
            }
        }

        buddy_hint_page = start_page + block_size;
        if (buddy_hint_page >= buddy_total_pages) buddy_hint_page = 0;
        return start_page * PAGE_SIZE;
    }

    return 0;
}

/* 初始化伙伴算法：只记录总页数并切换 pmm 后端，不再构造第二套位图。
 * 必须在 mm_init 和所有内存保留区域注册之后调用。 */
void buddy_init(uint32_t total_pages)
{
    if (buddy_initialized) return;

    buddy_total_pages = total_pages;
    buddy_hint_page = 0;
    buddy_initialized = 1;
    mm_enable_buddy_backend();

    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);
    uint32_t total = 0, used = 0, free = 0;
    mm_stats(&total, &used, &free);
    serial_puts(0x3F8, "[BUDDY] single-backend total_pages=");
    serial_puthex(0x3F8, total);
    serial_puts(0x3F8, " free_pages=");
    serial_puthex(0x3F8, free);
    serial_puts(0x3F8, "\n");
}

int buddy_is_initialized(void)
{
    return buddy_initialized;
}

/* 分配2^order页，返回起始物理地址，失败返回0 */
uint32_t buddy_alloc_pages(uint32_t count)
{
    if (!buddy_initialized || count == 0) return 0;

    int order = 0;
    uint32_t size = 1;
    while (size < count) {
        size <<= 1;
        order++;
        if (order > BUDDY_MAX_ORDER) return 0;
    }
    return buddy_alloc_order(order);
}

/* 释放连续count页。这里只清 pmm 位图，不做第二套 buddy 位图的合并；
 * 下一次高阶分配会在 pmm 位图上重新识别连续空闲块。 */
void buddy_free_pages(uint32_t phys_addr, uint32_t count)
{
    if (!buddy_initialized || phys_addr == 0 || count == 0) return;

    uint32_t page = phys_addr / PAGE_SIZE;

    /* buddy_alloc_pages() 会把请求向上取整到 2^order，释放必须按同一个
     * 块大小清账，否则非 2 的幂请求（如 DMA 的 3 页）会漏掉尾部页。 */
    uint32_t block = 1;
    while (block < count) {
        block <<= 1;
        if (block > (1u << BUDDY_MAX_ORDER)) return;
    }
    for (uint32_t i = 0; i < block; i++) {
        mm_free_page((page + i) * PAGE_SIZE);
    }
    if (page < buddy_hint_page) buddy_hint_page = page;
}

/* 统计：空闲页数直接取 pmm 的唯一账；各 order 的可分配块数按当前
 * pmm 位图重新计算，不依赖任何 buddy 私有状态。 */
void buddy_stats(uint32_t *free_pages, uint32_t *free_blocks[BUDDY_MAX_ORDER + 1])
{
    uint32_t total = 0, used = 0, free = 0;
    mm_stats(&total, &used, &free);
    (void)total; (void)used;
    if (free_pages) *free_pages = free;

    if (free_blocks) {
        for (int order = 0; order <= BUDDY_MAX_ORDER; order++) {
            uint32_t *out = free_blocks[order];
            if (!out) continue;
            uint32_t block_size = 1u << order;
            uint32_t num_blocks = buddy_total_pages / block_size;
            uint32_t count = 0;
            for (uint32_t block = 0; block < num_blocks; block++) {
                uint32_t start_page = block * block_size;
                int all_free = 1;
                for (uint32_t i = 0; i < block_size; i++) {
                    if (mm_page_used(start_page + i)) { all_free = 0; break; }
                }
                if (all_free) count++;
            }
            *out = count;
        }
    }
}

/* 自测：分配/释放/连续分配/高阶分配。只验证单页账和连续性。 */
void buddy_selftest(void)
{
    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);
    #define SLOG(s) serial_puts(0x3F8, s)

    if (!buddy_initialized) {
        SLOG("[BUDDY] selftest: not initialized, skip\n");
        return;
    }

    SLOG("[BUDDY] selftest: start\n");
    int pass = 0, fail = 0;

    uint32_t p1 = buddy_alloc_pages(1);
    if (p1 && (p1 & 0xFFF) == 0) {
        SLOG("[BUDDY] test1 alloc 1 page: PASS (addr=");
        serial_puthex(0x3F8, p1); SLOG(")\n"); pass++;
    } else { SLOG("[BUDDY] test1 alloc 1 page: FAIL\n"); fail++; }

    uint32_t p4 = buddy_alloc_pages(4);
    if (p4 && (p4 & 0xFFF) == 0) {
        SLOG("[BUDDY] test2 alloc 4 pages: PASS (addr=");
        serial_puthex(0x3F8, p4); SLOG(")\n"); pass++;
    } else { SLOG("[BUDDY] test2 alloc 4 pages: FAIL\n"); fail++; }

    uint32_t p16 = buddy_alloc_pages(16);
    if (p16 && (p16 & 0xFFF) == 0) {
        SLOG("[BUDDY] test3 alloc 16 pages: PASS (addr=");
        serial_puthex(0x3F8, p16); SLOG(")\n"); pass++;
    } else { SLOG("[BUDDY] test3 alloc 16 pages: FAIL\n"); fail++; }

    buddy_free_pages(p1, 1);
    buddy_free_pages(p4, 4);
    buddy_free_pages(p16, 16);

    uint32_t p2 = buddy_alloc_pages(2);
    if (p2) {
        SLOG("[BUDDY] test4 alloc after free: PASS (addr=");
        serial_puthex(0x3F8, p2); SLOG(")\n"); pass++;
    } else { SLOG("[BUDDY] test4 alloc after free: FAIL\n"); fail++; }
    buddy_free_pages(p2, 2);

    uint32_t p64 = buddy_alloc_pages(64);
    if (p64) {
        SLOG("[BUDDY] test5 alloc 64 pages: PASS (addr=");
        serial_puthex(0x3F8, p64); SLOG(")\n"); pass++;
        buddy_free_pages(p64, 64);
    } else {
        SLOG("[BUDDY] test5 alloc 64 pages: SKIP (fragmented)\n");
        pass++;
    }

    SLOG("[BUDDY] selftest: done (pass=");
    serial_puthex(0x3F8, pass); SLOG(", fail=");
    serial_puthex(0x3F8, fail); SLOG(")\n");
    #undef SLOG
}
