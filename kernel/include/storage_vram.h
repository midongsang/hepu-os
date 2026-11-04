#ifndef STORAGE_VRAM_H
#define STORAGE_VRAM_H

#include <stdint.h>

/* 初始化存储-backed显存分区 */
int sbvr_init(uint32_t dev_id, uint64_t start_lba, uint64_t total_sectors);

/* 在存储设备上创建/格式化显存分区 */
int sbvr_format(uint32_t dev_id, uint64_t start_lba, uint64_t total_sectors,
                const char *label);

/* 分配/释放显存块 (4KB块) */
int sbvr_alloc_block(void);
int sbvr_free_block(uint32_t block_idx);

/* 读写显存数据 */
int sbvr_read(uint32_t block_idx, uint32_t offset, void *buf, uint32_t size);
int sbvr_write(uint32_t block_idx, uint32_t offset, const void *buf, uint32_t size);

/* 同步到存储 */
int sbvr_sync(void);

/* 统计信息 */
void sbvr_stats(uint32_t *total_mb, uint32_t *used_mb, uint32_t *free_mb);

/* 是否可用 */
int sbvr_is_available(void);

#endif
