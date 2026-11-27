/* IndexField / BankField 主机端测试（M2-index）。
 *
 * 覆盖：
 *   - SystemIO IndexField：8/16/32 位 granule 正路径、跨 granule 拼装、非对齐 bit 抽取
 *   - SystemMemory IndexField 正路径（内存回调模拟索引窗口）
 *   - BankField 多 bank 切换（同一 bank 寄存器，不同 BankValue）
 *   - 负路径：EC 上 IndexField/BankField -> UNSUP；index/data/bank/region 缺失；
 *     控制寄存器非字段；位宽 >64；region 越界；均不返回猜测值
 *   - 端到端：EC IndexField 电池 poll 停在 UNKNOWN，不产生假百分比
 *
 * 构建: gcc -DAML_HOST_TEST tools/host-tests/idx_bank_field_test.c \
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

/* ---------------------------------------------------------------- 模拟硬件 */

/* SystemIO 索引/数据对：
 *   byte  pair: index 0x100, data 0x101
 *   word  pair: index 0x104-0x105, data 0x106-0x107
 *   dword pair: index 0x108-0x10B, data 0x10C-0x10F
 *   bank select: 0x110
 */
static uint8_t  io_b[16];
static uint16_t io_w[16];
static uint32_t io_d[16];
static uint32_t cur_b, cur_w, cur_d;
static uint8_t  cur_bank;

uint8_t aml_host_inb(uint16_t port)
{
    switch (port) {
        case 0x101: return io_b[cur_b & 15];
        case 0x106: return (uint8_t)(io_w[cur_w & 15] & 0xFFu);
        case 0x107: return (uint8_t)(io_w[cur_w & 15] >> 8);
        case 0x10C: return (uint8_t)(io_d[cur_d & 15] & 0xFFu);
        case 0x10D: return (uint8_t)((io_d[cur_d & 15] >> 8) & 0xFFu);
        case 0x10E: return (uint8_t)((io_d[cur_d & 15] >> 16) & 0xFFu);
        case 0x10F: return (uint8_t)((io_d[cur_d & 15] >> 24) & 0xFFu);
        default: return 0xFF;
    }
}

void aml_host_outb(uint16_t port, uint8_t val)
{
    switch (port) {
        case 0x100: cur_b = val; break;
        case 0x104: cur_w = (cur_w & 0xFF00u) | val; break;
        case 0x105: cur_w = (cur_w & 0x00FFu) | ((uint16_t)val << 8); break;
        case 0x108: cur_d = (cur_d & 0xFFFFFF00u) | val; break;
        case 0x109: cur_d = (cur_d & 0xFFFF00FFu) | ((uint32_t)val << 8); break;
        case 0x10A: cur_d = (cur_d & 0xFF00FFFFu) | ((uint32_t)val << 16); break;
        case 0x10B: cur_d = (cur_d & 0x00FFFFFFu) | ((uint32_t)val << 24); break;
        case 0x110: cur_bank = val; break;
        default: break;
    }
}

/* SystemMemory 回调：mode 1 = 内存 IndexField 窗口；mode 2 = BankField 双 bank 行。 */
static int mem_mode;
static uint8_t mem_file[16];
static uint8_t mem_idx;
static uint8_t bank_mem[2][8];

int aml_host_mem_read(uint64_t phys, uint8_t *out)
{
    if (mem_mode == 1) {
        uint64_t off = phys - 0x100000u;
        if (off == 0) *out = mem_idx;
        else if (off == 1) *out = mem_file[mem_idx & 15];
        else *out = 0;
        return 0;
    }
    if (mem_mode == 2) {
        uint64_t off = phys - 0x100100u;
        *out = bank_mem[cur_bank & 1][off & 7];
        return 0;
    }
    return -1;
}

int aml_host_mem_write(uint64_t phys, uint8_t val)
{
    if (mem_mode == 1) {
        if (phys - 0x100000u == 0) mem_idx = val;
        return 0;
    }
    return -1;
}

/* 模拟 EC：IBF 恒置位，真碰到 EC 事务会超时；本测试 EC 路径应在碰端口前就被拒。 */
static int ec_present = 1;
uint8_t ec_host_inb(uint16_t port) { (void)port; return ec_present ? 0x02 : 0xFF; }
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

    /* ---------- A. SystemIO IndexField 正路径 ---------- */
    check(load_blob(g_battery_idxfield_io_ssdt,
                    (uint32_t)sizeof(g_battery_idxfield_io_ssdt)) == 0,
          "IO IndexField 建表成功");
    memset(io_b, 0, sizeof(io_b));
    memset(io_w, 0, sizeof(io_w));
    memset(io_d, 0, sizeof(io_d));
    io_b[0] = 0x11; io_b[1] = 0x22; io_b[2] = 0x33; io_b[3] = 0x44;
    io_b[4] = 0x34; io_b[5] = 0x12; io_b[6] = 0x1F;
    io_w[0] = 0x1234; io_w[1] = 0xABCD;
    io_d[0] = 0x12345678u;

    check(eval_int("BY00", &x) == 0 && x == 0x11, "byte granule: BY00=0x11");
    check(eval_int("BY01", &x) == 0 && x == 0x22, "byte granule: BY01=0x22");
    check(eval_int("BY02", &x) == 0 && x == 0x33, "byte granule: BY02=0x33");
    check(eval_int("BY03", &x) == 0 && x == 0x44, "byte granule: BY03=0x44");
    check(eval_int("XA12", &x) == 0 && x == 0x234, "12 位跨 granule 拼装=0x234");
    check(eval_int("UN05", &x) == 0 && x == 0x1F, "5 位非对齐抽取=0x1F");
    check(eval_int("WR00", &x) == 0 && x == 0x1234, "word granule: WR00=0x1234");
    check(eval_int("WR01", &x) == 0 && x == 0xABCD, "word granule: WR01=0xABCD");
    check(eval_int("DV00", &x) == 0 && x == 0x12345678, "dword granule: DV00=0x12345678");

    /* ---------- B. SystemMemory IndexField 正路径 ---------- */
    mem_mode = 1;
    check(load_blob(g_battery_idxfield_mem_ssdt,
                    (uint32_t)sizeof(g_battery_idxfield_mem_ssdt)) == 0,
          "内存 IndexField 建表成功");
    memset(mem_file, 0, sizeof(mem_file));
    mem_file[0] = 0x5A;
    mem_file[1] = 0x34; mem_file[2] = 0x12;
    check(eval_int("MM00", &x) == 0 && x == 0x5A, "内存 IndexField 字节=0x5A");
    check(eval_int("MM01", &x) == 0 && x == 0x1234, "内存 IndexField 跨字节 16 位=0x1234");
    mem_mode = 0;

    /* ---------- C. BankField 多 bank 切换 ---------- */
    mem_mode = 2;
    check(load_blob(g_battery_bankfield_ssdt,
                    (uint32_t)sizeof(g_battery_bankfield_ssdt)) == 0,
          "BankField 建表成功");
    memset(bank_mem, 0, sizeof(bank_mem));
    bank_mem[0][0] = 0x77;
    bank_mem[1][0] = 0x88;
    check(eval_int("BB00", &x) == 0 && x == 0x77, "bank 0 读出 0x77");
    check(eval_int("BB01", &x) == 0 && x == 0x88, "切到 bank 1 读出 0x88");
    mem_mode = 0;

    /* ---------- D. 负路径：缺名/非字段/溢出/越界 ---------- */
    check(load_blob(g_battery_idxfield_bad_ssdt,
                    (uint32_t)sizeof(g_battery_idxfield_bad_ssdt)) == 0,
          "负路径夹具建表成功");
    check(eval_int("BX00", &x) == AML_E_NOTFOUND, "缺 IndexName -> NOTFOUND");
    check(eval_int("BX01", &x) == AML_E_NOTFOUND, "缺 DataName -> NOTFOUND");
    check(eval_int("BX02", &x) == AML_E_BADCODE, "控制寄存器非字段 -> BADCODE");
    check(eval_int("BIG0", &x) == AML_E_UNSUP, "位宽 65 >64 -> UNSUP");
    check(eval_int("SM01", &x) == AML_E_BADCODE, "region 越界 -> BADCODE");
    check(eval_int("BK00", &x) == AML_E_NOTFOUND, "BankField 缺 region -> NOTFOUND");

    /* ---------- E. 负路径：EC 上的 IndexField / BankField ---------- */
    check(load_blob(g_battery_bankfield_ec_ssdt,
                    (uint32_t)sizeof(g_battery_bankfield_ec_ssdt)) == 0,
          "EC BankField 夹具建表成功");
    check(eval_int("EF00", &x) == AML_E_UNSUP, "EC bank 寄存器 -> UNSUP");

    check(load_blob(g_battery_idxfield_ec_ssdt,
                    (uint32_t)sizeof(g_battery_idxfield_ec_ssdt)) == 0,
          "EC IndexField 夹具建表成功");
    check(eval_int("ECI0.EB00", &x) == AML_E_UNSUP, "EC IndexField 数据/索引 -> UNSUP");

    /* ---------- F. 端到端：EC IndexField 电池停在 UNKNOWN，无假百分比 ---------- */
    {
        acpi_batt_sample_t s;
        fi_blob = g_battery_idxfield_ec_ssdt;
        fi_len = (uint32_t)sizeof(g_battery_idxfield_ec_ssdt);
        aml_ns_reset();
        ec_present = 1;
        check(acpi_battery_init() == 0, "EC IndexField 电池 init 成功");
        memset(&s, 0, sizeof(s));
        check(acpi_battery_poll(&s) == 2, "poll 返回 UNKNOWN(2)");
        check(s.percent == 0, "EC IndexField 不产生假百分比");
        check(s.remaining == 0 && s.full == 0, "不残留假容量");
        fi_blob = 0;
    }

    if (fails) {
        printf("FAIL %d 项\n", fails);
        return 1;
    }
    printf("PASS idx_bank_field (%d checks)\n", checks);
    return 0;
}
