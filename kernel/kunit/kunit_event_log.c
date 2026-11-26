/* ============================================================================
 * KUnit: 统一事件日志核心一致性
 * 只读既有 ring + 追加一条 KUnit 事件，不清空启动日志。
 *
 * 覆盖：序号单调、级别过滤、满环覆盖最旧、emergency 计数、
 *       带盘时的追加保留旧内容、轮转触发与 .1 文件生成。
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
    static char text[EVENT_LOG_TEXT_MAX];
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

/* 满环：写入超过容量的条目后，条数稳定在容量，最旧条目被覆盖，序号仍递增。 */
static kunit_result_t test_ring_wraps_oldest(void)
{
    event_log_entry_t before[EVENT_LOG_CAP];
    uint32_t base_last_seq = 0;
    uint32_t base = event_log_snapshot(before, EVENT_LOG_CAP);
    if (base > 0) base_last_seq = before[base - 1].seq;

    for (uint32_t i = 0; i < EVENT_LOG_CAP + 5; i++) {
        event_log_emit(EVENT_INFO, "kunit", 77, "wrap flood %u", i);
    }

    event_log_entry_t after[EVENT_LOG_CAP];
    uint32_t got = event_log_snapshot(after, EVENT_LOG_CAP);
    KUNIT_ASSERT_EQ(got, EVENT_LOG_CAP, "ring stays at capacity after flood");
    KUNIT_ASSERT(after[0].seq > base_last_seq,
                 "oldest entries overwritten, seq still advanced");
    for (uint32_t i = 1; i < got; i++) {
        KUNIT_ASSERT(after[i].seq > after[i - 1].seq,
                     "wrapped ring seq strictly increasing");
    }
    return KUNIT_PASS;
}

/* emergency 路径：定长环 + 溢出 drop 计数；不走锁/变参格式化。 */
static kunit_result_t test_emergency_drop_count(void)
{
    uint32_t drops_before = event_log_emergency_drop_count();
    uint32_t cap = EVENT_EMERG_CAP;

    /* emergency_write 不做变参格式化，测试这里只传静态定长消息。 */
    for (uint32_t i = 0; i < cap + 5; i++) {
        event_log_emergency_write(EVENT_FATAL, "kernel", 13,
                                  "emergency sample event");
    }

    KUNIT_ASSERT_EQ(event_log_emergency_count(), cap,
                    "emergency ring stays at fixed capacity");
    KUNIT_ASSERT_EQ(event_log_emergency_drop_count(), drops_before + 5,
                    "overflow counted as drop");

    event_log_entry_t out[EVENT_EMERG_CAP + 1];
    uint32_t got = event_log_emergency_snapshot(out, EVENT_EMERG_CAP + 1);
    KUNIT_ASSERT(got >= cap, "emergency snapshot returns buffered entries");
    KUNIT_ASSERT(strstr(out[got - 1].msg, "drops=") != 0,
                 "overflow stat entry records drop count");
    return KUNIT_PASS;
}

static int ev_disk_ready(void)
{
#if defined(__x86_64__)
    extern int ext2_64_is_mounted(void);
    return ext2_64_is_mounted() != 0;
#else
    extern int ext2_mounted;
    return ext2_mounted != 0;
#endif
}

static void ev_skip_no_disk(const char *case_name)
{
    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[KUNIT-SKIP] event-log/");
    serial_puts(0x3F8, case_name);
    serial_puts(0x3F8, " (ext2 backend unavailable)\n");
}

/* 追加落盘：第一次持久化的标记在第二次持久化后仍然存在（旧内容保留）。 */
static kunit_result_t test_append_keeps_history(void)
{
    static uint8_t buf[EVENT_LOG_ROTATE_BYTES + EVENT_LOG_TEXT_MAX];
    uint32_t len = 0;

    if (!ev_disk_ready()) {
        ev_skip_no_disk("append_keeps_history");
        return KUNIT_SKIP;
    }

    KUNIT_ASSERT(event_log_persist("MARK-A-append-test\n", 19) >= 0,
                 "first persist succeeds");
    KUNIT_ASSERT(event_log_file_read(EVENT_LOG_PATH_MAIN, buf, sizeof(buf),
                                     &len) == 0,
                 "read back main file");
    KUNIT_ASSERT(len > 0 && strstr((char *)buf, "MARK-A-append-test") != 0,
                 "first marker present after first save");

    KUNIT_ASSERT(event_log_persist("MARK-B-append-test\n", 19) >= 0,
                 "second persist succeeds");
    len = 0;
    KUNIT_ASSERT(event_log_file_read(EVENT_LOG_PATH_MAIN, buf, sizeof(buf),
                                     &len) == 0,
                 "read back main file again");
    KUNIT_ASSERT(strstr((char *)buf, "MARK-A-append-test") != 0,
                 "previous marker still present after append");
    KUNIT_ASSERT(strstr((char *)buf, "MARK-B-append-test") != 0,
                 "new marker present after append");
    return KUNIT_PASS;
}

/* 轮转：旧主文件在超阈值后落为 .1，主文件只保留新内容。 */
static kunit_result_t test_rotation_creates_dot1(void)
{
    static uint8_t main_backup[EVENT_LOG_ROTATE_BYTES];
    static uint8_t rot1_backup[EVENT_LOG_ROTATE_BYTES];
    uint32_t main_len = 0, rot1_len = 0;
    static uint8_t buf[EVENT_LOG_ROTATE_BYTES + EVENT_LOG_TEXT_MAX];
    uint32_t len = 0;
    int had_main = 0, had_rot1 = 0;

    if (!ev_disk_ready()) {
        ev_skip_no_disk("rotation_creates_dot1");
        return KUNIT_SKIP;
    }

    /* 备份现场，测试结束后还原，避免污染真实事件日志。 */
    if (event_log_file_size(EVENT_LOG_PATH_MAIN, &main_len) == 0) {
        had_main = 1;
        event_log_file_read(EVENT_LOG_PATH_MAIN, main_backup,
                            sizeof(main_backup), &main_len);
    }
    if (event_log_file_size(EVENT_LOG_PATH_ROT1, &rot1_len) == 0) {
        had_rot1 = 1;
        event_log_file_read(EVENT_LOG_PATH_ROT1, rot1_backup,
                            sizeof(rot1_backup), &rot1_len);
    }

    event_log_test_set_rotate_threshold(64);
    event_log_persist("ROT-A-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n", 42);
    event_log_persist("ROT-B-bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n", 42);
    event_log_test_set_rotate_threshold(0);

    KUNIT_ASSERT(event_log_file_read(EVENT_LOG_PATH_ROT1, buf, sizeof(buf),
                                     &len) == 0 && len > 0,
                 "dot1 file exists after rotation");
    KUNIT_ASSERT(strstr((char *)buf, "ROT-A-") != 0,
                 "dot1 holds previous main content");

    len = 0;
    KUNIT_ASSERT(event_log_file_read(EVENT_LOG_PATH_MAIN, buf, sizeof(buf),
                                     &len) == 0 && len > 0,
                 "main file holds new content");
    KUNIT_ASSERT(strstr((char *)buf, "ROT-B-") != 0,
                 "main holds latest content");
    KUNIT_ASSERT(strstr((char *)buf, "ROT-A-") == 0,
                 "main does not keep rotated-away content");

    /* 还原现场。 */
    if (had_main) {
        event_log_file_write(EVENT_LOG_PATH_MAIN, main_backup, main_len);
    } else {
        event_log_file_remove(EVENT_LOG_PATH_MAIN);
    }
    if (had_rot1) {
        event_log_file_write(EVENT_LOG_PATH_ROT1, rot1_backup, rot1_len);
    } else {
        event_log_file_remove(EVENT_LOG_PATH_ROT1);
    }
    return KUNIT_PASS;
}

static const kunit_test_t event_log_tests[] = {
    { "stream_integrity",        test_event_stream_integrity,        "event-log" },
    { "format_filters",          test_event_format_filters,          "event-log" },
    { "ring_wraps_oldest",       test_ring_wraps_oldest,             "event-log" },
    { "emergency_drop_count",     test_emergency_drop_count,          "event-log" },
    { "append_keeps_history",    test_append_keeps_history,         "event-log" },
    { "rotation_creates_dot1",   test_rotation_creates_dot1,        "event-log" },
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
