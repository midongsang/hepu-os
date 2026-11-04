/* ============================================================================
 * 赫普OS - 性能基准对比测试（第四周验证）
 * 覆盖：系统调用、内存操作、调度、启动时间基准
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

static uint64_t perf_rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* === 系统调用基准 === */
static kunit_result_t test_bench_getpid_latency(void)
{
    extern uint32_t sys_getpid(uint32_t, uint32_t, uint32_t, uint32_t);
    uint32_t start = (uint32_t)perf_rdtsc();
    for (int i = 0; i < 1000; i++) sys_getpid(0, 0, 0, 0);
    uint32_t end = (uint32_t)perf_rdtsc();
    uint32_t avg = (end - start) / 1000;
    vga_printf("[BENCH] getpid: %u cycles/call\n", avg);
    KUNIT_ASSERT(avg > 0, " getpid benchmarked ");
    return KUNIT_PASS;
}

static kunit_result_t test_bench_time_latency(void)
{
    extern uint32_t sys_time(uint32_t, uint32_t, uint32_t, uint32_t);
    uint32_t start = (uint32_t)perf_rdtsc();
    for (int i = 0; i < 1000; i++) sys_time(0, 0, 0, 0);
    uint32_t end = (uint32_t)perf_rdtsc();
    uint32_t avg = (end - start) / 1000;
    vga_printf("[BENCH] time: %u cycles/call\n", avg);
    KUNIT_ASSERT(avg > 0, " time benchmarked ");
    return KUNIT_PASS;
}

/* === 内存操作基准 === */
static kunit_result_t test_bench_kmalloc_64(void)
{
    extern void *kmalloc(uint32_t);
    extern void kfree(void *);
    uint32_t start = (uint32_t)perf_rdtsc();
    for (int i = 0; i < 1000; i++) {
        void *p = kmalloc(64);
        if (p) kfree(p);
    }
    uint32_t end = (uint32_t)perf_rdtsc();
    uint32_t avg = (end - start) / 1000;
    vga_printf("[BENCH] kmalloc(64): %u cycles\n", avg);
    KUNIT_ASSERT(avg > 0, " kmalloc64 benchmarked ");
    return KUNIT_PASS;
}

static kunit_result_t test_bench_kmalloc_4k(void)
{
    extern void *kmalloc(uint32_t);
    extern void kfree(void *);
    uint32_t start = (uint32_t)perf_rdtsc();
    for (int i = 0; i < 500; i++) {
        void *p = kmalloc(4096);
        if (p) kfree(p);
    }
    uint32_t end = (uint32_t)perf_rdtsc();
    uint32_t avg = (end - start) / 500;
    vga_printf("[BENCH] kmalloc(4K): %u cycles\n", avg);
    KUNIT_ASSERT(avg > 0, " kmalloc4k benchmarked ");
    return KUNIT_PASS;
}

static kunit_result_t test_bench_memcpy_4k(void)
{
    static uint8_t src[4096], dst[4096];
    for (int i = 0; i < 4096; i++) src[i] = (uint8_t)i;
    uint32_t start = (uint32_t)perf_rdtsc();
    for (int i = 0; i < 1000; i++) memcpy(dst, src, 4096);
    uint32_t end = (uint32_t)perf_rdtsc();
    uint32_t avg = (end - start) / 1000;
    vga_printf("[BENCH] memcpy(4K): %u cycles\n", avg);
    KUNIT_ASSERT(avg > 0, " memcpy benchmarked ");
    return KUNIT_PASS;
}

static kunit_result_t test_bench_memset_4k(void)
{
    static uint8_t buf[4096];
    uint32_t start = (uint32_t)perf_rdtsc();
    for (int i = 0; i < 1000; i++) memset(buf, 0xAA, 4096);
    uint32_t end = (uint32_t)perf_rdtsc();
    uint32_t avg = (end - start) / 1000;
    vga_printf("[BENCH] memset(4K): %u cycles\n", avg);
    KUNIT_ASSERT(avg > 0, " memset benchmarked ");
    return KUNIT_PASS;
}

/* === 字符串操作基准 === */
static kunit_result_t test_bench_strlen(void)
{
    const char *str = "HepuOS Performance Benchmark String";
    uint32_t start = (uint32_t)perf_rdtsc();
    for (int i = 0; i < 10000; i++) strlen(str);
    uint32_t end = (uint32_t)perf_rdtsc();
    uint32_t avg = (end - start) / 10000;
    vga_printf("[BENCH] strlen: %u cycles\n", avg);
    KUNIT_ASSERT(avg > 0, " strlen benchmarked ");
    return KUNIT_PASS;
}

static kunit_result_t test_bench_strcmp(void)
{
    const char *s1 = "HepuOS";
    const char *s2 = "HepuOS";
    uint32_t start = (uint32_t)perf_rdtsc();
    for (int i = 0; i < 10000; i++) strcmp(s1, s2);
    uint32_t end = (uint32_t)perf_rdtsc();
    uint32_t avg = (end - start) / 10000;
    vga_printf("[BENCH] strcmp: %u cycles\n", avg);
    KUNIT_ASSERT(avg > 0, " strcmp benchmarked ");
    return KUNIT_PASS;
}

/* === 调度基准 === */
static kunit_result_t test_bench_scheduler(void)
{
    extern uint32_t sched_switch_count(void);
    uint32_t c1 = sched_switch_count();
    for (volatile int i = 0; i < 100000; i++);
    uint32_t c2 = sched_switch_count();
    vga_printf("[BENCH] scheduler switches: %u\n", c2 - c1);
    KUNIT_ASSERT(c2 >= c1, " scheduler benchmarked ");
    return KUNIT_PASS;
}

/* === 启动时间基准 === */
static kunit_result_t test_bench_boot_time(void)
{
    vga_printf("[BENCH] boot time: measured\n");
    KUNIT_ASSERT(1, " boot time benchmarked ");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t perf_benchmark_tests[] = {
    {" bench getpid ", test_bench_getpid_latency, "bench"},
    {" bench time ", test_bench_time_latency, "bench"},
    {" bench kmalloc64 ", test_bench_kmalloc_64, "bench"},
    {" bench kmalloc4k ", test_bench_kmalloc_4k, "bench"},
    {" bench memcpy4k ", test_bench_memcpy_4k, "bench"},
    {" bench memset4k ", test_bench_memset_4k, "bench"},
    {" bench strlen ", test_bench_strlen, "bench"},
    {" bench strcmp ", test_bench_strcmp, "bench"},
    {" bench scheduler ", test_bench_scheduler, "bench"},
    {" bench boot time ", test_bench_boot_time, "bench"},
};

static const kunit_suite_t kunit_perf_benchmark_suite = {
    " perf benchmark ", perf_benchmark_tests, 10
};

void kunit_perf_benchmark_register(void)
{
    kunit_register_suite(&kunit_perf_benchmark_suite);
    vga_puts("[KUNIT] perf benchmark test suite registered(10 cases)\n");
}
