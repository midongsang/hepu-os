#ifndef KBENCH_H
#include "types.h"
#define KBENCH_H

#include <stdint.h>

/* 基准测试结果 */
typedef struct {
    const char *name;
    u64 iterations;
    u64 total_cycles;
    u64 min_cycles;
    u64 max_cycles;
    u64 avg_cycles;
} kbench_result_t;

/* 基准测试函数 */
typedef void (*kbench_func_t)(void);

/* 基准测试项 */
typedef struct {
    const char *name;
    kbench_func_t setup;
    kbench_func_t func;
    kbench_func_t teardown;
    u64 iterations;
} kbench_test_t;

/* API */
void kbench_init(void);
int kbench_run(const kbench_test_t *test, kbench_result_t *result);
int kbench_run_all(const kbench_test_t *tests, u32 count,
                   kbench_result_t *results);
void kbench_print_result(const kbench_result_t *result);
void kbench_print_report(const kbench_result_t *results, u32 count);
u64 kbench_read_cycles(void);

/* 内置基准测试 */
extern const kbench_test_t kbench_string_tests[];
extern const u32 kbench_string_count;
extern const kbench_test_t kbench_mem_tests[];
extern const u32 kbench_mem_count;
extern const kbench_test_t kbench_kernel_tests[];
extern const u32 kbench_kernel_count;
int kbench_run_kernel(void);

#endif /* KBENCH_H */
