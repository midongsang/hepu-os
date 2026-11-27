/* 电池健康度/剩余时间：放电 1200mW、剩余 5700mWh -> 285 分钟（4h45m）。
 * 只验证“固件原始值换算”，不把 rate=0 或 design=0 硬算成 100%/0 分钟。
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
    g_table = g_battery_discharge_ssdt;
    g_len = (uint32_t)sizeof(g_battery_discharge_ssdt);

    check(aml_ns_build(&g_table, &g_len, 1) == 0, "放电夹具建表");
    check(acpi_battery_init() == 0, "init 成功");
    check(strcmp(acpi_battery_path(), "DIS0") == 0, "选中 DIS0");

    memset(&s, 0, sizeof(s));
    check(acpi_battery_poll(&s) == 0, "poll 可信");
    check(s.present == 1 && s.percent == 57, "57% 存在");
    check(s.charging == 0, "放电中");
    check(s.rate == 1200, "速率 1200mW");
    check(s.health == 100, "健康度 100%（full/design）");
    check(s.minutes == 285, "剩余 285 分钟（4h45m）");
    check(s.minutes_full == -1, "放电时不报充满时间");
    check(s.ac_present == 1 && s.ac_known == 1 && s.ac_online == 0, "AC 离线");
    printf("battery_energy: %d checks, %d fails\n", checks, fails);
    return fails ? 1 : 0;
}
