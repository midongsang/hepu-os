/* 真实固件缺省字段：_BST 只有 3 个元素（无电压），_BIX 全 0、_BIF 可信。
 * 期望：不因为缺电压或坏 _BIX 把电池判成不可信，仍读出 57%。
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

    g_table = g_battery_compat_ssdt;
    g_len = (uint32_t)sizeof(g_battery_compat_ssdt);

    check(aml_ns_build(&g_table, &g_len, 1) == 0, "兼容夹具建表");
    check(acpi_battery_init() == 0, "init 成功");
    check(strcmp(acpi_battery_path(), "CMP0") == 0, "选中 CMP0");

    memset(&s, 0, sizeof(s));
    check(acpi_battery_poll(&s) == 0, "poll 仍返回可信读数");
    check(s.percent == 57, "3 元素 _BST 仍读出 57%");
    check(s.full == 10000 && s.design == 10000, "坏 _BIX 回退到 _BIF 容量");
    check(s.used_bix == 0, "实际使用 _BIF");
    check(s.charging == 1, "充电状态正常");
    check(s.health == 100, "健康度正常");
    check(s.minutes == -1 && s.minutes_full == -1, "无速率不报时间");

    printf("battery_compat: %d checks, %d fails\n", checks, fails);
    return fails ? 1 : 0;
}
