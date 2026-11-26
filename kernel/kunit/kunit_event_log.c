/* ============================================================================
 * KUnit: 统一事件日志核心一致性
 * 只读既有 ring + 追加一条 KUnit 事件，不清空启动日志。
 * ========================================================================== */
#include <stdint.h>
#include "kunit.h"
#include "event_log.h"
#include "string.h"

static kunit_result_t test_event_stream_integrity(void)
{
    event_log_entry_t entries[EVENT_LOG_CAP];
    uint32_t n = event_log_count();
    KUNIT_ASSERT(n <= EVENT_LOG_CAP, "event count <= capacity");
    uint32_t got = event_log_snapshot(entries, EVENT_LOG_CAP);
    KUNIT_ASSERT_EQ(got, n, "snapshot returns same count");
    for (uint32_t i = 1; i < got; i++) {
        KUNIT_ASSERT(entries[i].seq > entries[i - 1].seq,
                     "event seq strictly increasing");
    }
    for (uint32_t i = 0; i < got; i++) {
        KUNIT_ASSERT(entries[i].source[EVENT_LOG_SRC_MAX - 1] == 0,
                     "source is NUL-terminated");
        KUNIT_ASSERT(entries[i].msg[EVENT_LOG_MSG_MAX - 1] == 0,
                     "message is NUL-terminated");
        KUNIT_ASSERT(entries[i].level <= EVENT_FATAL,
                     "level in range");
    }
    return KUNIT_PASS;
}

static kunit_result_t test_event_format_filters(void)
{
    static char text[4096];
    event_log_emit(EVENT_INFO, "kunit", 77, "event log selftest marker");
    uint32_t n = event_log_format(text, sizeof(text), EVENT_INFO);
    KUNIT_ASSERT(n > 0, "INFO format is non-empty");
    KUNIT_ASSERT(strstr(text, "event log selftest marker") != 0,
                 "latest event appears in formatted output");

    n = event_log_format(text, sizeof(text), EVENT_FATAL);
    KUNIT_ASSERT(strstr(text, "event log selftest marker") == 0,
                 "INFO event filtered out at FATAL level");
    return KUNIT_PASS;
}

static const kunit_test_t event_log_tests[] = {
    { "stream_integrity", test_event_stream_integrity, "event-log" },
    { "format_filters",   test_event_format_filters,   "event-log" },
};

const kunit_suite_t kunit_event_log_suite = {
    "event-log",
    event_log_tests,
    sizeof(event_log_tests) / sizeof(event_log_tests[0])
};

void kunit_event_log_register(void)
{
    extern void kunit_register_suite(const kunit_suite_t *);
    kunit_register_suite(&kunit_event_log_suite);
}
