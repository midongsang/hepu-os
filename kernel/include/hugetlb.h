#ifndef HUGETLB_H
#define HUGETLB_H

#include <stdint.h>
#include "paging.h"

/* 大页大小 */
#define HUGE_PAGE_2M  (2 * 1024 * 1024)
#define HUGE_PAGE_4M  (4 * 1024 * 1024)

/* 检查CPU是否支持大页(PSE) */
int hugetlb_supported(void);

/* 初始化 */
void hugetlb_init(void);

/* 大页分配/释放 */
uint32_t hugetlb_alloc_2m(void);
uint32_t hugetlb_alloc_4m(void);
void     hugetlb_free_2m(uint32_t phys_addr);
void     hugetlb_free_4m(uint32_t phys_addr);

/* 大页映射 */
int hugetlb_map_2m(page_directory_t *dir, uint32_t virt, uint32_t phys, uint32_t flags);
int hugetlb_map_4m(page_directory_t *dir, uint32_t virt, uint32_t phys, uint32_t flags);

/* 检查是否大页，返回大小（0=普通页） */
uint32_t hugetlb_is_huge_page(page_directory_t *dir, uint32_t virt);

/* 统计 */
void hugetlb_stats(uint32_t *huge_2m, uint32_t *huge_4m,
                   uint32_t *alloc_2m, uint32_t *alloc_4m);

/* V6.50: 自测 */
void hugetlb_selftest(void);

#endif
