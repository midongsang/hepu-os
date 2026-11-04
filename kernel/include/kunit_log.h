#ifndef KUNIT_LOG_H
#include "types.h"
#define KUNIT_LOG_H

#include <stdint.h>
#include "kunit.h"

#define KUNIT_LOG_MAGIC 0x4B54454C  /* "KTEL" */
#define KUNIT_LOG_MAX_ENTRIES 256
#define KUNIT_LOG_VERSION 1

/* 测试日志条目 */
typedef struct {
    u32 timestamp;
    u8  result;       /* kunit_result_t */
    u8  suite_id;
    u8  test_id;
    u16 name_len;
    char     name[64];
} kunit_log_entry_t;

/* 测试日志头部 */
typedef struct {
    u32 magic;
    u32 version;
    u32 entry_count;
    u32 total_tests;
    u32 passed;
    u32 failed;
    u32 skipped;
    u64 total_time_us;
    u32 boot_count;
} kunit_log_header_t;

void kunit_log_init(void);
int kunit_log_add_entry(const char *suite, const char *test,
                        kunit_result_t result);
int kunit_log_save(const kunit_stats_t *stats);
int kunit_log_load(kunit_log_header_t *header, kunit_log_entry_t *entries,
                   u32 max_entries);
int kunit_log_print(void);
int kunit_log_clear(void);
int kunit_log_get_summary(u32 *total_runs, u32 *total_passes,
                          u32 *total_failures);

#endif /* KUNIT_LOG_H */
