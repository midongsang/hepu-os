/* ============================================================================
 * 赫普OS - 64位物理内存管理契约接口
 * V0.10.6: 按照系统包袱准则，定义稳定契约边界
 * V0.54.6: 补齐完整 API + hepu_pmm_* 统一接口映射
 * ========================================================================== */

#ifndef HEPU_PMM64_H
#define HEPU_PMM64_H

#include <stdint.h>
#include <stddef.h>

/* ---- 私有 API（64 位专用，旧调用点继续用） ---- */
int      pmm64_init(uint64_t memory_size, uint64_t bitmap_addr);
uint64_t pmm64_alloc_page(void);
void     pmm64_free_page(uint64_t phys_addr);
uint64_t pmm64_alloc_pages(size_t count);
void     pmm64_free_pages(uint64_t phys_addr, size_t count);
void     pmm64_reserve_range(uint64_t base, uint64_t length);
void     pmm64_get_stats(uint64_t *total, uint64_t *used, uint64_t *free);
void     pmm64_get_detailed_stats(uint64_t *total, uint64_t *used, uint64_t *free, uint64_t *reserved,
                                   uint64_t *alloc_count, uint64_t *free_count);
uint64_t pmm64_get_memory_size_mb(void);
uint64_t pmm64_get_max_memory_mb(void);
int      pmm64_get_region_count(void);
int      pmm64_get_region(int index, uint64_t *base, uint64_t *length, int *type);
int      pmm64_is_valid_address(uint64_t addr);

/* ---- 操作表（稳定契约，供内核其他子系统引用） ---- */
struct pmm_ops {
    int      (*init)(uint64_t memory_size, uint64_t bitmap_addr);
    uint64_t (*alloc_page)(void);
    void     (*free_page)(uint64_t phys_addr);
    void     (*get_stats)(uint64_t *total, uint64_t *used, uint64_t *free);
    uint64_t (*alloc_pages)(size_t count);
    void     (*free_pages)(uint64_t phys_addr, size_t count);
};
const struct pmm_ops *pmm64_get_ops(void);

/* reserve_page（按物理页号预留单页，内部用 reserve_range 覆盖 1 页） */
static inline int pmm64_reserve_page(uint64_t phys) {
    pmm64_reserve_range(phys, 4096);
    return 0;
}

/* is_reserved（查区域表） */
static inline int pmm64_is_reserved(uint64_t phys) {
    int n = pmm64_get_region_count();
    for (int i = 0; i < n; i++) {
        uint64_t base, length; int type;
        pmm64_get_region(i, &base, &length, &type);
        if (type == 1 /* RESERVED */ && phys >= base && phys < base + length)
            return 1;
    }
    return 0;
}

/* 统计包装：返回 total_bytes 大小（32 位 mm_free_bytes 风格） */
static inline uint64_t pmm64_free_bytes(void) {
    uint64_t total, used, free;
    pmm64_get_stats(&total, &used, &free);
    return free * 4096;
}

/* 便捷包装（供 hepu_pmm 统一 API 用） */
static inline void pmm64_dump_regions(void) {
    /* 64 位没有 dump_regions stub，但 pmm64.c 里实现了，这里 extern 转发 */
    extern void pmm64_dump_regions_impl(void);
    pmm64_dump_regions_impl();
}

/* ============================================================================
 * V0.54.6: hepu_pmm_* 统一接口 → pmm64_* 私有 API 映射
 * 新代码 include "mm/hepu_pmm.h" 后直接用 hepu_pmm_* 即可跨架构
 * ========================================================================== */
#include "mm/hepu_pmm.h"

static inline void hepu_pmm_init(uint64_t mem_bytes, uint64_t bitmap_phys) {
    pmm64_init(mem_bytes, bitmap_phys);
}
static inline uint64_t hepu_pmm_alloc_page(void)    { return pmm64_alloc_page(); }
static inline uint64_t hepu_pmm_alloc_pages(size_t n){ return pmm64_alloc_pages(n); }
static inline void     hepu_pmm_free_page(uint64_t p){ pmm64_free_page(p); }
static inline void     hepu_pmm_free_pages(uint64_t p, size_t n) { pmm64_free_pages(p, n); }
static inline int      hepu_pmm_reserve_page(uint64_t p){ return pmm64_reserve_page(p); }
static inline int      hepu_pmm_is_reserved(uint64_t p){ return pmm64_is_reserved(p); }
static inline uint64_t hepu_pmm_free_bytes(void)    { return pmm64_free_bytes(); }
static inline uint64_t hepu_pmm_get_memory_size_mb(void){ return pmm64_get_memory_size_mb(); }
static inline int      hepu_pmm_is_valid_address(uint64_t addr) { return pmm64_is_valid_address(addr); }
static inline int      hepu_pmm_add_region(uint64_t start, uint64_t size, int type) {
    /* 64 位无 add_region，reserve_range 只支持 RESERVED 类型，其他返回 -1 */
    if (type == 1 /* RESERVED */) { pmm64_reserve_range(start, size); return 0; }
    return -1;
}

/* get_stats → 填充 mm_phys_stats_t（mm_shared.h 定义） */
static inline void hepu_pmm_get_stats(mm_phys_stats_t *out) {
    if (!out) return;
    uint64_t total, used, free, reserved, ac, fc;
    pmm64_get_detailed_stats(&total, &used, &free, &reserved, &ac, &fc);
    /* 注意: 64 位 get_detailed_stats 返回字节（×4096），需要除以 PAGE_SIZE 得页数 */
    out->total_pages    = total    / 4096;
    out->used_pages     = used     / 4096;
    out->free_pages     = free     / 4096;
    out->reserved_pages = reserved / 4096;
    out->total_bytes    = total;
    out->used_bytes     = used;
    out->free_bytes     = free;
}

#endif /* HEPU_PMM64_H */
