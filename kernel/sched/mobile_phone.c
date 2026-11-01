/* ============================================================================
 * 赫普OS - 手机模式 - 电话应用
 * 拨号盘 + 通话记录 + 联系人联动
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
extern char calls_buf[512];
extern int calls_count;
extern char calls_nums[CALLS_MAX][16];
extern char calls_times[CALLS_MAX][8];
extern int calls_flags[CALLS_MAX];
extern int call_sel;
extern char phone_prefill_to[16];
extern int phone_prefill_active;

/* 共享函数 (定义在 mobile.c 中) */
extern void ui_draw_app_bg(void);
extern void ui_title(const char *zh, const char *en, int x);
extern uint8_t ui_border(void);
extern void calls_load_from_disk(void);
extern void calls_clear_all(void);
extern void calls_delete(int idx);
extern void calls_append(const char *num, int missed);
extern void calls_mark_answered_last(void);

/* 共享函数 (定义在 mobile_perm_mgr.c 中) */
extern int mobile_perm_request(const char *app, const char *pkg, int perm);
extern void mobile_perm_denied_page(const char *app, const char *perm);

void mobile_run_phone(void)
{
    /* V2.67: 电话权限门 (PHONE) */
    if (!mobile_perm_request("phone", "com.hepu.phone", PM_PERM_PHONE)) {
        mobile_perm_denied_page("phone", prm_perm_name(PM_PERM_PHONE));
        return;
    }
    char num[16];
    int num_len = 0;
    num[0] = 0;
    /* V3.60: 通讯录联动——进入即预填号码 */
    if (phone_prefill_active) {
        int i3 = 0;
        while (phone_prefill_to[i3] && i3 < 15) { num[i3] = phone_prefill_to[i3]; i3++; }
        num[i3] = 0; num_len = i3;
        phone_prefill_active = 0;
    }
    int running = 1;
    int calling = 0;
    /* V2.75: 联系人缓存 + 选中 (W/S 切换, Enter 填号拨号) */
    static char ct_names[3][24];
    static char ct_nums[3][16];
    static int ct_count = 0;
    int ct_sel = -1;
    /* V2.77: 通话记录视图 (C 切换, Enter 回拨, X 删除) */
    int view = 0;   /* 0=拨号盘 1=通话记录 */
    int call_sel = 0;
    int clear_arm = 0;   /* V2.79: D 键清空二次确认 */
    calls_load_from_disk();
    const char *phone_keys[12] = {
        "1", "2", "3",
        "4", "5", "6",
        "7", "8", "9",
        "*", "0", "#"
    };
    while (running) {
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "Back: ESC", 0x0F);
        ui_title("电话", "Phone", 18);
        if (view == 0) {
        /* 号码显示行 (紧凑: y 16-30) */
        gfx_fillrect(4, 16, gfx_width - 8, 14, 0x00);
        gfx_rect(4, 16, gfx_width - 8, 14, 0x0F);
        gfx_puts(8, 18, num_len ? num : (calling ? "Calling..." : "Enter number"), 0x0F);
        /* 拨号盘 3x4 (紧凑: y 96 起, 键高 16) */
        int kx0 = (gfx_width - (3 * 40)) / 2;
        int ky0 = 96;
        int kw = 36, kh = 16;
        for (int i = 0; i < 12; i++) {
            int kx = kx0 + (i % 3) * (kw + 4);
            int ky = ky0 + (i / 3) * (kh + 4);
            gfx_fillrect(kx, ky, kw, kh, 0x08);
            gfx_rect(kx, ky, kw, kh, 0x0F);
            gfx_puts(kx + kw / 2 - 4, ky + kh / 2 - 4, phone_keys[i], 0x0F);
        }
        /* 呼叫/挂断按钮 (紧凑: by 176) */
        int bx = (gfx_width - 2 * 56) / 2;
        int by = ky0 + 4 * (kh + 4);
        gfx_fillrect(bx, by, 56, 18, calling ? 0x0C : 0x0A);  /* 挂断红 / 呼叫绿 */
        gfx_puts(bx + 12, by + 5, calling ? "Hang up" : "Call", 0x0F);
        gfx_fillrect(bx + 64, by, 56, 18, 0x08);
        gfx_puts(bx + 76, by + 5, "Clear", 0x0F);
        gfx_puts(4, 32, "C: 通话记录", 0x08);
        /* V2.74/2.75: 联系人栏 (缓存 + 选中高亮, 需 CONTACTS 权限) */
        {
            extern int ide_ext2_read_file(const char *, const char *, char *, int);
            int pid = prm_app_register("phone", "com.hepu.phone");
            int cgrant = 0;
            prm_check_permission(pid, PM_PERM_CONTACTS, &cgrant);
            gfx_puts(4, 30, "Contacts:", 0x0B);
            if (cgrant) {
                /* 首次/权限恢复时加载 */
                if (ct_count == 0) {
                    char cb[256];
                    int cn = ide_ext2_read_file("/data/contacts", "contacts.txt", cb, 255);
                    int i = 0;
                    if (cn > 0) {
                        cb[cn] = 0;
                        char *line = cb;
                        while (line && i < 3) {
                            char *nl = strchr(line, '\n');
                            if (nl) *nl = 0;
                            char *sep = strchr(line, '|');
                            if (sep) {
                                *sep = 0;
                                int j = 0;
                                while (line[j] && j < 23) { ct_names[i][j] = line[j]; j++; }
                                ct_names[i][j] = 0;
                                j = 0;
                                while (sep[1 + j] && j < 15) { ct_nums[i][j] = sep[1 + j]; j++; }
                                ct_nums[i][j] = 0;
                                i++;
                            }
                            if (!nl) break;
                            line = nl + 1;
                        }
                    }
                    ct_count = i;
                }
                if (ct_count > 0) {
                    for (int i = 0; i < ct_count; i++) {
                        int y = 40 + i * 16;
                        if (i == ct_sel) gfx_fillrect(4, y, gfx_width - 8, 14, 0x01);
                        gfx_puts_zh(6, y + 1, ct_names[i], 0x0F);
                        gfx_puts(62, y + 1, ct_nums[i], 0x08);
                    }
                } else {
                    gfx_puts(6, 40, "no contact data", 0x08);
                }
            } else {
                gfx_puts(6, 40, "denied (Allow Contacts)", 0x0C);
            }
        }
        }
        if (view == 1) {
            /* V2.77: 通话记录视图 */
            ui_title("通话记录", "Call Log", 18);
            /* V2.79: 容量显示 x/8 */
            gfx_puts(gfx_width - 40, 18, "x/8", 0x08);
            if (calls_count == 0) {
                gfx_puts(8, 40, "no calls yet", 0x08);
            } else {
                for (int i = 0; i < calls_count; i++) {
                    int y = 40 + i * 20;
                    uint8_t bg = (i == call_sel) ? mob_accent : 0x08;
                    gfx_fillrect(4, y, gfx_width - 8, 18, bg);
                    gfx_rect(4, y, gfx_width - 8, 18, ui_border());   /* V3.49 */
                    if (calls_flags[i]) gfx_puts(10, y + 4, "!", 0x0C);  /* 未接红 ! */
                    gfx_puts(16, y + 4, calls_nums[i], calls_flags[i] ? 0x0B : 0x0F);
                    if (calls_flags[i])
                        gfx_puts(gfx_width - 96, y + 4, "MISSED", 0x0C);
                    gfx_puts(gfx_width - 52, y + 4, calls_times[i], 0x0A);
                }
                gfx_puts(4, 40 + calls_count * 20 + 2,
                         "Enter: 回拨  X: 删除  D: 清空  ESC: 返回", 0x08);
            }
            if (clear_arm)
                gfx_puts(4, gfx_height - 14, "D again: clear all!", 0x0C);
        }
        gfx_flip();
        /* 键盘输入 */
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) {
                if (view == 1) { view = 0; continue; }
                running = 0; break;
            }
            if (view == 1) {
                if (kc == 'w' || kc == 'W') { if (call_sel > 0) call_sel--; }
                else if (kc == 's' || kc == 'S') { if (call_sel < calls_count - 1) call_sel++; }
                else if (kc == 'd' || kc == 'D') {
                    /* V2.79: 清空全部 (二次确认) */
                    if (clear_arm) {
                        calls_clear_all();
                        calls_load_from_disk();
                        call_sel = 0;
                        clear_arm = 0;
                    } else {
                        clear_arm = 1;
                    }
                }
                else if (kc == 'x' || kc == 'X') {
                    calls_delete(call_sel);
                    calls_load_from_disk();
                    if (call_sel >= calls_count && call_sel > 0) call_sel--;
                }
                else if (kc == '\n' || kc == '\r') {
                    /* 回拨: 号码填入拨号框 */
                    int j = 0;
                    while (calls_nums[call_sel][j] && j < 14) { num[j] = calls_nums[call_sel][j]; j++; }
                    num[j] = 0;
                    num_len = j;
                    view = 0;
                }
                continue;
            }
            if (kc >= '0' && kc <= '9' && num_len < 14) {
                num[num_len++] = kc; num[num_len] = 0;
            } else if (kc == '*' && num_len < 14) {
                num[num_len++] = '*'; num[num_len] = 0;
            } else if (kc == '#' && num_len < 14) {
                num[num_len++] = '#'; num[num_len] = 0;
            } else if (kc == 'c' || kc == 'C') {
                /* V2.77: 切换通话记录 */
                calls_load_from_disk();
                if (call_sel >= calls_count) call_sel = calls_count > 0 ? calls_count - 1 : 0;
                view = 1;
            } else if (kc == 'w' || kc == 'W') {
                /* V2.75: 联系人上移 */
                if (ct_count > 0) ct_sel = (ct_sel <= 0) ? ct_count - 1 : ct_sel - 1;
            } else if (kc == 's' || kc == 'S') {
                /* V2.75: 联系人下移 */
                if (ct_count > 0) ct_sel = (ct_sel < 0 || ct_sel >= ct_count - 1) ? 0 : ct_sel + 1;
            } else if (kc == '\n' || kc == '\r') {
                if (ct_sel >= 0 && ct_sel < ct_count) {
                    /* V2.75: 选中联系人 -> 号码填入拨号框 */
                    int j = 0;
                    while (ct_nums[ct_sel][j] && j < 14) { num[j] = ct_nums[ct_sel][j]; j++; }
                    num[j] = 0;
                    num_len = j;
                    ct_sel = -1;
                } else if (calling) {
                    /* V2.81: 挂断 -> 最后一条记录标记已接 */
                    calling = 0; num_len = 0; num[0] = 0;
                    calls_mark_answered_last();
                    calls_load_from_disk();
                }
                else if (num_len > 0) { calling = 1; calls_append(num, 1); }
            } else if (kc == 'c' || kc == 'C') {
                num_len = 0; num[0] = 0;
            }
        }
        for (volatile int d = 0; d < 150000; d++) ;
    }
}
