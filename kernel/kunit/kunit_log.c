/* ============================================================================
 * 赫普OS - 测试结果持久化
 * 将测试结果保存到内存缓冲区，可导出到文件系统
 * ========================================================================== */

#include <stdint.h>
#include "types.h"
#include "kunit_log.h"
#include "vga.h"
#include "string.h"

static kunit_log_header_t log_header;
static kunit_log_entry_t log_entries[KUNIT_LOG_MAX_ENTRIES];
static u32 log_entry_count = 0;
static u32 boot_count = 0;
static int log_initialized = 0;

void kunit_log_init(void)
{
    memset(&log_header, 0, sizeof(log_header));
    memset(log_entries, 0, sizeof(log_entries));
    log_entry_count = 0;
    boot_count++;
    log_initialized = 1;

    log_header.magic = KUNIT_LOG_MAGIC;
    log_header.version = KUNIT_LOG_VERSION;
    log_header.boot_count = boot_count;

    vga_printf("[KUNIT-LOG] test log init(boot=%u)\n", boot_count);
}

int kunit_log_add_entry(const char *suite, const char *test,
                        kunit_result_t result)
{
    (void)suite;
    if (!log_initialized || !test) return -1;
    if (log_entry_count >= KUNIT_LOG_MAX_ENTRIES) return -1;

    kunit_log_entry_t *entry = &log_entries[log_entry_count];
    entry->timestamp = 0;  /* 简化：不记录实际时间 */
    entry->result = (u8)result;
    entry->suite_id = 0;
    entry->test_id = (u8)log_entry_count;

    u32 name_len = strlen(test);
    if (name_len > 63) name_len = 63;
    memcpy(entry->name, test, name_len);
    entry->name[name_len] = '\0';
    entry->name_len = (u16)name_len;

    log_entry_count++;
    log_header.entry_count = log_entry_count;

    return 0;
}

int kunit_log_save(const kunit_stats_t *stats)
{
    if (!log_initialized || !stats) return -1;

    log_header.total_tests = stats->total;
    log_header.passed = stats->passed;
    log_header.failed = stats->failed;
    log_header.skipped = stats->skipped;
    log_header.total_time_us = stats->total_time_us;

    vga_printf("[KUNIT-LOG] save test results: %u passed, %u failed, %u skipped \n",
               stats->passed, stats->failed, stats->skipped);

    /* 在实际系统中，这里会写入文件系统
     * 目前保存在内存缓冲区中 */
    return 0;
}

int kunit_log_load(kunit_log_header_t *header, kunit_log_entry_t *entries,
                   u32 max_entries)
{
    if (!header || !entries) return -1;
    if (!log_initialized) return -1;

    *header = log_header;
    u32 count = log_entry_count;
    if (count > max_entries) count = max_entries;
    memcpy(entries, log_entries, count * sizeof(kunit_log_entry_t));

    return (int)count;
}

int kunit_log_print(void)
{
    if (!log_initialized) return -1;

    vga_puts("\n=== test log ===\n");
    vga_printf(" boot count: %u\n", log_header.boot_count);
    vga_printf(" entry count: %u\n", log_entry_count);
    vga_printf(" total: %u passed: %u failed: %u skipped: %u\n",
               log_header.total_tests, log_header.passed,
               log_header.failed, log_header.skipped);

    for (u32 i = 0; i < log_entry_count; i++) {
        const char *result_str = "?";
        switch (log_entries[i].result) {
            case KUNIT_PASS: result_str = "PASS"; break;
            case KUNIT_FAIL: result_str = "FAIL"; break;
            case KUNIT_SKIP: result_str = "SKIP"; break;
        }
        vga_printf("  [%u] %s: %s\n", i, result_str, log_entries[i].name);
    }

    return 0;
}

int kunit_log_clear(void)
{
    if (!log_initialized) return -1;
    memset(log_entries, 0, sizeof(log_entries));
    log_entry_count = 0;
    log_header.entry_count = 0;
    log_header.total_tests = 0;
    log_header.passed = 0;
    log_header.failed = 0;
    log_header.skipped = 0;
    vga_puts("[KUNIT-LOG] log cleared \n");
    return 0;
}

int kunit_log_get_summary(u32 *total_runs, u32 *total_passes,
                          u32 *total_failures)
{
    if (!log_initialized) return -1;
    if (total_runs) *total_runs = log_header.entry_count;
    if (total_passes) *total_passes = log_header.passed;
    if (total_failures) *total_failures = log_header.failed;
    return 0;
}
