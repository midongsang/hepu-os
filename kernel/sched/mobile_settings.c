/* ============================================================================
 * 赫普OS - 手机模式 - 设置应用
 * ========================================================================== */

#include "mobile.h"
#include "vga_gfx.h"
#include "vga.h"
#include "keyboard.h"
#include "font_zh.h"
#include "permission_manager.h"
#include "string.h"
#include <stdint.h>

/* 共享变量 (定义在 mobile.c 中) */
extern uint8_t mob_accent;
extern int sys_lang_en;
extern int ui_tron;
extern int mob_invert;
extern int mob_wallpaper;
extern int mob_datefmt;
extern int mob_time12;
extern int accent_idx;
extern const uint8_t accent_colors[6];
extern const char *accent_names[6];
extern int set_vals[SET_ITEMS];
extern const char *set_names[SET_ITEMS];

/* 共享函数 (定义在 mobile.c 中) */
extern void ui_draw_app_bg(void);
extern uint8_t ui_border(void);
extern const char *ui_tr(const char *zh, const char *en);
extern void gfx_set_invert(int inv);
extern void theme_save(void);
extern void tron_save(void);
extern void wallpaper_save(void);
extern void datefmt_save(void);
extern void accent_save(void);
extern void time12_save(void);
extern void lang_save(void);

/* 共享函数 (定义在其他模块文件中) */
extern void mobile_run_power(void);
extern void mobile_run_perm_mgr(void);
extern void mobile_run_privacy(void);

void mobile_run_settings(void)
{
    int running = 1;
    int sel = 0;
    int voff = 0;   /* V3.49: 视口滚动偏移 (13 项超出 320x200 屏幕) */
    const int vis = 10;   /* V3.49: 每屏可见行数 */
    while (running) {
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "Back: ESC", 0x0F);
        /* V3.63: 设置标题多语言 */
        if (sys_lang_en) gfx_puts((gfx_width - 44) / 2, 18, ui_tr("设置", "Settings"), 0x0B);
        else gfx_puts_zh((gfx_width - 48) / 2, 18, ui_tr("设置", "Settings"), 0x0B);
        if (voff > 0) gfx_puts(2, 20, "^", 0x08);          /* V3.49: 可上滚 */
        if (voff + vis < SET_ITEMS) gfx_puts(2, gfx_height - 14, "v", 0x08);   /* V3.49: 可下滚 */
        for (int i = voff; i < voff + vis && i < SET_ITEMS; i++) {
            /* V2.89/V3.49: 行高 18 (视口滚动) */
            int y = 30 + (i - voff) * 18;
            uint8_t bg = (i == sel) ? mob_accent : 0x08;
            gfx_fillrect(4, y, gfx_width - 8, 16, bg);
            gfx_rect(4, y, gfx_width - 8, 16, ui_border());   /* V3.49 */
            gfx_puts(8, y + 2, set_names[i], 0x0F);
            char val[24];
            if (i == 0 || i == 1) {
                gfx_puts(gfx_width - 40, y + 2, set_vals[i] ? "ON" : "OFF", 0x0B);
            } else if (i == 2) {
                gfx_puts(gfx_width - 40, y + 2, "60%", 0x0B);
            } else if (i == 3) {
                gfx_puts(gfx_width - 40, y + 2, "2.4G/8G", 0x0B);
            } else if (i == 4) {
                gfx_puts(gfx_width - 40, y + 2, "V1.02", 0x0B);
            } else if (i == 5) {
                /* V2.55 Power: 显示当前电量 */
                extern int pm_get_battery(void *);
                typedef struct { int present, charging, capacity, voltage, current,
                                 temperature, design_capacity, full_capacity, remaining_capacity; } pmb;
                pmb pb; int pp = 100;
                if (pm_get_battery(&pb) == 0) pp = pb.capacity;
                char pv[8]; pv[0]='0'+(pp/10)%10; pv[1]='0'+pp%10; pv[2]='%'; pv[3]=0;
                gfx_puts(gfx_width - 40, y + 2, pv, pp > 20 ? 0x0A : 0x0C);
            } else if (i == 6) {
                /* V2.70: Permissions —— 已注册应用数 */
                prm_app_perms_t apb[PM_MAX_APPS];
                int na = prm_app_list(apb, PM_MAX_APPS);
                char ac[8];
                ac[0] = (char)('0' + (na / 10) % 10);
                ac[1] = (char)('0' + na % 10);
                ac[2] = 0;
                gfx_puts(gfx_width - 40, y + 2, ac, 0x0B);
            } else if (i == 7) {
                /* V2.71: Privacy —— 日志条数 */
                prm_app_perm_stat_t psb[16];
                int nl = prm_log_stats(psb, 16);
                char ac[8];
                ac[0] = (char)('0' + (nl / 10) % 10);
                ac[1] = (char)('0' + nl % 10);
                ac[2] = 0;
                gfx_puts(gfx_width - 40, y + 2, ac, 0x0B);
            } else if (i == 8) {
                /* V2.89: 强调色 */
                gfx_puts(gfx_width - 60, y + 2, accent_names[accent_idx],
                         accent_colors[accent_idx]);
            } else if (i == 9) {
                /* V2.98: 时间制 */
                gfx_puts(gfx_width - 40, y + 2, mob_time12 ? "12H" : "24H", 0x0B);
            } else if (i == 10) {
                /* V2.99/V3.49: 主题 Dark / Light / TRON */
                gfx_puts(gfx_width - 40, y + 2,
                         ui_tron ? "TRON" : (mob_invert ? "Light" : "Dark"), 0x0B);
            } else if (i == 11) {
                /* V3.07: 壁纸 */
                static const char *wp_names[4] = {"Grad", "Sky", "Stripe", "Grid"};
                gfx_puts(gfx_width - 44, y + 2, wp_names[mob_wallpaper & 3], 0x0B);
            } else if (i == 12) {
                /* V3.09: 日期格式 */
                gfx_puts(gfx_width - 60, y + 2, mob_datefmt ? "M/D/Y" : "Y-M-D", 0x0B);
            } else if (i == 13) {
                /* V3.63: 系统语言 */
                gfx_puts(gfx_width - 44, y + 2, sys_lang_en ? "EN" : "中文", 0x0B);
            }
            (void)val;
        }
        /* 关于手机子页 */
        if (set_vals[4]) {
            gfx_fillrect(4, 36 + SET_ITEMS * 20 + 2, gfx_width - 8, 40, 0x00);
            gfx_puts(8, 36 + SET_ITEMS * 20 + 4, "HEPU OS V1.02", 0x0F);
            if (sys_lang_en) gfx_puts(8, 36 + SET_ITEMS * 20 + 14, "Hepu Hyper Evolve OS", 0x0B);
            else gfx_puts_zh(8, 36 + SET_ITEMS * 20 + 14, "赫普超进化系统", 0x0B);
            gfx_puts(8, 36 + SET_ITEMS * 20 + 24, "GPLv3  |  x86", 0x08);
        }
        gfx_flip();
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) { running = 0; break; }
            if (kc == 'w' || kc == 'W') {
                if (sel > 0) { sel--; if (sel < voff) voff = sel; }   /* V3.49: 视口联动 */
            }
            else if (kc == 's' || kc == 'S') {
                if (sel < SET_ITEMS - 1) { sel++; if (sel >= voff + vis) voff = sel - vis + 1; }   /* V3.49 */
            }
            else if (kc == '\n' || kc == '\r') {
                if (sel == 0 || sel == 1) set_vals[sel] = !set_vals[sel];
                else if (sel == 4) set_vals[4] = !set_vals[4];
                else if (sel == 5) mobile_run_power();
                else if (sel == 6) mobile_run_perm_mgr();
                else if (sel == 7) mobile_run_privacy();
                else if (sel == 8) accent_save();
                else if (sel == 9) { mob_time12 = !mob_time12; time12_save(); }
                else if (sel == 13) { sys_lang_en = !sys_lang_en; lang_save(); }
                else if (sel == 10) {
                    /* V3.55: 主题预览 (切换前确认) */
                    int psel = (ui_tron ? 2 : (mob_invert ? 1 : 0));
                    int prv = 1;
                    while (prv) {
                        ui_draw_app_bg();
                        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
                        gfx_puts(4, 3, "Back: ESC", 0x0F);
                        if (sys_lang_en) gfx_puts((gfx_width - 40) / 2, 18, "Themes", 0x0B);
                        else gfx_puts_zh((gfx_width - 48) / 2, 18, "主题预览", 0x0B);
                        int sx = 8, sw = 96, sh = 44;
                        for (int t = 0; t < 3; t++) {
                            int sy = 40 + t * 52;
                            gfx_fillrect(sx, sy, sw, sh, (t == 1) ? 0x0F : 0x00);
                            if (t == 2) {
                                for (int x = sx + 8; x < sx + sw; x += 8) gfx_vline(x, sy, sh, 0x02);
                                for (int y = sy + 8; y < sy + sh; y += 8) gfx_hline(sx, y, sw, 0x02);
                            }
                            gfx_puts(sx + 6, sy + 16, t == 0 ? "Dark" : (t == 1 ? "Light" : "TRON"), t == 1 ? 0x00 : 0x0B);
                            if (psel == t) gfx_rect(sx - 2, sy - 2, sw + 4, sh + 4, 0x0F);
                        }
                        gfx_puts(8, 40 + 3 * 52 + 2, "W/S select  Enter apply  ESC cancel", 0x08);
                        gfx_flip();
                        char pk;
                        while (keyboard_try_getchar(&pk)) {
                            if (pk == 0x1B) { prv = 0; }
                            else if (pk == 'w' || pk == 'W') { if (psel > 0) psel--; }
                            else if (pk == 's' || pk == 'S') { if (psel < 2) psel++; }
                            else if (pk == '\n' || pk == '\r') {
                                int want_tron = (psel == 2);
                                int want_inv = (psel == 1);
                                if (want_tron) { mob_invert = 0; gfx_set_invert(0); ui_tron = 1; mob_accent = 0x0B; }
                                else if (want_inv) { ui_tron = 0; mob_invert = 1; gfx_set_invert(1); }
                                else { ui_tron = 0; mob_invert = 0; gfx_set_invert(0); }
                                theme_save();
                                tron_save();
                                prv = 0;
                            }
                        }
                        for (volatile int d = 0; d < 150000; d++) ;
                    }
                }
                else if (sel == 11) {
                    mob_wallpaper = (mob_wallpaper + 1) & 3;
                    wallpaper_save();
                }
                else if (sel == 12) {
                    mob_datefmt = !mob_datefmt;
                    datefmt_save();
                }
            } else if (kc == 'a' || kc == 'A') {
                if (sel == 2 && set_vals[2] > 0) set_vals[2] -= 10;
                else if (sel == 8) { accent_idx = (accent_idx + 5) % 6; mob_accent = accent_colors[accent_idx]; }
            } else if (kc == 'd' || kc == 'D') {
                if (sel == 2 && set_vals[2] < 100) set_vals[2] += 10;
                else if (sel == 8) { accent_idx = (accent_idx + 1) % 6; mob_accent = accent_colors[accent_idx]; }
            }
        }
        for (volatile int d = 0; d < 150000; d++) ;
    }
}
