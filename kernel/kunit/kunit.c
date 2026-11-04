/* ============================================================================
 * 赫普OS - 内核单元测试框架
 * ========================================================================== */

#include <stdint.h>
#include "types.h"
#include "kunit.h"
#include "vga.h"
#include "string.h"

#define KUNIT_MAX_SUITES 64

static const kunit_suite_t *suites[KUNIT_MAX_SUITES];
static u32 suite_count = 0;
static kunit_stats_t global_stats;
static int kunit_initialized = 0;

void kunit_init(void)
{
    memset(&global_stats, 0, sizeof(global_stats));
    suite_count = 0;
    kunit_initialized = 1;
    vga_puts("[KUNIT] unit test framework init \n");
    kunit_register_suite(&kunit_string_suite);
    kunit_register_suite(&kunit_math_suite);
    kunit_register_suite(&kunit_bitops_suite);
}

void kunit_register_suite(const kunit_suite_t *suite)
{
    if (!kunit_initialized || !suite) return;
    if (suite_count >= KUNIT_MAX_SUITES) return;
    suites[suite_count++] = suite;
    vga_printf("[KUNIT] registered suite: %s(%u test)\n",
               suite->name, suite->test_count);
}

int kunit_run_suite(const kunit_suite_t *suite)
{
    if (!suite) return -1;
    u32 passed = 0, failed = 0, skipped = 0;
    vga_printf("\n=== test suite: %s ===\n", suite->name);
    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puts(0x3F8, "[KUNIT] suite: ");
    serial_puts(0x3F8, suite->name);
    serial_puts(0x3F8, " (");
    serial_puthex(0x3F8, suite->test_count);
    serial_puts(0x3F8, " tests)\n");

    for (u32 i = 0; i < suite->test_count; i++) {
        const kunit_test_t *test = &suite->tests[i];
        vga_printf("  [%u/%u] %s ... ", i + 1, suite->test_count, test->name);
        kunit_result_t result = test->func();
        switch (result) {
            case KUNIT_PASS:
                vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
                vga_puts("PASS\n"); passed++; break;
            case KUNIT_FAIL:
                vga_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
                vga_puts("FAIL\n"); failed++;
                serial_puts(0x3F8, "[KUNIT-FAIL] ");
                serial_puts(0x3F8, suite->name);
                serial_puts(0x3F8, "/");
                serial_puts(0x3F8, test->name);
                serial_puts(0x3F8, "\n");
                break;
            case KUNIT_SKIP:
                vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
                vga_puts("SKIP\n"); skipped++; break;
        }
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
    vga_printf(" result: %u passed, %u failed, %u skipped \n", passed, failed, skipped);
    serial_puts(0x3F8, "[KUNIT] result: ");
    serial_puthex(0x3F8, passed);
    serial_puts(0x3F8, " passed, ");
    serial_puthex(0x3F8, failed);
    serial_puts(0x3F8, " failed, ");
    serial_puthex(0x3F8, skipped);
    serial_puts(0x3F8, " skipped\n");
    if (failed > 0) {
        serial_puts(0x3F8, "[KUNIT-FAIL-SUITE] ");
        serial_puts(0x3F8, suite->name);
        serial_puts(0x3F8, " failed=");
        serial_puthex(0x3F8, failed);
        serial_puts(0x3F8, "\n");
    }
    global_stats.total += suite->test_count;
    global_stats.passed += passed;
    global_stats.failed += failed;
    global_stats.skipped += skipped;
    return failed;
}

int kunit_run_all(void)
{
    if (!kunit_initialized) return -1;
    vga_puts("\n=== Hepu OS kernel unit tests ===\n");
    u32 total_failed = 0;
    for (u32 i = 0; i < suite_count; i++) {
        vga_printf(">>> running suite[%u] %s\n", i, suites[i]->name);
        total_failed += kunit_run_suite(suites[i]);
    }
    kunit_print_report();
    return total_failed;
}

void kunit_get_stats(kunit_stats_t *stats)
{
    if (stats) *stats = global_stats;
}

void kunit_print_report(void)
{
    vga_printf("\n=== test report ===\n");
    vga_printf(" total: %u passed: %u failed: %u skipped: %u\n",
               global_stats.total, global_stats.passed,
               global_stats.failed, global_stats.skipped);
    vga_printf(" pass rate: %u%%\n",
               global_stats.total ?
               (global_stats.passed * 100 / global_stats.total) : 0);
    if (global_stats.failed == 0) {
        vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
        vga_puts(" all tests passed !\n");
    } else {
        vga_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        vga_printf(" has %u test failed !\n", global_stats.failed);
    }
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}
