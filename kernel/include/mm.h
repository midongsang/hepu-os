#ifndef MM_H
#define MM_H

#include <stdint.h>

#define PAGE_SIZE 4096

/* 内存区域类型 */
#define MEM_REGION_USABLE    0
#define MEM_REGION_RESERVED  1
#define MEM_REGION_KERNEL    2
#define MEM_REGION_MMIO      3

/* 初始化内存管理 */
void     mm_init(uint32_t mem_kb, uint32_t bitmap_addr);

/* 单页分配/释放 */
uint32_t mm_alloc_page(void);
void     mm_free_page(uint32_t phys_addr);

/* V6.85: 标记物理页为已占用（用于页表页等特殊页） */
void     mm_mark_page_used(uint32_t phys);

/* V6.85: 登记页表物理页，分配器必须跳过 */
void     mm_register_page_table_page(uint32_t phys);

/* 多页连续分配/释放 */
uint32_t mm_alloc_pages(uint32_t count);
void     mm_free_pages(uint32_t phys_addr, uint32_t count);

/* 在指定地址分配 */
int      mm_alloc_page_at(uint32_t phys_addr);

/* 保留页 */
int      mm_reserve_page(uint32_t phys_addr);

/* 内存区域管理 */
int      mm_add_region(uint32_t start_addr, uint32_t size, uint8_t type);
void     mm_dump_regions(void);

/* 统计 */
void     mm_stats(uint32_t *total, uint32_t *used, uint32_t *free);
uint32_t mm_free_bytes(void);

/* 检查页是否在保留区域内（伙伴算法需要） */
int      mm_is_page_reserved(uint32_t page);

/* ============================================================================
 * 伙伴算法（Buddy Allocator）- 高效连续物理页分配
 * 基于《赫普OS内存、显存与存储资源管理详解》1.1节实现
 * 适用于大页映射、DMA缓冲区、大块内核分配等需要物理连续内存的场景
 * ========================================================================== */

#define BUDDY_MAX_ORDER 10  /* 最大分配2^10=1024页=4MB */

/* 初始化伙伴算法（在mm_init之后、内存分配之前调用） */
void     buddy_init(uint32_t total_pages);

/* 分配连续count页（向上取整到2^order），返回起始物理地址，失败返回0 */
uint32_t buddy_alloc_pages(uint32_t count);

/* 释放连续count页 */
void     buddy_free_pages(uint32_t phys_addr, uint32_t count);

/* 伙伴算法统计 */
void     buddy_stats(uint32_t *free_pages, uint32_t *free_blocks[BUDDY_MAX_ORDER + 1]);

/* 伙伴算法自测 */
void     buddy_selftest(void);

/* ============================================================================
 * V0.54.6: hepu_pmm_* 统一接口 → mm_* 私有 API 映射
 * 新代码 include "mm/hepu_pmm.h" 后直接用 hepu_pmm_* 即可跨架构
 * ========================================================================== */
#include "mm/hepu_pmm.h"

static inline void hepu_pmm_init(uint64_t mem_bytes, uint64_t bitmap_phys) {
    mm_init((uint32_t)(mem_bytes / 1024), (uint32_t)bitmap_phys);
}
static inline uint64_t hepu_pmm_alloc_page(void) {
    return (uint64_t)mm_alloc_page();
}
static inline uint64_t hepu_pmm_alloc_pages(size_t n) {
    return (uint64_t)mm_alloc_pages((uint32_t)n);
}
static inline void hepu_pmm_free_page(uint64_t p) {
    mm_free_page((uint32_t)p);
}
static inline void hepu_pmm_free_pages(uint64_t p, size_t n) {
    mm_free_pages((uint32_t)p, (uint32_t)n);
}
static inline int hepu_pmm_reserve_page(uint64_t p) {
    return mm_reserve_page((uint32_t)p);
}
static inline int hepu_pmm_is_reserved(uint64_t p) {
    return mm_is_page_reserved((uint32_t)p);
}
static inline uint64_t hepu_pmm_free_bytes(void) {
    return (uint64_t)mm_free_bytes();
}
static inline void hepu_pmm_get_stats(mm_phys_stats_t *out) {
    if (!out) return;
    uint32_t t, u, f;
    mm_stats(&t, &u, &f);
    out->total_pages    = t;
    out->used_pages     = u;
    out->free_pages     = f;
    out->reserved_pages = 0;   /* mm_stats 没给 reserved，置 0 */
    out->total_bytes    = (uint64_t)t * 4096;
    out->used_bytes     = (uint64_t)u * 4096;
    out->free_bytes     = (uint64_t)f * 4096;
}
static inline uint64_t hepu_pmm_get_memory_size_mb(void) {
    uint32_t t, u, f;
    mm_stats(&t, &u, &f);
    return (uint64_t)(t + u + f) * 4096 / 1048576;
}
static inline int hepu_pmm_add_region(uint64_t start, uint64_t size, int type) {
    return mm_add_region((uint32_t)start, (uint32_t)size, (uint8_t)type);
}
static inline int hepu_pmm_is_valid_address(uint64_t addr) {
    /* 32 位简单校验：addr + 页大小 ≤ 可用区域 */
    uint32_t t, u, f;
    mm_stats(&t, &u, &f);
    uint64_t max = (uint64_t)(t + u + f) * 4096;
    return (addr + 4096) <= max ? 1 : 0;
}
static inline void hepu_pmm_dump_regions(void) { mm_dump_regions(); }

#endif /* MM_H */
