/* ============================================================================
 * 赫普OS 64位 - 内存压力管理（P1, V0.13.51）
 * 统一视图: 物理内存 + 内核堆(KHEAP) + 块缓存
 * 低内存响应: 优先回收块缓存（可回收数据页）
 * 压力等级: 0=正常 1=低(物理free<64MB) 2=紧急(free<32MB 或 KHEAP失败累计)
 * ========================================================================== */
#include <stdint.h>

#define MEM_LOW_THRESHOLD     (64u * 1024 * 1024)  /* 低内存阈值 */
#define MEM_CRITICAL_THRESHOLD (32u * 1024 * 1024)  /* 紧急阈值 */

typedef struct {
    uint64_t phys_total;       /* 物理内存总字节 */
    uint64_t phys_used;
    uint64_t phys_free;
    uint64_t kheap_total;      /* 内核堆 */
    uint64_t kheap_used;
    uint64_t kheap_peak;
    uint64_t cache_bytes;      /* 块缓存占用(数据页) */
    int      cache_slots_used;
    int      pressure_level;   /* 0/1/2 */
    uint64_t reclaim_count;    /* 累计回收次数 */
} mem_pressure_stats_t;

extern void pmm64_get_stats(uint64_t *total, uint64_t *used, uint64_t *free);
extern void kheap64_get_stats(uint64_t *total, uint64_t *used,
                              uint64_t *free, uint64_t *alloc_count,
                              uint64_t *free_count);
extern void ext2_cache_get_stats(uint32_t *hits, uint32_t *misses);
extern void ext2_cache_flush_all(void);

static uint64_t g_reclaim_count = 0;

/* 收集内存压力统计 */
void mem_pressure_collect(mem_pressure_stats_t *st)
{
    uint64_t total = 0, used = 0, free = 0;
    uint64_t kt = 0, ku = 0, kf = 0, ac = 0, fc = 0;
    uint32_t hits = 0, misses = 0;

    pmm64_get_stats(&total, &used, &free);
    kheap64_get_stats(&kt, &ku, &kf, &ac, &fc);
    ext2_cache_get_stats(&hits, &misses);

    st->phys_total = total;
    st->phys_used = used;
    st->phys_free = free;
    st->kheap_total = kt;
    st->kheap_used = ku;
    st->kheap_peak = ku;
    /* 块缓存占用: 已用槽数 x 4KB（数据页动态分配） */
    st->cache_bytes = 0;
    st->cache_slots_used = 0;
    extern int ext2_cache_used_count(void);
    st->cache_slots_used = ext2_cache_used_count();
    st->cache_bytes = (uint64_t)st->cache_slots_used * 4096;

    if (free < MEM_CRITICAL_THRESHOLD) {
        st->pressure_level = 2;
    } else if (free < MEM_LOW_THRESHOLD) {
        st->pressure_level = 1;
    } else {
        st->pressure_level = 0;
    }
    st->reclaim_count = g_reclaim_count;
}

/* 回收块缓存（低内存响应第一步） */
void mem_pressure_reclaim_cache(void)
{
    extern int ext2_cache_used_count(void);
    ext2_cache_flush_all();
    g_reclaim_count++;
}

/* 内存压力检查: 低内存时自动回收缓存 (可挂定时器/主循环) */
void mem_pressure_tick(void)
{
    mem_pressure_stats_t st;
    mem_pressure_collect(&st);
    if (st.pressure_level >= 1) {
        if (st.cache_slots_used > 0) {
            mem_pressure_reclaim_cache();
        }
    }
}
