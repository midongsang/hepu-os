/* ============================================================================
 * 赫普OS - 内核性能基准测试框架
 * ========================================================================== */

#include <stdint.h>
#include "types.h"
#include "kbench.h"
#include "vga.h"

static int kbench_initialized = 0;

/* 64位除以32位（手写长除，避免libgcc的__udivdi3/__umoddi3）
 * 返回商，余数通过rem_out返回 */
static u64 divrem64_u32(u64 dividend, u32 divisor, u32 *rem_out)
{
    u64 quotient = 0;
    u32 rem = 0;
    for (int i = 63; i >= 0; i--) {
        rem = (rem << 1) | (u32)((dividend >> i) & 1);
        if (rem >= divisor) {
            rem -= divisor;
            quotient |= (u64)1 << i;
        }
    }
    *rem_out = rem;
    return quotient;
}

static u64 div64_u32(u64 dividend, u32 divisor)
{
    u32 rem;
    return divrem64_u32(dividend, divisor, &rem);
}

/* 64位整数转十进制字符串 */
static void u64_to_dec(char *buf, u64 val)
{
    char tmp[24];
    int pos = 0;
    if (val == 0) { buf[0] = '0'; buf[1] = 0; return; }
    while (val > 0) {
        u32 digit;
        val = divrem64_u32(val, 10, &digit);
        tmp[pos++] = (char)('0' + digit);
    }
    for (int i = 0; i < pos; i++) buf[i] = tmp[pos - 1 - i];
    buf[pos] = 0;
}

/* 读取CPU周期数（RDTSC） */
u64 kbench_read_cycles(void)
{
    u32 lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}

void kbench_init(void)
{
    kbench_initialized = 1;
    vga_puts("[KBENCH] perf benchmark framework init \n");
}

/* 运行单个基准测试 */
int kbench_run(const kbench_test_t *test, kbench_result_t *result)
{
    if (!test || !result) return -1;

    result->name = test->name;
    result->iterations = test->iterations;
    result->total_cycles = 0;
    result->min_cycles = UINT64_MAX;
    result->max_cycles = 0;

    /* setup */
    if (test->setup) test->setup();

    /* 预热 */
    for (u32 i = 0; i < 100; i++) {
        if (test->func) test->func();
    }

    /* 正式测试 */
    for (u64 i = 0; i < test->iterations; i++) {
        u64 start = kbench_read_cycles();
        if (test->func) test->func();
        u64 end = kbench_read_cycles();
        u64 cycles = end - start;

        result->total_cycles += cycles;
        if (cycles < result->min_cycles) result->min_cycles = cycles;
        if (cycles > result->max_cycles) result->max_cycles = cycles;
    }

    /* teardown */
    if (test->teardown) test->teardown();

    /* 计算平均值（手写64/32除法，避免__udivdi3） */
    if (test->iterations > 0) {
        result->avg_cycles = div64_u32(result->total_cycles,
                                       (u32)test->iterations);
    } else {
        result->avg_cycles = 0;
    }

    return 0;
}

/* 运行所有基准测试 */
int kbench_run_all(const kbench_test_t *tests, u32 count,
                   kbench_result_t *results)
{
    if (!tests || !results || count == 0) return -1;

    vga_puts("\n=== Hepu OS perf benchmark ===\n");

    for (u32 i = 0; i < count; i++) {
        kbench_run(&tests[i], &results[i]);
        kbench_print_result(&results[i]);
    }

    kbench_print_report(results, count);
    return 0;
}

/* 打印单个结果（用十进制字符串避免%llu） */
void kbench_print_result(const kbench_result_t *result)
{
    char buf_avg[24], buf_min[24], buf_max[24];
    if (!result) return;
    u64_to_dec(buf_avg, result->avg_cycles);
    u64_to_dec(buf_min, result->min_cycles);
    u64_to_dec(buf_max, result->max_cycles);
    vga_printf("  %-20s avg=%8s min=%8s max=%8s cycles\n",
               result->name, buf_avg, buf_min, buf_max);
}

/* 打印报告 */
void kbench_print_report(const kbench_result_t *results, u32 count)
{
    if (!results || count == 0) return;

    vga_puts("\n=== benchmark report ===\n");
    vga_printf("%-20s %10s %10s %10s %10s\n",
               " name ", " iteration ", " avg ", " min ", " max ");
    vga_puts("------------------------------------------------------------\n");

    for (u32 i = 0; i < count; i++) {
        char buf_it[24], buf_avg[24], buf_min[24], buf_max[24];
        u64_to_dec(buf_it, results[i].iterations);
        u64_to_dec(buf_avg, results[i].avg_cycles);
        u64_to_dec(buf_min, results[i].min_cycles);
        u64_to_dec(buf_max, results[i].max_cycles);
        vga_printf("%-20s %10s %10s %10s %10s\n",
                   results[i].name, buf_it, buf_avg, buf_min, buf_max);
    }
}
