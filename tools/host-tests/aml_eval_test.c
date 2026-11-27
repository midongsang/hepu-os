/* 极简 AML 解释器 + 电池读数的主机端单测（M2）。
 *
 * 覆盖:
 *   - 命名空间建表与 _HID 查找
 *   - 方法调用、Local、Store、Index/DerefOf（包元素写入）
 *   - If/Else、LEqual、Add
 *   - Package/Buffer/String 常量
 *   - _BIF/_BIX/_BST 结构读取
 *   - EC 字段明确不支持（fail-closed）
 *   - 百分比可信门槛（满充为 0 / 超容 / 0% 无 critical）
 *
 * 构建: gcc -DAML_HOST_TEST -I kernel/arch/x86_64 tools/host-tests/aml_eval_test.c \
 *          kernel/arch/x86_64/aml64.c kernel/arch/x86_64/acpi_battery.c
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

/* 内核侧依赖的桩 */
void vga_puts(const char *s) { (void)s; }
void vga_put_hex(uint64_t v) { (void)v; }
void *kmalloc(uint32_t size) { return calloc(1, size ? size : 1); }
uint64_t vmm64_phys_to_ptr(uint64_t phys) { return phys; }

/* 主机侧模拟 EC：0x66 状态、0x62 数据；内存 256 字节。 */
static uint8_t ec_mem[256];
static int ec_host_present = 1;
static uint8_t ec_host_status;
static uint8_t ec_host_data;

uint8_t ec_host_inb(uint16_t port)
{
    if (!ec_host_present) return 0xFF;   /* 无 EC：IBF 永远为 1 -> 超时 */
    if (port == 0x66) return ec_host_status;
    if (port == 0x62) { ec_host_status &= (uint8_t)~0x01u; return ec_host_data; }
    return 0xFF;
}

void ec_host_outb(uint16_t port, uint8_t val)
{
    if (!ec_host_present) return;
    if (port == 0x66) {
        if (val == 0x80) ec_host_status = 0;   /* RD_EC */
    } else if (port == 0x62) {
        ec_host_data = ec_mem[val];
        ec_host_status |= 0x01;                /* OBF */
    }
}

static const unsigned char *g_tables[8];
static uint32_t g_lens[8];
static int g_ntab;

int fi_table_count(void) { return g_ntab; }

int fi_table_at(int i, const uint8_t **data, uint32_t *len)
{
    if (i < 0 || i >= g_ntab) return -1;
    if (data) *data = g_tables[i];
    if (len) *len = g_lens[i];
    return 0;
}

static int eval_int(const char *path, int64_t *out)
{
    aml_value_t *v = 0;
    int rc = aml_eval_path(path, &v);
    if (rc != 0) return rc;
    return aml_value_int(v, out);
}

static aml_value_t *eval_pkg(const char *path, int *rc_out)
{
    aml_value_t *v = 0;
    int rc = aml_eval_path(path, &v);
    if (rc_out) *rc_out = rc;
    return rc == 0 ? v : 0;
}

int main(void)
{
    int64_t x = 0;
    char dev[96];
    aml_value_t *pkg;
    int rc;

    g_tables[0] = g_battery_valid_ssdt;
    g_lens[0] = (uint32_t)sizeof(g_battery_valid_ssdt);
    g_tables[1] = g_battery_bix_ssdt;
    g_lens[1] = (uint32_t)sizeof(g_battery_bix_ssdt);
    g_tables[2] = g_battery_ec_ssdt;
    g_lens[2] = (uint32_t)sizeof(g_battery_ec_ssdt);
    g_tables[3] = g_battery_absent_ssdt;
    g_lens[3] = (uint32_t)sizeof(g_battery_absent_ssdt);
    g_tables[4] = g_battery_ec_read_ssdt;
    g_lens[4] = (uint32_t)sizeof(g_battery_ec_read_ssdt);
    g_tables[5] = g_battery_mutex_ssdt;
    g_lens[5] = (uint32_t)sizeof(g_battery_mutex_ssdt);
    g_tables[6] = g_battery_dual_ssdt;
    g_lens[6] = (uint32_t)sizeof(g_battery_dual_ssdt);
    g_ntab = 7;

    /* 1) 命名空间 */
    check(aml_ns_build(g_tables, g_lens, g_ntab) == 0, "命名空间建表成功");
    check(aml_ns_nodes() > 0, "命名空间节点非空");
    check(aml_find_device(0x41D00C0Au, "PNP0C0A", dev, sizeof(dev)) == 0, "按 _HID 找到电池设备");
    check(strcmp(dev, "BAT0") == 0, "设备路径为 BAT0");
    {
        int cur = 0, n = 0;
        while (n < 8 &&
               aml_find_device_next(0x41D00C0Au, "PNP0C0A", dev, sizeof(dev), &cur) == 0) {
            n++;
        }
        check(n >= 3, "多实例枚举至少看到 BAT0/MTX0/BAT1");
    }

    /* 2) _STA 与基础方法 */
    check(eval_int("BAT0._STA", &x) == 0 && x == 0x0F, "_STA 返回 0x0F");
    check(eval_int("BIX0._STA", &x) == 0 && x == 0x0F, "BIX0._STA 返回 0x0F");

    /* 3) If/Else 与算术 */
    check(eval_int("BAT0._TIF", &x) == 0 && x == 1, "If 真分支返回 1");
    check(eval_int("BAT0._TAR", &x) == 0 && x == 0x30, "Add(0x10,0x20)=0x30");

    /* 4) _BIF 结构 */
    pkg = eval_pkg("BAT0._BIF", &rc);
    check(pkg && aml_value_count(pkg) == 5, "_BIF 包有 5 个元素");
    if (pkg) {
        aml_value_int(aml_value_at(pkg, 1), &x);
        check(x == 10000, "_BIF 设计容量 10000");
        aml_value_int(aml_value_at(pkg, 2), &x);
        check(x == 10000, "_BIF 满充容量 10000");
    }

    /* 5) _BIX 结构 */
    pkg = eval_pkg("BIX0._BIX", &rc);
    check(pkg && aml_value_count(pkg) == 5, "_BIX 包有 5 个元素");
    if (pkg) {
        aml_value_int(aml_value_at(pkg, 2), &x);
        check(x == 10000, "_BIX 设计容量 10000");
        aml_value_int(aml_value_at(pkg, 3), &x);
        check(x == 10000, "_BIX 满充容量 10000");
    }

    /* 6) _BST：Store 到包元素后返回 */
    pkg = eval_pkg("BAT0._BST", &rc);
    check(pkg && aml_value_count(pkg) == 4, "_BST 包有 4 个元素");
    if (pkg) {
        aml_value_int(aml_value_at(pkg, 0), &x);
        check(x == 2, "_BST 状态位=充电中");
        aml_value_int(aml_value_at(pkg, 2), &x);
        check(x == 5700, "_BST 剩余容量 5700");
        aml_value_int(aml_value_at(pkg, 3), &x);
        check(x == 12000, "_BST 电压 12000mV");
    }

    /* 6b) Mutex/Acquire/Release 与 _OSI：真实 DSDT 常见前缀，不再让 _BST 整体失败 */
    pkg = eval_pkg("MTX0._BST", &rc);
    check(pkg && aml_value_count(pkg) == 4, "Mutex 夹具 _BST 执行成功");
    if (pkg) {
        aml_value_int(aml_value_at(pkg, 2), &x);
        check(x == 5700, "Mutex 夹具 _BST 剩余容量 5700");
    }

    /* 7) 失败关闭：EC 字段、缺对象、_STA=0 */
    check(eval_int("ECB0._BST", &x) == AML_E_UNSUP, "EC 字段读取明确失败(UNSUP)");
    check(eval_int("BAT0._NOPE", &x) != 0, "不存在的对象返回错误");
    check(eval_int("ABS0._STA", &x) == 0 && x == 0, "ABS0._STA 返回 0");

    /* 8) 端到端：poll 走 BAT0 -> 57% 充电中 */
    {
        acpi_batt_sample_t s;
        memset(&s, 0, sizeof(s));
        check(acpi_battery_poll(&s) == 0, "poll 返回可信读数");
        check(s.present == 1, "poll 认为电池存在");
        check(s.percent == 57, "poll 百分比 57");
        check(s.charging == 1, "poll 充电中");
        check(s.full == 10000 && s.remaining == 5700, "poll 容量字段正确");
        check(s.used_bix == 0, "poll 走 _BIF");
        check(s.health == 100, "健康度 100%");
        check(s.minutes == -1 && s.minutes_full == -1, "速率 0 不报时间");
        check(s.ac_present == 1 && s.ac_known == 1 && s.ac_online == 1,
              "poll ACPI0003._PSR=1");
        {
            acpi_ac_sample_t ac;
            check(acpi_ac_poll(&ac) == 0 && ac.present && ac.known && ac.online,
                  "独立 AC 查询 _PSR=1");
        }
    }

    /* 9) EC 字段读取：主机模拟 EC 状态机（16 位跨字节拼装） */
    {
        aml_value_t *p;
        ec_mem[0] = 0x44;   /* 5700 = 0x1644，低字节在前 */
        ec_mem[1] = 0x16;
        ec_host_present = 1;
        ec64_set_present(1);
        check(aml_find_device(0x41D00C09u, "PNP0C09", dev, sizeof(dev)) == 0, "找到 PNP0C09 EC 设备");
        check(strcmp(dev, "EC00") == 0, "EC 设备路径 EC00");

        /* _REG(3,1) 带参方法：把 Arg1 写入 ECRG；_BST 会先检查该标志 */
        {
            int64_t reg_args[2] = { 3, 1 };
            aml_value_t *rv = 0;
            check(aml_eval_path_ints("EC00._REG", reg_args, 2, &rv) == 0,
                  "_REG(3,1) 调用成功");
            check(eval_int("ECRG", &x) == 0 && x == 1, "_REG 写入 ECRG=1");
        }

        check(eval_int("ECR0.B0ST", &x) == 0 && x == 5700, "EC 16 位字段拼装=5700");
        p = eval_pkg("ECR0._BST", &rc);
        check(p && aml_value_count(p) == 4, "EC _BST 包有 4 个元素");
        if (p) {
            aml_value_int(aml_value_at(p, 0), &x);
            check(x == 2, "EC _BST 状态位=充电中");
            aml_value_int(aml_value_at(p, 2), &x);
            check(x == 5700, "EC _BST 剩余 5700");
            aml_value_int(aml_value_at(p, 3), &x);
            check(x == 12000, "EC _BST 电压 12000mV");
        }
        ec_host_present = 0;
        check(eval_int("ECR0._BST", &x) == AML_E_UNSUP, "EC 超时 fail-closed(UNSUP)");
        ec64_set_present(0);
        check(eval_int("ECR0._BST", &x) == AML_E_UNSUP, "EC 未启用 fail-closed(UNSUP)");
    }

    /* 10) 百分比可信门槛 */
    {
        int pct = -1;
        check(acpi_battery_make_percent(5700, 10000, 1, &pct) == 0 && pct == 57, "57% 可信");
        check(acpi_battery_make_percent(0, 0, 0, &pct) == -1, "满充为 0 拒绝");
        check(acpi_battery_make_percent(0, 10000, 0, &pct) == -4, "0% 无 critical 拒绝");
        check(acpi_battery_make_percent(0, 10000, 4, &pct) == 0 && pct == 0, "0% 带 critical 可信");
        check(acpi_battery_make_percent(10200, 10000, 1, &pct) == 0 && pct == 100, "5% 内超容按满仓");
        check(acpi_battery_make_percent(12000, 10000, 1, &pct) == -3, "明显超容拒绝");
        check(acpi_battery_make_percent(1, 100000, 0, &pct) == 0 && pct == 0, "1/100000 四舍五入到 0");
        check(acpi_battery_make_percent(99999, 100000, 0, &pct) == 0 && pct == 100, "99.999% 四舍五入到 100");
    }

    if (fails) {
        printf("FAIL %d 项\n", fails);
        return 1;
    }
    printf("PASS aml_eval (%d checks)\n", checks);
    return 0;
}
