#ifndef BIST_H
#define BIST_H

#include <stdint.h>

/* 内置自检 (Built-In Self-Test) 结果 */
typedef enum {
    BIST_PASS = 0,
    BIST_FAIL = 1,
    BIST_SKIP = 2
} bist_result_t;

/* 自检项 */
typedef struct {
    const char *name;
    const char *category;
    bist_result_t result;
    const char *detail;
    uint32_t duration_ms;
} bist_item_t;

/* 自检报告 */
typedef struct {
    int total;
    int passed;
    int failed;
    int skipped;
    uint32_t total_time_ms;
    bist_item_t items[32];
} bist_report_t;

/* 运行全部自检 */
int bist_run_all(bist_report_t *report);

/* 单项自检 */
bist_result_t bist_memory_test(void);
bist_result_t bist_cpu_test(void);
bist_result_t bist_interrupt_test(void);
bist_result_t bist_timer_test(void);
bist_result_t bist_vfs_test(void);
bist_result_t bist_ipc_test(void);
bist_result_t bist_driver_test(void);
bist_result_t bist_fingerprint_test(void);
bist_result_t bist_syscall_test(void);
bist_result_t bist_scheduler_test(void);

/* 打印报告 */
void bist_print_report(bist_report_t *report);

/* 初始化 */
void bist_init(void);

#endif /* BIST_H */
