/* ============================================================================
 * 赫普OS - 统一物理内存分配器 API
 * V0.54.6: 两边共用的物理内存接口（32 位 mm_* + 64 位 pmm64_* 统一在此）
 *
 * 设计原则：
 *   - 物理地址一律 uint64_t（32 位地址零扩展，64 位原生）
 *   - 页数一律 size_t
 *   - 两边各自实现，通过宏映射 hepu_pmm_* → 各自的私有 API
 *   - 调用点零改动：旧代码继续用 mm_alloc_page / pmm64_alloc_page，
 *     新代码直接用 hepu_pmm_* 即可跨架构
 * ========================================================================== */

#ifndef HEPU_PMM_H
#define HEPU_PMM_H

#include <stdint.h>
#include <stddef.h>
#include "mm/mm_shared.h"

/* --------------------------------------------------------------------------
 * 初始化
 *   init(memory_size_bytes, bitmap_phys_addr)
 * ------------------------------------------------------------------------ */
static inline void hepu_pmm_init(uint64_t mem_bytes, uint64_t bitmap_phys);

/* --------------------------------------------------------------------------
 * 核心分配接口（所有分配单位 = 页 / PAGE_SIZE）
 * ------------------------------------------------------------------------ */
/* 分配一页，返回物理地址；失败返回 0 */
static inline uint64_t hepu_pmm_alloc_page(void);

/* 连续分配 count 页，返回起始物理地址；失败返回 0 */
static inline uint64_t hepu_pmm_alloc_pages(size_t count);

/* 释放一页 */
static inline void hepu_pmm_free_page(uint64_t phys);

/* 释放 count 页（起始 phys 必须是 hepu_pmm_alloc_pages 返回的地址） */
static inline void hepu_pmm_free_pages(uint64_t phys, size_t count);

/* 预留一页（标记为不可分配，供内核/设备专用） */
static inline int hepu_pmm_reserve_page(uint64_t phys);

/* 查询某页是否被预留 */
static inline int hepu_pmm_is_reserved(uint64_t phys);

/* --------------------------------------------------------------------------
 * 统计接口
 * ------------------------------------------------------------------------ */
static inline void hepu_pmm_get_stats(mm_phys_stats_t *out);
static inline uint64_t hepu_pmm_free_bytes(void);
static inline uint64_t hepu_pmm_get_memory_size_mb(void);
static inline void hepu_pmm_dump_regions(void);

/* --------------------------------------------------------------------------
 * 可选扩展（pmm64 有、mm 暂无；mm 用 stub 返回 0）
 * ------------------------------------------------------------------------ */
static inline int hepu_pmm_add_region(uint64_t start, uint64_t size, int type);
static inline int hepu_pmm_is_valid_address(uint64_t addr);

#endif /* HEPU_PMM_H */
