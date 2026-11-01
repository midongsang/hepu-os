#ifndef SWAP_H
#define SWAP_H

#include <stdint.h>

/* 初始化swap子系统 */
void swap_init(void);

/* 磁盘swap分区支持 (V6.77新增) */
int swap_init_disk(uint32_t dev_id, uint64_t start_lba, uint64_t total_lba);
int swap_format_disk(uint32_t dev_id, uint64_t start_lba, uint64_t total_lba);
int swap_is_disk_mode(void);
int swap_auto_detect(void);

/* 启用/禁用swap */
int swap_set_enabled(uint8_t enable);

/* 换出一页到swap（返回槽号，-1失败） */
int swap_out(uint32_t phys_addr);

/* 从swap换入一页（返回新物理地址，0失败） */
uint32_t swap_in(int slot);

/* 检查swap是否可用 */
int swap_is_enabled(void);

/* 统计 */
void swap_stats(uint32_t *total_slots, uint32_t *used_slots,
                uint32_t *swap_in, uint32_t *swap_out);
uint32_t swap_total_kb(void);
uint32_t swap_used_kb(void);

/* 内存压力触发换出 */
int swap_swapout_until(uint32_t target_free_pages);

#endif
