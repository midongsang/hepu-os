/* ============================================================================
 * 赫普OS - 手机模式 - 浏览器应用 (内置站点模拟 + 书签)
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

/* 共享函数 (定义在 mobile.c 中) */
extern void ui_draw_app_bg(void);
extern uint8_t ui_border(void);
extern void ui_title(const char *zh, const char *en, int x);

/* 共享函数 (定义在 mobile_perm_mgr.c 中) */
extern int mobile_permission_prompt(const char *app, const char *perm);

/* V2.48: 浏览器应用 (内置站点模拟) */
#define WEB_URL_MAX 32
char web_url[WEB_URL_MAX];
int web_url_len = 0;
int web_page = 0;   /* 0=home 1=about 2=os 3=news 4=mail 5=store 6=apps 7=404 */
/* V2.84: 内置站点 (7 个) */
#define WEB_SITES 7
const char *web_site_keys[WEB_SITES] =
    {"home", "about", "os", "news", "mail", "store", "apps"};
const char *web_site_titles[WEB_SITES] =
    {"HEPU HOME", "ABOUT", "HEPU OS PROJECT", "NEWS", "MAIL", "STORE", "APPS"};
const char *web_site_line2[WEB_SITES] = {
    "hepu hyper evolve os",
    "open source / GPLv3",
    "microkernel from scratch",
    "hepu news daily",
    "you have 0 unread",
    "2 apps available",
    "phone sms camera album"
};
/* V2.84: 书签 (落盘 /data/browser/bookmarks.txt) */
#define WEB_BOOKMARKS 8
char web_bm_names[WEB_BOOKMARKS][16];
int web_bm_count = 0;

/* V2.84: URL -> 站点匹配 (首字母优先 + 子串) */
int web_match(const char *u)
{
    if (!u || !u[0]) return 0;
    char c0 = u[0];
    if (c0 == 'h' || strstr(u, "hepu")) return 0;
    if (c0 == 'a' || strstr(u, "about")) return 1;
    if (c0 == 'o' || strstr(u, "os")) return 2;
    if (c0 == 'n' || strstr(u, "news")) return 3;
    if (c0 == 'm' || strstr(u, "mail")) return 4;
    if (c0 == 's' || strstr(u, "store")) return 5;
    if (c0 == 'p' || strstr(u, "app")) return 6;
    return 7;   /* 404 */
}

/* V2.84: 从磁盘加载书签 */
void web_bookmarks_load(void)
{
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    char buf[256];
    int n = ide_ext2_read_file("/data/browser", "bookmarks.txt", buf, 255);
    if (n <= 0) { web_bm_count = 0; return; }
    buf[n] = 0;
    int cnt = 0;
    char *line = buf;
    while (line && cnt < WEB_BOOKMARKS) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        int j = 0;
        while (line[j] && j < 15) { web_bm_names[cnt][j] = line[j]; j++; }
        web_bm_names[cnt][j] = 0;
        cnt++;
        if (!nl) break;
        line = nl + 1;
    }
    web_bm_count = cnt;
}

/* V2.84: 收藏当前页 (查重 + 上限 8 + 落盘) */
void web_bookmark_add(const char *url)
{
    extern int ide_ext2_writefile(const char *, const char *);
    extern int ide_ext2_sync(void);
    for (int i = 0; i < web_bm_count; i++)
        if (strcmp(web_bm_names[i], url) == 0) return;
    if (web_bm_count >= WEB_BOOKMARKS) return;
    int j = 0;
    while (url[j] && j < 15) { web_bm_names[web_bm_count][j] = url[j]; j++; }
    web_bm_names[web_bm_count][j] = 0;
    web_bm_count++;
    char buf[256];
    int p = 0;
    for (int i = 0; i < web_bm_count; i++) {
        for (int k = 0; web_bm_names[i][k] && p < 250; k++) buf[p++] = web_bm_names[i][k];
        if (p < 250) buf[p++] = '\n';
    }
    buf[p] = 0;
    ide_ext2_writefile("data/browser/bookmarks.txt", buf);
    ide_ext2_sync();
}

/* V3.59: 删除书签 (前移 + 落盘) */
void web_bookmark_del(int idx)
{
    extern int ide_ext2_writefile(const char *, const char *);
    extern int ide_ext2_sync(void);
    if (idx < 0 || idx >= web_bm_count) return;
    for (int i = idx; i < web_bm_count - 1; i++)
        strcpy(web_bm_names[i], web_bm_names[i + 1]);
    web_bm_count--;
    char buf[256];
    int p = 0;
    for (int i = 0; i < web_bm_count; i++) {
        for (int k = 0; web_bm_names[i][k] && p < 250; k++) buf[p++] = web_bm_names[i][k];
        if (p < 250) buf[p++] = '\n';
    }
    buf[p] = 0;
    ide_ext2_writefile("data/browser/bookmarks.txt", buf);
    ide_ext2_sync();
}

/* V2.84: 书签列表页 (W/S 选择, Enter 打开, ESC 返回) */
void mobile_run_bookmarks(void)
{
    int running = 1, sel = 0;
    while (running) {
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "ESC:Back", 0x0F);
        gfx_puts((gfx_width - 40) / 2, 3, "Bookmarks", 0x0B);
        if (web_bm_count == 0) {
            gfx_puts(8, 40, "no bookmarks (C to add)", 0x08);
        } else {
            for (int i = 0; i < web_bm_count; i++) {
                int y = 36 + i * 20;
                uint8_t bg = (i == sel) ? mob_accent : 0x08;
                gfx_fillrect(4, y, gfx_width - 8, 18, bg);
                gfx_rect(4, y, gfx_width - 8, 18, ui_border());   /* V3.49 */
                gfx_puts(10, y + 4, web_bm_names[i], 0x0F);
            }
        }
        gfx_puts(8, gfx_height - 12, "Enter: Open  X: Delete", 0x08);   /* V3.59 */
        gfx_flip();
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) { running = 0; break; }
            if (kc == 'w' || kc == 'W') { if (sel > 0) sel--; }
            else if (kc == 's' || kc == 'S') { if (sel < web_bm_count - 1) sel++; }
            else if (kc == 'x' || kc == 'X') {   /* V3.59: 删除书签 */
                if (sel < web_bm_count) {
                    web_bookmark_del(sel);
                    if (sel >= web_bm_count && sel > 0) sel--;
                }
            }
            else if (kc == '\n' || kc == '\r') {
                if (sel < web_bm_count) {
                    int j = 0;
                    while (web_bm_names[sel][j] && j < WEB_URL_MAX - 1) {
                        web_url[j] = web_bm_names[sel][j]; j++;
                    }
                    web_url[j] = 0;
                    web_url_len = j;
                    web_page = web_match(web_url);
                    running = 0;
                }
            }
        }
        for (volatile int d = 0; d < 150000; d++) ;
    }
}

void mobile_draw_webpage(void)
{
    ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
    gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
    gfx_puts(4, 3, "Back: ESC", 0x0F);
    ui_title("浏览器", "Browser", 18);
    /* 地址栏 */
    gfx_fillrect(8, 32, gfx_width - 16, 16, 0x00);
    gfx_rect(8, 32, gfx_width - 16, 16, 0x0F);
    gfx_puts(12, 36, web_url_len ? web_url : "enter url...", 0x0F);
    /* 导航按钮 */
    gfx_puts(8, 52, "[<]", 0x0B);
    gfx_puts(50, 52, "[>]", 0x0B);
    gfx_puts(90, 52, "[R]", 0x0B);
    /* 页面内容 */
    int cy = 68;
    if (web_page >= 0 && web_page < WEB_SITES) {
        gfx_puts(12, cy, web_site_titles[web_page], 0x0A);
        gfx_puts_zh(12, cy + 14, web_site_keys[web_page], 0x0F);
        gfx_puts(12, cy + 28, web_site_line2[web_page], 0x08);
    } else {
        gfx_puts(12, cy, "404 NOT FOUND", 0x0C);
        gfx_puts(12, cy + 14, "try: hepu about os news mail store apps", 0x08);
    }
    /* V2.84: 导航/书签提示 */
    gfx_puts(8, gfx_height - 14,
             "< > nav  R reload  C bookmark  B list", 0x08);
    gfx_flip();
}

void mobile_run_browser(void)
{
    /* V2.66: 浏览器首次启动请求相机权限 (Android 式权限流程) */
    static int browser_pm_id = -2;
    if (browser_pm_id == -2) {
        browser_pm_id = prm_app_register("browser", "com.hepu.browser");
    }
    if (browser_pm_id >= 0 &&
        prm_request_permission(browser_pm_id, PM_PERM_CAMERA) == 0) {
        int r = mobile_permission_prompt("browser", prm_perm_name(PM_PERM_CAMERA));
        if (r == 1) prm_grant_permission(browser_pm_id, PM_PERM_CAMERA, PM_GRANT_ALLOWED);
        else if (r == 2) prm_grant_onetime(browser_pm_id, PM_PERM_CAMERA, 60000);
    }
    int running = 1;
    web_url_len = 0;
    web_url[0] = 0;
    web_page = 0;
    web_bookmarks_load();   /* V2.84: 进入浏览器加载书签 */
    while (running) {
        mobile_draw_webpage();
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) { running = 0; break; }
            else if (kc == '\n' || kc == '\r') {
                web_page = web_match(web_url);   /* V2.84: 多站点加载 */
            }
            else if (kc == 0x08 && web_url_len > 0) {
                web_url[--web_url_len] = 0;
            }
            else if (kc == '<') { if (web_page > 0) web_page--; }        /* 后退 */
            else if (kc == '>') { if (web_page < WEB_SITES) web_page++; } /* 前进 */
            else if (kc == 'r' || kc == 'R') { }                          /* 刷新(重绘) */
            else if (kc == 'c' || kc == 'C') { web_bookmark_add(web_url); }
            else if (kc == 'b' || kc == 'B') { mobile_run_bookmarks(); }
            else if (kc >= 0x20 && kc < 0x7F && web_url_len < WEB_URL_MAX - 1) {
                web_url[web_url_len++] = kc;
                web_url[web_url_len] = 0;
            }
        }
        for (volatile int d = 0; d < 150000; d++) ;
    }
}
