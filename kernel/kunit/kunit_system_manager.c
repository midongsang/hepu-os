/* ============================================================================
 * KUnit: 统一服务表一致性 / 依赖拓扑 / 健康标注 / 退避重试
 * 不清空启动注册表；用唯一前缀 k- 注册临时服务做定向验证。
 * ========================================================================== */
#include <stdint.h>
#include "kunit.h"
#include "services.h"
#include "event_log.h"
#include "string.h"

static kunit_result_t test_service_table_consistency(void)
{
    service_t list[SERVICE_MAX];
    int n = services_list(list, SERVICE_MAX);
    KUNIT_ASSERT(n >= 5, "boot registered at least 5 services");
    KUNIT_ASSERT_EQ(n, services_count(), "list count matches manager count");
    KUNIT_ASSERT(services_active_count() >= 5, "boot services active");

    for (int i = 0; i < n; i++) {
        KUNIT_ASSERT(list[i].def.name[0] != 0, "service has a name");
        KUNIT_ASSERT(list[i].state <= SERVICE_FAILED, "service state in range");
        KUNIT_ASSERT(list[i].health <= HEALTH_FAILED, "health in range");
    }

    service_t st;
    KUNIT_ASSERT(services_status("network", &st) == 0, "network status found");
    KUNIT_ASSERT(strcmp(st.def.name, "network") == 0, "network name exact");
    KUNIT_ASSERT(st.state == SERVICE_ACTIVE, "network active");
    return KUNIT_PASS;
}

/* 内置五项均为空壳：状态机 ACTIVE 但健康必须是 STUB，不得算 healthy。 */
static kunit_result_t test_stub_not_healthy(void)
{
    service_t st;
    KUNIT_ASSERT(services_status("network", &st) == 0, "network found");
    KUNIT_ASSERT_EQ(st.health, HEALTH_STUB, "network marked stub");
    KUNIT_ASSERT_EQ(services_is_healthy("network"), 0, "stub is not healthy");
    KUNIT_ASSERT_EQ(services_healthy_count(), 0, "stub boot set has no healthy svc");

    KUNIT_ASSERT(services_status("gui", &st) == 0, "gui found");
    KUNIT_ASSERT_EQ(st.health, HEALTH_STUB, "gui marked stub");
    return KUNIT_PASS;
}

/* ---- 临时服务夹具 ---- */

static int ok_start(void) { return 0; }
static int ok_stop(void) { return 0; }

static int reg_scratch(const char *name, const char *dep,
                       int restart, int max_retries, int (*start)(void))
{
    service_def_t d;
    memset(&d, 0, sizeof(d));
    strcpy(d.name, name);
    strcpy(d.description, "kunit scratch service");
    d.type = SERVICE_TYPE_DAEMON;
    d.start_policy = SERVICE_START_MANUAL;
    d.start_func = start;
    d.stop_func = ok_stop;
    d.enabled = 1;
    d.stub = 1;
    d.restart_on_failure = restart;
    d.max_retries = max_retries;
    if (dep) {
        d.dep_count = 1;
        strcpy(d.depends[0], dep);
    }
    return services_register(&d);
}

static uint32_t event_seq_for(uint32_t code, const char *name)
{
    event_log_entry_t entries[EVENT_LOG_CAP];
    uint32_t n = event_log_snapshot(entries, EVENT_LOG_CAP);
    for (uint32_t i = 0; i < n; i++) {
        if (entries[i].code == code && strstr(entries[i].msg, name)) {
            return entries[i].seq;
        }
    }
    return 0;
}

static int event_has_msg(uint32_t code, const char *needle)
{
    event_log_entry_t entries[EVENT_LOG_CAP];
    uint32_t n = event_log_snapshot(entries, EVENT_LOG_CAP);
    for (uint32_t i = 0; i < n; i++) {
        if (entries[i].code == code && strstr(entries[i].msg, needle)) return 1;
    }
    return 0;
}

/* 拓扑顺序：k-c 依赖 k-b，k-b 依赖 k-a；启动事件必须 a→b→c。 */
static kunit_result_t test_topo_order(void)
{
    KUNIT_ASSERT_EQ(reg_scratch("k-a", 0, 0, 0, ok_start), 0, "reg k-a");
    KUNIT_ASSERT_EQ(reg_scratch("k-b", "k-a", 0, 0, ok_start), 0, "reg k-b");
    KUNIT_ASSERT_EQ(reg_scratch("k-c", "k-b", 0, 0, ok_start), 0, "reg k-c");

    KUNIT_ASSERT_EQ(services_start("k-c"), 0, "chain starts from leaf");

    uint32_t sa = event_seq_for(11, "k-a");
    uint32_t sb = event_seq_for(11, "k-b");
    uint32_t sc = event_seq_for(11, "k-c");
    KUNIT_ASSERT(sa && sb && sc, "start events recorded");
    KUNIT_ASSERT(sa < sb && sb < sc, "topo order: a before b before c");

    service_t st;
    KUNIT_ASSERT(services_status("k-b", &st) == 0, "k-b found");
    KUNIT_ASSERT_EQ(st.state, SERVICE_ACTIVE, "k-b active");
    return KUNIT_PASS;
}

/* 循环依赖：k-c1 <-> k-c2 必须被检出并记事件，不假装启动。 */
static kunit_result_t test_cycle_detected(void)
{
    KUNIT_ASSERT_EQ(reg_scratch("k-c1", "k-c2", 0, 0, ok_start), 0, "reg k-c1");
    KUNIT_ASSERT_EQ(reg_scratch("k-c2", "k-c1", 0, 0, ok_start), 0, "reg k-c2");

    KUNIT_ASSERT_EQ(services_validate_graph(), -1, "cycle rejected");
    KUNIT_ASSERT(event_has_msg(16, "cycle"), "cycle event emitted");

    service_t st;
    KUNIT_ASSERT(services_status("k-c1", &st) == 0, "k-c1 found");
    KUNIT_ASSERT_EQ(st.state, SERVICE_FAILED, "cycle svc not started");
    return KUNIT_PASS;
}

/* 缺依赖：启动报错、置 FAILED，事件同时报出依赖者与缺失依赖名。 */
static kunit_result_t test_missing_dep_reported(void)
{
    KUNIT_ASSERT_EQ(reg_scratch("k-m1", "k-nope", 0, 0, ok_start), 0, "reg k-m1");

    KUNIT_ASSERT_EQ(services_start("k-m1"), -1, "missing dep start fails");
    service_t st;
    KUNIT_ASSERT(services_status("k-m1", &st) == 0, "k-m1 found");
    KUNIT_ASSERT_EQ(st.health, HEALTH_FAILED, "missing dep marked failed");
    KUNIT_ASSERT(event_has_msg(16, "k-m1"), "event names dependent");
    KUNIT_ASSERT(event_has_msg(16, "k-nope"), "event names missing dep");
    return KUNIT_PASS;
}

/* 退避：失败计数、指数退避到期才重试、上限到了放弃且不再重试。 */
static int flaky_start_calls;
static int flaky_start(void) { flaky_start_calls++; return -1; }

static uint64_t fake_tick;
static uint64_t fake_clock(void) { return fake_tick; }

static kunit_result_t test_backoff_count_and_limit(void)
{
    services_set_clock_hook(fake_clock);
    fake_tick = 0;
    flaky_start_calls = 0;

    KUNIT_ASSERT_EQ(reg_scratch("k-flaky", 0, 1, 2, flaky_start), 0, "reg k-flaky");
    KUNIT_ASSERT_EQ(services_start("k-flaky"), -1, "first start fails");

    service_t st;
    KUNIT_ASSERT(services_status("k-flaky", &st) == 0, "k-flaky found");
    KUNIT_ASSERT_EQ(st.fail_count, 1, "fail count = 1");
    KUNIT_ASSERT_EQ(st.retry_attempt, 0, "no retry executed yet");
    KUNIT_ASSERT_NE(st.backoff_until, 0, "retry scheduled with backoff");
    KUNIT_ASSERT_EQ(st.state, SERVICE_FAILED, "state failed");

    /* 退避未到点：monitor 不重试。 */
    services_monitor_tick();
    KUNIT_ASSERT_EQ(flaky_start_calls, 1, "no retry before backoff elapses");

    /* 越过退避窗口：第一次自动重试，再次失败，排第二次重试。 */
    fake_tick = 100;
    services_monitor_tick();
    KUNIT_ASSERT_EQ(flaky_start_calls, 2, "retry after backoff elapses");
    KUNIT_ASSERT(services_status("k-flaky", &st) == 0, "status re-fetch");
    KUNIT_ASSERT_EQ(st.retry_attempt, 1, "one retry executed");
    KUNIT_ASSERT_EQ(st.fail_count, 2, "fail count = 2");

    /* 第三次尝试失败：达到上限，放弃。 */
    fake_tick = 200;
    services_monitor_tick();
    KUNIT_ASSERT_EQ(flaky_start_calls, 3, "final attempt after backoff");
    KUNIT_ASSERT(services_status("k-flaky", &st) == 0, "status re-fetch");
    KUNIT_ASSERT_EQ(st.retry_attempt, 2, "retry limit reached");
    KUNIT_ASSERT_EQ(st.fail_count, 3, "fail count = 3");
    KUNIT_ASSERT_EQ(st.state, SERVICE_FAILED, "stays failed after give up");
    KUNIT_ASSERT(event_has_msg(15, "k-flaky"), "give-up event emitted");

    /* 放弃后再走 monitor tick：不得再次拉起。 */
    fake_tick = 300;
    services_monitor_tick();
    KUNIT_ASSERT_EQ(flaky_start_calls, 3, "no retry after give up");

    services_set_clock_hook(0);
    return KUNIT_PASS;
}

static const kunit_test_t system_tests[] = {
    { "service_table_consistency", test_service_table_consistency, "system-manager" },
    { "stub_not_healthy",         test_stub_not_healthy,         "system-manager" },
    { "topo_order",                test_topo_order,              "system-manager" },
    { "cycle_detected",            test_cycle_detected,           "system-manager" },
    { "missing_dep_reported",     test_missing_dep_reported,      "system-manager" },
    { "backoff_count_and_limit",  test_backoff_count_and_limit,   "system-manager" },
};

const kunit_suite_t kunit_system_manager_suite = {
    "system-manager",
    system_tests,
    sizeof(system_tests) / sizeof(system_tests[0])
};

void kunit_system_manager_register(void)
{
    extern void kunit_register_suite(const kunit_suite_t *);
    kunit_register_suite(&kunit_system_manager_suite);
}
