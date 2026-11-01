/* ============================================================================
 * 赫普OS - 手机模式 - 权限管理
 * 两级: 应用列表 -> 应用权限详情; W/S 选择, Enter 切换授权, ESC 返回
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

/* 共享函数 (定义在 mobile.c 中) */
extern void ui_draw_app_bg(void);
extern uint8_t ui_border(void);
extern void gfx_puts_glow(int x, int y, const char *s, int c);

/* 共享函数 (定义在 mobile_common.c 中) */
extern const char *perm_en(int perm);
extern const char *perm_grant_name(int g);
extern const char *pkg_short(const char *pkg);

/* 第二级: 单个应用的权限详情页 (V2.70 显示全部权限可滚动; 修改自动持久化) */
#define PERM_DETAIL_ROWS 8
void mobile_run_perm_app(int app_id)
{
    prm_app_perms_t ap;
    if (prm_app_get(app_id, &ap) != 0) return;
    const char *title = pkg_short(ap.package);
    int running = 1, sel = 0;
    /* 权限总数 (CAMERA..DEVICE_ADMIN) */
    const int pcount = PM_PERM_DEVICE_ADMIN + 1;
    while (running) {
        if (sel > pcount - 1) sel = pcount - 1;
        int off = (sel / PERM_DETAIL_ROWS) * PERM_DETAIL_ROWS;
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "ESC:Back", 0x0F);
        gfx_puts((gfx_width - (int)strlen(title) * 8) / 2, 3, title, 0x0B);
        for (int i = 0; i < PERM_DETAIL_ROWS; i++) {
            int p = off + i;
            if (p >= pcount) break;
            int y = 36 + i * 16;
            uint8_t bg = (p == sel) ? mob_accent : 0x08;
            gfx_fillrect(4, y, gfx_width - 8, 14, bg);
            gfx_puts(8, y + 1, perm_en(p), (p == sel) ? 0x0F : 0x0B);
            uint8_t gc = (ap.grants[p] == PM_GRANT_DENIED) ? 0x08 : 0x0A;
            gfx_puts(gfx_width - 56, y + 1, perm_grant_name(ap.grants[p]), gc);
        }
        gfx_puts(8, gfx_height - 12, "Enter: Grant/Revoke", 0x08);
        gfx_flip();
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) { running = 0; break; }
            if (kc == 'w' || kc == 'W') { if (sel > 0) sel--; }
            else if (kc == 's' || kc == 'S') { if (sel < pcount - 1) sel++; }
            else if (kc == '\n' || kc == '\r') {
                /* Enter: 已授权 -> 撤销; 拒绝 -> 授予; 修改后立即持久化到磁盘 */
                if (ap.grants[sel] == PM_GRANT_DENIED)
                    prm_grant_permission(app_id, sel, PM_GRANT_ALLOWED);
                else
                    prm_revoke_permission(app_id, sel);
                prm_app_get(app_id, &ap);
                /* V2.70: UI 权限修改立即持久化到磁盘 */
                prm_save_state("/perm.dat");
            }
        }
        for (volatile int d = 0; d < 150000; d++) ;
    }
}

/* 第一级: 应用列表 */
void mobile_run_perm_mgr(void)
{
    int running = 1, sel = 0, off = 0;
    prm_app_brief_t apps[16];   /* 轻量条目, 栈安全 */
    while (running) {
        int napps = prm_app_list_brief(apps, 16);
        if (napps < 0) napps = 0;
        if (sel >= napps) sel = napps - 1;
        if (sel < 0) sel = 0;
        if (off > sel) off = sel;
        if (off < 0) off = 0;
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "ESC:Back", 0x0F);
        gfx_puts((gfx_width - 44) / 2, 3, "Permissions", 0x0B);
        if (napps == 0) {
            if (sys_lang_en) gfx_puts((gfx_width - 104) / 2, 60, "No apps registered", 0x08);
            else gfx_puts_zh((gfx_width - 72) / 2, 60, "暂无注册应用", 0x08);
        } else {
            int rows = (gfx_height - 16 - 14) / 20;   /* 可见行数 */
            if (rows > 8) rows = 8;
            for (int i = 0; i < rows; i++) {
                int idx = off + i;
                if (idx >= napps) break;
                int y = 36 + i * 20;
                uint8_t bg = (idx == sel) ? mob_accent : 0x08;
                gfx_fillrect(4, y, gfx_width - 8, 18, bg);
                gfx_rect(4, y, gfx_width - 8, 18, ui_border());   /* V3.49 */
                gfx_puts(8, y + 2, pkg_short(apps[idx].package), 0x0F);
                char cnts[16];
                int cnt = apps[idx].grant_count;
                cnts[0] = (char)('0' + cnt / 10 % 10);
                cnts[1] = (char)('0' + cnt % 10);
                cnts[2] = 0;
                gfx_puts(gfx_width - 24, y + 2, cnts, 0x0B);
            }
        }
        gfx_puts(8, gfx_height - 12, "Enter: Detail", 0x08);
        gfx_flip();
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) { running = 0; break; }
            if (kc == 'w' || kc == 'W') { if (sel > 0) sel--; }
            else if (kc == 's' || kc == 'S') { if (sel < napps - 1) sel++; }
            else if (kc == '\n' || kc == '\r') {
                if (napps > 0) mobile_run_perm_app(apps[sel].app_id);
            }
        }
        for (volatile int d = 0; d < 150000; d++) ;
    }
}

/* V2.66: 权限请求弹窗 —— 返回 1=允许 0=拒绝 2=仅此一次 */
int mobile_permission_prompt(const char *app, const char *perm)
{
    int sel = 0;   /* 0=允许 1=拒绝 2=仅此一次 */
    int running = 1;
    while (running) {
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "Back: ESC", 0x0F);
        /* V3.66: 权限弹窗多语言 */
        if (sys_lang_en) gfx_puts_glow((gfx_width - 80) / 2, 30, "Permission", 0x0E);
        else gfx_puts_zh((gfx_width - 96) / 2, 30, "权限请求", ui_tron ? 0x0B : 0x0E);
        gfx_puts((gfx_width - 170) / 2, 58, app, ui_tron ? 0x0B : 0x0F);
        char req[48];
        int ri = 0;
        const char *rs = sys_lang_en ? "requests access: " : "请求访问: ";
        while (rs[ri]) { req[ri] = rs[ri]; ri++; }
        const char *pm = perm;
        while (pm && *pm && ri < 46) { req[ri++] = *pm++; }
        req[ri] = 0;
        if (sys_lang_en) gfx_puts_glow((gfx_width - 190) / 2, 78, req, 0x0F);
        else gfx_puts_zh((gfx_width - 160) / 2, 78, req + 5, ui_tron ? 0x0B : 0x0F);
        gfx_rect((gfx_width - 224) / 2, 104, 222, 16, ui_border());   /* V3.52: TRON 选项框 */
        gfx_puts((gfx_width - 220) / 2, 108,
                 "[ Allow ] [ Deny ] [ Once ]", ui_tron ? 0x0B : 0x07);
        gfx_puts_glow((gfx_width - 220) / 2 + sel * 74, 108, ">", 0x0F);
        gfx_puts((gfx_width - 176) / 2, 132,
                 "A/D select   Enter confirm", ui_tron ? 0x0B : 0x08);
        gfx_flip();
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) return 0;
            if (kc == 'a' || kc == 'A') sel = 0;
            if (kc == 'd' || kc == 'D') sel = 1;
            if (kc == 'o' || kc == 'O') sel = 2;
            if (kc == '\n' || kc == '\r') { running = 0; break; }
        }
        for (volatile int d = 0; d < 150000; d++) ;
    }
    if (sel == 1) return 0;
    if (sel == 2) return 2;
    return 1;
}

/* V2.67: 通用权限请求——查/注册应用, 未授权弹窗, 会话内只弹一次 */
int mobile_perm_asked[PM_MAX_APPS];
int mobile_perm_request(const char *app, const char *pkg, int perm)
{
    int id = prm_app_register(app, pkg);
    if (id < 0) return 0;
    if (prm_request_permission(id, perm) == 1) return 1;
    if (id < PM_MAX_APPS && mobile_perm_asked[id]) return 0;
    if (id < PM_MAX_APPS) mobile_perm_asked[id] = 1;
    int r = mobile_permission_prompt(app, prm_perm_name(perm));
    if (r == 1) prm_grant_permission(id, perm, PM_GRANT_ALLOWED);
    else if (r == 2) prm_grant_onetime(id, perm, 60000);
    return prm_request_permission(id, perm);
}

/* V2.67: 权限被拒提示页——告知用户权限未授予 */
void mobile_perm_denied_page(const char *app, const char *perm)
{
    int running = 1;
    while (running) {
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "Back: ESC", 0x0F);
        if (sys_lang_en) gfx_puts_glow((gfx_width - 120) / 2, 30, "Permission Denied", 0x0C);
        else gfx_puts_zh((gfx_width - 96) / 2, 30, "权限被拒绝", 0x0C);
        gfx_puts((gfx_width - 120) / 2, 60, app, ui_tron ? 0x0B : 0x0F);
        if (sys_lang_en) gfx_puts((gfx_width - 60) / 2, 80, perm, ui_tron ? 0x0B : 0x0F);
        else gfx_puts_zh((gfx_width - 120) / 2, 80, perm, ui_tron ? 0x0B : 0x0F);
        gfx_puts((gfx_width - 230) / 2, 108,
                 "Permission denied - cannot use", ui_tron ? 0x0B : 0x08);
        gfx_puts((gfx_width - 170) / 2, 126,
                 "Back: ESC to return", ui_tron ? 0x0B : 0x07);
        gfx_flip();
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) { running = 0; break; }
        }
        for (volatile int d = 0; d < 150000; d++) ;
    }
}
