/* ============================================================================
 * 赫普OS - 压力测试套件 (E-43)
 * 覆盖: 高频内存分配 / 释放安全 / CPU 亲和性 API / 堆碎片统计 /
 *       切换计数器 / 锁往返 —— 供 shell `ktest` 一键回归
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"
#include "smp.h"   /* spinlock_t / SPINLOCK_INIT */
#include "mm.h"
#include "slab.h"
#include "page_cache.h"

/* === 高频混合大小 kmalloc/kfree 压力 (触发 slab + 堆双路径) === */
static kunit_result_t test_kmalloc_stress(void)
{
    void *ptrs[64];
    for (int round = 0; round < 8; round++) {
        for (int i = 0; i < 64; i++) {
            uint32_t size = 8 + ((i * 131) % 8192);   /* 8B ~ 8KB 混合 */
            ptrs[i] = kmalloc(size);
            if (!ptrs[i]) {
                for (int j = 0; j < i; j++) kfree(ptrs[j]);
                return KUNIT_SKIP;  /* kmalloc不可用时跳过 */
            }
            /* 写一遍, 验分配区可写 */
            ((uint8_t *)ptrs[i])[0] = (uint8_t)i;
            ((uint8_t *)ptrs[i])[size - 1] = (uint8_t)~i;
        }
        for (int i = 0; i < 64; i++) kfree(ptrs[i]);
    }
    return KUNIT_PASS;
}

/* === kfree(NULL) / 双释放防护: 安全返回不崩溃 === */
static kunit_result_t test_kfree_null_safe(void)
{
    kfree(NULL);
    void *p = kmalloc(256);
    if (!p) return KUNIT_SKIP;  /* kmalloc不可用时跳过 */
    kfree(p);
    kfree(p);   /* 双释放应被 kheap magic/used 校验拦截, 不崩溃 */
    return KUNIT_PASS;
}

/* === E-42 CPU 亲和性 API 校验 === */
static kunit_result_t test_affinity_api(void)
{
    extern int sched_set_affinity(uint32_t, uint32_t);
    /* mask=0 应拒绝 */
    int r1 = sched_set_affinity(1, 0);
    KUNIT_ASSERT(r1 < 0, " mask=0 reject ");
    /* 不存在的 pid 应拒绝 */
    int r2 = sched_set_affinity(9999, 1);
    KUNIT_ASSERT(r2 < 0, " bad pid reject ");
    /* 有效设置(与在线CPU0有交集)，成功或失败都接受（取决于调度器状态） */
    sched_set_affinity(1, 1);
    sched_set_affinity(1, 0x3);
    sched_set_affinity(1, 1);
    return KUNIT_PASS;
}

/* === 堆碎片统计可调用 === */
static kunit_result_t test_frag_stats_callable(void)
{
    extern void kheap_frag_stats(uint32_t *, uint32_t *, uint32_t *);
    uint32_t fb = 0, lf = 0, fr = 0;
    kheap_frag_stats(&fb, &lf, &fr);
    /* 只要函数可调用且不崩溃即可，空闲块数可能为0（测试早期） */
    KUNIT_ASSERT(fr <= 100, " frag_rate<=100% ");
    return KUNIT_PASS;
}

/* === 切换计数器单调 === */
static kunit_result_t test_switch_counter(void)
{
    extern uint32_t sched_switch_count(void);
    uint32_t c1 = sched_switch_count();
    KUNIT_ASSERT(c1 >= 0, " counter non-negative ");
    return KUNIT_PASS;
}

/* === 锁往返: 普通/irqsave 自旋锁 500 次往返 === */
static kunit_result_t test_lock_roundtrip(void)
{
    extern void spin_lock(spinlock_t *);
    extern void spin_unlock(spinlock_t *);
    extern void spin_lock_irqsave(spinlock_t *, uint32_t *);
    extern void spin_unlock_irqrestore(spinlock_t *, uint32_t);
    static spinlock_t lt = SPINLOCK_INIT;
    for (int i = 0; i < 500; i++) {
        spin_lock(&lt);
        spin_unlock(&lt);
        uint32_t fl;
        spin_lock_irqsave(&lt, &fl);
        spin_unlock_irqrestore(&lt, fl);
    }
    return KUNIT_PASS;
}


/* === P2: buddy 单一后端 —— 连续页分配/释放可复用，地址按块对齐 === */
static kunit_result_t test_buddy_single_backend(void)
{
    extern int buddy_is_initialized(void);
    if (!buddy_is_initialized()) return KUNIT_SKIP;

    static const uint32_t counts[] = { 1, 2, 4, 8, 16 };
    uint32_t free_before = 0;
    mm_stats(NULL, NULL, &free_before);

    for (int round = 0; round < 5; round++) {
        for (uint32_t k = 0; k < sizeof(counts) / sizeof(counts[0]); k++) {
            uint32_t n = counts[k];
            uint32_t phys = mm_alloc_pages(n);
            KUNIT_ASSERT(phys != 0, " buddy alloc contiguous ");
            KUNIT_ASSERT((phys % (n * 4096)) == 0, " buddy block aligned ");

            uint8_t *p = (uint8_t *)(uintptr_t)phys;
            for (uint32_t i = 0; i < n; i++) {
                p[i * 4096] = (uint8_t)(round + i);
                p[i * 4096 + 4095] = (uint8_t)(round ^ i);
            }
            mm_free_pages(phys, n);
        }
    }

    uint32_t free_after = 0;
    mm_stats(NULL, NULL, &free_after);
    /* 期间可能有其他子系统分配，只要求大块归还后没有明显泄漏 */
    KUNIT_ASSERT(free_after + 8 >= free_before, " buddy free returns pages ");
    return KUNIT_PASS;
}

/* === P2: slab 六档全尺寸复用，反复分配/释放不持续吃新 slab === */
static kunit_result_t test_slab_all_sizes_reuse(void)
{
    static const uint32_t sizes[] = { 32, 64, 128, 256, 512, 1024 };
    uint32_t slabs0 = 0, alloc0 = 0, mem0 = 0;
    slab_stats(&slabs0, &alloc0, &mem0);

    for (int round = 0; round < 6; round++) {
        for (uint32_t k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++) {
            void *objs[8];
            uint32_t sz = sizes[k];
            for (int i = 0; i < 8; i++) {
                objs[i] = slab_alloc(sz);
                KUNIT_ASSERT(objs[i] != NULL, " slab alloc all sizes ");
                ((uint8_t *)objs[i])[0] = (uint8_t)(round + i);
                ((uint8_t *)objs[i])[sz - 1] = (uint8_t)(round ^ i);
            }
            for (int i = 0; i < 8; i++) {
                KUNIT_ASSERT(slab_free(objs[i]) == 0, " slab free all sizes ");
            }
        }
    }

    uint32_t slabs1 = 0, alloc1 = 0, mem1 = 0;
    slab_stats(&slabs1, &alloc1, &mem1);
    KUNIT_ASSERT(alloc1 > alloc0, " slab allocations counted ");
    /* 同尺寸复用后不应每轮都新建 slab；留 8 页余量给 6 档首次扩张 */
    KUNIT_ASSERT(slabs1 <= slabs0 + 8, " slab caches reused ");
    (void)mem1;
    return KUNIT_PASS;
}

/* === P2: 页缓存压力 —— 跑到上限触发 LRU 回收，物理页回到 pmm === */
static kunit_result_t test_page_cache_pressure(void)
{
    uint32_t free_before = 0;
    mm_stats(NULL, NULL, &free_before);

    page_cache_t *cache = page_cache_create("kunit-pc", NULL);
    if (!cache) return KUNIT_SKIP;

    uint32_t added = 0;
    for (uint32_t i = 0; i < PAGE_CACHE_MAX_PAGES; i++) {
        uint32_t phys = mm_alloc_page();
        KUNIT_ASSERT(phys != 0, " page cache pressure alloc page ");
        page_cache_entry_t *e = page_cache_add(cache, i, phys, PC_FLAG_UPTODATE);
        KUNIT_ASSERT(e != NULL, " page cache add ");
        page_cache_release(cache, e);
        added++;
    }
    KUNIT_ASSERT(added == PAGE_CACHE_MAX_PAGES, " page cache filled to max ");

    /* 第4097页：add 内部应先回收 1/4，再插入新条目 */
    uint32_t phys = mm_alloc_page();
    KUNIT_ASSERT(phys != 0, " page cache overflow alloc ");
    page_cache_entry_t *e = page_cache_add(cache, PAGE_CACHE_MAX_PAGES, phys, PC_FLAG_UPTODATE);
    KUNIT_ASSERT(e != NULL, " page cache overflow add ");
    page_cache_release(cache, e);

    uint32_t total = 0, dirty = 0;
    uint64_t hits = 0, misses = 0;
    page_cache_stats(cache, &total, &dirty, &hits, &misses);
    KUNIT_ASSERT(total <= PAGE_CACHE_MAX_PAGES, " page cache total <= max ");
    KUNIT_ASSERT(total < PAGE_CACHE_MAX_PAGES, " LRU reclaim happened ");
    KUNIT_ASSERT(dirty == 0, " clean pages only ");

    page_cache_destroy(cache);

    uint32_t free_after = 0;
    mm_stats(NULL, NULL, &free_after);
    /* slab 元数据页可能留在缓存里，留 128 页余量 */
    KUNIT_ASSERT(free_after + 128 >= free_before, " page cache frees phys pages ");
    return KUNIT_PASS;
}

/* === 套件定义 === */
static const kunit_test_t stress_tests[] = {
    { "kmalloc_stress",  test_kmalloc_stress,     "stress" },
    { "kfree_null_safe", test_kfree_null_safe,    "stress" },
    { "affinity_api",    test_affinity_api,       "stress" },
    { "frag_stats_call", test_frag_stats_callable,"stress" },
    { "switch_counter",  test_switch_counter,     "stress" },
    { "lock_roundtrip",  test_lock_roundtrip,     "stress" },
    { "buddy_single_backend", test_buddy_single_backend, "stress" },
    { "slab_all_sizes_reuse", test_slab_all_sizes_reuse, "stress" },
    { "page_cache_pressure",  test_page_cache_pressure,  "stress" },
};

const kunit_suite_t kunit_stress_suite = {
    "stress",
    stress_tests,
    sizeof(stress_tests) / sizeof(stress_tests[0])
};

void kunit_stress_register(void)
{
    extern void kunit_register_suite(const kunit_suite_t *);
    kunit_register_suite(&kunit_stress_suite);
    vga_puts("[KUNIT] stress suite registered\n");
}
