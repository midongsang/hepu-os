/* ============================================================================
 * 赫普OS - 内置性能基准测试
 * ========================================================================== */

#include <stdint.h>
#include "types.h"
#include "kbench.h"
#include "string.h"

/* === 字符串基准测试 === */
static char bench_str_buf[256];

static void bench_strlen_func(void)
{
    volatile u32 len = strlen("The quick brown fox jumps over the lazy dog");
    (void)len;
}

static void bench_strcpy_func(void)
{
    strcpy(bench_str_buf, "The quick brown fox jumps over the lazy dog");
}

static void bench_strcmp_func(void)
{
    volatile int result = strcmp("abcdefghij", "abcdefghik");
    (void)result;
}

const kbench_test_t kbench_string_tests[] = {
    {"strlen", NULL, bench_strlen_func, NULL, 10000},
    {"strcpy", NULL, bench_strcpy_func, NULL, 10000},
    {"strcmp", NULL, bench_strcmp_func, NULL, 10000},
};
const u32 kbench_string_count = 3;

/* === 内存基准测试 === */
static u8 bench_mem_src[1024];
static u8 bench_mem_dst[1024];

static void bench_memset_setup(void)
{
    memset(bench_mem_src, 0, 1024);
}

static void bench_memset_func(void)
{
    memset(bench_mem_dst, 0xAA, 1024);
}

static void bench_memcpy_func(void)
{
    memcpy(bench_mem_dst, bench_mem_src, 1024);
}

static void bench_memcmp_func(void)
{
    volatile int result = memcmp(bench_mem_src, bench_mem_dst, 1024);
    (void)result;
}

const kbench_test_t kbench_mem_tests[] = {
    {"memset(1K)", bench_memset_setup, bench_memset_func, NULL, 1000},
    {"memcpy(1K)", NULL, bench_memcpy_func, NULL, 1000},
    {"memcmp(1K)", NULL, bench_memcmp_func, NULL, 1000},
};
const u32 kbench_mem_count = 3;
