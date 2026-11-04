/* ============================================================================
 * 赫普OS - 移动桌面时钟应用
 * 从 mobile.c 拆分（大文件拆分目标<500行）
 * ========================================================================== */

#include "mobile.h"
#include "vga_gfx.h"
#include "font_zh.h"
#include "vga.h"
#include "rtc.h"
#include <string.h>

void mobile_run_clock(void)
{
    int mode = 0;          /* 0=时钟 1=闹钟 2=计时器 3=世界时钟 */
    int al_fired = 0;
    int tm_sec = 60, tm_left = 60, tm_running = 0;
    int alarm_flash = 0;   /* 到点红闪剩余帧 */
    int running = 1;
    /* V3.74: 秒表状态 (PIT 毫秒精度) */
    int sw_on = 0, sw_acc = 0, sw_laps = 0, sw_lapms = 0;
    uint32_t sw_t0 = 0;
    /* V2.91: 手动校时状态 */
    int adj_active = 0, adj_field = 0, rtc_msg = 0, adj_bad = 0;   /* V2.96: 非法日期标记 */
    int adj_v[6] = {12, 0, 0, 1, 1, 2026};
    uint32_t last_s = 0xFFFFFFFF;
    /* V2.87: STORAGE 权限门 (闹钟落盘) */
    if (!mobile_perm_request("clock", "com.hepu.clock", PM_PERM_STORAGE)) {
        mobile_perm_denied_page("clock", prm_perm_name(PM_PERM_STORAGE));
        return;
    }
    alarm_load();
    wc_load();   /* V3.06: 世界时钟城市恢复 */
    while (running) {
        extern uint32_t pit_get_seconds(void);
        uint32_t s = pit_get_seconds();
        int hh = (int)(s / 3600) % 24;
        int mm = (int)((s % 3600) / 60);
        int ss = (int)(s % 60);
        /* 秒变化检测: 计时器递减 + 闹钟检测 */
        if (last_s != s) {
            last_s = s;
            if (tm_running && tm_left > 0) {
                tm_left--;
                if (tm_left == 0) { tm_running = 0; alarm_flash = 30; }
            }
            /* V2.90: 任一启用闹钟到点触发 */
            {
                int match = 0;
                for (int ai = 0; ai < ALARM_MAX; ai++) {
                    if (al_armed[ai] && hh == al_h[ai] && mm == al_m[ai] && ss < 3)
                        match = 1;
                }
                if (match && !al_fired) { al_fired = 1; alarm_flash = 30; }
                if (ss >= 3) al_fired = 0;
            }
        }
        /* 渲染 */
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "Back: ESC", 0x0F);
        ui_title("时钟", "Clock", 18);
        gfx_puts(8, 30, "1:Clock 2:Alarm 3:Timer 4:World 5:SW", 0x08);
        if (alarm_flash > 0) {
            /* 到点: 全屏红闪 */
            gfx_fillrect(0, 0, gfx_width, gfx_height,
                         ((alarm_flash / 4) % 2) ? 0x0C : 0x00);
            if (!sys_lang_en) gfx_puts_zh((gfx_width - 96) / 2, gfx_height / 2 - 8, "到点啦!", 0x0F);
            gfx_puts((gfx_width - 56) / 2, gfx_height / 2 + 8, "TIME UP", 0x0F);
            alarm_flash--;
        } else if (mode == 0) {
            /* V2.87: 优先 RTC 真实时间, 失败回退 uptime */
            /* V2.91: T 键进入手动校时 (adj_active), adj_v[0..5] 时/分/秒/日/月/年 */
            extern int rtc_get_time(int *, int *, int *, int *, int *, int *);
            int rh, rm, rs, rd, rmo, ry;
            int rtc_ok = (rtc_get_time(&rh, &rm, &rs, &rd, &rmo, &ry) == 0);
            char tb[12];
            if (adj_active) {
                tb[0] = '0' + adj_v[0] / 10; tb[1] = '0' + adj_v[0] % 10; tb[2] = ':';
                tb[3] = '0' + adj_v[1] / 10; tb[4] = '0' + adj_v[1] % 10; tb[5] = ':';
                tb[6] = '0' + adj_v[2] / 10; tb[7] = '0' + adj_v[2] % 10; tb[8] = 0;
            } else if (rtc_ok) {
                rh %= 24; rm %= 60; rs %= 60;
                if (mob_time12) {   /* V2.98: 12h 显示 (带秒+AM/PM) */
                    int h12 = rh % 12;
                    if (h12 == 0) h12 = 12;
                    tb[0] = '0' + h12 / 10; tb[1] = '0' + h12 % 10; tb[2] = ':';
                    tb[3] = '0' + rm / 10; tb[4] = '0' + rm % 10; tb[5] = ':';
                    tb[6] = '0' + rs / 10; tb[7] = '0' + rs % 10;
                    tb[8] = ' '; tb[9] = (rh < 12) ? 'A' : 'P'; tb[10] = 'M';
                    tb[11] = 0;
                } else {
                    tb[0] = '0' + rh / 10; tb[1] = '0' + rh % 10; tb[2] = ':';
                    tb[3] = '0' + rm / 10; tb[4] = '0' + rm % 10; tb[5] = ':';
                    tb[6] = '0' + rs / 10; tb[7] = '0' + rs % 10; tb[8] = 0;
                }
            } else {
                tb[0] = '0' + hh / 10; tb[1] = '0' + hh % 10; tb[2] = ':';
                tb[3] = '0' + mm / 10; tb[4] = '0' + mm % 10; tb[5] = ':';
                tb[6] = '0' + ss / 10; tb[7] = '0' + ss % 10; tb[8] = 0;
            }
            gfx_puts((gfx_width - 96) / 2, 60, tb, 0x0A);
            if (adj_active) {
                /* 校时字段光标 (时间行: 时0 分1 秒2; 日期行: 年3 月4 日5) */
                static const int date_off[3] = {0, 5, 8};
                int cx = (gfx_width - 80) / 2
                       + (adj_field < 3 ? adj_field * 24 + 3 : date_off[adj_field - 3]);
                int cy = (adj_field < 3) ? 76 : 100;
                gfx_puts(cx, cy, "^", 0x0C);
            } else if (rtc_msg > 0) {
                gfx_puts((gfx_width - 60) / 2, 110, "RTC UPDATED", 0x0A);
                rtc_msg--;
            }
            if (adj_active) {
                char db[16];
                db[0] = '0' + (adj_v[5] / 1000) % 10; db[1] = '0' + (adj_v[5] / 100) % 10;
                db[2] = '0' + (adj_v[5] / 10) % 10; db[3] = '0' + adj_v[5] % 10; db[4] = '-';
                db[5] = '0' + adj_v[4] / 10; db[6] = '0' + adj_v[4] % 10; db[7] = '-';
                db[8] = '0' + adj_v[3] / 10; db[9] = '0' + adj_v[3] % 10; db[10] = 0;
                gfx_puts((gfx_width - 80) / 2, 84, db, 0x0F);
                if (adj_bad)
                    gfx_puts((gfx_width - 80) / 2, 118, "INVALID DATE", 0x0C);
                gfx_puts(8, gfx_height - 14,
                         "W/S: field A/D: adj Enter: write  ESC: exit", 0x0C);
            } else if (rtc_ok) {
                char db[18];
                if (mob_datefmt) {
                    /* MM/DD/YYYY */
                    db[0] = '0' + rmo / 10; db[1] = '0' + rmo % 10; db[2] = '/';
                    db[3] = '0' + rd / 10; db[4] = '0' + rd % 10; db[5] = '/';
                    db[6] = '0' + (ry / 1000) % 10; db[7] = '0' + (ry / 100) % 10;
                    db[8] = '0' + (ry / 10) % 10; db[9] = '0' + ry % 10; db[10] = ' ';
                } else {
                    /* YYYY-MM-DD */
                    db[0] = '0' + (ry / 1000) % 10; db[1] = '0' + (ry / 100) % 10;
                    db[2] = '0' + (ry / 10) % 10; db[3] = '0' + ry % 10; db[4] = '-';
                    db[5] = '0' + rmo / 10; db[6] = '0' + rmo % 10; db[7] = '-';
                    db[8] = '0' + rd / 10; db[9] = '0' + rd % 10; db[10] = ' ';
                }
                const char *wn = calc_wday_name(calc_wday(ry, rmo, rd));
                for (int wi = 0; wi < 3; wi++) db[11 + wi] = wn[wi];
                db[14] = 0;
                gfx_puts((gfx_width - 90) / 2, 84, db, 0x0F);
                gfx_puts((gfx_width - 100) / 2, 104, "RTC time  [T]: set", 0x08);
            } else {
                gfx_puts((gfx_width - 72) / 2, 84, "2026-09-07", 0x0F);
                gfx_puts((gfx_width - 150) / 2, 104,
                         "uptime clock (since boot)", 0x08);
            }
        } else if (mode == 1) {
            /* V2.90: 多闹钟列表 (3 槽) */
            if (sys_lang_en) gfx_puts((gfx_width - 64) / 2, 34, "Alarm List", 0x0B);
            else gfx_puts_zh((gfx_width - 64) / 2, 34, "闹钟列表", 0x0B);
            for (int ai = 0; ai < ALARM_MAX; ai++) {
                int ay = 48 + ai * 24;
                uint8_t bg = (ai == al_sel) ? mob_accent : 0x08;
                gfx_fillrect(4, ay, gfx_width - 8, 22, bg);
                gfx_rect(4, ay, gfx_width - 8, 22, 0x0F);
                char ab[16];
                ab[0] = '1' + ai; ab[1] = ' '; ab[2] = ' ';
                ab[3] = '0' + al_h[ai] / 10; ab[4] = '0' + al_h[ai] % 10; ab[5] = ':';
                ab[6] = '0' + al_m[ai] / 10; ab[7] = '0' + al_m[ai] % 10; ab[8] = 0;
                gfx_puts(8, ay + 2, ab, 0x0F);
                gfx_puts(gfx_width - 36, ay + 2, al_armed[ai] ? "ON" : "OFF",
                         al_armed[ai] ? 0x0A : 0x08);
            }
            if (al_full > 0) {
                gfx_puts(8, gfx_height - 26, "ALL SLOTS USED", 0x0C);
                al_full--;
            }
            gfx_puts(8, gfx_height - 14,
                     "W/S pick A/D min [/] hr Enter:on/off +=:add -=:del", 0x08);
        } else if (mode == 2) {
            /* 计时器 */
            int tmm = tm_left / 60, tss = tm_left % 60;
            char tb[12];
            tb[0] = '0' + tmm / 10; tb[1] = '0' + tmm % 10; tb[2] = ':';
            tb[3] = '0' + tss / 10; tb[4] = '0' + tss % 10; tb[5] = 0;
            if (sys_lang_en) gfx_puts((gfx_width - 48) / 2, 48, "Timer", 0x0B);
            else gfx_puts_zh((gfx_width - 80) / 2, 48, "计时器", 0x0B);
            gfx_puts((gfx_width - 44) / 2, 68, tb,
                     tm_running ? 0x0A : 0x0F);
            gfx_puts((gfx_width - 70) / 2, 92,
                     tm_running ? "RUNNING" : "READY",
                     tm_running ? 0x0A : 0x08);
            gfx_puts(8, gfx_height - 14,
                     "W/S:+10s A/D:+1s Enter:start C:reset", 0x08);
        } else if (mode == 3) {
            /* V3.00: 世界时钟 (RTC=本地上海 UTC+8 基准) */
            extern int rtc_get_time(int *, int *, int *, int *, int *, int *);
            int rh2, rm2, rs2, rd2, rmo2, ry2;
            int rtc_ok2 = (rtc_get_time(&rh2, &rm2, &rs2, &rd2, &rmo2, &ry2) == 0);
            if (!rtc_ok2) { rh2 = hh; rm2 = mm; }
            rh2 %= 24; rm2 %= 60;
            /* V3.06: 可配置城市列表 (wc_cnt 项) */
            gfx_puts(8, 40, "World (base: RTC local)", 0x08);
            for (int wi = 0; wi < wc_cnt; wi++) {
                int t = rh2 + wc_off[wi];
                int day = 0;
                while (t < 0) { t += 24; day--; }
                while (t >= 24) { t -= 24; day++; }
                int y = 58 + wi * 20;
                if (wi == wc_sel) {
                    gfx_fillrect(2, y, gfx_width - 4, 18, mob_accent);
                    gfx_rect(2, y, gfx_width - 4, 18, 0x0F);
                }
                gfx_puts(8, y + 2, wc_name[wi], 0x0F);
                char tb2[8];
                tb2[0] = '0' + t / 10; tb2[1] = '0' + t % 10; tb2[2] = ':';
                tb2[3] = '0' + rm2 / 10; tb2[4] = '0' + rm2 % 10; tb2[5] = 0;
                gfx_puts(gfx_width - 68, y + 2, tb2, 0x0A);
                if (day != 0) {
                    char db2[4];
                    db2[0] = (day > 0) ? '+' : '-';
                    int da = (day > 0) ? day : -day;
                    if (da > 9) da = 9;
                    db2[1] = '0' + da; db2[2] = 'd'; db2[3] = 0;
                    gfx_puts(gfx_width - 30, y + 2, db2, 0x0B);
                }
            }
        } else if (mode == 4) {
            /* V3.74: 秒表 (分:秒.百分秒) */
            extern uint32_t pit_get_ticks(void);
            uint32_t now = pit_get_ticks();
            uint32_t ms = sw_acc + (sw_on ? (now - sw_t0) : 0);
            int sm = (int)(ms / 60000), ss = (int)((ms / 1000) % 60),
                cs = (int)((ms / 10) % 100);
            char sb[16];
            sb[0] = '0' + sm / 10; sb[1] = '0' + sm % 10; sb[2] = ':';
            sb[3] = '0' + ss / 10; sb[4] = '0' + ss % 10; sb[5] = '.';
            sb[6] = '0' + cs / 10; sb[7] = '0' + cs % 10; sb[8] = 0;
            gfx_puts((gfx_width - 72) / 2, 58, sb, sw_on ? 0x0A : 0x0F);
            gfx_puts((gfx_width - 88) / 2, 80, sw_on ? "RUNNING" : "STOPPED",
                     sw_on ? 0x0A : 0x08);
            char ln[8]; int lv = sw_laps;
            char l2[6]; int lj = 0;
            if (lv == 0) l2[lj++] = '0';
            while (lv > 0) { l2[lj++] = '0' + lv % 10; lv /= 10; }
            for (int q = 0; q < lj; q++) ln[q] = l2[lj - 1 - q];
            ln[lj] = 0;
            if (sys_lang_en) { gfx_puts(8, 106, "Laps: ", 0x0B); gfx_puts(56, 106, ln, 0x0F); }
            else { gfx_puts_zh(8, 106, "圈数: ", 0x0B); gfx_puts(56, 106, ln, 0x0F); }
            int lm = sw_lapms / 60000, lss = (sw_lapms / 1000) % 60,
                lcs = (sw_lapms / 10) % 100;
            char lt2[12];
            lt2[0] = '0' + lm / 10; lt2[1] = '0' + lm % 10; lt2[2] = ':';
            lt2[3] = '0' + lss / 10; lt2[4] = '0' + lss % 10; lt2[5] = '.';
            lt2[6] = '0' + lcs / 10; lt2[7] = '0' + lcs % 10; lt2[8] = 0;
            if (sys_lang_en) { gfx_puts(8, 122, "Last: ", 0x0B); gfx_puts(56, 122, lt2, 0x0F); }
            else { gfx_puts_zh(8, 122, "最近: ", 0x0B); gfx_puts(56, 122, lt2, 0x0F); }
            gfx_puts(8, gfx_height - 14,
                     "Space: start/stop  L: lap  C: clear", 0x08);
        }


                    gfx_puts(8, gfx_height - 14,
                     "W/S pick  +=:add  -=:del", 0x08);
gfx_flip();
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) {
                if (adj_active) { adj_active = 0; }
                else { running = 0; break; }
            }
            else if (adj_active) {
                /* V2.91: 校时键盘 */
                int inc = (kc == 'd' || kc == 'D') ? 1 : -1;
                if (kc == 'w' || kc == 'W') adj_field = (adj_field + 5) % 6;
                else if (kc == 's' || kc == 'S') adj_field = (adj_field + 1) % 6;
                else if (kc == 'a' || kc == 'A' || kc == 'd' || kc == 'D') {
                    if (adj_field == 0) adj_v[0] = (adj_v[0] + inc + 24) % 24;
                    else if (adj_field == 1) adj_v[1] = (adj_v[1] + inc + 60) % 60;
                    else if (adj_field == 2) adj_v[2] = (adj_v[2] + inc + 60) % 60;
                    else if (adj_field == 3) {
                        adj_v[3] += inc;
                        if (adj_v[3] < 1) adj_v[3] = 31;
                        if (adj_v[3] > 31) adj_v[3] = 1;
                    } else if (adj_field == 4) {
                        adj_v[4] += inc;
                        if (adj_v[4] < 1) adj_v[4] = 12;
                        if (adj_v[4] > 12) adj_v[4] = 1;
                    } else {
                        adj_v[5] += inc;
                        if (adj_v[5] < 2000) adj_v[5] = 2099;
                        if (adj_v[5] > 2099) adj_v[5] = 2000;
                    }
                }
                else if (kc == '\n' || kc == '\r') {
                    /* V2.96: 非法日期拒绝 (日 > 当月天数) */
                    if (adj_v[3] < 1 ||
                        adj_v[3] > calc_days_in_month(adj_v[5], adj_v[4])) {
                        adj_bad = 1;
                    } else {
                        extern void rtc_set_time(int, int, int, int, int, int);
                        rtc_set_time(adj_v[0], adj_v[1], adj_v[2],
                                     adj_v[3], adj_v[4], adj_v[5]);
                        adj_bad = 0;
                        adj_active = 0;
                        rtc_msg = 60;
                    }
                }
            }
            else if (kc == '1') mode = 0;
            else if (kc == '2') mode = 1;
            else if (kc == '3') mode = 2;
            else if (kc == '4') mode = 3;
            else if (kc == '5') mode = 4;
            else if (mode == 3 && (kc == 'w' || kc == 'W')) { if (wc_sel > 0) wc_sel--; }
            else if (mode == 3 && (kc == 's' || kc == 'S')) { if (wc_sel < wc_cnt - 1) wc_sel++; }
            else if (mode == 3 && (kc == '+' || kc == '=')) {
                int add = -1;
                for (int p = 0; p < 12 && add < 0; p++) {
                    int dup = 0;
                    for (int i = 0; i < wc_cnt; i++) {
                        int same = 1;
                        for (int j = 0; j < 9; j++) {
                            if (wc_pool_name[p][j] != wc_name[i][j]) { same = 0; break; }
                        }
                        if (same) { dup = 1; break; }
                    }
                    if (!dup) add = p;
                }
                if (add >= 0 && wc_cnt < WC_MAX) {
                    int j = 0;
                    while (wc_pool_name[add][j] && j < 8) { wc_name[wc_cnt][j] = wc_pool_name[add][j]; j++; }
                    wc_name[wc_cnt][j] = 0;
                    wc_off[wc_cnt] = wc_pool_off[add];
                    wc_cnt++;
                    wc_sel = wc_cnt - 1;
                    wc_save();
                }
            }
            else if (mode == 3 && (kc == '-' || kc == '_') && wc_cnt > 1) {
                for (int i = wc_sel; i < wc_cnt - 1; i++) {
                    int j = 0;
                    while (wc_name[i + 1][j] && j < 9) { wc_name[i][j] = wc_name[i + 1][j]; j++; }
                    wc_name[i][j] = 0;
                    wc_off[i] = wc_off[i + 1];
                }
                wc_cnt--;
                if (wc_sel >= wc_cnt) wc_sel = wc_cnt - 1;
                wc_save();
            }
            else if (mode == 0 && (kc == 't' || kc == 'T')) {
                /* V2.91: 进入校时 (以当前 RTC 为初值) */
                extern int rtc_get_time(int *, int *, int *, int *, int *, int *);
                if (rtc_get_time(&adj_v[0], &adj_v[1], &adj_v[2],
                                 &adj_v[3], &adj_v[4], &adj_v[5]) == 0) {
                    adj_active = 1; adj_field = 0;
                }
            }
            else if (mode == 1) {
                if (kc == 'w' || kc == 'W') { al_sel = (al_sel + 2) % 3; }
                else if (kc == 's' || kc == 'S') { al_sel = (al_sel + 1) % 3; }
                else if (kc == 'a' || kc == 'A') { al_m[al_sel] = (al_m[al_sel] + 59) % 60; alarm_save(); }
                else if (kc == 'd' || kc == 'D') { al_m[al_sel] = (al_m[al_sel] + 1) % 60; alarm_save(); }
                else if (kc == '[') { al_h[al_sel] = (al_h[al_sel] + 23) % 24; alarm_save(); }
                else if (kc == ']') { al_h[al_sel] = (al_h[al_sel] + 1) % 24; alarm_save(); }
                else if (kc == '\n' || kc == '\r') { al_armed[al_sel] = !al_armed[al_sel]; alarm_save(); }
                /* V3.02: 增删闹钟 */
                else if (kc == '+' || kc == '=') {
                    int fk = -1;
                    for (int k = 0; k < ALARM_MAX; k++)
                        if (al_h[k] == 0 && al_m[k] == 0 && !al_armed[k]) { fk = k; break; }
                    if (fk >= 0) { al_h[fk] = 7; al_m[fk] = 0; al_armed[fk] = 0; al_sel = fk; alarm_save(); }
                    else al_full = 40;
                }
                else if (kc == '-' || kc == '_') {
                    for (int k = al_sel; k < ALARM_MAX - 1; k++) {
                        al_h[k] = al_h[k + 1]; al_m[k] = al_m[k + 1]; al_armed[k] = al_armed[k + 1];
                    }
                    al_h[ALARM_MAX - 1] = 0; al_m[ALARM_MAX - 1] = 0; al_armed[ALARM_MAX - 1] = 0;
                    alarm_save();
                }
            } else if (mode == 2) {
                if (kc == 'w' || kc == 'W')
                    tm_left = (tm_left + 10 > 5999) ? 5999 : tm_left + 10;
                else if (kc == 's' || kc == 'S')
                    tm_left = (tm_left - 10 < 1) ? 1 : tm_left - 10;
                else if (kc == 'a' || kc == 'A')
                    tm_left = (tm_left + 1 > 5999) ? 5999 : tm_left + 1;
                else if (kc == 'd' || kc == 'D')
                    tm_left = (tm_left - 1 < 1) ? 1 : tm_left - 1;
                else if (kc == '\n' || kc == '\r') tm_running = !tm_running;
                else if (kc == 'c' || kc == 'C') { tm_running = 0; tm_left = tm_sec; }
            } else if (mode == 4) {
                /* V3.74: 秒表控制 */
                extern uint32_t pit_get_ticks(void);
                if (kc == ' ' || kc == '\n' || kc == '\r') {
                    if (!sw_on) { sw_t0 = pit_get_ticks(); sw_on = 1; }
                    else { sw_acc += (int)(pit_get_ticks() - sw_t0); sw_on = 0; }
                }
                else if (kc == 'l' || kc == 'L') {
                    if (sw_on) {
                        sw_lapms = sw_acc + (int)(pit_get_ticks() - sw_t0);
                        sw_laps++;
                    }
                }
                else if (kc == 'c' || kc == 'C') {
                    sw_on = 0; sw_acc = 0; sw_laps = 0; sw_lapms = 0;
                }
            }
        }
        for (volatile int d = 0; d < 150000; d++) ;
    }
}
