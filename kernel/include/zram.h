#ifndef ZRAM_H
#define ZRAM_H

#include <stdint.h>

/* zram统计 */
typedef struct {
    uint32_t total_pages;       /* 总页数 */
    uint32_t compressed_pages;  /* 已压缩页数 */
    uint32_t original_bytes;    /* 原始数据总字节数 */
    uint32_t compressed_bytes;  /* 压缩后总字节数 */
    uint32_t compress_errors;   /* 压缩失败次数 */
    uint32_t decompress_errors; /* 解压失败次数 */
} zram_stats_t;

/* 初始化zram */
void zram_init(uint32_t max_pages);

/* 压缩一页（4096字节），返回压缩后大小（0=压缩失败） */
uint32_t zram_compress_page(const uint8_t *src, uint8_t *dst, uint32_t dst_size);

/* 解压一页，返回解压后大小（0=解压失败） */
uint32_t zram_decompress_page(const uint8_t *src, uint32_t src_size, uint8_t *dst, uint32_t dst_size);

/* 存储压缩页到zram设备，返回页索引（-1=失败） */
int zram_store_page(uint32_t page_idx, const uint8_t *data);

/* 从zram设备读取压缩页并解压，返回0=成功 */
int zram_load_page(uint32_t page_idx, uint8_t *data);

/* 释放zram中的页 */
void zram_free_page(uint32_t page_idx);

/* 获取统计信息 */
void zram_get_stats(zram_stats_t *stats);

/* 获取压缩率（百分比） */
uint32_t zram_get_ratio(void);

/* 自测 */
void zram_selftest(void);

#endif
