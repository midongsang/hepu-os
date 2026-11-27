/* 多电池槽位选择：BAT0 明确 absent、BAT1 有可信 _BST。
 * 旧实现只取命名空间第一个 PNP0C0A，会把 BAT1 的真读数遮成 ABSENT。
 * 这里走真实 acpi_battery_init() -> acpi_battery_poll()，不是纯函数模拟。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aml64.h"
#include "acpi_battery.h"
#include "ec64.h"
#include "battery_ssdt_blobs.h"

static int fails;
static int checks;

static void check(int cond, const char *what)
{
    checks++;
    if (!cond) {
        printf("  FAIL: %s\n", what);
        fails++;
    }
}

void vga_puts(const char *s) { (void)s; }
void vga_put_hex(uint64_t v) { (void)v; }
void *kmalloc(uint32_t size) { return calloc(1, size ? size : 1); }
uint64_t vmm64_phys_to_ptr(uint64_t phys) { return phys; }

/* EC 只读路径在本用例不触发；提供链接桩，不假装有 EC。 */
uint8_t ec_host_inb(uint16_t port) { (void)port; return 0xFF; }
void ec_host_outb(uint16_t port, uint8_t val) { (void)port; (void)val; }

static const uint8_t *g_table;
static uint32_t g_len;
int fi_table_count(void) { return 1; }
int fi_table_at(int i, const uint8_t **data, uint32_t *len)
{
    if (i != 0) return -1;
    if (data) *data = g_table;
    if (len) *len = g_len;
    return 0;
}

int main(void)
{
    acpi_batt_sample_t s;
    int cur = 0, n = 0;
    char dev[96];

    g_table = g_battery_dual_ssdt;
    g_len = (uint32_t)sizeof(g_battery_dual_ssdt);

    check(aml_ns_build(&g_table, &g_len, 1) == 0, "双电池命名空间建表");
    while (n < 4 &&
           aml_find_device_next(0x41D00C0Au, "PNP0C0A", dev, sizeof(dev), &cur) == 0) {
        n++;
    }
    check(n == 2, "枚举到两个 PNP0C0A 槽位");

    check(acpi_battery_init() == 0, "init 成功");
    check(strcmp(acpi_battery_path(), "DUA1") == 0, "跳过 absent BAT0，选中 BAT1");

    memset(&s, 0, sizeof(s));
    check(acpi_battery_poll(&s) == 0, "poll 返回可信读数");
    check(s.present == 1, "选中电池 present");
    check(s.percent == 57, "读数来自 DUA1 的 57%");
    check(s.charging == 1, "DUA1 充电中");
    check(s.remaining == 5700 && s.full == 10000, "容量字段来自 BAT1");

    printf("多电池槽位: %d checks, %d fails\n", checks, fails);
    return fails ? 1 : 0;
}
