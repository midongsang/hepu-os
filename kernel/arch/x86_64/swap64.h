#ifndef SWAP64_H
#define SWAP64_H

#include <stdint.h>

/* 赫普OS 64位 - 交换区 (Swap) V0.13.59
 * 槽位管理 + ext2 文件后端(swap.hepu), 页粒度(4096B)
 * swap 文件懒扩展: 只写实际使用的页
 */

#define SWAP_PAGE_SIZE  4096
#define SWAP_MAX_PAGES  512          /* 2MB swap 文件 */

/* 初始化交换区(懒): 定位/创建 /rootfs/swap.hepu */
int swap64_init(void);

/* 分配一个空闲交换槽, 返回槽号或-1 */
int swap64_alloc_slot(void);

/* 释放交换槽 */
int swap64_free_slot(int slot);

/* 写一页到槽: swap64_write(slot, data) */
int swap64_write(int slot, const uint8_t *page);

/* 从槽读一页: swap64_read(slot, page) */
int swap64_read(int slot, uint8_t *page);

/* 统计: 已用槽/总槽 */
int swap64_get_stats(uint32_t *used, uint32_t *total);

#endif
