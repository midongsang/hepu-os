/* ============================================================================
 * 赫普OS - 手机模式：短信模块 (从 mobile.c 拆分)
 * V2.44: 短信应用
 * V2.74: 短信数据从 /data/sms/sms.txt 磁盘加载
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

/* V2.44: 短信应用 */
#define SMS_COUNT 20   /* V3.30: 短信条数上限 8->20 (列表滚动) */
#define SMS_LIST_VIEW 7   /* V3.30: 列表可见项数 (内部高 200, 每项 20px) */
/* V2.74: 短信数据改从 /data/sms/sms.txt 磁盘加载 (SMS 权限门控数据) */
char sms_disk_buf[2048];   /* V3.29: 扩容 512->2048, 支持 8 条长短信 */
int sms_disk_count = 0;
const char *sms_senders[SMS_COUNT];
const char *sms_preview[SMS_COUNT];
int sms_dir[SMS_COUNT];            /* V3.31: 0=接收(R:) 1=发送(S:) */
char sms_times[SMS_COUNT][6];      /* V3.33: 每条收发时间 HH:MM (固定数组, 重载安全) */
int sms_open_idx = -1;
int sms_unread_flags[SMS_COUNT];   /* V2.78: 未读标记 (行首 '*' 持久化) */
int sms_view_map[SMS_COUNT];       /* V3.31: 列表视图序->磁盘序(同号聚合取最新) */
int sms_view_count = 0;
/* V2.80: sms_unread_count 定义移至文件顶部 (通知面板前向使用) */

/* V2.74: 从磁盘加载短信列表 (解析 "发件人|内容" 行) */
void sms_load_from_disk(void)
{
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    int n = ide_ext2_read_file("/data/sms", "sms.txt", sms_disk_buf, 2047);
    if (n <= 0) return;
    sms_disk_buf[n] = 0;
    int cnt = 0;
    sms_unread_count = 0;
    char *line = sms_disk_buf;
    while (line && cnt < SMS_COUNT) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char *sender = line;
        sms_unread_flags[cnt] = 0;
        sms_dir[cnt] = 0;
        if (sender[0] == '*') { sms_unread_flags[cnt] = 1; sms_unread_count++; sender++; }
        /* V3.31: 方向前缀 R:/S: (旧格式无前缀视为接收) */
        if (sender[0] == 'R' && sender[1] == ':') { sms_dir[cnt] = 0; sender += 2; }
        else if (sender[0] == 'S' && sender[1] == ':') { sms_dir[cnt] = 1; sender += 2; }
        /* V3.33: 时间字段 "HH:MM|" (新格式: R:14:30|号码|内容; 旧格式无时间) */
        sms_times[cnt][0] = 0;
        if (sender[2] == ':' && sender[5] == '|') {
            sms_times[cnt][0] = sender[0]; sms_times[cnt][1] = sender[1];
            sms_times[cnt][2] = sender[2]; sms_times[cnt][3] = sender[3];
            sms_times[cnt][4] = sender[4]; sms_times[cnt][5] = 0;
            sender += 6;
        }
        char *sep = strchr(sender, '|');
        if (sep) { *sep = 0; sms_senders[cnt] = sender; sms_preview[cnt] = sep + 1; cnt++; }
        if (!nl) break;
        line = nl + 1;
    }
    sms_disk_count = cnt;
    /* V3.31: 同号聚合 (每组取最新一条 = 磁盘最后一条) */
    sms_view_count = 0;
    for (int i = 0; i < cnt; i++) {
        int found = -1;
        for (int j = 0; j < sms_view_count; j++) {
            if (strcmp(sms_senders[sms_view_map[j]], sms_senders[i]) == 0) { found = j; break; }
        }
        if (found >= 0) sms_view_map[found] = i;
        else { sms_view_map[sms_view_count++] = i; }
    }
}

/* V2.78: 磁盘行编辑 (去第 idx 行未读标记, 或删除第 idx 行) */
void sms_edit_disk(int unmark_idx, int del_idx)
{
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    extern int ide_ext2_writefile(const char *, const char *);
    extern int ide_ext2_sync(void);
    char buf[2048], out[2048];
    int n = ide_ext2_read_file("/data/sms", "sms.txt", buf, 2047);
    if (n < 0) n = 0;
    buf[n] = 0;
    int op = 0, i = 0;
    char *line = buf;
    while (line && op < 500) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char *w = line;
        if (i == del_idx) { i++; if (!nl) break; line = nl + 1; continue; }   /* 删除 */
        if (i == unmark_idx && w[0] == '*') w++;                               /* 去未读 */
        while (*w && op < 499) out[op++] = *w++;
        out[op++] = '\n';
        i++;
        if (!nl) break;
        line = nl + 1;
    }
    if (op > 0 && out[op - 1] == '\n') op--;
    out[op] = 0;
    {
        extern int snprintf(char *, int, const char *, ...);
        char dbg[96];
        snprintf(dbg, 95, "[SMSED] un=%d del=%d len=%d\n", unmark_idx, del_idx, op);
        serial_puts(0x3F8, dbg);
    }
    int wr = ide_ext2_writefile("data/sms/sms.txt", out);
    {
        extern int snprintf(char *, int, const char *, ...);
        char dbg[96];
        snprintf(dbg, 95, "[SMSED] wr=%d\n", wr);
        serial_puts(0x3F8, dbg);
    }
    ide_ext2_sync();
}

static void sms_mark_read(int idx)
{
    if (idx < 0 || idx >= sms_disk_count) return;
    if (!sms_unread_flags[idx]) return;   /* 已读无操作 */
    sms_edit_disk(idx, -1);
}

/* V3.33: 打开会话 -> 该号码全部标记已读 */
void sms_mark_read_peer(const char *peer)
{
    if (!peer || !*peer) return;
    for (int i = 0; i < sms_disk_count; i++) {
        if (strcmp(sms_senders[i], peer) == 0) sms_mark_read(i);
    }
}

void sms_delete(int idx)
{
    if (idx < 0 || idx >= sms_disk_count) return;
    sms_edit_disk(-1, idx);
}

/* V2.76: 短信追加落盘 (回复/新短信 -> /data/sms/sms.txt) */
void sms_append(const char *to, const char *body)
{
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    extern int ide_ext2_writefile(const char *, const char *);
    extern int ide_ext2_sync(void);
    extern int snprintf(char *, int, const char *, ...);
    char dbg[128];
    snprintf(dbg, 127, "[SMSAP] to=%d body=%d\n",
             to ? (int)strlen(to) : -1, body ? (int)strlen(body) : -1);
    serial_puts(0x3F8, dbg);
    if (!to || !*to || !body || !*body) {
        serial_puts(0x3F8, "[SMSAP] reject empty\n");
        return;
    }
    char buf[2048];
    int n = ide_ext2_read_file("/data/sms", "sms.txt", buf, 2047);
    if (n < 0) n = 0;
    buf[n] = 0;
    int p = strlen(buf);   /* 实际内容长度 (read_file 已补 0) */
    if (p < 0 || p > 2000) p = 0;
    /* V3.29: 超过容量上限(留 200B 新条目空间)时拒绝新增, 不再重写丢历史 */
    int need = (int)strlen(to) + (int)strlen(body) + 10;  /* S:(2)+时间(5)+|(2)+内容+\n */
    if (p + need > 2000) {
        serial_puts(0x3F8, "[SMSAP] full, reject new\n");
        return;
    }
    /* V3.29: 短信行数达 SMS_COUNT 上限时拒绝新增(磁盘不无限增长) */
    {
        int lines = 0;
        for (int i = 0; i < p; i++) if (buf[i] == '\n') lines++;
        if (lines >= SMS_COUNT) {
            serial_puts(0x3F8, "[SMSAP] count full, reject new\n");
            return;
        }
    }
    buf[p++] = 'S'; buf[p++] = ':';   /* V3.31: 发送方向标记 */
    /* V3.33: 写入 RTC 时间 HH:MM */
    {
        extern int rtc_get_time(int *, int *, int *, int *, int *, int *);
        int th = 0, tm = 0, ts = 0, td = 0, tmo = 0, ty = 0;
        rtc_get_time(&th, &tm, &ts, &td, &tmo, &ty);
        buf[p++] = '0' + th / 10; buf[p++] = '0' + th % 10;
        buf[p++] = ':';
        buf[p++] = '0' + tm / 10; buf[p++] = '0' + tm % 10;
        buf[p++] = '|';
    }
    while (p < 2000 && *to) buf[p++] = *to++;
    buf[p++] = '|';
    while (p < 2000 && *body) buf[p++] = *body++;
    buf[p++] = '\n';
    buf[p] = 0;
    int w = ide_ext2_writefile("data/sms/sms.txt", buf);
    snprintf(dbg, 127, "[SMSAP] w=%d len=%d\n", w, p);
    serial_puts(0x3F8, dbg);
    ide_ext2_sync();
}

/* V2.76: 首次启动写入欢迎短信 (幂等, 验证落盘链路) */
void sms_seed_welcome(void)
{
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    char buf[512];
    int n = ide_ext2_read_file("/data/sms", "sms.txt", buf, 511);
    if (n <= 0) return;
    buf[n] = 0;
    int lines = 0;
    for (int i = 0; i < n; i++)
        if (buf[i] == '\n') lines++;
    if (lines >= 4) return;   /* 已有 4+ 条则跳过 (3 demo + 欢迎) */
    sms_append("10086", "欢迎使用赫普OS");
}

/* V2.47: QWERTY 全键盘行布局 */
const char *qkbd_rows[4] = {
    "qwertyuiop",
    "asdfghjkl",
    "zxcvbnm",
    "space,del,ent"
};
#define SMS_IN_MAX 40
char sms_compose_to[16];
int sms_compose_to_len = 0;
char sms_compose_body[SMS_IN_MAX];
int sms_compose_body_len = 0;
int sms_composing = 0;
int sms_ime_on = 0;   /* V3.25: 短信编写中文输入法开关(~ 切换) */
int sms_reply_idx = -1;  /* V2.76: 详情页回复预填发件人 */
int sms_fwd_idx = -1;   /* V3.34: 会话页转发预填内容 */
char sms_prefill_to[16];     /* V3.36: 通讯录->短信预填收件人 */
int sms_prefill_active = 0;  /* V3.36 */

/* 绘制 QWERTY 键盘 (底部区域) */
void mobile_draw_qkbd(void)
{
    int kb_y = gfx_height - 64;
    int key_h = 14;
    int key_w = (gfx_width - 16) / 10;  /* 10 列 */
    for (int r = 0; r < 3; r++) {
        const char *row = qkbd_rows[r];
        int n = 0; while (row[n]) n++;
        int off = (gfx_width - n * (key_w + 2)) / 2;
        for (int c = 0; c < n; c++) {
            int kx = off + c * (key_w + 2);
            int ky = kb_y + r * (key_h + 2);
            gfx_fillrect(kx, ky, key_w, key_h, 0x08);
            gfx_rect(kx, ky, key_w, key_h, 0x08);
            char ch[2] = {row[c], 0};
            gfx_puts(kx + key_w / 2 - 3, ky + key_h / 2 - 4, ch, 0x0F);
        }
    }
    /* 底行: 空格/删除/回车 */
    int by = kb_y + 3 * (key_h + 2);
    int bw = (gfx_width - 16) / 3;
    gfx_fillrect(8, by, bw, key_h, 0x08);  gfx_puts(8 + bw/2 - 12, by + 3, "Space", 0x0F);
    gfx_fillrect(8 + bw + 4, by, bw, key_h, 0x0C);  gfx_puts(8 + bw + 4 + bw/2 - 12, by + 3, "Del", 0x0F);
    gfx_fillrect(8 + 2*(bw + 4), by, bw, key_h, 0x0A);  gfx_puts(8 + 2*(bw+4) + bw/2 - 12, by + 3, "Send", 0x0F);
}

/* V2.47: 写新短信 (QWERTY 输入) */
void mobile_run_compose(void)
{
    sms_composing = 1;
    sms_compose_to_len = 0;
    sms_compose_body_len = 0;
    sms_compose_to[0] = 0;
    sms_compose_body[0] = 0;
    /* V2.76: 回复预填收件人 */
    int field = 0;  /* 0=收件人 1=内容 */
    if (sms_reply_idx >= 0 && sms_reply_idx < sms_disk_count) {
        int j = 0;
        while (sms_senders[sms_reply_idx][j] && j < 14) {
            sms_compose_to[j] = sms_senders[sms_reply_idx][j];
            j++;
        }
        sms_compose_to[j] = 0;
        sms_compose_to_len = j;
        sms_reply_idx = -1;
        field = 1;   /* V2.76: 回复预填收件人后直接输入内容 */
    }
    /* V3.34: 转发预填内容 (To 留空, 直输号码) */
    if (sms_fwd_idx >= 0 && sms_fwd_idx < sms_disk_count) {
        int j = 0;
        while (sms_preview[sms_fwd_idx][j] && j < 200) {
            sms_compose_body[j] = sms_preview[sms_fwd_idx][j];
            j++;
        }
        sms_compose_body[j] = 0;
        sms_compose_body_len = j;
        sms_fwd_idx = -1;
        field = 0;   /* 转发后先输收件人 */
    }
    /* V3.36: 通讯录联动预填收件人 (reply/fwd 优先级更高) */
    if (sms_prefill_active) {
        int j = 0;
        while (sms_prefill_to[j] && j < 14) {
            sms_compose_to[j] = sms_prefill_to[j];
            j++;
        }
        sms_compose_to[j] = 0;
        sms_compose_to_len = j;
        sms_prefill_active = 0;
        field = 1;   /* 预填收件人后直接输入内容 */
    }
    int sent = 0;
    while (sms_composing) {
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "Back: ESC", 0x0F);
        ui_title("写短信", "New SMS", 18);
        /* 收件人行 */
        gfx_puts(8, 32, "To:", 0x0B);
        gfx_fillrect(36, 30, gfx_width - 44, 16, field == 0 ? 0x01 : 0x00);
        gfx_rect(36, 30, gfx_width - 44, 16, 0x0F);
        gfx_puts_zh(40, 34, sms_compose_to_len ? sms_compose_to : "recipient", 0x0F);  /* V3.26: UTF-8 中文收件人 */
        /* 内容行 */
        gfx_puts(8, 52, "Msg:", 0x0B);
        gfx_fillrect(36, 50, gfx_width - 44, 16, field == 1 ? 0x01 : 0x00);
        gfx_rect(36, 50, gfx_width - 44, 16, 0x0F);
        gfx_puts_zh(40, 54, sms_compose_body_len ? sms_compose_body : "message...", 0x0F);  /* V3.26: UTF-8 中文内容 */
        if (sent) gfx_puts(8, 72, "Message sent!", 0x0A);
        /* QWERTY 键盘 */
        /* V3.25: 中文输入法候选条（键盘上方） */
        if (sms_ime_on) {
            gfx_fillrect(0, gfx_height - 86, gfx_width, 20, 0x01);
            gfx_rect(0, gfx_height - 86, gfx_width, 20, 0x0F);
            if (sys_lang_en) gfx_puts(4, gfx_height - 82, "CN", 0x0B); else gfx_puts_zh(4, gfx_height - 82, "中", 0x0B);
            int nc = ime_candidate_count();
            if (nc > 0) {
                gfx_puts(16, gfx_height - 82, ime_pinyin(), 0x0A);
                int x = 60;
                for (int ci = 0; ci < IME_PAGE_SIZE; ci++) {   /* V3.27: 页内候选 */
                    char w[IME_CHAR_MAX];
                    if (ime_candidate_get(ci, w, IME_CHAR_MAX) == 0) {
                        /* V3.28: 词组候选宽度 = 汉字数*16 */
                        int wlen = 0; while (w[wlen]) wlen++;
                        int wn = wlen / 3;
                        if (x + 8 + wn * 16 + 6 > gfx_width - 28) break; /* 留页码位 */
                        char num[2]; num[0] = (char)('1' + ci); num[1] = 0;
                        gfx_puts(x, gfx_height - 82, num, 0x08);
                        gfx_puts_zh(x + 8, gfx_height - 82, w, 0x0F);
                        x += 8 + wn * 16 + 4;
                    }
                }
                int pgc_n = ime_page_index() + 1;    /* V3.27: 页码指示 1/3 */
                int pgc_d = ime_page_count();
                if (pgc_d > 1) {
                    char pgc[4];
                    pgc[0] = (char)('0' + pgc_n); pgc[1] = '/';
                    pgc[2] = (char)('0' + pgc_d); pgc[3] = 0;
                    gfx_puts(gfx_width - 26, gfx_height - 82, pgc, 0x08);  /* V3.28: 右端固定 */
                }
            } else {
                gfx_puts(16, gfx_height - 82, "pinyin...", 0x08);
            }
        }
        mobile_draw_qkbd();
        gfx_flip();
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) { sms_composing = 0; break; }
            if (kc == 0x09) { field = !field; continue; }  /* Tab 切字段 */
            if (field == 0) {
                if (kc >= 0x20 && kc < 0x7F && sms_compose_to_len < 14) {
                    sms_compose_to[sms_compose_to_len++] = kc;
                    sms_compose_to[sms_compose_to_len] = 0;
                } else if (kc == 0x08 && sms_compose_to_len > 0) {
                    sms_compose_to[--sms_compose_to_len] = 0;
                } else if (kc == '\n' || kc == '\r') {
                    field = 1;   /* V2.76: To 字段 Enter 切到内容 */
                    continue;
                }
            } else {
                /* V3.25: 中文输入法（~ 切换中/英, 拼音->候选->选字上屏） */
                if (kc == '~' || kc == '`' || kc == '\\') {  /* V3.25: 真机/~ / \\ 三键均可切换 */
                    sms_ime_on = !sms_ime_on;
                    ime_reset();
                }
                else if (sms_ime_on && ((kc >= 'a' && kc <= 'z') || kc == 0x08)) {
                    if (!ime_input(kc)) {
                        /* 拼音空时退格删正文最后一字符(兼容 UTF-8) */
                        int bl = sms_compose_body_len;
                        if (bl > 0) {
                            int n = 1;
                            if ((unsigned char)sms_compose_body[bl-1] >= 0x80) {
                                int st = bl - 1;
                                while (st > 0 && ((unsigned char)sms_compose_body[st] & 0xC0) == 0x80) st--;
                                n = bl - st;
                            }
                            sms_compose_body_len -= n;
                            sms_compose_body[sms_compose_body_len] = 0;
                        }
                    }
                }
                else if (sms_ime_on && kc == '0') {   /* V3.27: 0 键候选翻页 */
                    ime_page_next();
                }
                else if (sms_ime_on && kc >= '1' && kc <= '9') {
                    char w[IME_CHAR_MAX];
                    if (ime_candidate_get(kc - '1', w, IME_CHAR_MAX) == 0) {
                        int bl = (int)strlen(w);
                        if (sms_compose_body_len + bl < SMS_IN_MAX) {
                            for (int i = 0; i < bl; i++)
                                sms_compose_body[sms_compose_body_len++] = w[i];
                            sms_compose_body[sms_compose_body_len] = 0;
                            ime_bump(w);        /* V3.39: 记录使用频率 */
                        }
                        ime_reset();
                    }
                }
                else if (sms_ime_on && kc == ' ' && ime_candidate_count() > 0) {
                    char w[IME_CHAR_MAX];
                    int bl = ime_commit_first(w, IME_CHAR_MAX);
                    if (bl > 0 && sms_compose_body_len + bl < SMS_IN_MAX) {
                        for (int i = 0; i < bl; i++)
                            sms_compose_body[sms_compose_body_len++] = w[i];
                        sms_compose_body[sms_compose_body_len] = 0;
                        ime_bump(w);        /* V3.39 */
                    }
                    ime_reset();
                }
                else if (sms_ime_on && (kc == '\n' || kc == '\r')) {
                    if (ime_pinyin_len() > 0) {
                        char w[IME_CHAR_MAX];
                        int bl = ime_commit_first(w, IME_CHAR_MAX);
                        if (bl > 0 && sms_compose_body_len + bl < SMS_IN_MAX) {
                            for (int i = 0; i < bl; i++)
                                sms_compose_body[sms_compose_body_len++] = w[i];
                            sms_compose_body[sms_compose_body_len] = 0;
                            ime_bump(w);        /* V3.39 */
                        }
                        ime_reset();
                    } else if (sms_compose_to_len > 0 && sms_compose_body_len > 0) {
                        /* V2.76: 落盘持久化 */
                        sms_append(sms_compose_to, sms_compose_body);
                        sent = 1;
                        sms_composing = 0;   /* 返回列表显示新条目 */
                    }
                }
                else {
                    /* V3.25: 英文模式(默认) */
                    if (kc >= 0x20 && kc < 0x7F && sms_compose_body_len < SMS_IN_MAX - 1) {
                        sms_compose_body[sms_compose_body_len++] = kc;
                        sms_compose_body[sms_compose_body_len] = 0;
                    } else if (kc == 0x08 && sms_compose_body_len > 0) {
                        sms_compose_body[--sms_compose_body_len] = 0;
                    } else if (kc == '\n' || kc == '\r') {
                        if (sms_compose_to_len > 0 && sms_compose_body_len > 0) {
                            sms_append(sms_compose_to, sms_compose_body);
                            sent = 1;
                            sms_composing = 0;
                        }
                    }
                }
            }
        }
        for (volatile int d = 0; d < 150000; d++) ;
    }
}

/* V3.31: 会话视图 (同号码往来记录, R/S 方向分色, Enter 回复)
 * V3.32: 长对话滚动窗口 + 单条选择删除 (X) / 清空会话 (C) */
#define CONV_VIEW 4   /* V3.38: 会话可见条数 (每条 2 行, 长内容折行) 每行 16px, 内部高 200) */
/* V3.38: 长内容折 2 行 (按显示宽度: 中文16px ASCII8px, 内容区 76..616 可用 540) */
void sms_wrap2(const char *text, char *l1, int c1, char *l2, int c2)
{
    const int MAXW = 540;
    int w = 0, i = 0, j1 = 0, j2 = 0;
    while (text[i]) {
        int chw = ((unsigned char)text[i] >= 0x80) ? 16 : 8;
        if (w + chw > MAXW) break;
        int bl = 1;
        if ((unsigned char)text[i] >= 0x80) {
            bl = 1;
            while (((unsigned char)text[i + bl] & 0xC0) == 0x80) bl++;
        }
        if (j1 + bl < c1) {
            for (int t = 0; t < bl; t++) l1[j1++] = text[i + t];
            i += bl;
            w += chw;
        } else break;
    }
    l1[j1] = 0;
    w = 0;
    while (text[i]) {
        int chw = ((unsigned char)text[i] >= 0x80) ? 16 : 8;
        if (w + chw > MAXW) break;
        int bl = 1;
        if ((unsigned char)text[i] >= 0x80) {
            bl = 1;
            while (((unsigned char)text[i + bl] & 0xC0) == 0x80) bl++;
        }
        if (j2 + bl + 1 < c2) {
            for (int t = 0; t < bl; t++) l2[j2++] = text[i + t];
            i += bl;
            w += chw;
        } else break;
    }
    if (text[i]) {   /* 还有剩余: 尾部加 … (U+2026 UTF-8) */
        if (j2 + 3 < c2) { l2[j2++] = 0xE2; l2[j2++] = 0x80; l2[j2++] = 0xA6; }
    }
    l2[j2] = 0;
}

void mobile_run_conversation(int disk_sel)
{
    /* V3.32: peer 复制到局部缓冲 (sms_load_from_disk 重载会移动 sms_disk_buf 内字符串,
     * 直接持指针会在删除/回复后失效指向错误号码) */
    char peer[16];
    int pj = 0;
    while (sms_senders[disk_sel][pj] && pj < 15) { peer[pj] = sms_senders[disk_sel][pj]; pj++; }
    peer[pj] = 0;
    int running = 1;
    int conv_sel = 0;        /* V3.32: 会话内选择 (相对该号码第几条) */
    int conv_top = 0;        /* V3.32: 会话滚动窗口起点 */
    int ct_down = 0, ct_last = 0;   /* V3.43: 触屏拖动滚动 */
    while (running) {
        /* 重新统计该号码记录数 (删除后刷新) */
        int n = 0;
        for (int i = 0; i < sms_disk_count; i++)
            if (strcmp(sms_senders[i], peer) == 0) n++;
        if (conv_sel > n - 1) conv_sel = n - 1;
        if (conv_sel < 0) conv_sel = 0;
        if (conv_top > n - CONV_VIEW) conv_top = n - CONV_VIEW;
        if (conv_top < 0) conv_top = 0;
        if (conv_sel < conv_top) conv_top = conv_sel;
        if (conv_sel >= conv_top + CONV_VIEW) conv_top = conv_sel - CONV_VIEW + 1;

        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "Back: ESC", 0x0F);
        /* V3.35: 会话标题显示联系人姓名 */
        const char *cname = contacts_name_of(peer);
        gfx_puts_zh((gfx_width - 48) / 2, 18, cname ? cname : peer, 0x0B);
        /* 记录序号 (仅展示用) */
        if (n > 0) {
            char hdr[16];
            hdr[0] = '0' + (conv_sel + 1) / 10; hdr[1] = '0' + (conv_sel + 1) % 10;
            hdr[2] = '/'; hdr[3] = '0' + n / 10; hdr[4] = '0' + n % 10; hdr[5] = 0;
            gfx_puts(gfx_width - 32, 18, hdr, 0x08);
        }
        /* 该号码第 k 条 (k 从 0) — V3.38: 每条 2 行 (长内容折行) */
        int k = 0;
        int y = 36;
        for (int i = 0; i < sms_disk_count && k < conv_top + CONV_VIEW; i++) {
            if (strcmp(sms_senders[i], peer) != 0) continue;
            if (k >= conv_top) {
                char l1[64], l2[64];
                sms_wrap2(sms_preview[i], l1, 64, l2, 64);
                if (k == conv_sel) {
                    gfx_fillrect(2, y, gfx_width - 4, 31, mob_accent);
                    gfx_rect(2, y, gfx_width - 4, 31, 0x0F);
                }
                gfx_puts(6, y + 2, sms_dir[i] ? "S>" : "R>", sms_dir[i] ? 0x0A : 0x0C);
                /* V3.33: 时间 (8x16 ASCII) */
                if (sms_times[i][0]) gfx_puts(28, y + 2, sms_times[i], 0x07);
                gfx_puts_zh(76, y + 2, l1, 0x0F);
                if (l2[0]) gfx_puts_zh(76, y + 18, l2, 0x07);
                y += 32;
            }
            k++;
        }
        gfx_puts(6, gfx_height - 16, "W/S:sel X:del C:clear F:fwd Enter:reply", 0x0A);
        gfx_flip();
        /* V3.43: 触屏/鼠标拖动滚动 (会话页) */
        {
            uint8_t tbtn = mouse_get_buttons();
            int tmy = mouse_get_y();
            if (tbtn & WM_MOUSE_LEFT) {
                if (!ct_down) { ct_down = 1; ct_last = tmy; }
                else {
                    int dy = tmy - ct_last;
                    if (dy < -15) { if (conv_sel < n - 1) conv_sel++; ct_last = tmy; }
                    else if (dy > 15) { if (conv_sel > 0) conv_sel--; ct_last = tmy; }
                }
            } else ct_down = 0;
        }
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) { running = 0; break; }
            if (kc == 'w' || kc == 'W') { if (conv_sel > 0) conv_sel--; }
            else if (kc == 's' || kc == 'S') { if (conv_sel < n - 1) conv_sel++; }
            else if (kc == '\n' || kc == '\r') {
                sms_reply_idx = disk_sel;
                mobile_run_compose();
                sms_load_from_disk();
                running = 0;
                break;
            }
            else if (kc == 'x' || kc == 'X') {
                /* V3.32: 删除选中单条 (定位该号码第 conv_sel 条) */
                int k2 = 0;
                for (int i = 0; i < sms_disk_count; i++) {
                    if (strcmp(sms_senders[i], peer) != 0) continue;
                    if (k2 == conv_sel) { sms_delete(i); break; }
                    k2++;
                }
                sms_load_from_disk();
                /* 已删除最后一条则上移选择 */
                if (n <= 1) { running = 0; break; }
                if (conv_sel > n - 2) conv_sel--;
            }
            else if (kc == 'f' || kc == 'F') {
                /* V3.34: 转发选中单条 (内容预填, 直输收件人) */
                int k2 = 0;
                for (int i = 0; i < sms_disk_count; i++) {
                    if (strcmp(sms_senders[i], peer) != 0) continue;
                    if (k2 == conv_sel) { sms_fwd_idx = i; break; }
                    k2++;
                }
                mobile_run_compose();
                sms_load_from_disk();
                running = 0;
                break;
            }
            else if (kc == 'c' || kc == 'C') {
                /* V3.32: 清空该号码全部记录 */
                for (int i = sms_disk_count - 1; i >= 0; i--) {
                    if (strcmp(sms_senders[i], peer) == 0) sms_delete(i);
                }
                sms_load_from_disk();
                running = 0;
                break;
            }
        }
        for (volatile int d = 0; d < 150000; d++) ;
    }
}

void mobile_run_sms(void)
{
    /* V2.67: 短信权限门 (SMS) */
    if (!mobile_perm_request("sms", "com.hepu.sms", PM_PERM_SMS)) {
        mobile_perm_denied_page("sms", prm_perm_name(PM_PERM_SMS));
        return;
    }
    /* V2.74: 权限通过后从磁盘加载短信数据 */
    sms_load_from_disk();
    contacts_load();                    /* V3.35: 联系人姓名联动 */
    int running = 1;
    int sel = 0;
    int scroll_top = 0;                 /* V3.30: 列表滚动窗口起点 */
    int sl_tdown = 0, sl_tlast = 0;     /* V3.45: 列表触屏拖动滚动 */
    /* V3.58: 短信列表搜索 (/ 过滤发送者/姓名/预览) */
    int sms_searching = 0;
    char sms_sq[17];
    int sms_sqlen = 0;
    int sms_fv[SMS_COUNT];
    int sms_fvc = 0;
    sms_sq[0] = 0;
    /* V3.36: 通讯录联动——直接进入写短信 (compose 内预填收件人) */
    if (sms_prefill_active) {
        mobile_run_compose();
        sms_prefill_active = 0;         /* 防再次进入 */
    }
    while (running) {
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "Back: ESC", 0x0F);
        if (sms_open_idx >= 0) {
            /* 消息详情 */
            gfx_puts_zh((gfx_width - 48) / 2, 18, sms_senders[sms_open_idx], 0x0B);
            gfx_rect(8, 36, gfx_width - 16, gfx_height - 80, 0x0F);
            gfx_puts_zh(14, 44, sms_preview[sms_open_idx], 0x0F);
            gfx_puts(14, 44 + 20, "Enter: reply   ESC: back", 0x0A);
        } else {
            ui_title("短信", "SMS", 18);
            if (sms_searching) {   /* V3.58: 搜索框 */
                gfx_fillrect(4, 20, gfx_width - 8, 14, 0x01);
                gfx_puts(8, 22, ">", 0x0B);
                gfx_puts(16, 22, sms_sq, 0x0F);
                gfx_puts(gfx_width - 74, 22, "ESC exit", 0x08);
            }
            /* V2.78: 未读数 */
            if (sms_unread_count > 0) {
                char ubuf[8];
                ubuf[0] = 'U'; ubuf[1] = ':';
                ubuf[2] = '0' + (sms_unread_count > 9 ? 9 : sms_unread_count);
                ubuf[3] = 0;
                gfx_puts(gfx_width - 28, 18, ubuf, 0x0C);
            }
            /* 消息列表 (V3.30: 滚动窗口; V3.31: 视图聚合 sms_view_map) */
            int disp_cnt = sms_searching ? sms_fvc : sms_view_count;   /* V3.58 */
            int total = disp_cnt > 0 ? disp_cnt : SMS_COUNT;
            int view_end = scroll_top + SMS_LIST_VIEW;
            if (view_end > total + 1) view_end = total + 1;   /* +1 含 +New 入口 */
            for (int i = scroll_top; i < view_end; i++) {
                int y = 36 + (i - scroll_top) * 20;
                if (i < disp_cnt) {
                    int di = sms_searching ? sms_fv[i] : sms_view_map[i];   /* V3.58 */
                    uint8_t bg = (i == sel) ? mob_accent : 0x08;
                    gfx_fillrect(4, y, gfx_width - 8, 18, bg);
                    gfx_rect(4, y, gfx_width - 8, 18, ui_border());   /* V3.49 */
                    int sx = 8;
                    /* V3.33: 会话级未读聚合徽标 (*N, 9+ 封顶) */
                    int unr = 0;
                    for (int j = 0; j < sms_disk_count; j++)
                        if (strcmp(sms_senders[j], sms_senders[di]) == 0 && sms_unread_flags[j]) unr++;
                    if (unr > 0) {
                        char ub[3];
                        ub[0] = '*'; ub[1] = '0' + (unr > 9 ? 9 : unr); ub[2] = 0;
                        gfx_puts(8, y + 4, ub, 0x0C);
                        sx = 24;
                    }
                    /* V3.35: 联系人姓名优先 (未匹配显示号码) */
                    const char *cname = contacts_name_of(sms_senders[di]);
                    gfx_puts_zh(sx, y + 2, cname ? cname : sms_senders[di], 0x0B);
                    gfx_puts_zh(60, y + 2, sms_dir[di] ? "S" : "", 0x0A);   /* V3.31: 发送标记 */
                    gfx_puts_zh(76, y + 2, sms_preview[di], 0x08);
                    /* V3.33: 时间右对齐 (内部宽 320, 5 字符 x8px) */
                    if (sms_times[di][0]) gfx_puts(gfx_width - 46, y + 2, sms_times[di], 0x07);
                } else if (i < total) {
                    /* V3.31: 磁盘 0 条时的空槽位 */
                    (void)0;
                    uint8_t bg = (i == sel) ? mob_accent : 0x08;
                    gfx_fillrect(4, y, gfx_width - 8, 18, bg);
                    gfx_rect(4, y, gfx_width - 8, 18, ui_border());   /* V3.49 */
                } else {
                    /* V2.47: 写新短信入口 */
                    uint8_t bg = (sel == total) ? 0x01 : 0x00;
                    gfx_fillrect(4, y, gfx_width - 8, 18, bg);
                    gfx_rect(4, y, gfx_width - 8, 18, 0x0A);
                    gfx_puts(8, y + 2, "+ New Message", 0x0F);
                }
            }
        }
        gfx_flip();
        /* V3.45: 短信列表触屏/鼠标拖动滚动 (列表态, 详情态不响应) */
        if (sms_open_idx < 0) {
            uint8_t tbtn = mouse_get_buttons();
            int tmy = mouse_get_y();
            if (tbtn & WM_MOUSE_LEFT) {
                if (!sl_tdown) { sl_tdown = 1; sl_tlast = tmy; }
                else {
                    int dy = tmy - sl_tlast;
                    if (dy < -15) {
                        if (sel < sms_view_count) { sel++; if (sel >= scroll_top + SMS_LIST_VIEW) scroll_top = sel - SMS_LIST_VIEW + 1; }
                        sl_tlast = tmy;
                    } else if (dy > 15) {
                        if (sel > 0) { sel--; if (sel < scroll_top) scroll_top = sel; }
                        sl_tlast = tmy;
                    }
                }
            } else sl_tdown = 0;
        }
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) {
                if (sms_searching) { sms_searching = 0; sms_sq[0] = 0; sms_sqlen = 0; sel = 0; continue; }   /* V3.58 */
                if (sms_open_idx >= 0) sms_open_idx = -1;  /* 返回列表 */
                else { running = 0; break; }
            }
            if (sms_open_idx >= 0) {
                /* V2.76: 详情页 Enter 回复 */
                if (kc == '\n' || kc == '\r') {
                    int ridx = sms_open_idx;
                    sms_open_idx = -1;
                    sms_reply_idx = ridx;
                    mobile_run_compose();
                    sms_load_from_disk();  /* 刷新列表 */
                }
                continue;
            }
            if (sms_searching) {   /* V3.58: 搜索态输入 */
                if (kc == 0x08) { if (sms_sqlen > 0) { sms_sq[--sms_sqlen] = 0; } }
                else if ((kc >= '0' && kc <= '9') || (kc >= 'a' && kc <= 'z') ||
                         (kc >= 'A' && kc <= 'Z')) {
                    if (sms_sqlen < 16) { sms_sq[sms_sqlen++] = kc; sms_sq[sms_sqlen] = 0; }
                }
                sms_fvc = 0;
                for (int fi = 0; fi < sms_view_count; fi++) {
                    int di = sms_view_map[fi];
                    const char *cn = contacts_name_of(sms_senders[di]);
                    if (strstr(sms_senders[di], sms_sq) ||
                        (cn && strstr(cn, sms_sq)) ||
                        strstr(sms_preview[di], sms_sq)) {
                        sms_fv[sms_fvc++] = di;
                    }
                }
                if (sel > sms_fvc) sel = sms_fvc;
                continue;
            }
            if (kc == '/') { sms_searching = 1; sms_sq[0] = 0; sms_sqlen = 0; sms_fvc = 0; sel = 0; continue; }
            if (kc == 'w' || kc == 'W') { if (sel > 0) { sel--; if (sel < scroll_top) scroll_top = sel; } }
            else if (kc == 's' || kc == 'S') { if (sel < (sms_searching ? sms_fvc : sms_view_count)) { sel++; if (sel >= scroll_top + SMS_LIST_VIEW) scroll_top = sel - SMS_LIST_VIEW + 1; } }
            else if (kc == 'x' || kc == 'X') {
                /* V2.78: 删除短信 (落盘, V3.31 映射到磁盘索引) */
                if (sel < sms_view_count) {
                    sms_delete(sms_view_map[sel]);
                    sms_load_from_disk();
                    if (sel >= sms_view_count && sel > 0) sel--;
                }
            }
            else if (kc == '\n' || kc == '\r') {
                if (sel == (sms_searching ? sms_fvc : sms_view_count)) { mobile_run_compose();   /* V3.58 */ sms_load_from_disk(); }
                else {
                    int di = sms_view_map[sel];
                    sms_mark_read_peer(sms_senders[di]);   /* V3.33: 会话级全已读 */
                    sms_load_from_disk();
                    sms_open_idx = di;      /* V3.31: 进入会话视图 */
                    mobile_run_conversation(di);
                    sms_open_idx = -1;      /* V3.31: 会话返回后复位 */
                }
            }
        }
        for (volatile int d = 0; d < 150000; d++) ;
    }
}

/* V2.45: 音乐播放器 */
#define MUSIC_COUNT 4
const char *music_titles[MUSIC_COUNT] = {"Morning", "Rain", "Ocean", "Night"};

/* V2.94: 收藏 (落盘 /data/music/favorites.txt, 4 字符 0/1) */
int music_fav[MUSIC_COUNT] = {0, 0, 0, 0};
int music_fav_view = 0;   /* 0=曲目列表 1=收藏列表 */
void music_fav_save(void)
{
    extern int ide_ext2_writefile(const char *, const char *);
    char buf[MUSIC_COUNT + 1];
    for (int i = 0; i < MUSIC_COUNT; i++) buf[i] = music_fav[i] ? '1' : '0';
    buf[MUSIC_COUNT] = 0;
    ide_ext2_writefile("/data/music/favorites.txt", buf);
    ide_ext2_sync();
}
void music_fav_load(void)
{
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    char buf[16];
    int n = ide_ext2_read_file("/data/music", "favorites.txt", buf, 15);
    if (n >= MUSIC_COUNT) {
        for (int i = 0; i < MUSIC_COUNT; i++) music_fav[i] = (buf[i] == '1');
    }
}


int music_cur = 0;
int music_pos;   /* V2.95: 前向声明 (定义在 V2.85 音乐区) */
/* V2.97: 播放模式 0=顺序循环 1=单曲循环 2=随机 (R 键切换) */
int music_mode = 0;
int music_rand = 1;   /* 伪随机 LCG 状态 */
/* V3.08: 播放列表排序 (0=默认 1=名称 2=收藏优先) + 随机种子持久化 */
int music_sort_mode = 0;
int music_order[MUSIC_COUNT];
void music_rand_save(void)
{
    extern void cmos_write_byte(uint8_t, uint8_t);
    cmos_write_byte(0x3C, music_rand & 0x1F);
}
void music_rand_load(void)
{
    extern uint8_t cmos_read_byte(uint8_t);
    int v = cmos_read_byte(0x3C) & 0x1F;
    music_rand = (v != 0) ? v : 1;
}
int music_order_pos(int idx)
{
    for (int i = 0; i < MUSIC_COUNT; i++)
        if (music_order[i] == idx) return i;
    return 0;
}
void music_build_order(void)
{
    for (int i = 0; i < MUSIC_COUNT; i++) music_order[i] = i;
    if (music_sort_mode == 1) {
        for (int i = 1; i < MUSIC_COUNT; i++) {
            int v = music_order[i], j = i - 1;
            while (j >= 0 && music_titles[music_order[j]][0] > music_titles[v][0]) {
                music_order[j + 1] = music_order[j]; j--;
            }
            music_order[j + 1] = v;
        }
    } else if (music_sort_mode == 2) {
        for (int i = 0; i < MUSIC_COUNT; i++)
            for (int j = i + 1; j < MUSIC_COUNT; j++)
                if (!music_fav[music_order[i]] && music_fav[music_order[j]]) {
                    int t = music_order[i];
                    music_order[i] = music_order[j];
                    music_order[j] = t;
                }
    }
}

/* V3.04: 最近播放历史 (5 条, 去重置顶, H 键查看) */
#define MUSIC_RECENT_MAX 5
int music_rec[MUSIC_RECENT_MAX] = {-1, -1, -1, -1, -1};
int music_hist_view = 0;
int music_last_cur = -1;
/* V3.01: 播放模式持久化 (前向声明) */
void music_mode_save(void);
void music_mode_load(void);
/* V2.95: 最近播放续播 (/data/music/recent.txt = idx|pos) */
void music_recent_save(void)
{
    extern int ide_ext2_writefile(const char *, const char *);
    char buf[MUSIC_RECENT_MAX * 6 + 1];
    int nb = 0;
    for (int k = 0; k < MUSIC_RECENT_MAX; k++) {
        if (music_rec[k] < 0) break;
        int v = music_rec[k], tj = 0;
        char tmp[4];
        do { tmp[tj++] = '0' + (v % 10); v /= 10; } while (v > 0);
        while (tj > 0) buf[nb++] = tmp[--tj];
        buf[nb++] = '|';
        if (k == 0) {
            v = music_pos; tj = 0;
            do { tmp[tj++] = '0' + (v % 10); v /= 10; } while (v > 0);
            while (tj > 0) buf[nb++] = tmp[--tj];
        } else {
            buf[nb++] = '0';
        }
        buf[nb++] = '\n';
    }
    buf[nb] = 0;
    ide_ext2_writefile("/data/music/recent.txt", buf);
    ide_ext2_sync();
}
/* V3.04: 最近播放恢复 (5 条, 首行=续播点) */
void music_recent_load(void)
{
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    char buf[40];
    int n = ide_ext2_read_file("/data/music", "recent.txt", buf, 39);
    for (int k = 0; k < MUSIC_RECENT_MAX; k++) music_rec[k] = -1;
    if (n >= 3) {
        buf[n] = 0;
        int li = 0, k = 0;
        while (k < MUSIC_RECENT_MAX && li < n) {
            int idx = 0, pos = 0;
            while (li < n && buf[li] >= '0' && buf[li] <= '9') { idx = idx * 10 + (buf[li] - '0'); li++; }
            if (li < n && buf[li] == '|') li++;
            while (li < n && buf[li] >= '0' && buf[li] <= '9') { pos = pos * 10 + (buf[li] - '0'); li++; }
            if (li < n && buf[li] == '\n') li++;
            if (idx >= 0 && idx < MUSIC_COUNT) {
                music_rec[k++] = idx;
                if (k == 1) { music_cur = idx; music_pos = (pos > 100) ? 0 : pos; }
            }
        }
    }
}
/* V3.04: 去重置顶 + 落盘 */
void music_recent_push(int idx)
{
    if (idx < 0 || idx >= MUSIC_COUNT) return;
    int k;
    for (k = 0; k < MUSIC_RECENT_MAX; k++)
        if (music_rec[k] == idx) break;
    if (k < MUSIC_RECENT_MAX) {
        for (int j = k; j > 0; j--) music_rec[j] = music_rec[j - 1];
    } else {
        for (int j = MUSIC_RECENT_MAX - 1; j > 0; j--) music_rec[j] = music_rec[j - 1];
    }
    music_rec[0] = idx;
    music_recent_save();
}

int music_playing = 0;
int music_pos = 0;    /* V2.85: 播放进度 0-100 (循环) */
int music_vol = 60;   /* V2.85: 音量 0-100 */

