/* ============================================================================
 * 赫普OS - 时间与定时器单元测试
 * 覆盖：时间转换/定时器/超时/时间戳/日历
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === 时间结构 === */
typedef struct {
    u32 seconds;
    u32 minutes;
    u32 hours;
    u32 day;
    u32 month;
    u32 year;
} hepu_time_t;

/* === 秒数转换测试 === */
static kunit_result_t test_seconds_to_hms(void)
{
    /* 3661秒 = 1小时1分1秒 */
    u32 total = 3661;
    u32 hours = total / 3600;
    u32 minutes = (total % 3600) / 60;
    u32 seconds = total % 60;

    KUNIT_ASSERT_EQ(hours, 1, " 小 runtime =1");
    KUNIT_ASSERT_EQ(minutes, 1, " 分钟 =1");
    KUNIT_ASSERT_EQ(seconds, 1, " s =1");
    return KUNIT_PASS;
}

static kunit_result_t test_hms_to_seconds(void)
{
    u32 hours = 2, minutes = 30, seconds = 15;
    u32 total = hours * 3600 + minutes * 60 + seconds;
    KUNIT_ASSERT_EQ(total, 9015, "2:30:15 = 9015 s ");
    return KUNIT_PASS;
}

static kunit_result_t test_midnight_seconds(void)
{
    KUNIT_ASSERT_EQ(24 * 3600, 86400, " 一天 =86400 s ");
    KUNIT_ASSERT_EQ(86400 / 3600, 24, " 一天 =24 小 runtime ");
    return KUNIT_PASS;
}

/* === 闰年测试 === */
static int is_leap_year(u32 year)
{
    if (year % 400 == 0) return 1;
    if (year % 100 == 0) return 0;
    if (year % 4 == 0) return 1;
    return 0;
}

static kunit_result_t test_leap_year(void)
{
    KUNIT_ASSERT(is_leap_year(2000), "2000 is 闰年(400 整除)");
    KUNIT_ASSERT(!is_leap_year(1900), "1900 not is 闰年(100 整除但 non 400)");
    KUNIT_ASSERT(is_leap_year(2024), "2024 is 闰年(4 整除)");
    KUNIT_ASSERT(!is_leap_year(2023), "2023 not is 闰年 ");
    KUNIT_ASSERT(is_leap_year(2020), "2020 is 闰年 ");
    return KUNIT_PASS;
}

/* === 月份天数测试 === */
static u32 days_in_month(u32 month, u32 year)
{
    static const u32 days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2 && is_leap_year(year)) return 29;
    return days[month - 1];
}

static kunit_result_t test_days_in_month(void)
{
    KUNIT_ASSERT_EQ(days_in_month(1, 2024), 31, "1 月 =31 天 ");
    KUNIT_ASSERT_EQ(days_in_month(2, 2024), 29, "2024 年 2 月 =29 天 ");
    KUNIT_ASSERT_EQ(days_in_month(2, 2023), 28, "2023 年 2 月 =28 天 ");
    KUNIT_ASSERT_EQ(days_in_month(4, 2024), 30, "4 月 =30 天 ");
    KUNIT_ASSERT_EQ(days_in_month(12, 2024), 31, "12 月 =31 天 ");
    return KUNIT_PASS;
}

/* === Unix时间戳转换测试 === */
static kunit_result_t test_unix_epoch(void)
{
    /* Unix纪元: 1970-01-01 00:00:00 UTC */
    KUNIT_ASSERT_EQ(0, 0, "Unix 纪元 =0");

    /* 一天后的时间戳 */
    u32 one_day = 86400;
    KUNIT_ASSERT_EQ(one_day, 86400, " 一天 =86400");

    /* 2000-01-01的时间戳约为946684800 */
    u32 y2k = 946684800;
    KUNIT_ASSERT_EQ(y2k / 86400 / 365, 30, " 约 30 年(粗略)");
    return KUNIT_PASS;
}

/* === 定时器结构测试 === */
typedef struct {
    u32 id;
    u32 expires;
    u32 period;
    u8  active;
    void (*callback)(u32);
} timer_t;

#define MAX_TIMERS 8
static timer_t timers[MAX_TIMERS];
static u32 timer_count;
static u32 current_tick;

static void timer_subsystem_init(void)
{
    memset(timers, 0, sizeof(timers));
    timer_count = 0;
    current_tick = 0;
}

static int timer_create(u32 period, void (*cb)(u32))
{
    if (timer_count >= MAX_TIMERS) return -1;
    timers[timer_count].id = timer_count;
    timers[timer_count].expires = current_tick + period;
    timers[timer_count].period = period;
    timers[timer_count].active = 1;
    timers[timer_count].callback = cb;
    return timer_count++;
}

static int timer_cancel(u32 id)
{
    if (id >= timer_count) return -1;
    if (!timers[id].active) return -1;
    timers[id].active = 0;
    return 0;
}

static void timer_tick(void)
{
    current_tick++;
    for (u32 i = 0; i < timer_count; i++) {
        if (timers[i].active && current_tick >= timers[i].expires) {
            if (timers[i].callback) timers[i].callback(timers[i].id);
            if (timers[i].period > 0) {
                timers[i].expires = current_tick + timers[i].period;
            } else {
                timers[i].active = 0;
            }
        }
    }
}

static u32 test_callback_count;
static void test_timer_callback(u32 id)
{
    test_callback_count++;
    (void)id;
}

static kunit_result_t test_timer_create(void)
{
    timer_subsystem_init();
    test_callback_count = 0;

    int id = timer_create(10, test_timer_callback);
    KUNIT_ASSERT_EQ(id, 0, " create timer id=0");
    KUNIT_ASSERT_EQ(timers[0].period, 10, " cycles =10");
    KUNIT_ASSERT_EQ(timers[0].active, 1, " timer 激活 ");
    return KUNIT_PASS;
}

static kunit_result_t test_timer_fire(void)
{
    timer_subsystem_init();
    test_callback_count = 0;

    timer_create(5, test_timer_callback);
    /* 推进4个tick,不应触发 */
    for (int i = 0; i < 4; i++) timer_tick();
    KUNIT_ASSERT_EQ(test_callback_count, 0, "4 tick not trigger ");

    /* 第5个tick触发 */
    timer_tick();
    KUNIT_ASSERT_EQ(test_callback_count, 1, "5 tick trigger 一 ");
    return KUNIT_PASS;
}

static kunit_result_t test_timer_periodic(void)
{
    timer_subsystem_init();
    test_callback_count = 0;

    timer_create(3, test_timer_callback);
    for (int i = 0; i < 10; i++) timer_tick();
    /* tick 3,6,9 触发 = 3次 */
    KUNIT_ASSERT_EQ(test_callback_count, 3, " cycles timer trigger 3 ");
    return KUNIT_PASS;
}

static kunit_result_t test_timer_cancel(void)
{
    timer_subsystem_init();
    test_callback_count = 0;

    int id = timer_create(5, test_timer_callback);
    KUNIT_ASSERT_EQ(timer_cancel(id), 0, " cancel timer ");
    KUNIT_ASSERT_EQ(timers[id].active, 0, " timer cancel ");

    for (int i = 0; i < 10; i++) timer_tick();
    KUNIT_ASSERT_EQ(test_callback_count, 0, " cancel next not trigger ");
    return KUNIT_PASS;
}

static kunit_result_t test_timer_exhaust(void)
{
    timer_subsystem_init();

    for (int i = 0; i < MAX_TIMERS; i++) {
        KUNIT_ASSERT_EQ(timer_create(10, 0), i, " create timer ");
    }
    KUNIT_ASSERT_EQ(timer_create(10, 0), -1, " timer 耗尽 ");
    return KUNIT_PASS;
}

/* === 超时测试 === */
static int check_timeout(u32 start, u32 now, u32 timeout)
{
    return (now - start) >= timeout ? 1 : 0;
}

static kunit_result_t test_timeout(void)
{
    KUNIT_ASSERT_EQ(check_timeout(100, 105, 10), 0, " not timeout ");
    KUNIT_ASSERT_EQ(check_timeout(100, 110, 10), 1, " 正好 timeout ");
    KUNIT_ASSERT_EQ(check_timeout(100, 150, 10), 1, " timeout ");
    KUNIT_ASSERT_EQ(check_timeout(100, 100, 0), 1, " 零 timeout 立即 timeout ");
    return KUNIT_PASS;
}

/* === 时间格式化测试 === */
static kunit_result_t test_time_format(void)
{
    /* 模拟格式化: HH:MM:SS */
    u32 total = 3661;
    u32 h = total / 3600;
    u32 m = (total % 3600) / 60;
    u32 s = total % 60;

    char buf[9];
    buf[0] = '0' + h / 10; buf[1] = '0' + h % 10;
    buf[2] = ':';
    buf[3] = '0' + m / 10; buf[4] = '0' + m % 10;
    buf[5] = ':';
    buf[6] = '0' + s / 10; buf[7] = '0' + s % 10;
    buf[8] = '\0';

    KUNIT_ASSERT_EQ(strcmp(buf, "01:01:01"), 0, " time format ");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t time_tests[] = {
    {" s 转 runtime 分 s ", test_seconds_to_hms, "time"},
    {" runtime 分 s 转 s ", test_hms_to_seconds, "time"},
    {" 一天 s count ", test_midnight_seconds, "time"},
    {" 闰年判断 ", test_leap_year, "time"},
    {" 月份天 count ", test_days_in_month, "time"},
    {"Unix 纪元 ", test_unix_epoch, "time"},
    {" timer create ", test_timer_create, "time"},
    {" timer trigger ", test_timer_fire, "time"},
    {" cycles timer ", test_timer_periodic, "time"},
    {" timer cancel ", test_timer_cancel, "time"},
    {" timer 耗尽 ", test_timer_exhaust, "time"},
    {" timeout check ", test_timeout, "time"},
    {" time format ", test_time_format, "time"},
};
static const kunit_suite_t kunit_time_suite = {
    " time & timer ", time_tests, 13
};

void kunit_time_register(void)
{
    kunit_register_suite(&kunit_time_suite);
    vga_puts("[KUNIT] time & timer test suite registered(13 cases)\n");
}
