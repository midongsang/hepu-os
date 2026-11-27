/* 多电池运行中回退：第一个 present 槽位 DUF0 缺 _BST，
 * 第二个 DUF1 可信。init 仍选 DUF0，poll 必须跳过坏槽并切到 DUF1，
 * 不能把整机电池状态锁死在 "--"。
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

    g_table = g_battery_dual_fallback_ssdt;
    g_len = (uint32_t)sizeof(g_battery_dual_fallback_ssdt);

    check(aml_ns_build(&g_table, &g_len, 1) == 0, "回退夹具建表");
    while (n < 4 &&
           aml_find_device_next(0x41D00C0Au, "PNP0C0A", dev, sizeof(dev), &cur) == 0) {
        n++;
    }
    check(n == 2, "枚举到 DUF0/DUF1 两个槽位");
    check(acpi_battery_init() == 0, "init 成功");
    check(strcmp(acpi_battery_path(), "DUF0") == 0, "init 先选第一个 present 槽位");

    memset(&s, 0, sizeof(s));
    check(acpi_battery_poll(&s) == 0, "poll 跳过坏槽并返回可信读数");
    check(strcmp(acpi_battery_path(), "DUF1") == 0, "运行中切到可用槽位 DUF1");
    check(s.present == 1, "回退后 present");
    check(s.percent == 57, "读数来自 DUF1 的 57%");
    check(s.charging == 1, "DUF1 充电中");
    check(s.remaining == 5700 && s.full == 10000, "容量字段来自 DUF1");
    check(s.health == 100, "健康度由 DUF1 的 full/design 算");

    printf("battery_fallback: %d checks, %d fails\n", checks, fails);
    return fails ? 1 : 0;
}
