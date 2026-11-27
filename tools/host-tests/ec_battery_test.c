/* 主机端“虚拟 EC 电池”端到端测试（M2-full）。
 *
 * 与 aml_eval_test 的区别：这里不手工调用 _REG/_BST，而是只加载
 * battery-ec-read.ssdt，走真实 acpi_battery_init() → acpi_battery_poll() 路径，
 * 由本文件的 0x62/0x66 虚拟 EC 状态机提供 B0ST。用于模拟 QEMU 无法注入的
 * PNP0C09 + EC 字段型电池，覆盖 _REG、_BST、超时和遗留 OBF 四种情况。
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
    if (!cond) {
        printf("  FAIL: %s\n", what);
        fails++;
    }
}

/* 内核依赖桩 */
static char logbuf[8192];
static int loglen;
void vga_puts(const char *s)
{
    while (s && *s && loglen < (int)sizeof(logbuf) - 1) logbuf[loglen++] = *s++;
    logbuf[loglen] = 0;
}
void vga_put_hex(uint64_t v) { (void)v; }
void *kmalloc(uint32_t size) { return calloc(1, size ? size : 1); }
uint64_t vmm64_phys_to_ptr(uint64_t phys) { return phys; }

/* 虚拟 EC：内存 256B、0x66 状态、0x62 数据 */
static uint8_t ec_mem[256];
static int ec_present = 1;
static uint8_t ec_status;
static uint8_t ec_data;

uint8_t ec_host_inb(uint16_t port)
{
    if (!ec_present) return 0xFF;           /* 无 EC：IBF 恒 1，读超时 */
    if (port == 0x66) return ec_status;
    if (port == 0x62) { ec_status &= (uint8_t)~0x01u; return ec_data; }
    return 0xFF;
}

void ec_host_outb(uint16_t port, uint8_t val)
{
    if (!ec_present) return;
    if (port == 0x66) {
        if (val == 0x80) ec_status = 0;    /* RD_EC */
    } else if (port == 0x62) {
        ec_data = ec_mem[val];
        ec_status |= 1;                    /* OBF */
    }
}

/* 单表：只加载 EC 型电池夹具 */
int fi_table_count(void) { return 1; }
int fi_table_at(int i, const uint8_t **data, uint32_t *len)
{
    if (i != 0 || !data || !len) return -1;
    *data = g_battery_ec_read_ssdt;
    *len = (uint32_t)sizeof(g_battery_ec_read_ssdt);
    return 0;
}

static int has_log(const char *needle) { return strstr(logbuf, needle) != 0; }

int main(void)
{
    acpi_batt_sample_t s;

    /* B0ST 是 region offset 0 的 16 位字段，小端 0x1644 = 5700 */
    ec_mem[0] = 0x44;
    ec_mem[1] = 0x16;

    check(acpi_battery_init() == 0, "init 成功");
    check(has_log("[BAT] aml dev=ECR0"), "找到 PNP0C0A 设备 ECR0");
    check(has_log("[BAT] ec=0x1 dev=EC00"), "找到 PNP0C09 EC00");
    check(has_log("[BAT] ec reg=0"), "_REG(3,1) 执行成功");
    check(has_log("[BAT] ec glk=1"), "_GLK=1 被诊断并上报");
    check(has_log("[BAT] ac=none"), "无 ACPI0003 时明确 ac=none");

    memset(&s, 0, sizeof(s));
    check(acpi_battery_poll(&s) == 0, "EC 电池 poll 返回可信");
    check(s.present == 1, "present=1");
    check(s.percent == 57, "虚拟 EC 读出 57%");
    check(s.remaining == 5700 && s.full == 10000, "容量 5700/10000");
    check(s.charging == 1, "state=2 判充电中");
    check(s.ac_present == 0 && s.ac_known == 0, "无 AC 设备不伪造交流状态");

    /* 无 EC 硬件：必须超时 fail-closed，不能拿上次的 57% */
    ec_present = 0;
    memset(&s, 0, sizeof(s));
    check(acpi_battery_poll(&s) == 2, "EC 缺失返回 UNKNOWN(2)");
    check(s.percent == 0, "EC 缺失不产生假百分比");

    /* 遗留 OBF：模拟上一次事务残留，下一次读必须丢弃旧字节并重新读对 */
    ec_present = 1;
    ec_status = 1;
    ec_data = 0x99;
    memset(&s, 0, sizeof(s));
    check(acpi_battery_poll(&s) == 0 && s.percent == 57,
          "遗留 OBF 被丢弃后仍读 57%");

    printf("RESULT: %s (%d checks, %d failed)\n",
           fails ? "FAIL" : "PASS", checks, fails);
    return fails ? 1 : 0;
}
