#ifndef BLK_CACHE64_H
#define BLK_CACHE64_H

#include <stdint.h>

/* 统一块缓存层 (V0.13.58)
 * 扇区粒度(512B)缓存, 写穿(write-through), LRU淘汰
 * 挂在 storage_hal 层, 全部存储驱动(IDE/USB/AHCI/NVMe)受益
 */

/* 初始化缓存(懒初始化: 首次IO自动建立) */
void blk_cache_init(void);

/* 带缓存读: 逐扇区命中/填充, 返回0成功 */
int blk_cache_read(uint32_t lba, uint8_t count, uint8_t *buffer);

/* 带缓存写: 写穿直落盘 + 更新缓存, 返回0成功 */
int blk_cache_write(uint32_t lba, uint8_t count, const uint8_t *buffer);

/* 缓存失效: 直写路径绕过缓存后调用, 避免旧数据 */
void blk_cache_invalidate(uint32_t lba, uint8_t count);

/* 清空全部缓存槽 */
void blk_cache_flush(void);

/* 统计: 命中/未命中/占用槽/总槽 */
int blk_cache_get_stats(uint32_t *hits, uint32_t *misses,
                        uint32_t *slots_used, uint32_t *slots_total);

/* 底层原始读写(无缓存), 由 storage_hal 提供 */
extern int storage_hal_raw_read(uint32_t lba, uint8_t count, uint8_t *buffer);
extern int storage_hal_raw_write(uint32_t lba, uint8_t count, const uint8_t *buffer);

#endif
