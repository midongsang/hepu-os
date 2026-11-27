/* ============================================================================
 * 赫普OS - ACPI 电池读数（M2）
 *
 * 判定顺序（全部失败则维持“已声明但没读到”，绝不填假数）：
 *   1. 命名空间里找 _HID = PNP0C0A 的设备；
 *   2. 求值 _STA：bit0=present；明确 0 才算“没有电池”；
 *   3. 求值 _BST：Package{状态, 速率, 剩余容量, 电压}；
 *   4. 优先 _BIX，其次 _BIF：拿设计容量与满充容量；
 *   5. 剩余/满充 → 百分比，_BST 状态 bit1 → 充电中。
 *
 * 可信门槛：满充容量 > 0；剩余容量不得明显超过满充；0% 必须带 critical 位，
 * 否则视为固件没更新（例如 EC 还没喂数据），退回“未读到”。
 * ========================================================================== */

#include "acpi_battery.h"
#include "aml64.h"
#include "ec64.h"
#include "firmware_info.h"

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t v);

#define BAT_HID_EISA 0x41D00C0Au   /* PNP0C0A 的压缩 EISA ID */

static int g_bat_ready;
static char g_bat_path[96];
static int g_bat_last_rc = 2;

static void bat_num(int64_t v)
{
    char buf[24];
    int n = 0;
    uint64_t u;
    if (v < 0) { vga_puts("-"); u = (uint64_t)(-v); }
    else u = (uint64_t)v;
    if (u == 0) { vga_puts("0"); return; }
    while (u && n < 23) { buf[n++] = (char)('0' + (u % 10)); u /= 10; }
    while (n > 0) {
        char t[2];
        t[0] = buf[--n];
        t[1] = 0;
        vga_puts(t);
    }
}

static int path_child(const char *parent, const char *child, char *out, int cap)
{
    int used = 0;
    while (parent[used]) used++;
    if (used + 1 + (int)sizeof("_STA") > cap) return -1;
    for (int i = 0; i < used; i++) out[i] = parent[i];
    out[used] = '.';
    int j = 0;
    while (child[j] && used + 1 + j < cap - 1) { out[used + 1 + j] = child[j]; j++; }
    out[used + 1 + j] = 0;
    return 0;
}

int acpi_battery_init(void)
{
    const uint8_t *tables[24];
    uint32_t lens[24];
    int n, i;
    int rc;

    if (g_bat_ready) return 0;

    n = fi_table_count();
    if (n <= 0) {
        vga_puts("[BAT] aml: no acpi tables\n");
        return -1;
    }
    if (n > 24) n = 24;
    for (i = 0; i < n; i++) {
        if (fi_table_at(i, &tables[i], &lens[i]) != 0) return -1;
    }
    rc = aml_ns_build(tables, lens, n);
    if (rc != 0) {
        vga_puts("[BAT] aml: namespace build failed code=");
        bat_num(rc);
        vga_puts("\n");
        return -1;
    }
    if (aml_find_device(BAT_HID_EISA, "PNP0C0A", g_bat_path, sizeof(g_bat_path)) != 0) {
        vga_puts("[BAT] aml: PNP0C0A device not found in namespace\n");
        return -1;
    }
    vga_puts("[BAT] aml dev=");
    vga_puts(g_bat_path);
    vga_puts("\n");
    /* EC 型电池：命名空间里有 PNP0C09 才允许碰 0x62/0x66，避免无 EC 机器盲写 */
    {
        char ecpath[96];
        if (aml_find_device(0x41D00C09u, "PNP0C09", ecpath, sizeof(ecpath)) == 0) {
            ec64_set_present(1);
            vga_puts("[BAT] ec=0x1 dev=");
            vga_puts(ecpath);
            vga_puts("\n");
        } else {
            ec64_set_present(0);
            vga_puts("[BAT] ec=0x0\n");
        }
    }
    g_bat_ready = 1;
    return 0;
}

int acpi_battery_ready(void) { return g_bat_ready; }

const char *acpi_battery_path(void) { return g_bat_path; }

static int eval_child(const char *obj, aml_value_t **out)
{
    char path[96];
    if (path_child(g_bat_path, obj, path, sizeof(path)) != 0) return AML_E_NOTFOUND;
    return aml_eval_path(path, out);
}

/* 百分比与可信门槛抽成纯函数，主机单测直接覆盖阈值，不依赖命名空间。 */
int acpi_battery_make_percent(int64_t rem, int64_t full, int state, int *pct)
{
    int percent;
    if (full <= 0) return -1;                 /* 没有满充容量：不可信 */
    if (rem < 0) return -2;
    if (rem > full) {
        if (rem <= full + full / 20) rem = full;   /* 5% 内按满仓处理 */
        else return -3;                            /* 明显超过满仓：固件单位不对 */
    }
    if (rem == 0 && !(state & 4) && !(state & 2)) return -4;  /* 0% 却不 critical：陈旧 */
    percent = (int)((rem * 100 + full / 2) / full);
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (pct) *pct = percent;
    return 0;
}

int acpi_battery_poll(acpi_batt_sample_t *out)
{
    aml_value_t *sta = 0, *bst = 0, *bif = 0, *bix = 0;
    int64_t sta_i = 0, state = 0, rem = 0, full = 0, design = 0, unit = 0, volt = 0;
    int rc_sta, rc_bst, rc_bix, rc_bif;
    int percent;

    if (!out) return -1;
    out->present = 0;
    out->percent = 0;
    out->charging = 0;
    out->state_bits = 0;
    out->power_unit = 0;
    out->remaining = 0;
    out->full = 0;
    out->design = 0;
    out->voltage_mv = 0;
    out->used_bix = 0;

    if (!g_bat_ready && acpi_battery_init() != 0) {
        g_bat_last_rc = 2;
        return 2;
    }

    aml_sample_begin();

    rc_sta = eval_child("_STA", &sta);
    if (rc_sta == 0) {
        if (aml_value_int(sta, &sta_i) != 0) { aml_sample_end(); return 2; }
        if ((sta_i & 1) == 0) {
            /* 固件把槽位声明出来但明确 present=0：这是“没有电池”，不是故障 */
            vga_puts("[BAT] aml sta=absent\n");
            out->present = 0;
            g_bat_last_rc = 1;
            aml_sample_end();
            return 1;
        }
    } else if (rc_sta != AML_E_NOTFOUND) {
        vga_puts("[BAT] aml sta failed code=");
        bat_num(rc_sta);
        vga_puts("\n");
        aml_sample_end();
        g_bat_last_rc = 2;
        return 2;
    }

    out->present = 1;

    rc_bst = eval_child("_BST", &bst);
    if (rc_bst != 0 || aml_value_count(bst) < 4) {
        vga_puts("[BAT] aml bst failed code=");
        bat_num(rc_bst != 0 ? rc_bst : -6);
        vga_puts("\n");
        aml_sample_end();
        g_bat_last_rc = 2;
        return 2;
    }
    if (aml_value_int(aml_value_at(bst, 0), &state) != 0 ||
        aml_value_int(aml_value_at(bst, 1), &rem) != 0) {
        aml_sample_end();
        g_bat_last_rc = 2;
        return 2;
    }
    /* 元素 2 才是剩余容量，元素 1 是放电速率 */
    if (aml_value_int(aml_value_at(bst, 2), &rem) != 0) rem = 0;
    if (aml_value_int(aml_value_at(bst, 3), &volt) != 0) volt = 0;

    rc_bix = eval_child("_BIX", &bix);
    rc_bif = eval_child("_BIF", &bif);
    if (rc_bix == 0 && aml_value_count(bix) >= 4) {
        if (aml_value_int(aml_value_at(bix, 1), &unit) != 0) unit = 0;
        if (aml_value_int(aml_value_at(bix, 2), &design) != 0) design = 0;
        if (aml_value_int(aml_value_at(bix, 3), &full) != 0) full = 0;
        if (aml_value_int(aml_value_at(bix, 4), &volt) != 0 && volt == 0) volt = 0;
        out->used_bix = 1;
    } else if (rc_bif == 0 && aml_value_count(bif) >= 3) {
        if (aml_value_int(aml_value_at(bif, 0), &unit) != 0) unit = 0;
        if (aml_value_int(aml_value_at(bif, 1), &design) != 0) design = 0;
        if (aml_value_int(aml_value_at(bif, 2), &full) != 0) full = 0;
    } else {
        vga_puts("[BAT] aml bif/bix missing\n");
        aml_sample_end();
        g_bat_last_rc = 2;
        return 2;
    }

    out->state_bits = (int)state;
    out->charging = (state & 2) ? 1 : 0;
    out->power_unit = (int)unit;
    out->remaining = rem;
    out->full = full;
    out->design = design;
    out->voltage_mv = volt;

    {
        int rc = acpi_battery_make_percent(rem, full, (int)state, &percent);
        if (rc != 0) {
            vga_puts("[BAT] aml untrusted rc=");
            bat_num(rc);
            vga_puts(" rem=");
            bat_num(rem);
            vga_puts(" full=");
            bat_num(full);
            vga_puts("\n");
            aml_sample_end();
            g_bat_last_rc = 2;
            return 2;
        }
    }
    if (rem > full) rem = full;   /* 与纯函数口径对齐，供页面显示 */
    out->remaining = rem;
    out->percent = percent;

    vga_puts("[BAT] aml sta=");
    vga_put_hex((uint64_t)(sta_i & 0xFF));
    vga_puts(" bst=1 ");
    vga_puts(out->used_bix ? "bix=1 " : "bif=1 ");
    vga_puts("pct=");
    bat_num(percent);
    vga_puts(" chg=");
    bat_num(out->charging);
    vga_puts(" rem=");
    bat_num(rem);
    vga_puts(" full=");
    bat_num(full);
    vga_puts(" unit=");
    bat_num(unit);
    vga_puts("\n");

    aml_sample_end();
    g_bat_last_rc = 0;
    return 0;
}

int acpi_battery_last_rc(void) { return g_bat_last_rc; }
