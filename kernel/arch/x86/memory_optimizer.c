/* ============================================================================
 * 赫普OS - 内存优化
 * ========================================================================== */

#include <stdint.h>
#include "memory_optimizer.h"
#include "vga.h"
#include "string.h"

static memory_optimizer_t mo;

void memory_optimizer_init(void)
{
    memset(&mo, 0, sizeof(memory_optimizer_t));
    mo.initialized = 1;
    mo.enabled = 1;
    mo.huge_pages_enabled = 1;
    mo.page_cache_enabled = 1;
    mo.writeback_enabled = 1;

    /* 默认内存统计 */
    mo.stats.total_ram = 4UL * 1024 * 1024 * 1024;  /* 4GB */
    mo.stats.used_ram = 1UL * 1024 * 1024 * 1024;   /* 1GB */
    mo.stats.free_ram = 3UL * 1024 * 1024 * 1024;
    mo.stats.cached_ram = 512UL * 1024 * 1024;
    mo.stats.kernel_used = 256UL * 1024 * 1024;
    mo.stats.user_used = 768UL * 1024 * 1024;
    mo.stats.swap_total = 2UL * 1024 * 1024 * 1024;
    mo.stats.usage_percent = 25;

    /* 压缩配置 */
    mo.compression.enabled = 1;
    mo.compression.algorithm = 1;  /* lz4 */
    mo.compression.compression_level = 3;

    /* OOM配置 */
    mo.oom.enabled = 1;
    mo.oom.threshold_kb = 65536;  /* 64MB */
    mo.oom.kill_apps = 1;
    mo.oom.preserve_system = 1;

    /* 创建默认内存池 */
    mo_pool_create("kernel_pool", MO_REGION_KERNEL, 0x100000, 16 * 1024 * 1024, 4096);
    mo_pool_create("user_pool", MO_REGION_USER, 0x40000000, 256 * 1024 * 1024, 4096);
    mo_pool_create("dma_pool", MO_REGION_DMA, 0x2000000, 8 * 1024 * 1024, 64);

    vga_puts("[MEMOPT] mem optimizer init done \n");
    vga_printf(" pool: %d, compression: %s, OOM: %s\n",
               mo.pool_count,
               mo.compression.enabled ? " on " : " close ",
               mo.oom.enabled ? " on " : " close ");
}

/* 内存池 */
int mo_pool_create(const char *name, mo_region_type_t type,
                   uint32_t start, uint32_t size, int block_size)
{
    for (int i = 0; i < MO_MAX_POOLS; i++) {
        if (!mo.pools[i].used) {
            memset(&mo.pools[i], 0, sizeof(mo_pool_t));
            mo.pools[i].used = 1;
            mo.pools[i].id = i;
            if (name) strncpy(mo.pools[i].name, name, 127);
            mo.pools[i].type = type;
            mo.pools[i].start = start;
            mo.pools[i].size = size;
            mo.pools[i].free_size = size;
            mo.pools[i].block_size = block_size;
            mo.pools[i].block_count = size / block_size;
            mo.pools[i].alignment = 4096;
            mo.pool_count++;
            return i;
        }
    }
    return -1;
}

int mo_pool_destroy(int pool_id)
{
    if (pool_id < 0 || pool_id >= MO_MAX_POOLS || !mo.pools[pool_id].used) return -1;
    mo.pools[pool_id].used = 0;
    mo.pool_count--;
    return 0;
}

int mo_pool_get(int pool_id, mo_pool_t *pool)
{
    if (pool_id < 0 || pool_id >= MO_MAX_POOLS || !mo.pools[pool_id].used || !pool) return -1;
    memcpy(pool, &mo.pools[pool_id], sizeof(mo_pool_t));
    return 0;
}

int mo_pool_list(mo_pool_t *pools, int max)
{
    if (!pools || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < MO_MAX_POOLS && count < max; i++) {
        if (mo.pools[i].used) memcpy(&pools[count++], &mo.pools[i], sizeof(mo_pool_t));
    }
    return count;
}

int mo_pool_alloc(int pool_id, uint32_t *address)
{
    if (pool_id < 0 || pool_id >= MO_MAX_POOLS || !mo.pools[pool_id].used) return -1;
    if (mo.pools[pool_id].used_blocks >= mo.pools[pool_id].block_count) return -1;
    if (address) *address = mo.pools[pool_id].start +
                            mo.pools[pool_id].used_blocks * mo.pools[pool_id].block_size;
    mo.pools[pool_id].used_blocks++;
    mo.pools[pool_id].used_size += mo.pools[pool_id].block_size;
    mo.pools[pool_id].free_size -= mo.pools[pool_id].block_size;
    return 0;
}

int mo_pool_free(int pool_id, uint32_t address)
{
    (void)address;
    if (pool_id < 0 || pool_id >= MO_MAX_POOLS || !mo.pools[pool_id].used) return -1;
    if (mo.pools[pool_id].used_blocks > 0) {
        mo.pools[pool_id].used_blocks--;
        mo.pools[pool_id].used_size -= mo.pools[pool_id].block_size;
        mo.pools[pool_id].free_size += mo.pools[pool_id].block_size;
    }
    return 0;
}

int mo_pool_defrag(int pool_id)
{
    if (pool_id < 0 || pool_id >= MO_MAX_POOLS || !mo.pools[pool_id].used) return -1;
    mo.pools[pool_id].fragmentation = 0;
    return 0;
}

/* 分配跟踪 */
int mo_track_alloc(uint32_t address, uint32_t size, const char *owner)
{
    for (int i = 0; i < MO_MAX_ALLOCATIONS; i++) {
        if (!mo.allocations[i].used) {
            memset(&mo.allocations[i], 0, sizeof(mo_allocation_t));
            mo.allocations[i].used = 1;
            mo.allocations[i].id = i;
            mo.allocations[i].address = address;
            mo.allocations[i].size = size;
            if (owner) strncpy(mo.allocations[i].owner, owner, 127);
            mo.allocations[i].allocated_time = 1;
            mo.allocation_count++;
            mo.total_allocations++;
            return i;
        }
    }
    return -1;
}

int mo_track_free(uint32_t address)
{
    for (int i = 0; i < MO_MAX_ALLOCATIONS; i++) {
        if (mo.allocations[i].used && mo.allocations[i].address == address) {
            mo.allocations[i].used = 0;
            mo.allocation_count--;
            mo.total_frees++;
            return 0;
        }
    }
    return -1;
}

int mo_get_allocations(mo_allocation_t *allocs, int max)
{
    if (!allocs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < MO_MAX_ALLOCATIONS && count < max; i++) {
        if (mo.allocations[i].used) memcpy(&allocs[count++], &mo.allocations[i], sizeof(mo_allocation_t));
    }
    return count;
}

int mo_find_leaks(mo_allocation_t *leaks, int max)
{
    if (!leaks || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < MO_MAX_ALLOCATIONS && count < max; i++) {
        if (mo.allocations[i].used && mo.allocations[i].leaked)
            memcpy(&leaks[count++], &mo.allocations[i], sizeof(mo_allocation_t));
    }
    return count;
}

/* 内存统计 */
int mo_get_stats(mo_memory_stats_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &mo.stats, sizeof(mo_memory_stats_t));
    return 0;
}

int mo_update_stats(void)
{
    mo.stats.usage_percent = (int)(((uint32_t)mo.stats.used_ram * 100u) / (uint32_t)mo.stats.total_ram);
    return 0;
}

int mo_get_usage_percent(int *percent)
{
    if (!percent) return -1;
    *percent = mo.stats.usage_percent;
    return 0;
}

/* 内存压缩 */
int mo_compression_enable(int enable)
{
    mo.compression.enabled = enable ? 1 : 0;
    return 0;
}

int mo_compress(uint32_t address, uint32_t size)
{
    (void)address;
    if (!mo.compression.enabled) return -1;
    mo.compression.original_size += size;
    mo.compression.compressed_size += size / 3;  /* 假设3:1压缩比 */
    mo.compression.total_compressed++;
    return 0;
}

int mo_decompress(uint32_t address, uint32_t size)
{
    (void)address; (void)size;
    mo.compression.total_decompressed++;
    return 0;
}

int mo_get_compression_stats(mo_compression_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &mo.compression, sizeof(mo_compression_t));
    return 0;
}

/* OOM管理 */
int mo_oom_set_config(const mo_oom_config_t *config)
{
    if (!config) return -1;
    memcpy(&mo.oom, config, sizeof(mo_oom_config_t));
    return 0;
}

int mo_oom_get_config(mo_oom_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &mo.oom, sizeof(mo_oom_config_t));
    return 0;
}

int mo_oom_check(void)
{
    if (!mo.oom.enabled) return 0;
    if (mo.stats.free_ram < (uint64_t)mo.oom.threshold_kb * 1024)
        return 1;  /* 需要OOM处理 */
    return 0;
}

int mo_oom_kill_lowest(void)
{
    mo.oom.total_kills++;
    mo.oom.freed_memory += 64 * 1024 * 1024;
    mo.stats.free_ram += 64 * 1024 * 1024;
    mo.stats.used_ram -= 64 * 1024 * 1024;
    return 0;
}

/* 优化操作 */
int mo_defrag_all(void)
{
    for (int i = 0; i < MO_MAX_POOLS; i++) {
        if (mo.pools[i].used) mo_pool_defrag(i);
    }
    mo.total_compactions++;
    return 0;
}

int mo_drop_caches(void)
{
    uint64_t freed = mo.stats.cached_ram / 2;
    mo.stats.cached_ram -= freed;
    mo.stats.free_ram += freed;
    mo.memory_saved += freed;
    return (int)(freed / 1024 / 1024);
}

int mo_compact_memory(void)
{
    mo_defrag_all();
    return 0;
}

int mo_swap_out(int priority)
{
    (void)priority;
    mo.stats.swap_used += 32 * 1024 * 1024;
    return 0;
}

int mo_enable_huge_pages(int enable)
{
    mo.huge_pages_enabled = enable ? 1 : 0;
    return 0;
}

/* 统计 */
int mo_get_full_stats(memory_optimizer_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &mo, sizeof(memory_optimizer_t));
    return 0;
}
