/* ============================================================================
 * KUnit: 统一服务表与系统管理只读一致性
 * 不清空/不重启真实服务，只验证 boot 注册表可被统一 API 读取。
 * ========================================================================== */
#include <stdint.h>
#include "kunit.h"
#include "services.h"
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
    }

    service_t st;
    KUNIT_ASSERT(services_status("network", &st) == 0, "network status found");
    KUNIT_ASSERT(strcmp(st.def.name, "network") == 0, "network name exact");
    KUNIT_ASSERT(st.state == SERVICE_ACTIVE, "network active");
    return KUNIT_PASS;
}

static const kunit_test_t system_tests[] = {
    { "service_table_consistency", test_service_table_consistency, "system-manager" },
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
