/* ============================================================================
 * 赫普OS - V1.02 HAL侧新模块测试
 * 覆盖：IPC权限检查(ipc_perm)、ACPI、设备热插拔(hotplug)
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"
#include "ipc_perm.h"
#include "acpi.h"
#include "hotplug.h"

/* === IPC权限检查测试 === */

static kunit_result_t test_ipc_perm_register(void)
{
    /* 确保ipc_perm已初始化（如果未初始化，先初始化） */
    extern void ipc_perm_init(void);
    static int initialized = 0;
    if (!initialized) { ipc_perm_init(); initialized = 1; }

    /* 注册/注销/重复注册 */
    int r1 = ipc_perm_register(5, 100, IPC_OBJ_QUEUE, 0x01, 0x01);
    KUNIT_ASSERT(r1 >= 0, " registered queue perm ");
    int r2 = ipc_perm_register(5, 101, IPC_OBJ_PIPE, 0x01, 0x01);
    KUNIT_ASSERT(r2 >= 0, " registered 管道 perm ");
    /* 重复注册同一对象返回-3 */
    KUNIT_ASSERT_EQ(ipc_perm_register(5, 100, IPC_OBJ_QUEUE, 0x01, 0x01), -3,
                    " 重复 registered 返回 -3");

    /* 所有者查询 */
    uint32_t owner = 0;
    KUNIT_ASSERT_EQ(ipc_perm_get_owner(100, IPC_OBJ_QUEUE, &owner), 0,
                    " query all 者 ok ");
    KUNIT_ASSERT_EQ(owner, 5, " all 者 PID=5");

    /* 注销 */
    KUNIT_ASSERT_EQ(ipc_perm_unregister(100, IPC_OBJ_QUEUE), 0, " unregister ok ");
    KUNIT_ASSERT_EQ(ipc_perm_unregister(100, IPC_OBJ_QUEUE), -1, " 重复 unregister failed ");
    ipc_perm_unregister(101, IPC_OBJ_PIPE);
    return KUNIT_PASS;
}

static kunit_result_t test_ipc_perm_check(void)
{
    /* 所有者访问 */
    ipc_perm_register(5, 200, IPC_OBJ_QUEUE, 0x01, 0x01);
    KUNIT_ASSERT_EQ(ipc_perm_check(5, 200, IPC_OBJ_QUEUE, IPC_OP_SEND, 1), 1,
                    " all 者允许写 ");

    /* 内核级进程放行 */
    KUNIT_ASSERT_EQ(ipc_perm_check(1, 200, IPC_OBJ_QUEUE, IPC_OP_RECV, 0), 1,
                    " kernel level process 放行 ");

    /* 受信对象放行 */
    KUNIT_ASSERT_EQ(ipc_perm_set_trusted(200, IPC_OBJ_QUEUE, 1), 0, " 设受信 ");
    KUNIT_ASSERT_EQ(ipc_perm_check(20, 200, IPC_OBJ_QUEUE, IPC_OP_SEND, 1), 1,
                    " 受信 pair 象任何 process 可访问 ");
    ipc_perm_unregister(200, IPC_OBJ_QUEUE);

    /* 未注册对象：创建放行 */
    KUNIT_ASSERT_EQ(ipc_perm_check(10, 999, IPC_OBJ_QUEUE, IPC_OP_CREATE, 0), 1,
                    " create ops 放行 ");
    return KUNIT_PASS;
}

static kunit_result_t test_ipc_perm_stats(void)
{
    uint32_t entries, denied;
    ipc_perm_stats(&entries, &denied);
    KUNIT_ASSERT(entries <= 32, " perm entry count not 超 限 ");
    KUNIT_ASSERT(denied <= 0xFFFFFF, " denied count at 合理范围 ");
    return KUNIT_PASS;
}

/* === ACPI测试 === */

static kunit_result_t test_acpi_struct_sizes(void)
{
    /* ACPI规范定义的结构大小 */
    KUNIT_ASSERT_EQ(sizeof(acpi_sdt_header_t), 36, "SDT table header =36 bytes ");
    KUNIT_ASSERT_EQ(sizeof(acpi_rsdp_t), 36, "RSDP ACPI2.0=36 bytes ");
    return KUNIT_PASS;
}

static kunit_result_t test_acpi_parse_null(void)
{
    /* 空RSDP必须安全返回（不崩溃） */
    KUNIT_ASSERT_EQ(acpi_parse_rsdp(0), -1, " empty RSDP 返回 -1");
    return KUNIT_PASS;
}

static kunit_result_t test_acpi_get_table_initial(void)
{
    /* 未探测硬件时查询返回-1或不崩溃 */
    acpi_sdt_t t;
    int r = acpi_get_table(ACPI_TABLE_FADT, &t);
    KUNIT_ASSERT(r == -1 || r == 0, " query FADT not 崩溃 ");
    return KUNIT_PASS;
}

/* === 设备热插拔测试 === */

static uint32_t hotplug_cb_count = 0;
static uint32_t hotplug_cb_last_type = 0;

static void hotplug_test_cb(uint8_t bus, const char *name, uint32_t dev_id,
                            uint8_t event_type, void *data)
{
    hotplug_cb_count++;
    hotplug_cb_last_type = event_type;
    (void)bus; (void)name; (void)dev_id; (void)data;
}

static kunit_result_t test_hotplug_callback(void)
{
    hotplug_cb_count = 0;
    int h = hotplug_register_callback(hotplug_test_cb, NULL,
                                      (1u << HOTPLUG_BUS_USB));
    KUNIT_ASSERT(h >= 0, " registered 回调 ok ");

    /* 通知USB总线插入，回调应触发 */
    KUNIT_ASSERT_EQ(hotplug_notify_insert(HOTPLUG_BUS_USB, "usb_test", 1), 0,
                    "USB insert notify ok ");
    KUNIT_ASSERT_EQ(hotplug_cb_count, 1, " 回调 trigger 1 ");
    KUNIT_ASSERT_EQ(hotplug_cb_last_type, HOTPLUG_EVT_INSERT, " insert events type ");

    /* 通知PCI总线（未监听），回调不应触发 */
    hotplug_cb_count = 0;
    KUNIT_ASSERT_EQ(hotplug_notify_remove(HOTPLUG_BUS_PCI, "pci_test", 2), 0,
                    "PCI remove notify ok ");
    KUNIT_ASSERT_EQ(hotplug_cb_count, 0, " not listen total 线 not trigger 回调 ");

    hotplug_unregister_callback(h);
    return KUNIT_PASS;
}

static kunit_result_t test_hotplug_stats(void)
{
    uint32_t inserts, removes, pending;
    hotplug_stats(&inserts, &removes, &pending);
    KUNIT_ASSERT(inserts <= 0xFFFFFF, " insert count at 合理范围 ");
    KUNIT_ASSERT(removes <= 0xFFFFFF, " remove count at 合理范围 ");
    KUNIT_ASSERT(pending <= 32, " events queue not 超过 限 ");
    return KUNIT_PASS;
}

/* === 测试套件 === */
static const kunit_test_t v102_hal_tests[] = {
    {" perm registered / unregister ", test_ipc_perm_register, "ipc_perm"},
    {" perm check ", test_ipc_perm_check, "ipc_perm"},
    {" perm stats ", test_ipc_perm_stats, "ipc_perm"},
    {"ACPI struct size ", test_acpi_struct_sizes, "acpi"},
    {"ACPI empty ptr protected ", test_acpi_parse_null, "acpi"},
    {"ACPI table query ", test_acpi_get_table_initial, "acpi"},
    {" 热插拔回调 ", test_hotplug_callback, "hotplug"},
    {" 热插拔 stats ", test_hotplug_stats, "hotplug"},
};
static const kunit_suite_t kunit_v102_hal_suite = {
    "V1.02 HAL side module ", v102_hal_tests, 8
};

void kunit_v102_hal_register(void)
{
    kunit_register_suite(&kunit_v102_hal_suite);
    vga_puts("[KUNIT] V1.02 HAL side module test suite registered(8 cases)\n");
}
