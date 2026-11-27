/* Create*Field 缓冲字段 + 建库容错主机端测试（M2-cf）。
 *
 * 覆盖：
 *   - 建库容错：外层未识别 DataRegion 按 PkgLength 跳过并记降级；If/Else 两分支
 *     声明都登记；诊断接口透出 degraded / unknown_skipped / stop_off；跳过未知对象
 *     不会让缺字段“看起来成功”。
 *   - CreateDWordField 端到端：_BST 从 Buffer 拆包剩余容量，acpi_battery 进 VALID。
 *   - CreateByteField + Store 回写 Buffer 往返读回。
 *   - 负路径：字段越界 / 源不是 Buffer / 位宽 65 -> 明确错误码，不返回猜测值。
 *
 * 构建: gcc -DAML_HOST_TEST tools/host-tests/create_field_test.c \
 *          kernel/arch/x86_64/aml64.c kernel/arch/x86_64/acpi_battery.c \
 *          kernel/arch/x86_64/ec64.c
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

/* 内核侧依赖桩 */
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

/* 本测试不碰 EC 事务；桩保持 fail-closed。 */
uint8_t ec_host_inb(uint16_t port) { (void)port; return 0xFF; }
void ec_host_outb(uint16_t port, uint8_t val) { (void)port; (void)val; }

/* 端到端夹具表（供 acpi_battery_init）。 */
static const unsigned char *fi_blob;
static uint32_t fi_len;
int fi_table_count(void) { return fi_blob ? 1 : 0; }
int fi_table_at(int i, const uint8_t **data, uint32_t *len)
{
    if (i != 0 || !data || !len) return -1;
    *data = fi_blob;
    *len = fi_len;
    return 0;
}

static int load_blob(const unsigned char *blob, uint32_t len)
{
    const uint8_t *tabs[1];
    uint32_t lens[1];
    tabs[0] = blob;
    lens[0] = len;
    aml_ns_reset();
    return aml_ns_build(tabs, lens, 1);
}

static int eval_int(const char *path, int64_t *out)
{
    aml_value_t *v = 0;
    int rc = aml_eval_path(path, &v);
    if (rc != 0) return rc;
    return aml_value_int(v, out);
}

int main(void)
{
    int64_t x = 0;
    int degraded = -1, skipped = -1, stop_off = -99;
    aml_value_t *v = 0;
    acpi_batt_sample_t s;

    /* ---------- A. 建库容错 + 诊断透出 ---------- */
    check(load_blob(g_battery_tolerant_ssdt,
                    (uint32_t)sizeof(g_battery_tolerant_ssdt)) == 0,
          "容错夹具建表成功");
    aml_ns_diag(&degraded, &skipped, &stop_off);
    check(degraded > 0, "未识别对象计入降级计数");
    check(skipped > 0, "未识别外层对象按长度跳过计数 >0");
    check(stop_off == -1, "可定位长度，未硬中止 stop_off=-1");
    check(eval_int("BAT0.INTH", &x) == 0 && x == 9, "If then 分支声明已登记 INTH=9");
    check(eval_int("BAT0.INEL", &x) == 0 && x == 8, "Else 分支声明已登记 INEL=8");

    /* 干净基线：无降级、未中止。 */
    check(load_blob(g_battery_create_dword_ssdt,
                    (uint32_t)sizeof(g_battery_create_dword_ssdt)) == 0,
          "CreateDWord 夹具建表成功");
    aml_ns_diag(&degraded, &skipped, &stop_off);
    check(degraded == 0 && skipped == 0 && stop_off == -1,
          "干净表 degraded=0 skipped=0 stop_off=-1");

    /* ---------- B. CreateByteField + Store 回写往返 ---------- */
    check(eval_int("BAT0._BXX", &x) == 0 && x == 0x5A,
          "CreateByteField Store(0x5A) 后读回=0x5A");

    /* ---------- C. CreateDWordField 拆包 _BST ---------- */
    check(aml_eval_path("BAT0._BST", &v) == 0, "_BST 求值成功");
    check(aml_value_count(v) == 4, "_BST 为 4 元素 Package");
    {
        int64_t state = 0, rate = 0, rem = 0, volt = 0;
        const aml_value_t *e0 = aml_value_at(v, 0);
        const aml_value_t *e1 = aml_value_at(v, 1);
        const aml_value_t *e2 = aml_value_at(v, 2);
        const aml_value_t *e3 = aml_value_at(v, 3);
        check(e0 && aml_value_int(e0, &state) == 0 && state == 2, "_BST[0]=state 2");
        check(e1 && aml_value_int(e1, &rate) == 0 && rate == 0, "_BST[1]=rate 0");
        check(e2 && aml_value_int(e2, &rem) == 0 && rem == 7500,
              "_BST[2]=剩余容量 7500（DWord 拆包）");
        check(e3 && aml_value_int(e3, &volt) == 0 && volt == 12000,
              "_BST[3]=电压 12000");
    }

    /* ---------- D. 负路径：明确错误码，无数值 ---------- */
    check(load_blob(g_battery_create_bad_ssdt,
                    (uint32_t)sizeof(g_battery_create_bad_ssdt)) == 0,
          "负路径夹具建表成功");
    x = -999;
    check(eval_int("BAD0._B01", &x) == AML_E_ARG && x == -999,
          "越界 DWordField -> ARG，不填猜测值");
    x = -999;
    check(eval_int("BAD0._B02", &x) == AML_E_ARG && x == -999,
          "源不是 Buffer -> ARG，不填猜测值");
    x = -999;
    check(eval_int("BAD0._B03", &x) == AML_E_UNSUP && x == -999,
          "位宽 65 -> UNSUP，不填猜测值");

    /* ---------- E. 端到端：拆包电池进 VALID ---------- */
    aml_ns_reset();
    fi_blob = g_battery_create_dword_ssdt;
    fi_len = (uint32_t)sizeof(g_battery_create_dword_ssdt);
    check(acpi_battery_init() == 0, "端到端 init 成功");
    memset(&s, 0, sizeof(s));
    check(acpi_battery_poll(&s) == 0, "拆包电池 poll 返回可信(0)");
    check(s.present == 1, "present=1");
    check(s.percent == 75, "7500/10000 -> 75%");
    check(s.remaining == 7500 && s.full == 10000, "容量 7500/10000");

    printf("RESULT: %s (%d checks, %d failed)\n",
           fails ? "FAIL" : "PASS", checks, fails);
    return fails ? 1 : 0;
}
