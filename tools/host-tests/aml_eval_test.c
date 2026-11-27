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
#include "battery_ssdt_blobs.h"

static int fails;

static void check(int cond, const char *what)
{
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

static const unsigned char *g_tables[4];
static uint32_t g_lens[4];
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
    g_ntab = 4;

    /* 1) 命名空间 */
    check(aml_ns_build(g_tables, g_lens, g_ntab) == 0, "命名空间建表成功");
    check(aml_ns_nodes() > 0, "命名空间节点非空");
    check(aml_find_device(0x41D00C0Au, "PNP0C0A", dev, sizeof(dev)) == 0, "按 _HID 找到电池设备");
    check(strcmp(dev, "BAT0") == 0, "设备路径为 BAT0");

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
    }

    /* 9) 百分比可信门槛 */
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
    printf("PASS aml_eval (30 checks)\n");
    return 0;
}
