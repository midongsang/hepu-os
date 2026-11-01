/* ============================================================================
 * 赫普OS - 64位物理内存管理契约接口
 * V0.10.6: 按照系统包袱准则，定义稳定契约边界
 * ========================================================================== */

#ifndef HEPU_PMM64_H
#define HEPU_PMM64_H

#include <stdint.h>
#include <stddef.h>

/* 物理内存管理操作表（稳定契约） */
struct pmm_ops {
    /* 初始化物理内存管理器 */
    int (*init)(uint64_t memory_size, uint64_t bitmap_addr);

    /* 分配一个物理页面 */
    uint64_t (*alloc_page)(void);

    /* 释放一个物理页面 */
    void (*free_page)(uint64_t phys_addr);

    /* 获取内存统计信息 */
    void (*get_stats)(uint64_t *total, uint64_t *used, uint64_t *free);

    /* 分配连续物理页面 */
    uint64_t (*alloc_pages)(size_t count);

    /* 释放连续物理页面 */
    void (*free_pages)(uint64_t phys_addr, size_t count);
};

/* 获取默认的物理内存管理操作表 */
const struct pmm_ops *pmm64_get_ops(void);

/* 便捷函数声明（在pmm64.c中实现） */
int pmm64_init(uint64_t memory_size, uint64_t bitmap_addr);
uint64_t pmm64_alloc_page(void);
void pmm64_free_page(uint64_t phys_addr);
void pmm64_get_stats(uint64_t *total, uint64_t *used, uint64_t *free);

#endif /* HEPU_PMM64_H */
