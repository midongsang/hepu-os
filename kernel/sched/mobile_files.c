/* ============================================================================
 * 赫普OS - 手机模式 - 文件管理器
 * ext2文件浏览 + 复制/剪切/粘贴 + 新建/重命名/删除
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

/* 共享函数 (定义在 mobile.c 中) */
extern void ui_draw_app_bg(void);
extern void ui_title(const char *zh, const char *en, int x);

/* 文件管理器辅助函数 (定义在 mobile.c 中) */
extern void fm_fmt_size(int size, char *buf, int bufsz);
extern void fm_join(const char *dir, const char *name, char *out, int outsz);
extern void fm_parent(const char *path, char *out, int outsz);
extern const char *fm_basename(const char *path);
extern int files_domain_granted(const char *path);
extern void fm_view_file(const char *dir, const char *name);

/* ext2文件系统函数 (已在 ide_ext2.h 中声明) */
extern int ide_ext2_mv(const char *src, const char *dst);
extern int ide_ext2_writefile(const char *path, const char *buf);

/* MAC安全检查 */
extern int sec_check_active_file(const char *dir, const char *name, unsigned char op);

void mobile_run_files(void)
{
    if (ide_ext2_mount() != 0) {
        int running = 1;
        while (running) {
            ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
            gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
            gfx_puts(4, 3, "Back: ESC", 0x0F);
            if (sys_lang_en) gfx_puts((gfx_width - 88) / 2, 60, "No disk detected", 0x0C);
            else gfx_puts_zh((gfx_width - 96) / 2, 60, "未检测到磁盘", 0x0C);
            if (sys_lang_en) gfx_puts((gfx_width - 176) / 2, 84, "Attach an ext2 disk on IDE", 0x07);
            else gfx_puts_zh((gfx_width - 200) / 2, 84, "请在 IDE 接口接入 ext2 磁盘", 0x07);
            gfx_flip();
            char kc;
            while (keyboard_try_getchar(&kc)) {
                if (kc == 0x1B) { running = 0; break; }
            }
            for (volatile int d = 0; d < 150000; d++) ;
        }
        return;
    }

    static char cur_path[300] = "/";
    /* V3.76: 剪贴板 + 输入状态 (新建文件夹/重命名) */
    static char fm_clip[300] = "";
    static int fm_clip_cut = 0, fm_clip_ok = 0;
    int fm_input = 0;          /* 0=无 1=新建文件夹 2=重命名 */
    static char fm_in[64];
    static int fm_in_len = 0;
    int running = 1;
    while (running) {
        fm_entry_t entries[FM_MAX_ENTRIES];
        int count = ide_ext2_list_entries(cur_path, entries, FM_MAX_ENTRIES);
        if (count < 0) count = 0;
        int sel = 0;
        int list_running = 1;
        while (list_running) {
            ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
            gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
            gfx_puts(4, 3, "ESC", 0x0F);
            ui_title("文件管理", "Files", 18);
            gfx_puts(4, 32, cur_path, 0x07);
            int visible = (gfx_height - 46) / 15;
            if (visible > count) visible = count;
            for (int i = 0; i < visible; i++) {
                int y = 46 + i * 15;
                uint8_t col = (i == sel) ? 0x0F : 0x07;
                gfx_puts(8, y, entries[i].is_dir ? "D" : "F", entries[i].is_dir ? 0x0A : 0x08);
                gfx_puts(24, y, entries[i].name, col);
                char sz[16];
                fm_fmt_size(entries[i].size, sz, 16);
                gfx_puts(gfx_width - 64, y, sz, 0x08);
            }
            if (fm_input > 0) {
                /* V3.76: 输入框 (新建文件夹/重命名) */
                gfx_fillrect(0, 38, gfx_width, 52, 0x00);
                gfx_rect(8, 40, gfx_width - 16, 18, 0x0F);
                gfx_puts(12, 45, fm_in, 0x0F);
                gfx_puts(4, 64, fm_input == 1 ? "New: Ent OK  ESC" : "Ren: Ent OK  ESC", 0x08);
            } else {
                gfx_puts(4, gfx_height - 14,
                         "W/S Ent D del U up N new R ren C cp X cut V paste", 0x07);
                if (fm_clip_ok)
                    gfx_puts(gfx_width - 90, gfx_height - 14,
                             fm_clip_cut ? "[cut]" : "[copy]", 0x0A);
            }
            gfx_flip();
            char kc;
            while (keyboard_try_getchar(&kc)) {
                if (fm_input > 0) {
                    /* V3.76: 输入模式 */
                    if (kc == 0x1B) { fm_input = 0; fm_in_len = 0; fm_in[0] = 0; }
                    else if (kc == '\r' || kc == '\n') {
                        if (fm_in_len > 0) {
                            if (fm_input == 1) {
                                static char nd[300];
                                fm_join(cur_path, fm_in, nd, 300);
                                ide_ext2_mkdir(nd);
                            } else {
                                static char ov2[300], nv2[300];
                                fm_join(cur_path, entries[sel].name, ov2, 300);
                                fm_join(cur_path, fm_in, nv2, 300);
                                if (strcmp(ov2, nv2) != 0) ide_ext2_mv(ov2, nv2);
                            }
                            ide_ext2_sync();
                        }
                        fm_input = 0; fm_in_len = 0; fm_in[0] = 0;
                        list_running = 0;
                        break;
                    }
                    else if (kc == 0x08 && fm_in_len > 0) { fm_in[--fm_in_len] = 0; }
                    else if (fm_in_len < 62 && kc >= 32 && kc < 127) {
                        fm_in[fm_in_len++] = (char)kc;
                        fm_in[fm_in_len] = 0;
                    }
                    continue;
                }
                if (kc == 0x1B) { list_running = 0; running = 0; break; }
                if ((kc == 'w' || kc == 'W') && sel > 0) sel--;
                if ((kc == 's' || kc == 'S') && sel < count - 1) sel++;
                if (kc == 'u' || kc == 'U') {
                    static char par[300];
                    fm_parent(cur_path, par, 300);
                    memcpy(cur_path, par, 300);
                    list_running = 0;
                    break;
                }
                if (kc == '\r' || kc == '\n') {
                    if (count == 0) continue;
                    if (entries[sel].is_dir) {
                        static char sub[300];
                        fm_join(cur_path, entries[sel].name, sub, 300);
                        /* V2.79: 数据域权限门 (Contacts/SMS/Phone) */
                        if (!files_domain_granted(sub)) {
                            int dr = 1;
                            while (dr) {
                                ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
                                gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
                                gfx_puts(4, 3, "Back: ESC", 0x0F);
                                if (sys_lang_en) gfx_puts((gfx_width - 152) / 2, 30, "Data domain access denied", 0x0C);
                                else gfx_puts_zh((gfx_width - 192) / 2, 30, "数据域访问被拒绝", 0x0C);
                                gfx_puts((gfx_width - 220) / 2, 60,
                                         "no Contacts/SMS/Phone grant", 0x0F);
                                gfx_puts((gfx_width - 230) / 2, 80,
                                         "grant in Settings -> Permissions", 0x08);
                                gfx_puts((gfx_width - 170) / 2, 108,
                                         "Back: ESC to return", 0x07);
                                gfx_flip();
                                char kc;
                                while (keyboard_try_getchar(&kc)) {
                                    if (kc == 0x1B) { dr = 0; break; }
                                }
                                for (volatile int d2 = 0; d2 < 150000; d2++) ;
                            }
                            continue;
                        }
                        memcpy(cur_path, sub, 300);
                        list_running = 0;
                        break;
                    } else {
                        /* V2.65: MAC 文件访问门卫 (当前活动应用域) */
                        if (sec_check_active_file(cur_path, entries[sel].name, 0x01)) {
                            fm_view_file(cur_path, entries[sel].name);
                        } else {
                            gfx_fillrect(0, gfx_height - 14, gfx_width, 14, 0x00);
                            gfx_puts(4, gfx_height - 14, "MAC: access denied (system file)", 0x0C);
                            gfx_flip();
                            for (volatile int d2 = 0; d2 < 900000; d2++) ;
                        }
                    }
                }
                /* V3.76: 复制/剪切/粘贴/新建/重命名 */
                if ((kc == 'c' || kc == 'C') && count > 0 && !entries[sel].is_dir) {
                    fm_join(cur_path, entries[sel].name, fm_clip, 300);
                    fm_clip_cut = 0; fm_clip_ok = 1;
                }
                else if ((kc == 'x' || kc == 'X') && count > 0 && !entries[sel].is_dir) {
                    fm_join(cur_path, entries[sel].name, fm_clip, 300);
                    fm_clip_cut = 1; fm_clip_ok = 1;
                }
                else if ((kc == 'v' || kc == 'V') && fm_clip_ok) {
                    static char dst2[300];
                    fm_join(cur_path, fm_basename(fm_clip), dst2, 300);
                    if (strcmp(fm_clip, dst2) == 0) {
                        gfx_fillrect(0, gfx_height - 14, gfx_width, 14, 0x00);
                        gfx_puts(4, gfx_height - 14, "same dir: no op", 0x0C);
                        gfx_flip();
                        for (volatile int d2 = 0; d2 < 600000; d2++) ;
                    } else if (fm_clip_cut) {
                        if (ide_ext2_mv(fm_clip, dst2) == 0) {
                            ide_ext2_sync();
                            fm_clip_ok = 0;
                            list_running = 0;
                            break;
                        }
                    } else {
                        static char sbuf[4096];
                        static char sdir[300], snm[200];
                        fm_parent(fm_clip, sdir, 300);
                        int l0 = 0;
                        while ((snm[l0] = fm_basename(fm_clip)[l0]) != 0) l0++;
                        int n = ide_ext2_read_file(sdir, snm, sbuf, 4095);
                        if (n > 0) {
                            if (ide_ext2_writefile(dst2, sbuf) >= 0) {
                                ide_ext2_sync();
                                fm_clip_ok = 0;
                                list_running = 0;
                                break;
                            }
                        }
                    }
                }
                else if (kc == 'n' || kc == 'N') {
                    fm_input = 1; fm_in_len = 0; fm_in[0] = 0;
                }
                else if (kc == 'r' || kc == 'R') {
                    if (count > 0) {
                        fm_input = 2;
                        int l = 0;
                        while (entries[sel].name[l] && l < 62) {
                            fm_in[l] = entries[sel].name[l]; l++;
                        }
                        fm_in_len = l; fm_in[l] = 0;
                    }
                }
                if (kc == 'd' || kc == 'D') {
                    if (count > 0) {
                        /* V2.65: MAC 删除门卫 (活动应用域无 DELETE 权限则拒绝) */
                        if (!sec_check_active_file(cur_path, entries[sel].name, 0x20)) {
                            gfx_fillrect(0, gfx_height - 14, gfx_width, 14, 0x00);
                            gfx_puts(4, gfx_height - 14, "MAC: delete denied", 0x0C);
                            gfx_flip();
                            for (volatile int d2 = 0; d2 < 900000; d2++) ;
                            continue;
                        }
                        if (ide_ext2_delete(cur_path, entries[sel].name) == 0) {
                            ide_ext2_sync();
                        }
                        list_running = 0;
                        break;
                    }
                }
            }
            for (volatile int d = 0; d < 150000; d++) ;
        }
    }
}
