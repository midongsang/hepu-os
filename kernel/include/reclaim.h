#ifndef RECLAIM_H
#define RECLAIM_H

#include <stdint.h>

/* 内存压力等级 */
#define MEM_OK                0
#define MEM_PRESSURE_NORMAL   1
#define MEM_PRESSURE_HIGH     2
#define MEM_PRESSURE_CRITICAL 3

/* 初始化内存回收子系统 */
void reclaim_init(void);

/* 注册可回收页到LRU */
int reclaim_add_page(uint32_t phys_addr);

/* 标记页被访问 */
int reclaim_mark_accessed(uint32_t phys_addr);

/* 从LRU移除页 */
int reclaim_remove_page(uint32_t phys_addr);

/* 回收最久未使用页（返回物理地址，0=无） */
uint32_t reclaim_reclaim_page(void);

/* 批量回收直到空闲页达到目标 */
int reclaim_until(uint32_t target_free_pages);

/* 检查内存压力（自动触发回收） */
int reclaim_check_pressure(void);

/* 获取当前空闲页数 */
uint32_t free_pages_now(void);

/* 统计 */
void reclaim_stats(uint32_t *reclaimed, uint32_t *calls,
                   uint32_t *lru_used, uint32_t *pressure);

#endif
