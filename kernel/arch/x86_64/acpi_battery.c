/* ============================================================================
 * 赫普OS - ACPI 电池读数（M2）
 *
 * 判定顺序（全部失败则维持“已声明但没读到”，绝不填假数）：
 *   1. 命名空间里枚举 _HID = PNP0C0A 的设备，选第一个 _STA 明确的 present 槽位；
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
#define EC_HID_EISA  0x41D00C09u   /* PNP0C09 */
#define AC_HID_EISA  0x04700003u   /* ACPI0003 */

static int g_bat_ready;
static char g_bat_path[96];
static char g_ac_path[96];
static int g_ac_found;
static char g_ec_reg_path[128];
static int g_ec_reg_ready;
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

static int eval_named_child(const char *base, const char *obj, aml_value_t **out);
static int ec_call_reg(void);

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
    /* 同一 HID 可能有多个实例（双电池、可拆电池槽）。逐个看 _STA，选第一个
     * 明确 present 的槽位；全都 absent 时保留第一个，交给 poll 报 ABSENT。
     * 不能只取命名空间里的第一个，否则 BAT0 空槽会把 BAT1 的真电池遮掉。 */
    {
        char cand[4][96];
        int ncand = 0, cursor = 0;
        int chosen = -1, fallback = -1, any = -1;
        while (ncand < 4 &&
               aml_find_device_next(BAT_HID_EISA, "PNP0C0A", cand[ncand],
                                    sizeof(cand[0]), &cursor) == 0) {
            ncand++;
        }
        if (ncand == 0) {
            vga_puts("[BAT] aml: PNP0C0A device not found in namespace\n");
            return -1;
        }
        vga_puts("[BAT] aml candidates=");
        bat_num(ncand);
        vga_puts("\n");
        for (int ci = 0; ci < ncand; ci++) {
            char stpath[128];
            aml_value_t *stv = 0;
            int64_t st = 0;
            int rc;
            vga_puts("[BAT] aml dev=");
            vga_puts(cand[ci]);
            vga_puts("\n");
            if (any < 0) any = ci;
            if (path_child(cand[ci], "_STA", stpath, sizeof(stpath)) != 0) {
                if (chosen < 0) chosen = ci;
                continue;
            }
            aml_sample_begin();
            rc = aml_eval_path(stpath, &stv);
            aml_sample_end();
            if (rc == AML_E_NOTFOUND) {
                if (chosen < 0) chosen = ci;   /* 没有 _STA 视为已声明 */
            } else if (rc == 0 && aml_value_int(stv, &st) == 0) {
                if ((st & 1) && chosen < 0) chosen = ci;
                else if (!(st & 1) && fallback < 0) fallback = ci;
            } else if (chosen < 0 && fallback < 0) {
                fallback = ci;   /* _STA 求值失败：保留候选，让 poll 给出 UNKNOWN */
            }
        }
        if (chosen < 0) chosen = (fallback >= 0) ? fallback : any;
        if (chosen < 0) chosen = 0;
        {
            int k = 0;
            while (cand[chosen][k] && k < (int)sizeof(g_bat_path) - 1) {
                g_bat_path[k] = cand[chosen][k];
                k++;
            }
            g_bat_path[k] = 0;
        }
        vga_puts("[BAT] aml dev=selected ");
        vga_puts(g_bat_path);
        vga_puts("\n");
    }
    /* EC 型电池：命名空间里有 PNP0C09 才允许碰 0x62/0x66，避免无 EC 机器盲写 */
    {
        char ecpath[96];
        if (aml_find_device(EC_HID_EISA, "PNP0C09", ecpath, sizeof(ecpath)) == 0) {
            char path[128];
            aml_value_t *v = 0;
            int64_t x = 0;
            int rrc;
            int glk = 0;

            ec64_set_present(1);
            ec64_set_glk(0);
            vga_puts("[BAT] ec=0x1 dev=");
            vga_puts(ecpath);
            vga_puts("\n");

            /* _GLK=1 表示 EC 与 SMI 共享，规范要求先拿 ACPI Global Lock。
             * 当前内核没有实现 FACS 全局锁，因此 glk=1 直接锁死 EC 读；
             * _GLK 存在但求值失败也按 glk=1 处理，避免 fail-open。 */
            if (path_child(ecpath, "_GLK", path, sizeof(path)) == 0) {
                aml_sample_begin();
                rrc = aml_eval_path(path, &v);
                if (rrc == 0 && aml_value_int(v, &x) == 0) {
                    glk = (int)(x & 1);
                    vga_puts("[BAT] ec glk=");
                    bat_num(glk);
                    vga_puts("\n");
                } else {
                    glk = 1;
                    vga_puts("[BAT] ec glk=unknown\n");
                }
                aml_sample_end();
            } else {
                vga_puts("[BAT] ec glk=0\n");
            }
            ec64_set_glk(glk);
            if (glk) vga_puts("[BAT] ec lockout=1\n");

            if (glk) {
                vga_puts("[BAT] ec reg=skip-glk\n");
            } else if (path_child(ecpath, "_REG", g_ec_reg_path, sizeof(g_ec_reg_path)) == 0) {
                /* 向 EC 设备声明 EmbeddedControl(3) 的 OS 处理器已安装。
                 * 不少 DSDT 的 _BST 会先看 ECON 之类的标志；不调用 _REG 时该标志
                 * 保持 0，_BST 可能直接返回 0。 */
                g_ec_reg_ready = 1;
                aml_sample_begin();
                rrc = ec_call_reg();
                aml_sample_end();
                vga_puts("[BAT] ec reg=");
                if (rrc == AML_E_NOTFOUND) vga_puts("none\n");
                else { bat_num(rrc); vga_puts("\n"); }
            } else {
                vga_puts("[BAT] ec reg=none\n");
            }
        } else {
            ec64_set_present(0);
            vga_puts("[BAT] ec=0x0\n");
        }
    }

    /* 交流适配器：ACPI0003 存在时读 _PSR；没有就明确 none，不猜插没插。 */
    g_ac_path[0] = 0;
    g_ac_found = 0;
    {
        char acpath[96];
        if (aml_find_device(AC_HID_EISA, "ACPI0003", acpath, sizeof(acpath)) == 0) {
            int k = 0;
            while (acpath[k] && k < 95) { g_ac_path[k] = acpath[k]; k++; }
            g_ac_path[k] = 0;
            g_ac_found = 1;
            vga_puts("[BAT] ac dev=");
            vga_puts(g_ac_path);
            vga_puts("\n");
            {
                aml_value_t *v = 0;
                int64_t x = 0;
                int rrc;
                aml_sample_begin();
                rrc = eval_named_child(g_ac_path, "_PSR", &v);
                aml_sample_end();
                vga_puts("[BAT] ac psr=");
                if (rrc == 0 && aml_value_int(v, &x) == 0) bat_num(x & 1);
                else vga_puts("none");
                vga_puts("\n");
            }
        } else {
            vga_puts("[BAT] ac=none\n");
        }
    }
    g_bat_ready = 1;
    return 0;
}

int acpi_battery_ready(void) { return g_bat_ready; }

const char *acpi_battery_path(void) { return g_bat_path; }

static int eval_named_child(const char *base, const char *obj, aml_value_t **out)
{
    char path[128];
    if (path_child(base, obj, path, sizeof(path)) != 0) return AML_E_NOTFOUND;
    return aml_eval_path(path, out);
}

static int eval_child(const char *obj, aml_value_t **out)
{
    return eval_named_child(g_bat_path, obj, out);
}

/* _REG(3,1)：声明 EmbeddedControl 的 OS 处理器已安装。
 * 必须在当前 sample 的 overlay 内执行，_BST 才能看到 _REG 写入的标志。 */
static int ec_call_reg(void)
{
    int64_t reg_args[2] = { 3, 1 };
    aml_value_t *v = 0;
    if (ec64_glk()) return AML_E_UNSUP;   /* 未拿全局锁，连 _REG 也不碰 EC */
    if (!g_ec_reg_ready) return AML_E_NOTFOUND;
    return aml_eval_path_ints(g_ec_reg_path, reg_args, 2, &v);
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

int acpi_ac_poll(acpi_ac_sample_t *out)
{
    aml_value_t *v = 0;
    int64_t x = 0;
    int rc;

    if (!out) return -1;
    out->present = 0;
    out->known = 0;
    out->online = 0;
    if (!g_bat_ready && acpi_battery_init() != 0) return 1;
    out->present = g_ac_found;
    if (!g_ac_found) return 1;

    aml_sample_begin();
    (void)ec_call_reg();
    rc = eval_named_child(g_ac_path, "_PSR", &v);
    if (rc == 0 && aml_value_int(v, &x) == 0) {
        out->known = 1;
        out->online = (int)(x & 1);
    }
    aml_sample_end();
    return out->known ? 0 : 2;
}

int acpi_battery_poll(acpi_batt_sample_t *out)
{
    aml_value_t *sta = 0, *bst = 0, *bif = 0, *bix = 0;
    int64_t sta_i = 0, state = 0, rate = 0, rem = 0, full = 0, design = 0, unit = 0, volt = 0;
    int rc_sta, rc_bst, rc_bix, rc_bif;
    int percent;

    if (!out) return -1;
    out->present = 0;
    out->percent = 0;
    out->charging = 0;
    out->state_bits = 0;
    out->power_unit = 0;
    out->remaining = 0;
    out->rate = 0;
    out->full = 0;
    out->design = 0;
    out->voltage_mv = 0;
    out->health = -1;
    out->minutes = -1;
    out->minutes_full = -1;
    out->used_bix = 0;
    out->ac_present = 0;
    out->ac_known = 0;
    out->ac_online = 0;

    if (!g_bat_ready && acpi_battery_init() != 0) {
        g_bat_last_rc = 2;
        return 2;
    }

    aml_sample_begin();

    /* 每个采样都重放 _REG：overlay 生命周期只到 sample_end，不能只在 init 调一次。 */
    (void)ec_call_reg();

    out->ac_present = g_ac_found;
    if (g_ac_found) {
        aml_value_t *psr = 0;
        int64_t psr_i = 0;
        int rc_psr = eval_named_child(g_ac_path, "_PSR", &psr);
        if (rc_psr == 0 && aml_value_int(psr, &psr_i) == 0) {
            out->ac_known = 1;
            out->ac_online = (int)(psr_i & 1);
        }
    }

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
        aml_value_int(aml_value_at(bst, 1), &rate) != 0) {
        aml_sample_end();
        g_bat_last_rc = 2;
        return 2;
    }
    /* 元素 2 才是剩余容量，元素 1 是放电/充电速率 */
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
    out->rate = rate;
    out->full = full;
    out->design = design;
    out->voltage_mv = volt;

    /* 健康度与时间只做“固件原始值的换算”，不参与 VALID 判定：
     * - full/design 都 >0 才算健康度；full 明显大于 design 时标 -1，不硬压成 100%；
     * - 时间只在速率 >0 时算，0 速率显示 --，不猜。 */
    if (design > 0 && full > 0) {
        if (full <= design + design / 20) {
            int hp = (int)((full * 100 + design / 2) / design);
            if (hp > 100) hp = 100;
            out->health = hp;
        } else {
            out->health = -1;
        }
    }
    if (rate > 0) {
        if (state & 2) {
            if (full > rem) {
                int64_t m = ((full - rem) * 60 + rate / 2) / rate;
                if (m >= 0 && m < 1440) out->minutes_full = (int)m;
            }
        } else if (rem > 0) {
            int64_t m = (rem * 60 + rate / 2) / rate;
            if (m >= 0 && m < 1440) out->minutes = (int)m;
        }
    }

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
    vga_puts(" rate=");
    bat_num(out->rate);
    vga_puts(" health=");
    bat_num(out->health);
    vga_puts(" min=");
    bat_num(out->minutes);
    vga_puts(" min_full=");
    bat_num(out->minutes_full);
    vga_puts("\n");

    aml_sample_end();
    g_bat_last_rc = 0;
    return 0;
}

int acpi_battery_last_rc(void) { return g_bat_last_rc; }
