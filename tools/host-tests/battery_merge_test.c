/* 双电池合并：两块都 present、都可信（30% + 70%）。
 * 验证 poll 不是"只取第一块"：容量/速率按同一 unit 相加，百分比由总容量算。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aml64.h"
#include "acpi_battery.h"
#include "ec64.h"
#include "battery_ssdt_blobs.h"

static int fails, checks;
static void check(int cond, const char *what)
{
    checks++;
    if (!cond) { printf("  FAIL: %s\n", what); fails++; }
}

void vga_puts(const char *s) { (void)s; }
void vga_put_hex(uint64_t v) { (void)v; }
void *kmalloc(uint32_t size) { return calloc(1, size ? size : 1); }
uint64_t vmm64_phys_to_ptr(uint64_t phys) { return phys; }
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
    char dev[96];
    int cur = 0, n = 0;

    g_table = g_battery_dual_both_ssdt;
    g_len = (uint32_t)sizeof(g_battery_dual_both_ssdt);

    check(aml_ns_build(&g_table, &g_len, 1) == 0, "双满槽命名空间建表");
    while (n < 4 &&
           aml_find_device_next(0x41D00C0Au, "PNP0C0A", dev, sizeof(dev), &cur) == 0) {
        n++;
    }
    check(n == 2, "枚举到 DUB0/DUB1 两个槽位");
    check(acpi_battery_init() == 0, "init 成功");
    check(strcmp(acpi_battery_path(), "DUB0") == 0, "首选第一个 present 槽位 DUB0");

    memset(&s, 0, sizeof(s));
    check(acpi_battery_poll(&s) == 0, "poll 返回可信读数");
    check(s.present == 1, "合并后 present");
    check(s.remaining == 10000 && s.full == 20000, "容量合并 3000+7000 / 10000+10000");
    check(s.design == 20000, "设计容量合并 20000");
    check(s.percent == 50, "百分比按总容量算 = 50%");
    check(s.charging == 1, "任一块充电即充电中");
    check(s.health == 100, "合并后健康度 100%");
    check(s.minutes == -1 && s.minutes_full == -1, "速率 0 仍不报时间");
    check(s.used_bix == 0, "两块都走 _BIF");

    printf("battery_merge: %d checks, %d fails\n", checks, fails);
    return fails ? 1 : 0;
}
