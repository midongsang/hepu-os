#ifndef TRANSCOMP_H
#define TRANSCOMP_H

#include <stdint.h>

/* 透明压缩常量 */
#define TRANSCOMP_MAX_BLOCKS    4096   /* 最大跟踪块数 */
#define TRANSCOMP_BLOCK_SIZE    4096   /* 块大小 */
#define TRANSCOMP_COMPRESSED_THRESHOLD  0.9  /* 压缩率低于90%才存储压缩版 */

/* 块状态 */
#define TRANSCOMP_STATE_UNCOMPRESSED  0  /* 未压缩 */
#define TRANSCOMP_STATE_COMPRESSED    1  /* 已压缩 */
#define TRANSCOMP_STATE_EMPTY         2  /* 空 */

/* 压缩块映射条目 */
typedef struct {
    uint32_t orig_block;     /* 原始块号 */
    uint32_t comp_size;      /* 压缩后大小 */
    uint8_t  state;          /* 状态 */
    uint8_t  *comp_data;     /* 压缩数据指针（内存版） */
} transcomp_block_map_t;

/* 透明压缩管理器 */
typedef struct {
    transcomp_block_map_t block_map[TRANSCOMP_MAX_BLOCKS];
    uint32_t total_blocks;
    uint32_t compressed_blocks;
    uint32_t original_bytes;
    uint32_t compressed_bytes;
    uint32_t compress_count;
    uint32_t decompress_count;
    uint32_t skip_count;  /* 压缩率差，跳过压缩的次数 */
    int initialized;
} transcomp_mgr_t;

/* 初始化透明压缩 */
int transcomp_init(void);

/* 写入块（自动压缩） */
int transcomp_write_block(uint32_t block, const void *data);

/* 读取块（自动解压） */
int transcomp_read_block(uint32_t block, void *data);

/* 丢弃块（释放压缩数据） */
int transcomp_discard_block(uint32_t block);

/* 获取统计信息 */
void transcomp_get_stats(transcomp_mgr_t *stats);

/* 获取压缩率（百分比） */
uint32_t transcomp_get_ratio(void);

/* 自测 */
void transcomp_selftest(void);

#endif
