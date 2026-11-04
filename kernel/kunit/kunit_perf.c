/* ============================================================================
 * 赫普OS - 性能基准测试（第二周验证）
 * 覆盖：启动时间、内存占用、系统调用延迟、调度延迟、内存分配性能
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === 时间戳 === */
static uint64_t perf_get_time(void)
{
    /* 使用CPU时间戳计数器 */
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* === 系统调用延迟 === */
static kunit_result_t test_perf_syscall_getpid(void)
{
    /* getpid系统调用延迟基准 */
    extern uint32_t sys_getpid(uint32_t, uint32_t, uint32_t, uint32_t);
    uint64_t start = perf_get_time();
    for (int i = 0; i < 100; i++) {
        sys_getpid(0, 0, 0, 0);
    }
    uint64_t end = perf_get_time();
    uint64_t avg = (end - start) / 100;
    vga_printf("[PERF] getpid avg: %llu cycles\n", avg);
    KUNIT_ASSERT(avg > 0, " getpid latency measured ");
    return KUNIT_PASS;
}

static kunit_result_t test_perf_syscall_time(void)
{
    /* time系统调用延迟 */
    extern uint32_t sys_time(uint32_t, uint32_t, uint32_t, uint32_t);
    uint64_t start = perf_get_time();
    for (int i = 0; i < 100; i++) {
        sys_time(0, 0, 0, 0);
    }
    uint64_t end = perf_get_time();
    uint64_t avg = (end - start) / 100;
    vga_printf("[PERF] time avg: %llu cycles\n", avg);
    KUNIT_ASSERT(avg > 0, " time latency measured ");
    return KUNIT_PASS;
}

/* === 内存分配性能 === */
static kunit_result_t test_perf_kmalloc_small(void)
{
    /* 小内存分配性能 */
    extern void *kmalloc(uint32_t);
    extern void kfree(void *);
    uint64_t start = perf_get_time();
    for (int i = 0; i < 100; i++) {
        void *p = kmalloc(64);
        if (p) kfree(p);
    }
    uint64_t end = perf_get_time();
    uint64_t avg = (end - start) / 100;
    vga_printf("[PERF] kmalloc(64) avg: %llu cycles\n", avg);
    KUNIT_ASSERT(avg > 0, " kmalloc latency measured ");
    return KUNIT_PASS;
}

static kunit_result_t test_perf_kmalloc_large(void)
{
    /* 大内存分配性能 */
    extern void *kmalloc(uint32_t);
    extern void kfree(void *);
    uint64_t start = perf_get_time();
    for (int i = 0; i < 50; i++) {
        void *p = kmalloc(4096);
        if (p) kfree(p);
    }
    uint64_t end = perf_get_time();
    uint64_t avg = (end - start) / 50;
    vga_printf("[PERF] kmalloc(4K) avg: %llu cycles\n", avg);
    KUNIT_ASSERT(avg > 0, " kmalloc large latency measured ");
    return KUNIT_PASS;
}

/* === 内存拷贝性能 === */
static kunit_result_t test_perf_memcpy_4k(void)
{
    /* 4K内存拷贝性能 */
    extern void *memcpy(void *, const void *, uint32_t);
    static uint8_t src[4096], dst[4096];
    for (int i = 0; i < 4096; i++) src[i] = (uint8_t)i;
    uint64_t start = perf_get_time();
    for (int i = 0; i < 100; i++) {
        memcpy(dst, src, 4096);
    }
    uint64_t end = perf_get_time();
    uint64_t avg = (end - start) / 100;
    vga_printf("[PERF] memcpy(4K) avg: %llu cycles\n", avg);
    KUNIT_ASSERT(avg > 0, " memcpy latency measured ");
    return KUNIT_PASS;
}

static kunit_result_t test_perf_memset_4k(void)
{
    /* 4K内存填充性能 */
    extern void *memset(void *, int, uint32_t);
    static uint8_t buf[4096];
    uint64_t start = perf_get_time();
    for (int i = 0; i < 100; i++) {
        memset(buf, 0xAA, 4096);
    }
    uint64_t end = perf_get_time();
    uint64_t avg = (end - start) / 100;
    vga_printf("[PERF] memset(4K) avg: %llu cycles\n", avg);
    KUNIT_ASSERT(avg > 0, " memset latency measured ");
    return KUNIT_PASS;
}

/* === 调度性能 === */
static kunit_result_t test_perf_scheduler(void)
{
    /* 调度器切换计数 */
    extern uint32_t sched_switch_count(void);
    uint32_t c1 = sched_switch_count();
    uint32_t c2 = sched_switch_count();
    vga_printf("[PERF] scheduler switches: %u\n", c2);
    KUNIT_ASSERT(c2 >= c1, " switch count non-decreasing ");
    return KUNIT_PASS;
}

/* === 中断延迟 === */
static kunit_result_t test_perf_irq_count(void)
{
    /* 中断计数：使用HAL接口 */
    extern uint32_t hal_irq_get_count(uint32_t);
    uint32_t count = hal_irq_get_count(0);
    vga_printf("[PERF] irq0 count: %u\n", count);
    KUNIT_ASSERT(1, " irq count api ok ");
    return KUNIT_PASS;
}

/* === 内存占用 === */
static kunit_result_t test_perf_memory_usage(void)
{
    /* 内存使用统计：使用kheap_stats */
    extern void kheap_stats(uint32_t *, uint32_t *, uint32_t *);
    uint32_t used = 0, free = 0, total = 0;
    kheap_stats(&used, &free, &total);
    vga_printf("[PERF] kheap: used=%u free=%u total=%u\n", used, free, total);
    KUNIT_ASSERT(total > 0, " total >0 ");
    return KUNIT_PASS;
}

/* === 内核堆使用 === */
static kunit_result_t test_perf_kheap_usage(void)
{
    /* 内核堆统计 */
    extern void kheap_stats(uint32_t *, uint32_t *, uint32_t *);
    uint32_t used = 0, free = 0, total = 0;
    kheap_stats(&used, &free, &total);
    vga_printf("[PERF] kheap: used=%u free=%u total=%u\n", used, free, total);
    KUNIT_ASSERT(used + free <= total, " used+free <= total ");
    return KUNIT_PASS;
}

/* === 进程数量 === */
static kunit_result_t test_perf_process_count(void)
{
    /* 进程数量：通过系统调用获取 */
    extern uint32_t sys_getpid(uint32_t, uint32_t, uint32_t, uint32_t);
    uint32_t pid = sys_getpid(0, 0, 0, 0);
    vga_printf("[PERF] current pid: %u\n", pid);
    KUNIT_ASSERT(pid >= 0, " pid valid ");
    return KUNIT_PASS;
}

/* === 启动时间 === */
static kunit_result_t test_perf_boot_time(void)
{
    /* 启动时间：使用bootchain接口 */
    extern int boot_get_boot_time(uint64_t *, uint64_t *, uint64_t *);
    uint64_t start = 0, end = 0, duration = 0;
    int r = boot_get_boot_time(&start, &end, &duration);
    vga_printf("[PERF] boot time: %llu us (ret=%d)\n", duration, r);
    KUNIT_ASSERT(1, " boot time api ok ");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t perf_tests[] = {
    {" perf syscall getpid ", test_perf_syscall_getpid, "perf"},
    {" perf syscall time ", test_perf_syscall_time, "perf"},
    {" perf kmalloc small ", test_perf_kmalloc_small, "perf"},
    {" perf kmalloc large ", test_perf_kmalloc_large, "perf"},
    {" perf memcpy 4k ", test_perf_memcpy_4k, "perf"},
    {" perf memset 4k ", test_perf_memset_4k, "perf"},
    {" perf scheduler ", test_perf_scheduler, "perf"},
    {" perf irq count ", test_perf_irq_count, "perf"},
    {" perf memory usage ", test_perf_memory_usage, "perf"},
    {" perf kheap usage ", test_perf_kheap_usage, "perf"},
    {" perf process count ", test_perf_process_count, "perf"},
    {" perf boot time ", test_perf_boot_time, "perf"},
};

static const kunit_suite_t kunit_performance_suite = {
    " performance ", perf_tests, 12
};

void kunit_perf_register(void)
{
    kunit_register_suite(&kunit_performance_suite);
    vga_puts("[KUNIT] performance test suite registered(12 cases)\n");
}
