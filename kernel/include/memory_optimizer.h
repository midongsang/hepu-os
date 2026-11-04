#ifndef MEMORY_OPTIMIZER_H
#define MEMORY_OPTIMIZER_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 内存优化
 * 内存压缩 / 回收 / 池化 / 大页 / OOM管理
 * ========================================================================== */

#define MO_MAX_POOLS 32
#define MO_MAX_ALLOCATIONS 1024
#define MO_NAME_LEN 128

/* 内存区域类型 */
typedef enum {
    MO_REGION_KERNEL = 0,
    MO_REGION_USER,
    MO_REGION_DMA,
    MO_REGION_IO,
    MO_REGION_VRAM,
    MO_REGION_CACHE,
    MO_REGION_BUFFER,
} mo_region_type_t;

/* 内存池 */
typedef struct {
    int used;
    int id;
    char name[MO_NAME_LEN];
    mo_region_type_t type;
    uint32_t start;
    uint32_t size;
    uint32_t used_size;
    uint32_t free_size;
    int block_size;
    int block_count;
    int used_blocks;
    int alignment;
    int fragmentation;  /* 0-100 */
} mo_pool_t;

/* 内存分配记录 */
typedef struct {
    int used;
    int id;
    uint32_t address;
    uint32_t size;
    char owner[MO_NAME_LEN];
    uint64_t allocated_time;
    int pool_id;
    int leaked;
} mo_allocation_t;

/* 内存统计 */
typedef struct {
    uint64_t total_ram;
    uint64_t used_ram;
    uint64_t free_ram;
    uint64_t cached_ram;
    uint64_t buffer_ram;
    uint64_t kernel_used;
    uint64_t user_used;
    uint64_t swap_used;
    uint64_t swap_total;
    int usage_percent;
} mo_memory_stats_t;

/* 压缩配置 */
typedef struct {
    int enabled;
    int algorithm;  /* 0=zram,1=lz4,2=zstd,3=lzo */
    int compression_level;
    uint64_t compressed_size;
    uint64_t original_size;
    uint64_t ratio;  /* 百分比 */
    uint64_t total_compressed;
    uint64_t total_decompressed;
} mo_compression_t;

/* OOM配置 */
typedef struct {
    int enabled;
    int threshold_kb;  /* 触发阈值 */
    int kill_services;  /* 是否杀服务 */
    int kill_apps;      /* 是否杀应用 */
    int preserve_system;  /* 保护系统进程 */
    uint64_t total_kills;
    uint64_t freed_memory;
} mo_oom_config_t;

/* 内存优化器状态 */
typedef struct {
    int initialized;
    int enabled;
    /* 池 */
    mo_pool_t pools[MO_MAX_POOLS];
    int pool_count;
    /* 分配 */
    mo_allocation_t allocations[MO_MAX_ALLOCATIONS];
    int allocation_count;
    /* 统计 */
    mo_memory_stats_t stats;
    /* 压缩 */
    mo_compression_t compression;
    /* OOM */
    mo_oom_config_t oom;
    /* 优化 */
    int huge_pages_enabled;
    int page_cache_enabled;
    int writeback_enabled;
    /* 统计 */
    uint64_t total_allocations;
    uint64_t total_frees;
    uint64_t total_compactions;
    uint64_t memory_saved;
} memory_optimizer_t;

/* 初始化 */
void memory_optimizer_init(void);

/* 内存池 */
int mo_pool_create(const char *name, mo_region_type_t type,
                   uint32_t start, uint32_t size, int block_size);
int mo_pool_destroy(int pool_id);
int mo_pool_get(int pool_id, mo_pool_t *pool);
int mo_pool_list(mo_pool_t *pools, int max);
int mo_pool_alloc(int pool_id, uint32_t *address);
int mo_pool_free(int pool_id, uint32_t address);
int mo_pool_defrag(int pool_id);

/* 分配跟踪 */
int mo_track_alloc(uint32_t address, uint32_t size, const char *owner);
int mo_track_free(uint32_t address);
int mo_get_allocations(mo_allocation_t *allocs, int max);
int mo_find_leaks(mo_allocation_t *leaks, int max);

/* 内存统计 */
int mo_get_stats(mo_memory_stats_t *stats);
int mo_update_stats(void);
int mo_get_usage_percent(int *percent);

/* 内存压缩 */
int mo_compression_enable(int enable);
int mo_compress(uint32_t address, uint32_t size);
int mo_decompress(uint32_t address, uint32_t size);
int mo_get_compression_stats(mo_compression_t *stats);

/* OOM管理 */
int mo_oom_set_config(const mo_oom_config_t *config);
int mo_oom_get_config(mo_oom_config_t *config);
int mo_oom_check(void);
int mo_oom_kill_lowest(void);

/* 优化操作 */
int mo_defrag_all(void);
int mo_drop_caches(void);
int mo_compact_memory(void);
int mo_swap_out(int priority);
int mo_enable_huge_pages(int enable);

/* 统计 */
int mo_get_full_stats(memory_optimizer_t *stats);

#endif /* MEMORY_OPTIMIZER_H */
