/* ============================================================================
 * 赫普OS - 手机模式：通讯录模块 (从 mobile.c 拆分)
 * V3.35: 通讯录 (落盘 /data/contacts/contacts.txt, 格式 姓名|号码)
 * ========================================================================== */

#include "mobile.h"
#include "serial.h"
#include "ime.h"
#include <stdint.h>
#include "vga_gfx.h"
#include "vga.h"
#include "mouse.h"
#include "keyboard.h"
#include "pit.h"
#include "string.h"
#include "font_zh.h"
#include "wm.h"
#include "appreg.h"
#include "ide_ext2.h"
#include "ext2.h"
#include "permission_manager.h"

/* V3.35: 通讯录 (落盘 /data/contacts/contacts.txt, 格式 姓名|号码) */
#define CONTACTS_MAX 12
char contacts_names[CONTACTS_MAX][24];
char contacts_nums[CONTACTS_MAX][16];
/* V3.70: 联系人标签 (第三字段 姓名|号码|标签, 兼容旧数据无标签) */
#define CONTACTS_TAG_MAX 5
const char *contacts_tags_def[CONTACTS_TAG_MAX] = {"家人", "朋友", "同事", "工作", "其他"};
char contacts_tags[CONTACTS_MAX][8];
int contacts_tag_idx(const char *t)
{
    if (!t || !t[0]) return -1;
    for (int i = 0; i < CONTACTS_TAG_MAX; i++)
        if (strcmp(t, contacts_tags_def[i]) == 0) return i;
    return -1;
}
int contacts_count = 0;
char contacts_buf[2048];
int contacts_ime_on = 0;      /* V3.37: 通讯录新增页中文输入开关 */
/* V3.68: 通讯录多重轮转备份 (backup_0..backup_4, 保留 5 份历史) + 操作状态反馈 */
#define CONTACTS_BACKUP_MAX 5
char contacts_msg[40];
int contacts_msg_left = 0;
void contacts_msg_show(const char *m)
{
    int i = 0;
    while (m[i] && i < 38) { contacts_msg[i] = m[i]; i++; }
    contacts_msg[i] = 0;
    contacts_msg_left = 90;   /* 约 4-6 秒 (每主循环 ~50ms) */
}

void contacts_load(void)
{
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    int n = ide_ext2_read_file("/data/contacts", "contacts.txt", contacts_buf, 2047);
    if (n <= 0) { contacts_count = 0; return; }
    contacts_buf[n] = 0;
    int cnt = 0;
    char *line = contacts_buf;
    while (line && cnt < CONTACTS_MAX) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char *sep = strchr(line, '|');
        if (sep) {
            *sep = 0;
            int i = 0;
            while (line[i] && i < 23) { contacts_names[cnt][i] = line[i]; i++; }
            contacts_names[cnt][i] = 0;
            i = 0;
            char *num = sep + 1;
            while (num[i] && num[i] != '|' && i < 15) { contacts_nums[cnt][i] = num[i]; i++; }   /* V3.70: 停在 '|' 防第三字段污染号码 */
            contacts_nums[cnt][i] = 0;
            /* V3.70: 第三字段标签 (可选) */
            contacts_tags[cnt][0] = 0;
            char *sep2 = strchr(num, '|');
            if (sep2) {
                *sep2 = 0;
                int k = 0;
                while (sep2[1 + k] && k < 7) { contacts_tags[cnt][k] = sep2[1 + k]; k++; }
                contacts_tags[cnt][k] = 0;
            }
            cnt++;
        }
        if (!nl) break;
        line = nl + 1;
    }
    contacts_count = cnt;
}

void contacts_save(void)
{
    extern int ide_ext2_writefile(const char *, const char *);
    extern int ide_ext2_sync(void);
    char out[2048];
    int op = 0;
    for (int i = 0; i < contacts_count && op < 2000; i++) {
        int j = 0;
        while (contacts_names[i][j] && op < 2046) out[op++] = contacts_names[i][j++];
        out[op++] = '|';
        j = 0;
        while (contacts_nums[i][j] && op < 2046) out[op++] = contacts_nums[i][j++];
        if (contacts_tags[i][0]) {
            out[op++] = '|';
            j = 0;
            while (contacts_tags[i][j] && op < 2046) out[op++] = contacts_tags[i][j++];
        }
        out[op++] = '\n';
    }
    if (op > 0 && out[op - 1] == '\n') op--;
    out[op] = 0;
    ide_ext2_writefile("data/contacts/contacts.txt", out);
    ide_ext2_sync();
}

/* V3.68: 备份联系人 —— 轮转 5 份历史 (backup_0 最新 .. backup_4 最旧), 含时间戳头行
 * 旧版 V3.41 单文件 backup.txt 仅作恢复兼容读取 */
void contacts_backup(void)
{
    extern int ide_ext2_writefile(const char *, const char *);
    extern int ide_ext2_sync(void);
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    extern int rtc_get_time(int *, int *, int *, int *, int *, int *);
    char out[2048];
    int op = 0;
    /* 时间戳头行 #TS:HH:MM */
    {
        int rh = 0, rm = 0, rs = 0, rd = 0, rmo = 0, ry = 0;
        if (rtc_get_time(&rh, &rm, &rs, &rd, &rmo, &ry) == 0) {
            out[op++] = '#'; out[op++] = 'T'; out[op++] = 'S'; out[op++] = ':';
            out[op++] = (char)('0' + rh / 10); out[op++] = (char)('0' + rh % 10);
            out[op++] = ':';
            out[op++] = (char)('0' + rm / 10); out[op++] = (char)('0' + rm % 10);
            out[op++] = '\n';
        }
    }
    for (int i = 0; i < contacts_count && op < 2000; i++) {
        int j = 0;
        while (contacts_names[i][j] && op < 2044) out[op++] = contacts_names[i][j++];
        out[op++] = '|';
        j = 0;
        while (contacts_nums[i][j] && op < 2044) out[op++] = contacts_nums[i][j++];
        if (contacts_tags[i][0]) {
            out[op++] = '|';
            j = 0;
            while (contacts_tags[i][j] && op < 2044) out[op++] = contacts_tags[i][j++];
        }
        out[op++] = '\n';
    }
    if (op > 0 && out[op - 1] == '\n') op--;
    out[op] = 0;
    /* 轮转: 旧份后移 (backup_3->backup_4, ..., backup_0->backup_1), 新份写 backup_0 */
    for (int v = CONTACTS_BACKUP_MAX - 2; v >= 0; v--) {
        char name[24];
        strcpy(name, "backup_");
        name[7] = (char)('0' + v); name[8] = 0;
        strcat(name, ".txt");
        char tmp[2048];
        int n = ide_ext2_read_file("/data/contacts", name, tmp, 2047);
        if (n > 0) {
            tmp[n] = 0;
            char newf[40];
            strcpy(newf, "data/contacts/backup_");
            newf[21] = (char)('0' + v + 1); newf[22] = 0;   /* V3.68: "data/contacts/backup_" 21 字符, 数字在 [21] */
            strcat(newf, ".txt");
            ide_ext2_writefile(newf, tmp);
        }
    }
    ide_ext2_writefile("data/contacts/backup_0.txt", out);
    ide_ext2_sync();
    contacts_msg_show("Backup OK");
}

/* V3.68: 从备份恢复联系人 (优先最新 backup_0, 兼容旧版 backup.txt; 覆盖当前列表并落盘) */
void contacts_restore(void)
{
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    char buf[2048];
    int n = ide_ext2_read_file("/data/contacts", "backup_0.txt", buf, 2047);
    if (n <= 0) {
        n = ide_ext2_read_file("/data/contacts", "backup.txt", buf, 2047);
        if (n <= 0) { contacts_msg_show("No backup"); return; }
    }
    buf[n] = 0;
    int cnt = 0;
    char *line = buf;
    while (line && cnt < CONTACTS_MAX) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (line[0] != '#') {   /* V3.68: 跳过时间戳头行 */
            char *sep = strchr(line, '|');
            if (sep) {
                *sep = 0;
                int i = 0;
                while (line[i] && i < 23) { contacts_names[cnt][i] = line[i]; i++; }
                contacts_names[cnt][i] = 0;
                i = 0;
                char *num = sep + 1;
                while (num[i] && num[i] != '|' && i < 15) { contacts_nums[cnt][i] = num[i]; i++; }   /* V3.70: 停在 '|' 防第三字段污染号码 */
                contacts_nums[cnt][i] = 0;
                contacts_tags[cnt][0] = 0;
                char *sep2 = strchr(num, '|');
                if (sep2) {
                    *sep2 = 0;
                    int k = 0;
                    while (sep2[1 + k] && k < 7) { contacts_tags[cnt][k] = sep2[1 + k]; k++; }
                    contacts_tags[cnt][k] = 0;
                }
                cnt++;
            }
        }
        if (!nl) break;
        line = nl + 1;
    }
    contacts_count = cnt;
    contacts_save();
    contacts_msg_show("Restore OK");
}

/* V3.35: 号码 -> 联系人姓名 (未匹配返回 NULL) */
const char *contacts_name_of(const char *num)
{
    if (!num || !*num) return NULL;
    for (int i = 0; i < contacts_count; i++) {
        if (strcmp(contacts_nums[i], num) == 0) return contacts_names[i];
    }
    return NULL;
}

/* V3.46: 搜索历史 (/data/contacts/search_hist.txt, 最多 8 条) */
#define SEARCH_HIST_MAX 8
char search_hist[SEARCH_HIST_MAX][17];
int  search_hist_cnt = 0;

void search_hist_load(void)
{
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    char buf[1024];
    int n = ide_ext2_read_file("/data/contacts", "search_hist.txt", buf, 1023);
    if (n <= 0) return;
    buf[n] = 0;
    search_hist_cnt = 0;
    char *line = buf;
    while (line && search_hist_cnt < SEARCH_HIST_MAX) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (line[0]) {
            strncpy(search_hist[search_hist_cnt], line, 16);
            search_hist[search_hist_cnt][16] = 0;
            search_hist_cnt++;
        }
        if (!nl) break;
        line = nl + 1;
    }
}

void search_hist_save(void)
{
    extern int ide_ext2_writefile(const char *, const char *);
    extern int ide_ext2_sync(void);
    char out[1024];
    int op = 0;
    for (int i = 0; i < search_hist_cnt && op < 1000; i++) {
        int j = 0;
        while (search_hist[i][j] && op < 1018) out[op++] = search_hist[i][j++];
        if (op < 1018) out[op++] = '\n';
    }
    if (op > 0) { out[op] = 0; ide_ext2_writefile("data/contacts/search_hist.txt", out); ide_ext2_sync(); }
}

/* V3.46: 记录搜索词 (去重 + 最新置顶 + 上限 8) */
void search_hist_add(const char *q)
{
    if (!q || !q[0]) return;
    search_hist_load();
    /* 去重: 已存在则移除 */
    for (int i = 0; i < search_hist_cnt; i++) {
        if (strcmp(search_hist[i], q) == 0) {
            for (int j = i; j < search_hist_cnt - 1; j++)
                strcpy(search_hist[j], search_hist[j + 1]);
            search_hist_cnt--;
            break;
        }
    }
    /* 置顶 */
    for (int j = search_hist_cnt; j > 0 && j < SEARCH_HIST_MAX; j--)
        strcpy(search_hist[j], search_hist[j - 1]);
    if (search_hist_cnt < SEARCH_HIST_MAX) search_hist_cnt++;
    strncpy(search_hist[0], q, 16);
    search_hist[0][16] = 0;
    search_hist_save();
}

/* V3.40: 联系人搜索匹配 (姓名/号码子串, UTF-8 字节级) */
int contacts_match_q(const char *name, const char *num, const char *q)
{
    if (!q || !q[0]) return 1;
    if (name && strstr(name, q)) return 1;
    if (num && strstr(num, q)) return 1;
    return 0;
}

/* V3.35: 通讯录界面: 列表 / 详情 / 新增(ASCII) / 删除 */
void mobile_run_contacts(void)
{
    if (!mobile_perm_request("contacts", "com.hepu.contacts", PM_PERM_CONTACTS)) {
        mobile_perm_denied_page("contacts", prm_perm_name(PM_PERM_CONTACTS));
        return;
    }
    contacts_load();
    int running = 1;
    int sel = 0;
    int adding = 0;
    int edit_field = 0;          /* 0=姓名 1=号码 2=标签(V3.70) */
    char new_name[24], new_num[16], new_tag[8];
    int new_name_len = 0, new_num_len = 0;
    int detail = -1;             /* -1=列表 其他=详情索引 */
    /* V3.70: 分组筛选 (group_filter: -1=全部 0..4=标签 5=未分组) + 分组选择页 */
    int group_filter = -1, grouping = 0, group_sel = 0;
    new_name[0] = 0; new_num[0] = 0; new_tag[0] = 0;
    /* V3.40: 搜索态 */
    int searching = 0;
    char search_buf[17];
    int search_len = 0;
    int view[CONTACTS_MAX];
    int view_cnt = 0;
    int show_hist = 0, hist_sel = 0;    /* V3.46: 搜索历史 */
    search_buf[0] = 0;
    search_hist_load();
    while (running) {
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "Back: ESC", 0x0F);
        if (adding) {
            /* 新增页 */
            if (sys_lang_en) gfx_puts((gfx_width - 64) / 2, 18, "New Contact", 0x0B);
            else gfx_puts_zh((gfx_width - 48) / 2, 18, "新增联系人", 0x0B);
            gfx_puts(8, 32, "Name:", 0x0B);
            gfx_fillrect(52, 30, gfx_width - 60, 16, edit_field == 0 ? 0x01 : 0x00);
            gfx_rect(52, 30, gfx_width - 60, 16, 0x0F);
            if (new_name_len) gfx_puts_zh(56, 34, new_name, 0x0F);
            else if (contacts_ime_on && edit_field == 0) gfx_puts(56, 34, ime_pinyin(), 0x0F);  /* V3.37 */
            else gfx_puts(56, 34, "(ASCII)", 0x0F);
            /* V3.37: 中文输入候选条 (Name 字段 IME 开启时) */
            if (contacts_ime_on && edit_field == 0) {
                gfx_fillrect(0, 72, gfx_width, 20, 0x01);
                gfx_rect(0, 72, gfx_width, 20, 0x0F);
                if (sys_lang_en) gfx_puts(4, 76, "CN", 0x0B); else gfx_puts_zh(4, 76, "中", 0x0B);
                gfx_puts(16, 76, ime_pinyin(), 0x0A);
                int ci = 0, cx = 60;
                char w[IME_CHAR_MAX];
                while (ci < ime_candidate_count() && ci < 9 && cx < gfx_width - 16) {
                    if (ime_candidate_get(ci, w, IME_CHAR_MAX) == 0) {
                        char lb[4];
                        lb[0] = '0' + (ci + 1) % 10;
                        lb[1] = ':';
                        lb[2] = 0;
                        gfx_puts(cx, 76, lb, 0x0C);
                        gfx_puts_zh(cx + 10, 76, w, 0x0F);
                        cx += 10 + 8 * (int)strlen(w) + 12;
                    }
                    ci++;
                }
            }
            gfx_puts(8, 52, "Num:", 0x0B);
            gfx_fillrect(52, 50, gfx_width - 60, 16, edit_field == 1 ? 0x01 : 0x00);
            gfx_rect(52, 50, gfx_width - 60, 16, 0x0F);
            gfx_puts(56, 54, new_num_len ? new_num : "13800000000", 0x0F);
            /* V3.70: Tag 字段 (T 键循环) */
            gfx_puts(8, 92, "Tag:", 0x0B);
            gfx_fillrect(52, 90, gfx_width - 60, 16, edit_field == 2 ? 0x01 : 0x00);
            gfx_rect(52, 90, gfx_width - 60, 16, 0x0F);
            if (new_tag[0]) gfx_puts_zh(56, 94, new_tag, 0x0F);
            else gfx_puts(56, 94, "(none) T:cycle", 0x0F);
            gfx_puts(8, 112, "Enter: save   Tab: switch   T:tag   ESC: cancel", 0x0A);
        } else if (detail >= 0) {
            /* 详情页 */
            gfx_puts_zh((gfx_width - 48) / 2, 18, contacts_names[detail], 0x0B);
            gfx_puts(14, 40, contacts_nums[detail], 0x0F);
            /* V3.70: 详情页标签 + T 键切换 */
            gfx_puts(14, 56, "Tag:", 0x0B);
            if (contacts_tags[detail][0]) gfx_puts_zh(56, 56, contacts_tags[detail], 0x0B);
            else gfx_puts(56, 56, "(none)", 0x0F);
            gfx_puts(14, 72, "D: call   S: sms   X: del   T: tag", 0x0A);   /* V3.60 V3.70 */
        } else {
            /* 列表页 (V3.40: 搜索态 view 过滤) */
            gfx_puts_zh((gfx_width - 48) / 2, 18, searching ? "搜索联系人" : "通讯录", 0x0B);
            if (searching) {
                gfx_fillrect(4, 26, gfx_width - 8, 16, 0x01);
                gfx_rect(4, 26, gfx_width - 8, 16, 0x0F);
                gfx_puts(8, 30, search_len ? search_buf :
                         (contacts_ime_on && ime_pinyin_len() > 0 ? ime_pinyin() : "query..."), 0x0F);
                /* V3.46: 搜索历史列表 (H 键) */
                if (show_hist) {
                    if (sys_lang_en) gfx_puts((gfx_width - 48) / 2, 44, "History", 0x0B);
                    else gfx_puts_zh((gfx_width - 80) / 2, 44, "搜索历史", 0x0B);
                    for (int i = 0; i < search_hist_cnt && i < 8; i++) {
                        int y = 58 + i * 20;
                        uint8_t bg = (i == hist_sel) ? mob_accent : 0x08;
                        gfx_fillrect(4, y, gfx_width - 8, 18, bg);
                        gfx_rect(4, y, gfx_width - 8, 18, ui_border());   /* V3.49 */
                        gfx_puts(8, y + 2, search_hist[i], 0x0F);
                    }
                    gfx_puts(8, 58 + 20 * 8, "W/S:sel Enter:use X:del C:clear ESC:back", 0x0A);   /* V3.47 */
                    goto hist_drawn;
                }
                /* V3.44: 搜索态 IME 候选条 */
                if (contacts_ime_on && ime_pinyin_len() > 0) {
                    gfx_fillrect(0, 44, gfx_width, 20, 0x01);
                    gfx_rect(0, 44, gfx_width, 20, 0x0F);
                    if (sys_lang_en) gfx_puts(4, 48, "CN", 0x0B); else gfx_puts_zh(4, 48, "中", 0x0B);
                    gfx_puts(16, 48, ime_pinyin(), 0x0A);
                    int ci = 0, cx = 60;
                    char w[IME_CHAR_MAX];
                    while (ci < ime_candidate_count() && ci < 9 && cx < gfx_width - 16) {
                        if (ime_candidate_get(ci, w, IME_CHAR_MAX) == 0) {
                            char lb[4];
                            lb[0] = '0' + (ci + 1) % 10;
                            lb[1] = ':';
                            lb[2] = 0;
                            gfx_puts(cx, 48, lb, 0x0A);
                            gfx_puts_zh(cx + 12, 48, w, 0x0F);
                            int wlen = 0; while (w[wlen]) wlen++;
                            cx += 12 + (wlen / 3) * 16 + 12;
                        }
                        ci++;
                    }
                }
                view_cnt = 0;
                for (int i = 0; i < contacts_count && view_cnt < CONTACTS_MAX; i++)
                    if (contacts_match_q(contacts_names[i], contacts_nums[i], search_buf))
                        view[view_cnt++] = i;
            } else {
                /* V3.70: 分组过滤 (group_filter: -1=全部 0..4=标签 5=未分组) */
                view_cnt = 0;
                for (int i = 0; i < contacts_count && view_cnt < CONTACTS_MAX; i++) {
                    int ok = 1;
                    if (group_filter >= 0) {
                        int ti = contacts_tag_idx(contacts_tags[i]);
                        if (group_filter == CONTACTS_TAG_MAX) { if (ti >= 0) ok = 0; }
                        else if (ti != group_filter) ok = 0;
                    }
                    if (ok) view[view_cnt++] = i;
                }
            }
            int cnt = view_cnt;
            int list_y0 = (searching && contacts_ime_on && ime_pinyin_len() > 0) ? 66 : 46;
            /* V3.70: 分组选择页 */
            if (grouping) {
                if (sys_lang_en) gfx_puts((gfx_width - 88) / 2, 18, "Select Group", 0x0B);
                else gfx_puts_zh((gfx_width - 80) / 2, 18, "选择分组", 0x0B);
                const char *grp_sel[CONTACTS_TAG_MAX + 2];
                grp_sel[0] = "全部"; grp_sel[1] = "未分组";
                for (int gi = 0; gi < CONTACTS_TAG_MAX; gi++) grp_sel[gi + 2] = contacts_tags_def[gi];
                int gn = CONTACTS_TAG_MAX + 2;
                for (int gi = 0; gi < gn; gi++) {
                    int y = 40 + gi * 20;
                    uint8_t bg = (gi == group_sel) ? mob_accent : 0x08;
                    gfx_fillrect(4, y, gfx_width - 8, 18, bg);
                    gfx_rect(4, y, gfx_width - 8, 18, ui_border());
                    gfx_puts_zh(8, y + 2, grp_sel[gi], 0x0B);
                }
                gfx_puts(8, 40 + gn * 20, "W/S:sel Enter:ok ESC:back", 0x0A);
            } else {
                if (group_filter >= 0) {
                    const char *gn = (group_filter == CONTACTS_TAG_MAX) ? "未分组" : contacts_tags_def[group_filter];
                    gfx_puts(8, 34, "Group:", 0x0B);
                    gfx_puts_zh(72, 34, gn, 0x0B);
                }
                for (int i = 0; i < cnt && i < 8; i++) {
                    int di = view[i];
                    int y = list_y0 + i * 20;
                    uint8_t bg = (i == sel) ? mob_accent : 0x08;
                    gfx_fillrect(4, y, gfx_width - 8, 18, bg);
                    gfx_rect(4, y, gfx_width - 8, 18, ui_border());   /* V3.49 */
                    gfx_puts_zh(8, y + 2, contacts_names[di], 0x0B);
                    gfx_puts(76, y + 2, contacts_nums[di], 0x08);
                    /* V3.70: 标签徽标 */
                    if (contacts_tags[di][0])
                        gfx_puts_zh(208, y + 2, contacts_tags[di], 0x0C);
                }
            }
            /* V3.68: 备份/恢复状态反馈 (绿色, 自动消失) */
            if (!grouping && contacts_msg_left > 0)
                gfx_puts(8, list_y0 + 19 * 8, contacts_msg, 0x0A);
            if (searching)
                gfx_puts(8, list_y0 + 20 * 8, "W/S:sel Enter:open ESC:exit H:hist", 0x0A);   /* V3.46 */
            else
                gfx_puts(8, list_y0 + 20 * 8, "A:add X:del / :search B:backup R:restore G:group", 0x0A);   /* V3.41 V3.70 */
        }
        hist_drawn: ;
        gfx_flip();
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) {
                if (grouping) { grouping = 0; continue; }   /* V3.70 */
                if (adding) { adding = 0; new_name_len = 0; new_num_len = 0; new_name[0] = 0; new_num[0] = 0; break; }
                else if (detail >= 0) { detail = -1; break; }
                else if (!searching) { running = 0; break; }
                /* searching 态 ESC 交由 searching 分支处理 (V3.46: 记录搜索词), 不 break */
            }
            if (adding) {
                /* V3.37: 中文输入法切换 (~ / ` / \ 三键) */
                if (kc == '~' || kc == '`' || kc == '\\') {
                    contacts_ime_on = !contacts_ime_on;
                    ime_reset();
                }
                else if (kc == '\t') { edit_field = (edit_field + 1) % 3; ime_reset(); }   /* V3.70: 0/1/2 */
                else if (kc == '\n' || kc == '\r') {
                    if (edit_field == 0 && contacts_ime_on && ime_pinyin_len() > 0) {
                        /* 拼音 Enter: 候选 1 上屏 */
                        char w[IME_CHAR_MAX];
                        int bl = ime_commit_first(w, IME_CHAR_MAX);
                        if (bl > 0 && new_name_len + bl < 23) {
                            for (int i = 0; i < bl; i++) new_name[new_name_len++] = w[i];
                            new_name[new_name_len] = 0;
                            ime_bump(w);        /* V3.39 */
                        }
                        ime_reset();
                    }
                    else if (edit_field == 0) { edit_field = 1; }
                    else if (edit_field >= 1 && new_name_len > 0 && new_num_len > 0) {   /* V3.70: 号码或标签字段可保存 */
                        /* 保存 */
                        if (contacts_count < CONTACTS_MAX) {
                            int i = 0;
                            while (new_name[i] && i < 23) { contacts_names[contacts_count][i] = new_name[i]; i++; }
                            contacts_names[contacts_count][i] = 0;
                            i = 0;
                            while (new_num[i] && i < 15) { contacts_nums[contacts_count][i] = new_num[i]; i++; }
                            contacts_nums[contacts_count][i] = 0;
                            i = 0;
                            while (new_tag[i] && i < 7) { contacts_tags[contacts_count][i] = new_tag[i]; i++; }
                            contacts_tags[contacts_count][i] = 0;
                            contacts_count++;
                            contacts_save();
                        }
                        adding = 0; edit_field = 0; contacts_ime_on = 0;
                        new_name_len = 0; new_num_len = 0;
                        new_name[0] = 0; new_num[0] = 0; new_tag[0] = 0;
                    }
                }
                /* V3.70: T 键循环标签 (Tag 字段) */
                else if (kc == 't' || kc == 'T') {
                    if (edit_field == 2) {
                        int ti = contacts_tag_idx(new_tag) + 1;
                        if (ti >= CONTACTS_TAG_MAX) ti = -1;
                        if (ti < 0) new_tag[0] = 0;
                        else { strcpy(new_tag, contacts_tags_def[ti]); }
                    }
                }
                else if (edit_field == 0 && contacts_ime_on && ((kc >= 'a' && kc <= 'z') || kc == 0x08)) {
                    if (!ime_input(kc)) {
                        /* 拼音空时退格删姓名尾字符 (兼容 UTF-8) */
                        int bl = new_name_len;
                        if (bl > 0) {
                            int n = 1;
                            if ((unsigned char)new_name[bl - 1] >= 0x80) {
                                int st = bl - 1;
                                while (st > 0 && ((unsigned char)new_name[st] & 0xC0) == 0x80) st--;
                                n = bl - st;
                            }
                            new_name_len -= n;
                            new_name[new_name_len] = 0;
                        }
                    }
                }
                else if (edit_field == 0 && contacts_ime_on && kc == '0') {
                    ime_page_next();
                }
                else if (edit_field == 0 && contacts_ime_on && kc >= '1' && kc <= '9') {
                    char w[IME_CHAR_MAX];
                    if (ime_candidate_get(kc - '1', w, IME_CHAR_MAX) == 0) {
                        int bl = (int)strlen(w);
                        if (new_name_len + bl < 23) {
                            for (int i = 0; i < bl; i++) new_name[new_name_len++] = w[i];
                            new_name[new_name_len] = 0;
                            ime_bump(w);        /* V3.39 */
                        }
                        ime_reset();
                    }
                }
                else if (edit_field == 0 && contacts_ime_on && kc == ' ' && ime_candidate_count() > 0) {
                    char w[IME_CHAR_MAX];
                    int bl = ime_commit_first(w, IME_CHAR_MAX);
                    if (bl > 0 && new_name_len + bl < 23) {
                        for (int i = 0; i < bl; i++) new_name[new_name_len++] = w[i];
                        new_name[new_name_len] = 0;
                        ime_bump(w);        /* V3.39 */
                    }
                    ime_reset();
                }
                else if (kc == 0x08) {
                    if (edit_field == 0 && new_name_len > 0) new_name[--new_name_len] = 0;
                    if (edit_field == 1 && new_num_len > 0) new_num[--new_num_len] = 0;
                }
                else if (kc >= 0x20 && kc < 0x7F) {
                    /* V3.35: 姓名统一转小写 (物理键盘大写键在无 Shift 手机端易丢) */
                    char ch = (kc >= 'A' && kc <= 'Z') ? (char)(kc + 32) : kc;
                    if (edit_field == 0 && new_name_len < 23) new_name[new_name_len++] = ch;
                    if (edit_field == 1 && new_num_len < 15 && ch >= '0' && ch <= '9') new_num[new_num_len++] = ch;
                }
                continue;
            }
            if (detail >= 0) {
                if (kc == 's' || kc == 'S') {
                    /* V3.36: 给该联系人发短信 (预填收件人, 切到短信写短信) */
                    int i2 = 0;
                    while (contacts_nums[detail][i2] && i2 < 15) { sms_prefill_to[i2] = contacts_nums[detail][i2]; i2++; }
                    sms_prefill_to[i2] = 0;
                    sms_prefill_active = 1;
                    detail = -1;
                    mobile_run_sms();
                    sms_prefill_active = 0;
                }
                else if (kc == 'd' || kc == 'D') {
                    /* V3.60: 给该联系人拨号 (预填号码, 切到电话应用) */
                    int i4 = 0;
                    while (contacts_nums[detail][i4] && i4 < 15) { phone_prefill_to[i4] = contacts_nums[detail][i4]; i4++; }
                    phone_prefill_to[i4] = 0;
                    phone_prefill_active = 1;
                    detail = -1;
                    mobile_run_phone();
                    phone_prefill_active = 0;
                }
                else if (kc == 'x' || kc == 'X') {
                    /* 删除当前联系人 */
                    for (int i = detail; i < contacts_count - 1; i++) {
                        int j = 0;
                        while (contacts_names[i + 1][j]) { contacts_names[i][j] = contacts_names[i + 1][j]; j++; }
                        contacts_names[i][j] = 0;
                        j = 0;
                        while (contacts_nums[i + 1][j]) { contacts_nums[i][j] = contacts_nums[i + 1][j]; j++; }
                        contacts_nums[i][j] = 0;
                        j = 0;
                        while (contacts_tags[i + 1][j]) { contacts_tags[i][j] = contacts_tags[i + 1][j]; j++; }   /* V3.70 */
                        contacts_tags[i][j] = 0;
                    }
                    contacts_count--;
                    contacts_save();
                    detail = -1;
                    if (sel > contacts_count - 1 && sel > 0) sel--;
                }
                else if (kc == 't' || kc == 'T') {   /* V3.70: 详情页循环标签 */
                    int ti = contacts_tag_idx(contacts_tags[detail]) + 1;
                    if (ti >= CONTACTS_TAG_MAX) ti = -1;
                    if (ti < 0) contacts_tags[detail][0] = 0;
                    else strcpy(contacts_tags[detail], contacts_tags_def[ti]);
                    contacts_save();
                }
                else if (kc == '\n' || kc == '\r') detail = -1;
                continue;
            }
            /* 列表 (V3.40: 搜索态优先) */
            if (searching) {
                /* V3.46/47: 历史列表态优先 (X 删除 / C 清空 / Enter 复用) */
                if (show_hist) {
                    if (kc == 'w' || kc == 'W') { if (hist_sel > 0) hist_sel--; }
                    else if (kc == 's' || kc == 'S') { if (hist_sel < search_hist_cnt - 1) hist_sel++; }
                    else if (kc == '\n' || kc == '\r') {
                        if (hist_sel < search_hist_cnt) {
                            strncpy(search_buf, search_hist[hist_sel], 16);
                            search_buf[16] = 0;
                            search_len = (int)strlen(search_buf);
                            show_hist = 0; sel = 0;
                            search_hist_add(search_buf);
                        }
                    }
                    else if (kc == 'x' || kc == 'X') {
                        if (search_hist_cnt > 0 && hist_sel < search_hist_cnt) {
                            for (int j = hist_sel; j < search_hist_cnt - 1; j++)
                                strcpy(search_hist[j], search_hist[j + 1]);
                            search_hist_cnt--;
                            if (hist_sel >= search_hist_cnt) hist_sel = search_hist_cnt - 1;
                            if (hist_sel < 0) hist_sel = 0;
                            search_hist_save();
                        }
                    }
                    else if (kc == 'c' || kc == 'C') {
                        search_hist_cnt = 0;
                        search_hist_save();
                        show_hist = 0;
                    }
                    else if (kc == 0x1B) { show_hist = 0; }
                    continue;
                }
                /* V3.44: 搜索框中文输入 (IME) */
                if (kc == '~' || kc == '`' || kc == '\\') {
                    contacts_ime_on = !contacts_ime_on; ime_reset();
                }
                else if (kc == 'h' || kc == 'H') {
                    /* V3.46: H 键开搜索历史 (优先于拼音输入) */
                    if (contacts_ime_on && ime_pinyin_len() > 0) ime_reset();
                    show_hist = 1; hist_sel = 0;
                }
                else if (contacts_ime_on && kc >= 'a' && kc <= 'z') {
                    ime_input(kc);   /* V3.44: 拼音输入 (IME 开时字母全进拼音缓冲) */
                }
                else if (contacts_ime_on && ime_pinyin_len() > 0 && kc == '0') {
                    ime_page_next();
                }
                else if (contacts_ime_on && ime_pinyin_len() > 0 && kc >= '1' && kc <= '9') {
                    char w[IME_CHAR_MAX];
                    if (ime_candidate_get(kc - '1', w, IME_CHAR_MAX) == 0) {
                        int bl = (int)strlen(w);
                        if (search_len + bl < 16) {
                            for (int i = 0; i < bl; i++) search_buf[search_len++] = w[i];
                            search_buf[search_len] = 0;
                            ime_bump(w);
                            sel = 0;
                        }
                        ime_reset();
                    }
                }
                else if (contacts_ime_on && ime_pinyin_len() > 0 && kc == ' ') {
                    char w[IME_CHAR_MAX];
                    int bl = ime_commit_first(w, IME_CHAR_MAX);
                    if (bl > 0 && search_len + bl < 16) {
                        for (int i = 0; i < bl; i++) search_buf[search_len++] = w[i];
                        search_buf[search_len] = 0;
                        ime_bump(w);
                        sel = 0;
                    }
                    ime_reset();
                }
                else if (kc == 0x1B) {
                    if (contacts_ime_on && ime_pinyin_len() > 0) { ime_reset(); }
                    else if (show_hist) { show_hist = 0; }
                    else {
                        if (search_len > 0) search_hist_add(search_buf);   /* V3.46: 记录搜索词 */
                        searching = 0; search_len = 0; search_buf[0] = 0; sel = 0; contacts_ime_on = 0;
                    }
                }
                else if (kc == 0x08) {
                    if (contacts_ime_on && ime_pinyin_len() > 0) ime_input(0x08);
                    else if (search_len > 0) { search_buf[--search_len] = 0; sel = 0; }
                }
                else if (kc >= 0x20 && kc < 0x7F && search_len < 16) {
                    search_buf[search_len++] = kc; search_buf[search_len] = 0; sel = 0;
                }
                else if (kc == 'w' || kc == 'W') { if (sel > 0) sel--; }
                else if (kc == 's' || kc == 'S') { if (sel < view_cnt - 1) sel++; }
                else if (kc == '\n' || kc == '\r') {
                    if (view_cnt > 0 && sel < view_cnt) detail = view[sel];
                }
                continue;
            }
            if (kc == '/') {
                searching = 1; search_len = 0; search_buf[0] = 0; sel = 0;
                contacts_ime_on = 1; ime_reset();   /* V3.44: 搜索默认中文输入 */
            }
            else if (grouping) {   /* V3.70: 分组选择页按键 (优先于列表导航) */
                if (kc == 'w' || kc == 'W') { if (group_sel > 0) group_sel--; }
                else if (kc == 's' || kc == 'S') { if (group_sel < CONTACTS_TAG_MAX + 1) group_sel++; }
                else if (kc == '\n' || kc == '\r') {
                    if (group_sel == 0) group_filter = -1;
                    else if (group_sel == 1) group_filter = CONTACTS_TAG_MAX;
                    else group_filter = group_sel - 2;
                    grouping = 0; sel = 0;
                }
            }
            else if (kc == 'w' || kc == 'W') { if (sel > 0) sel--; }
            else if (kc == 's' || kc == 'S') { if (sel < contacts_count - 1) sel++; }
            else if (kc == 'a' || kc == 'A') {
                adding = 1; edit_field = 0;
                new_name_len = 0; new_num_len = 0;
                new_name[0] = 0; new_num[0] = 0;
            }
            else if (kc == 'b' || kc == 'B') { contacts_backup(); }        /* V3.41 */
            else if (kc == 'r' || kc == 'R') { contacts_restore(); if (sel > contacts_count - 1) sel = contacts_count - 1; if (sel < 0) sel = 0; }  /* V3.41 */
            else if (kc == 'x' || kc == 'X') {
                if (contacts_count > 0 && sel < contacts_count) {
                    for (int i = sel; i < contacts_count - 1; i++) {
                        int j = 0;
                        while (contacts_names[i + 1][j]) { contacts_names[i][j] = contacts_names[i + 1][j]; j++; }
                        contacts_names[i][j] = 0;
                        j = 0;
                        while (contacts_nums[i + 1][j]) { contacts_nums[i][j] = contacts_nums[i + 1][j]; j++; }
                        contacts_nums[i][j] = 0;
                        j = 0;
                        while (contacts_tags[i + 1][j]) { contacts_tags[i][j] = contacts_tags[i + 1][j]; j++; }   /* V3.70 */
                        contacts_tags[i][j] = 0;
                    }
                    contacts_count--;
                    contacts_save();
                    if (sel > contacts_count - 1 && sel > 0) sel--;
                }
            }
            else if (kc == 'g' || kc == 'G') {   /* V3.70: 分组筛选 */
                if (!searching) { grouping = 1; group_sel = 0; }
            }
            else if (kc == '\n' || kc == '\r') {
                if (contacts_count > 0 && sel < contacts_count) detail = sel;
            }
        }
        for (volatile int d = 0; d < 150000; d++) ;
        if (contacts_msg_left > 0) contacts_msg_left--;
    }
}

