/* ============================================================================
 * 赫普OS - 移动桌面电源管理应用
 * 从 mobile.c 拆分
 * ========================================================================== */

#include "mobile.h"
#include "vga_gfx.h"
#include "font_zh.h"
#include "serial.h"
#include <string.h>

void mobile_run_power(void)
{
    int running = 1;
    int sel = 0;
    enum { P_SAVE, P_SLEEP, P_WAKE, P_TMO, P_NUM } ;
    while (running) {
        extern int pm_get_state(void);
        extern int pm_get_battery(void *);
        extern void pm_sleep(int);
        extern void pm_wakeup(void);
        extern void pm_set_sleep_timeout(int);
        extern int pm_get_sleep_timeout(void);
        typedef struct { int present, charging, capacity, voltage, current,
                         temperature, design_capacity, full_capacity, remaining_capacity; } pmb;
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "Back: ESC", 0x0F);
        if (sys_lang_en) gfx_puts((gfx_width - 36) / 2, 18, "Power", 0x0B);
        else gfx_puts_zh((gfx_width - 48) / 2, 18, "电源管理", 0x0B);
        pmb b;
        int pct = 100;
        if (pm_get_battery(&b) == 0) pct = b.capacity;
        int st = pm_get_state();
        const char *sn = (st == 0) ? "ON" : (st == 1) ? "IDLE" :
                         (st == 2) ? "STANDBY" : (st == 3) ? "SUSPEND" :
                         (st == 4) ? "HIBERNATE" : "OFF";
        /* 电池条 */
        gfx_rect(8, 42, 100, 16, 0x0F);
        uint8_t pcol = (pct > 20) ? 0x0A : (pct > 5) ? 0x0E : 0x0C;
        int pf = (pct * 98) / 100;
        if (pf < 1) pf = 1;
        gfx_fillrect(9, 43, pf, 14, pcol);
        /* 状态行 */
        gfx_puts(8, 66, "PM State:", 0x0F);
        gfx_puts(78, 66, sn, st == 3 ? 0x0C : 0x0B);
        if (sys_lang_en) gfx_puts(8, 80, "Batt", 0x0F); else gfx_puts_zh(8, 80, "电量", 0x0F);
        { char bb[8]; bb[0]='0'+(pct/10)%10; bb[1]='0'+pct%10; bb[2]='%'; bb[3]=0;
          gfx_puts(38, 80, bb, 0x0F); }
        if (sys_lang_en) gfx_puts(8, 94, "Chg", 0x0F); else gfx_puts_zh(8, 94, "充电", 0x0F);
        gfx_puts(38, 94, b.charging ? "CHG" : "DIS", b.charging ? 0x0A : 0x08);
        /* 操作项 */
        const char *ops[P_NUM] = {"Save-Power (tmo 30s)", "Sleep (S3)", "Wake (ON)", "Reset tmo 300s"};
        for (int i = 0; i < P_NUM; i++) {
            int y = 116 + i * 20;
            uint8_t bg = (i == sel) ? mob_accent : 0x08;
            gfx_fillrect(4, y, gfx_width - 8, 18, bg);
            gfx_rect(4, y, gfx_width - 8, 18, ui_border());   /* V3.49 */
            gfx_puts(8, y + 2, ops[i], (i == sel) ? 0x0F : 0x0B);
        }
        gfx_flip();
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) { running = 0; break; }
            if (kc == 'w' || kc == 'W') { if (sel > 0) sel--; }
            else if (kc == 's' || kc == 'S') { if (sel < P_NUM - 1) sel++; }
            else if (kc == '\n' || kc == '\r') {
                if (sel == P_SAVE) { pm_set_sleep_timeout(30); }
                else if (sel == P_SLEEP) { pm_sleep(3); }
                else if (sel == P_WAKE) { pm_wakeup(); }
                else if (sel == P_TMO) { pm_set_sleep_timeout(300); }
            }
        }
        for (volatile int d = 0; d < 150000; d++) ;
    }
}

/* V2.89: 强调色主题 (CMOS 0x38 持久化) */
const char *accent_names[6] = {"Blue", "Green", "Red", "Purple", "Cyan", "Yellow"};
const uint8_t accent_colors[6] = {0x01, 0x0A, 0x04, 0x0D, 0x03, 0x0E};
void accent_load(void)
{
    extern uint8_t cmos_read_byte(uint8_t);
    uint8_t c = cmos_read_byte(0x38);
    for (int i = 0; i < 6; i++) {
        if (c == accent_colors[i]) { mob_accent = c; accent_idx = i; return; }
    }
}
void accent_save(void)
{
    extern void cmos_write_byte(uint8_t, uint8_t);
    cmos_write_byte(0x38, mob_accent);
}

/* V2.98: 12 小时制 (CMOS 0x39 持久化) */
void time12_save(void)
{
    extern void cmos_write_byte(uint8_t, uint8_t);
    cmos_write_byte(0x39, mob_time12 ? 1 : 0);
}
void time12_load(void)
{
    extern uint8_t cmos_read_byte(uint8_t);
    mob_time12 = (cmos_read_byte(0x39) & 1);
}

/* V2.99: 浅色主题 (CMOS 0x3A 持久化 + gfx 反色) */
void theme_save(void)
{
    extern void cmos_write_byte(uint8_t, uint8_t);
    cmos_write_byte(0x3A, mob_invert ? 1 : 0);
}
void theme_load(void)
{
    extern uint8_t cmos_read_byte(uint8_t);
    mob_invert = (cmos_read_byte(0x3A) & 1);
    gfx_set_invert(mob_invert);
}

/* V3.49: TRON 主题 (CMOS 0x3C, 0=关 1=开) */
void tron_save(void)
{
    extern void cmos_write_byte(uint8_t, uint8_t);
    cmos_write_byte(0x3C, ui_tron ? 1 : 0);
}
void tron_load(void)
{
    extern uint8_t cmos_read_byte(uint8_t);
    ui_tron = (cmos_read_byte(0x3C) & 1);
}

/* V3.07: 壁纸 (CMOS 0x3B, 0=渐变 1=夜空 2=斜纹 3=网格) */
void wallpaper_save(void)
{
    extern void cmos_write_byte(uint8_t, uint8_t);
    cmos_write_byte(0x3B, mob_wallpaper & 3);
}
void wallpaper_load(void)
{
    extern uint8_t cmos_read_byte(uint8_t);
    mob_wallpaper = (cmos_read_byte(0x3B) & 3);
}

/* V3.09: 日期格式 (CMOS 0x3D, 0=YYYY-MM-DD 1=MM/DD/YYYY) */
void datefmt_save(void)
{
    extern void cmos_write_byte(uint8_t, uint8_t);
    cmos_write_byte(0x3D, mob_datefmt & 1);
}
void datefmt_load(void)
{
    extern uint8_t cmos_read_byte(uint8_t);
    mob_datefmt = (cmos_read_byte(0x3D) & 1);
}

/* V2.45: 设置中心 */
#define SET_ITEMS 14
const char *set_names[SET_ITEMS] = {"WiFi", "Bluetooth", "Brightness", "Storage", "About", "Power", "Permissions", "Privacy", "Accent", "TimeFmt", "Theme", "Wallpaper", "DateFmt", "Lang"};
int set_vals[SET_ITEMS] = {1, 0, 60, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};   /* .../强调色- /时间制- /主题- /壁纸- /语言- */

