#ifndef UNIFIED_MEM_H
#define UNIFIED_MEM_H

#include <stdint.h>

/* 内存类型 */
#define UMEM_TYPE_NORMAL    0  /* 普通物理内存 */
#define UMEM_TYPE_VRAM      1  /* 显存 */
#define UMEM_TYPE_COMPRESSED 2  /* 压缩内存（zram） */
#define UMEM_TYPE_COUNT     3

/* 内存区域标志 */
#define UMEM_FLAG_NORMAL    (1 << 0)
#define UMEM_FLAG_VRAM      (1 << 1)
#define UMEM_FLAG_COMPRESSED (1 << 2)
#define UMEM_FLAG_ANY       (UMEM_FLAG_NORMAL | UMEM_FLAG_VRAM | UMEM_FLAG_COMPRESSED)

/* 统一内存块 */
typedef struct {
    void     *ptr;           /* 数据指针 */
    uint32_t  size;          /* 大小 */
    int       type;          /* 内存类型 */
    int       used;          /* 是否使用 */
} umem_block_t;

/* 统一内存统计 */
typedef struct {
    uint32_t total_blocks[UMEM_TYPE_COUNT];
    uint32_t used_blocks[UMEM_TYPE_COUNT];
    uint32_t total_bytes[UMEM_TYPE_COUNT];
    uint32_t used_bytes[UMEM_TYPE_COUNT];
    uint32_t alloc_count;
    uint32_t free_count;
    uint32_t migrate_count;
} umem_stats_t;

/* 初始化统一内存管理器 */
int umem_init(void);

/* 分配统一内存（根据大小和标志选择最合适的内存类型） */
void *umem_alloc(uint32_t size, uint32_t flags);

/* 释放统一内存 */
void umem_free(void *ptr);

/* 重新分配统一内存 */
void *umem_realloc(void *ptr, uint32_t new_size);

/* 获取内存块类型 */
int umem_get_type(void *ptr);

/* 获取内存块大小 */
uint32_t umem_get_size(void *ptr);

/* 内存迁移（在不同类型内存之间迁移） */
int umem_migrate(void *ptr, int new_type);

/* 获取统计信息 */
void umem_get_stats(umem_stats_t *stats);

/* 自测 */
void umem_selftest(void);

#endif
