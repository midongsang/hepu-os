/* ============================================================================
 * hs_apps.c — 应用窗口实现 (V0.13.10 重建)
 * 终端 / 文件管理器 / 任务管理器
 * 渲染由 hpdesk 窗口系统调用 (w->render 回调), 键盘经 hs_apps_handle_key
 * ========================================================================== */

#include "hpdesk.h"
#include "png64.h"
#include "hs_apps.h"
#include "hsservices.h"
#include "hsinput.h"   /* KEY_EXT_* 扩展键统一码 */

/* 外部图形/系统接口 */
extern void gfx64_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void gfx64_fill_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
extern void gfx64_draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
extern void font64_draw_string(void *fb, int fw, int fh,
                               int x, int y, const char *str,
                               uint32_t color, uint16_t size);
extern void *gfx64_get_framebuffer(void);
extern void vga_puts(const char *s);
extern int strcmp(const char *a, const char *b);
extern char *strcpy(char *d, const char *s);

/* rtc */
typedef struct { uint8_t second, minute, hour, day, month; uint16_t year; uint8_t weekday; } hs_rtc_time_t;
extern void rtc64_get_time(hs_rtc_time_t *time);

/* ext2 */
extern int ext2_64_list_dir_entries(const char *path, char names[][256], uint8_t *types, int max_entries);
extern int ext2_64_list_dir_entries_ex(const char *path, char names[][256], uint8_t *types,
                                       uint32_t *sizes, int max_entries);
extern int ext2_64_read_file(const char *path, uint8_t *buf, uint32_t max);
extern int ext2_64_write_file(const char *path, const uint8_t *data, uint32_t size);
extern int ext2_64_create_file(const char *path);
extern int ext2_64_stat(const char *path, uint32_t *size, uint8_t *isdir);
extern int ext2_64_delete_file(const char *path);
extern int ext2_64_delete_tree(const char *path);
extern int ext2_64_mkdir(const char *path);
extern int ext2_64_rename(const char *oldp, const char *newp);

/* 网络/进程 */
extern void e1000_get_net_stats(uint64_t *tx, uint64_t *rx, uint32_t *txr, uint32_t *rxr);
extern int hs_process_get_cpu_usage(void);
extern int hs_process_get_mem_usage(void);
extern hs_window_t *hs_window_get_focused(void);
extern int hs_process_monitor_init(void);
extern void hs_process_monitor_update(void);
extern hs_process_info_t *hs_process_get_list(int *count);

/* ============================================================================
 * 通用工具
 * ========================================================================== */
static int hs_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void hs_itoa(int v, char *buf)
{
    int a = v < 0 ? -v : v;
    char tmp[12]; int n = 0;
    do { tmp[n++] = '0' + (a % 10); a /= 10; } while (a);
    if (v < 0) tmp[n++] = '-';
    int s = 0, e = n - 1;
    while (s < e) { char c = tmp[s]; tmp[s] = tmp[e]; tmp[e] = c; s++; e--; }
    tmp[n] = 0;
    strcpy(buf, tmp);
}

static void hs_uitoa64(uint64_t v, char *buf)
{
    char tmp[24]; int n = 0;
    do { tmp[n++] = '0' + (v % 10); v /= 10; } while (v);
    int s = 0, e = n - 1;
    while (s < e) { char c = tmp[s]; tmp[s] = tmp[e]; tmp[e] = c; s++; e--; }
    tmp[n] = 0;
    strcpy(buf, tmp);
}

/* 行宽截断拷贝 (内核无 strncpy) */
static void hs_strcpyn(char *d, const char *s, int max)
{
    int i = 0;
    while (s[i] && i < max) { d[i] = s[i]; i++; }
    d[i] = 0;
}

/* ============================================================================
 * 终端
 * ========================================================================== */
#define TERM_MAX_LINES 64
#define TERM_MAX_LEN   60
#define TERM_INPUT_MAX 60

static hs_window_t *t_win = 0;
static char t_lines[TERM_MAX_LINES][TERM_MAX_LEN + 1];
static int t_line_count = 0;
static int t_scroll = 0;          /* 0=最新行, >0=上翻行数 */
static char t_input[TERM_INPUT_MAX + 1];
static int t_in_len = 0;
static int t_frame = 0;
static char t_cwd[128] = "/";
/* V0.13.39: 命令历史 + 补全 */
#define T_HIST_MAX 10
static char t_history[T_HIST_MAX][64];
static int t_hist_count = 0;
static int t_hist_idx = -1;
static int t_hist_edit = 0;

static void t_put(const char *s)
{
    /* 按 TERM_MAX_LEN 折行 */
    while (*s) {
        if (t_line_count >= TERM_MAX_LINES) {
            for (int i = 1; i < TERM_MAX_LINES; i++)
                strcpy(t_lines[i - 1], t_lines[i]);
            t_line_count = TERM_MAX_LINES - 1;
        }
        int len = 0;
        while (s[len] && len < TERM_MAX_LEN) len++;
        for (int j = 0; j < len; j++) t_lines[t_line_count][j] = s[j];
        t_lines[t_line_count][len] = 0;
        t_line_count++;
        s += len;
    }
    t_scroll = 0;
}

static void t_exec(const char *line)
{
    char cmd[64];
    int i = 0;
    while (line[i] && line[i] != ' ' && i < 60) { cmd[i] = line[i]; i++; }
    cmd[i] = 0;
    while (line[i] == ' ') i++;
    const char *arg = line + i;

    if (cmd[0] == 0) { t_put(""); return; }

    if (strcmp(cmd, "help") == 0) {
        t_put("Hepu Terminal commands:");
        t_put("  help clear ls cd cat echo date mem net ver whoami");
        t_put("  run/exec <elf> - load & run ELF user program from disk");
        t_put("  drv  - driver manager (update/list)");
        t_put("  usb  - usb devices (status/list/hotplug)");
        t_put("  reg  - registry (get/set/list)");
        t_put("  disk - disk info/fsck");
        t_put("  lang - language (get/set zh|en)");
        t_put("  devmode <password> - become developer (off to revert)");
        t_put("  kmod - kernel modules");
        t_put("  ps ifconfig uname mkdir touch rm rmdir cp mv head wc");
        t_put("  Tab=complete  Up/Dn=history  PgUp/PgDn=scroll");
        return;
    }
    if (strcmp(cmd, "drv") == 0) {
        extern int drv_get_count(void);
        if (strcmp(arg, "list") == 0 || arg[0] == 0) {
            char buf[64]; char tmp[8];
            strcpy(buf, "drivers installed: ");
            hs_itoa(drv_get_count(), tmp);
            strcpy(buf + hs_strlen(buf), tmp);
            t_put(buf);
        } else {
            t_put("usage: drv list|update");
        }
        return;
    }
    if (strcmp(cmd, "usb") == 0) {
        extern int usb64_dev_count(void);
        extern int usb64_dev_info(int idx, uint8_t *port, uint16_t *vid, uint16_t *pid, char *kind);
        int n = usb64_dev_count();
        char buf[64]; char tmp[8];
        strcpy(buf, "usb devices: ");
        hs_itoa(n, tmp);
        strcpy(buf + hs_strlen(buf), tmp);
        t_put(buf);
        for (int i = 0; i < n; i++) {
            uint8_t port; uint16_t vid, pid; char kind[16];
            if (usb64_dev_info(i, &port, &vid, &pid, kind) != 0) break;
            strcpy(buf, "  ");
            hs_strcpyn(buf + 2, kind, 20);
            t_put(buf);
        }
        return;
    }
    if (strcmp(cmd, "reg") == 0) {
        extern void reg64_list(void);
        reg64_list();
        t_put("(registry above)");
        return;
    }
    if (strcmp(cmd, "lang") == 0) {
        extern int l10n64_get_lang(void);
        extern int l10n64_set_lang(int lang);
        if (strcmp(arg, "zh") == 0) l10n64_set_lang(0);
        else if (strcmp(arg, "en") == 0) l10n64_set_lang(1);
        t_put(l10n64_get_lang() ? "language: en" : "language: zh");
        return;
    }
    if (strcmp(cmd, "clear") == 0) {
        t_line_count = 0; t_scroll = 0;
        return;
    }
    if (strcmp(cmd, "echo") == 0) { t_put(arg); return; }
    if (strcmp(cmd, "whoami") == 0) { t_put("user"); return; }
    if (strcmp(cmd, "ver") == 0 || strcmp(cmd, "version") == 0) {
        t_put("Hepu OS v0.56.1 (x86_64)");
        return;
    }
    if (strcmp(cmd, "date") == 0) {
        hs_rtc_time_t tm;
        rtc64_get_time(&tm);
        char buf[64];
        char tmp[8];
        int bp = 0;
        hs_itoa(tm.year, buf);
        bp = hs_strlen(buf);
        buf[bp++] = '-'; buf[bp] = 0;
        hs_itoa(tm.month, tmp); strcpy(buf + hs_strlen(buf), tmp);
        bp = hs_strlen(buf);
        buf[bp++] = '-'; buf[bp] = 0;
        hs_itoa(tm.day, tmp); strcpy(buf + hs_strlen(buf), tmp);
        bp = hs_strlen(buf);
        buf[bp++] = ' '; buf[bp] = 0;
        hs_itoa(tm.hour, tmp); strcpy(buf + hs_strlen(buf), tmp);
        bp = hs_strlen(buf);
        buf[bp++] = ':'; buf[bp] = 0;
        hs_itoa(tm.minute, tmp); strcpy(buf + hs_strlen(buf), tmp);
        t_put(buf);
        return;
    }
    if (strcmp(cmd, "mem") == 0) {
        char buf[64]; char tmp[16];
        int mu = hs_process_get_mem_usage();
        strcpy(buf, "Memory used: ");
        hs_itoa(mu, tmp);
        strcpy(buf + hs_strlen(buf), tmp);
        strcpy(buf + hs_strlen(buf), " KB");
        t_put(buf);
        return;
    }
    if (strcmp(cmd, "net") == 0) {
        uint64_t tx = 0, rx = 0; uint32_t txr = 0, rxr = 0;
        e1000_get_net_stats(&tx, &rx, &txr, &rxr);
        char buf[64]; char tmp[24];
        strcpy(buf, "Net TX: "); hs_uitoa64(tx, tmp); strcpy(buf + hs_strlen(buf), tmp);
        strcpy(buf + hs_strlen(buf), " B  RX: "); hs_uitoa64(rx, tmp); strcpy(buf + hs_strlen(buf), tmp);
        strcpy(buf + hs_strlen(buf), " B");
        t_put(buf);
        strcpy(buf, "Up: "); hs_uitoa64(txr, tmp); strcpy(buf + hs_strlen(buf), tmp);
        strcpy(buf + hs_strlen(buf), " B/s  Dn: "); hs_uitoa64(rxr, tmp); strcpy(buf + hs_strlen(buf), tmp);
        strcpy(buf + hs_strlen(buf), " B/s");
        t_put(buf);
        return;
    }
    if (strcmp(cmd, "ls") == 0) {
        const char *path = (arg[0]) ? arg : t_cwd;
        static char names[128][256];
        uint8_t types[128];
        int n = ext2_64_list_dir_entries(path, names, types, 128);
        if (n < 0) { t_put("ls: no such directory"); return; }
        char line[80];
        for (int k = 0; k < n; k++) {
            hs_strcpyn(line, names[k], 60);
            if (types[k]) {
                int L = hs_strlen(line);
                line[L] = '/';
                line[L + 1] = 0;
            }
            t_put(line);
        }
        return;
    }
    if (strcmp(cmd, "cd") == 0) {
        if (arg[0]) {
            hs_strcpyn(t_cwd, arg, 120);
            t_put(t_cwd);
        } else {
            strcpy(t_cwd, "/");
        }
        return;
    }
    /* V0.13.41: mkdir/mv 文件操作 (验证 ext2_64_mkdir/rename 全链路) */
    if (strcmp(cmd, "mkdir") == 0) {
        if (!arg[0]) { t_put("mkdir: missing path"); return; }
        char full[256];
        if (arg[0] == '/') hs_strcpyn(full, arg, 200);
        else {
            hs_strcpyn(full, t_cwd, 120);
            int fl = hs_strlen(full);
            if (fl > 0 && full[fl - 1] != '/') { full[fl++] = '/'; full[fl] = 0; }
            hs_strcpyn(full + fl, arg, 130);
        }
        extern int ext2_64_mkdir(const char *path);
        if (ext2_64_mkdir(full) == 0) t_put("ok: created");
        else t_put("mkdir: failed");
        return;
    }
    if (strcmp(cmd, "mv") == 0 || strcmp(cmd, "rename") == 0) {
        char oldp[256], newp[256];
        int j = 0;
        while (arg[j] && arg[j] != ' ' && j < 200) { oldp[j] = arg[j]; j++; }
        oldp[j] = 0;
        while (arg[j] == ' ') j++;
        if (!arg[j]) { t_put("mv: usage mv <old> <new>"); return; }
        hs_strcpyn(newp, arg + j, 200);
        extern int ext2_64_rename(const char *oldp_, const char *newp_);
        int rc = ext2_64_rename(oldp, newp);
        if (rc == 0) t_put("ok: renamed");
        else if (rc == -2) t_put("mv: target exists");
        else if (rc == -3) t_put("mv: source not found");
        else t_put("mv: failed");
        return;
    }
    if (strcmp(cmd, "rm") == 0) {
        if (!arg[0]) { t_put("rm: usage rm <path>"); return; }
        char full[512];
        if (arg[0] == '/') hs_strcpyn(full, arg, 510);
        else {
            hs_strcpyn(full, t_cwd, 250);
            int fl = hs_strlen(full);
            if (fl > 0 && full[fl - 1] != '/') { full[fl++] = '/'; full[fl] = 0; }
            hs_strcpyn(full + fl, arg, 250);
        }
        extern int ext2_64_delete_tree(const char *path);
        int rc = ext2_64_delete_tree(full);
        if (rc == 0) t_put("ok: removed");
        else if (rc == -2) t_put("rm: too deep");
        else t_put("rm: failed");
        return;
    }
    if (strcmp(cmd, "rmdir") == 0) {
        if (!arg[0]) { t_put("rmdir: usage rmdir <path>"); return; }
        char full[512];
        if (arg[0] == '/') hs_strcpyn(full, arg, 510);
        else {
            hs_strcpyn(full, t_cwd, 250);
            int fl = hs_strlen(full);
            if (fl > 0 && full[fl - 1] != '/') { full[fl++] = '/'; full[fl] = 0; }
            hs_strcpyn(full + fl, arg, 250);
        }
        extern int ext2_64_delete_dir(const char *path);
        int rc = ext2_64_delete_dir(full);
        if (rc == 0) t_put("ok: removed dir");
        else if (rc == -2) t_put("rmdir: not empty");
        else t_put("rmdir: failed");
        return;
    }
    /* V0.13.45: run/exec 命令 — 从 ext2 盘加载 ELF 用户程序执行
     * (兼容层宿主通道: ELF64 原生程序直接跑, ELF32/PE/Mach-O 后续经 binfmt 接入) */
    if (strcmp(cmd, "run") == 0 || strcmp(cmd, "exec") == 0) {
        if (!arg[0]) { t_put("run: usage run <elf path>"); return; }
        char full[256];
        if (arg[0] == '/') hs_strcpyn(full, arg, 200);
        else {
            hs_strcpyn(full, t_cwd, 120);
            int fl = hs_strlen(full);
            if (fl > 0 && full[fl - 1] != '/') { full[fl++] = '/'; full[fl] = 0; }
            hs_strcpyn(full + fl, arg, 130);
        }
        extern int user_process_exec(const char *path);
        int pid = user_process_exec(full);
        if (pid < 0) { t_put("run: failed to execute"); return; }
        char rbuf[48];
        strcpy(rbuf, "ok: PID ");
        char numbuf[24];
        hs_uitoa64((uint64_t)pid, numbuf);
        hs_strcpyn(rbuf+hs_strlen(rbuf), numbuf, 20);
        t_put(rbuf);
        return;
    }
    if (strcmp(cmd, "disk") == 0) {
        extern int disk64_fsck(void);
        if (strcmp(arg, "fsck") == 0) {
            int rc = disk64_fsck();
            t_put(rc == 0 ? "fsck: PASS (no corruption)"
                          : "fsck: issues found (see VGA log)");
        } else {
            t_put("usage: disk fsck");
        }
        return;
    }
    /* V0.55 P4: devmode 普通用户 <-> 开发者 (角色持久化到 /account.conf)
     * GUI 终端不阻塞取键, 密码走参数: devmode <password> | devmode off */
    if (strcmp(cmd, "devmode") == 0) {
        extern int auth64_is_configured(void);
        extern int auth64_current_role(void);
        extern int auth64_elevate(const char *pass);
        extern int auth64_deelevate(void);
        if (!auth64_is_configured()) {
            t_put("devmode: no account (complete first-boot setup first)");
            return;
        }
        if (strcmp(arg, "off") == 0) {
            if (auth64_current_role() != 2) {
                t_put("devmode: not in developer mode");
            } else {
                t_put(auth64_deelevate() == 0 ?
                      "devmode: switched back to normal user" :
                      "devmode: failed to save");
            }
            return;
        }
        if (auth64_current_role() == 2) {
            t_put("devmode: already in developer mode");
            return;
        }
        if (!arg[0]) {
            t_put("usage: devmode <password> | devmode off");
            return;
        }
        t_put(auth64_elevate(arg) == 0 ?
              "devmode: developer mode enabled (persistent)" :
              "devmode: wrong password");
        return;
    }
    t_put("Unknown command. Type 'help' for list.");
}

static void t_render(hs_window_t *w)
{
    void *fb = gfx64_get_framebuffer();
    int W = g_shell.screen_width, H = g_shell.screen_height;
    int cx = w->x + 6, cy = w->y + 26;
    int ch = w->height - 26 - 24;
    int row_h = 16;
    /* 预留 20px 给底部输入行, 避免最后一行输出与提示行重叠 */
    int visible = (ch - 20) / row_h;
    if (visible < 1) visible = 1;

    /* V0.13.39: 经典终端配色 — 黑底绿字(固定, 不随主题) */
    uint32_t TERM_BG = 0x000000;
    uint32_t TERM_FG = 0x33FF33;
    uint32_t TERM_HI = 0x66FF66;
    gfx64_fill_rect(w->x + 3, w->y + 24, w->width - 6, w->height - 28, TERM_BG);
    gfx64_fill_rect(w->x + 3, w->y + 22 + ch, w->width - 6, 26, 0x0A0F0A);

    /* 输出区: 从 t_scroll 偏移显示 */
    int start = t_line_count - visible - t_scroll;
    if (start < 0) start = 0;
    for (int r = 0; r < visible; r++) {
        int li = start + r;
        if (li < t_line_count) {
            font64_draw_string(fb, W, H, cx, cy + r * row_h,
                               t_lines[li], TERM_FG, 12);
        }
    }

    /* 输入行 */
    int iy = cy + ch - 20;
    char prompt[72];
    strcpy(prompt, "> ");
    hs_strcpyn(prompt + 2, t_input, TERM_INPUT_MAX);
    font64_draw_string(fb, W, H, cx, iy, prompt, TERM_HI, 12);

    /* 光标 (闪烁块, 2 帧周期) */
    t_frame++;
    if ((t_frame & 1) == 0) {
        int tx = cx + (2 + t_in_len) * 6;
        gfx64_fill_rect(tx, iy + 1, 7, 13, TERM_FG);
    }
}

static void t_complete(void)
{
    /* 1. 命令名补全 */
    extern int shell_get_command_names(char names[][16], int max);
    char names[64][16];
    int n = shell_get_command_names(names, 64);
    if (t_in_len == 0 || t_input[0] == ' ') return;
    /* 仅补全第一个词(命令名) */
    int is_cmd_word = 1;
    for (int i = 0; i < t_in_len; i++)
        if (t_input[i] == ' ') { is_cmd_word = 0; break; }
    if (is_cmd_word) {
        int matches = 0;
        const char *m1 = 0;
        for (int i = 0; i < n; i++) {
            int j = 0;
            while (names[i][j] && t_input[j] && names[i][j] == t_input[j]) j++;
            if (t_input[j] == 0 && names[i][j] != 0) {
                if (!m1) m1 = names[i];
                matches++;
            }
        }
        if (matches == 1 && m1) {
            int j = 0;
            while (m1[j] && j < TERM_INPUT_MAX) { t_input[j] = m1[j]; j++; }
            t_input[j] = 0;
            t_in_len = j;
        } else if (matches > 1) {
            /* 列出候选。V0.54.12(R4 GFX-01): out[80] 旧实现用无界 strcpy
               拼接候选名（每个可达 256 字符），≥5 个候选即栈溢出。改为逐字符
               有界拼接，缓冲将满时安全截断并强制 NUL。 */
            char out[80];
            int olen = 0;
            const char *hdr = "candidates: ";
            while (hdr[olen] && olen < (int)sizeof(out) - 1) {
                out[olen] = hdr[olen];
                olen++;
            }
            for (int i = 0; i < n; i++) {
                int j = 0;
                while (names[i][j] && t_input[j] && names[i][j] == t_input[j]) j++;
                if (t_input[j] == 0 && names[i][j] != 0) {
                    int k = 0;
                    while (names[i][k] && olen < (int)sizeof(out) - 1) {
                        out[olen++] = names[i][k++];
                    }
                    if (olen < (int)sizeof(out) - 1) out[olen++] = ' ';
                }
            }
            out[olen] = 0;
            t_put(out);
        }
        return;
    }
    /* 2. 文件补全: 从路径前缀匹配当前目录条目 */
    {
        static char names2[128][256];
        uint8_t types2[128];
        int fn = ext2_64_list_dir_entries(t_cwd, names2, types2, 128);
        if (fn <= 0) return;
        /* 取最后一个词为前缀 */
        int pre = 0;
        for (int i = 0; i < t_in_len; i++)
            if (t_input[i] == ' ') pre = i + 1;
        int plen = t_in_len - pre;
        const char *m2 = 0;
        int matches = 0;
        for (int i = 0; i < fn; i++) {
            int j = 0;
            while (names2[i][j] && j < plen && names2[i][j] == t_input[pre + j]) j++;
            if (j == plen && names2[i][j] != 0) {
                if (!m2) m2 = names2[i];
                matches++;
            }
        }
        if (matches == 1 && m2) {
            int j = 0;
            while (m2[j] && t_in_len + j < TERM_INPUT_MAX) {
                t_input[t_in_len] = m2[j];
                t_in_len++;
                j++;
            }
            t_input[t_in_len] = 0;
        }
    }
}

static void t_handle_key(int key)
{
    if (key == '\t') { t_complete(); return; }
    if (key == '\b' || key == 0x0E) {          /* 退格 */
        if (t_in_len > 0) { t_in_len--; t_input[t_in_len] = 0; }
        t_hist_edit = 1;
        return;
    }
    if (key == '\n' || key == '\r' || key == 0x1C) {  /* 回车 */
        t_input[t_in_len] = 0;
        if (t_input[0]) {
            t_put(t_input);
            t_exec(t_input);
            /* 记录历史(去重最近) */
            if (t_hist_count == 0 ||
                strcmp(t_history[t_hist_count - 1], t_input) != 0) {
                if (t_hist_count >= T_HIST_MAX) {
                    for (int i = 1; i < T_HIST_MAX; i++)
                        strcpy(t_history[i - 1], t_history[i]);
                    t_hist_count = T_HIST_MAX - 1;
                }
                hs_strcpyn(t_history[t_hist_count], t_input, 60);
                t_hist_count++;
            }
        }
        t_in_len = 0;
        t_input[0] = 0;
        t_hist_idx = -1;
        t_hist_edit = 0;
        return;
    }
    if (key == 0xE3) {   /* 上: 命令历史 */
        if (t_hist_count > 0) {
            if (t_hist_idx < 0) t_hist_idx = t_hist_count - 1;
            else if (t_hist_idx > 0) t_hist_idx--;
            strcpy(t_input, t_history[t_hist_idx]);
            t_in_len = hs_strlen(t_input);
            t_hist_edit = 1;
        }
        return;
    }
    if (key == 0xE2) {   /* 下: 历史前进 */
        if (t_hist_idx >= 0) {
            if (t_hist_idx < t_hist_count - 1) {
                t_hist_idx++;
                strcpy(t_input, t_history[t_hist_idx]);
                t_in_len = hs_strlen(t_input);
            } else {
                t_hist_idx = -1;
                t_input[0] = 0;
                t_in_len = 0;
            }
            t_hist_edit = 1;
        }
        return;
    }
    if (key == 0x11) {   /* PgUp: 上滚 */
        if (t_line_count > 8) {
            t_scroll++;
            if (t_scroll > t_line_count - 8) t_scroll = t_line_count - 8;
        }
        return;
    }
    if (key == 0x12) {   /* PgDn: 下滚 */
        if (t_scroll > 0) t_scroll--;
        return;
    }
    if (key >= 32 && key < 127 && t_in_len < TERM_INPUT_MAX) {
        t_input[t_in_len++] = (char)key;
        t_input[t_in_len] = 0;
    }
}

/* ============================================================================
 * 文件管理器 (V0.54.8: 排序 / 大小 / 新建 / 重命名 / 删除 / 路径跳转 / 编辑器)
 * ========================================================================== */
#define FS_MAX_ITEMS 128
#define FS_MAX_VISIBLE 24
#define FS_ROW_H 18
#define FS_PATH_MAX 192
#define FS_PROMPT_MAX 96

#define FS_PROMPT_NONE   0
#define FS_PROMPT_MKDIR  1
#define FS_PROMPT_RENAME 2
#define FS_PROMPT_GOTO   3
#define FS_PROMPT_MKFILE 4

#define FS_CLIP_MAX 8
#define FS_COPY_BUF (128 * 1024)
#define FS_COPY_ENTRIES 64

static hs_window_t *f_win = 0;
static char f_path[FS_PATH_MAX] = "/";
static char f_items[FS_MAX_ITEMS][256];
static uint8_t f_types[FS_MAX_ITEMS];
static uint32_t f_sizes[FS_MAX_ITEMS];
static int f_count = 0;
static int f_sel = 0;
static int f_scroll = 0;
static char f_status[64] = "";
/* 过滤 / 视图 / 菜单 / 查看器 */
static char f_filter[32] = "";
static int f_filter_len = 0;
static int f_matched[FS_MAX_ITEMS];   /* 过滤/排序后的显示索引 */
static int f_match_count = 0;
static int f_view_mode = 0;           /* 0=列表, 1=网格 */
/* V0.55.1: 文件菜单状态当前未被使用(统一走桌面菜单), 先标注保留意图 */
static int f_fs_menu __attribute__((unused)) = 0;   /* 0=桌面菜单, 1=文件菜单 */
static int f_dclick_tick = 0;         /* 双击检测: 上次点击 tick */
static char f_status2[FS_PROMPT_MAX + 1] = "";
static hs_window_t *v_win = 0;
static char v_data[8192];

/* V0.55.0 P0: PNG 图片查看 (png64 解码器接入点)
 * 图片不再当文本读: 最大 128KB 原文件 -> RGBA 位图 -> 按窗口等比缩放显示 */
static char v_png_file[131072];
static png64_image_t v_png;
static int v_is_png = 0;

static int v_len = 0;
static int v_scroll = 0;
static int v_truncated = 0;
static char v_name[64] = "";
static void hs_viewer_render(hs_window_t *w);

/* 新建/重命名/跳转 输入框 */
static int f_prompt = FS_PROMPT_NONE;
static char f_prompt_buf[FS_PROMPT_MAX];
static int f_prompt_len = 0;
static char f_old_name[64];

/* 删除确认 */
static int f_del_confirm = 0;
static char f_del_name[64];
static char f_del_path[FS_PATH_MAX];

/* 多选标记 / 剪贴板 */
static uint8_t f_mark[FS_MAX_ITEMS];
static char f_clip_paths[FS_CLIP_MAX][FS_PATH_MAX];
static char f_clip_names[FS_CLIP_MAX][64];
static int f_clip_count = 0;
static int f_clip_mode = 0;          /* 0=空 1=复制 2=剪切 */
static uint8_t f_copy_buf[FS_COPY_BUF];

/* 属性浮层 */
static int f_props = 0;
static char f_props_name[64];
static char f_props_path[FS_PATH_MAX];
static uint32_t f_props_size;
static uint8_t f_props_isdir;

static int f_match_entry(const char *name, int type)
{
    if (f_filter_len == 0) return 1;
    const char *q = f_filter;
    const char *p = name;
    for (; *q; q++) {
        char cq = *q, cp = 0;
        int found = 0;
        for (const char *pp = p; *pp; pp++) {
            cp = *pp;
            if (cp >= 'A' && cp <= 'Z') cp += 32;
            if (cq >= 'A' && cq <= 'Z') cq += 32;
            if (cp == cq) { found = 1; p = pp + 1; break; }
        }
        if (!found) return 0;
    }
    return 1;
}

static void f_refresh(void);

static int f_visible_rows(void)
{
    int h = f_win ? f_win->height : 360;
    int ch = h - 26 - 40;              /* 内容区高度 (状态栏 40px 已扣除) */
    int rows = (ch - 52) / FS_ROW_H;   /* 26px 路径栏 + 26px 底部输入/确认条 */
    if (rows < 1) rows = 1;
    if (rows > FS_MAX_VISIBLE) rows = FS_MAX_VISIBLE;
    return rows;
}

static int f_name_cmp(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return (ca < cb) ? -1 : 1;
        a++; b++;
    }
    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

/* 目录优先，再按名称不区分大小写排序 */
static int f_cmp_items(int ia, int ib)
{
    if (f_types[ia] != f_types[ib]) return f_types[ib] - f_types[ia];
    return f_name_cmp(f_items[ia], f_items[ib]);
}

static int f_join(char *out, int cap, const char *dir, const char *name)
{
    int dl = hs_strlen(dir);
    int nl = hs_strlen(name);
    if (dl >= cap) return -1;
    strcpy(out, dir);
    int pos = dl;
    if (pos > 0 && out[pos - 1] != '/') {
        if (pos + 1 >= cap) return -1;
        out[pos++] = '/';
        out[pos] = 0;
    }
    if (pos + nl >= cap) return -1;
    strcpy(out + pos, name);
    return 0;
}

static void f_fmt_size(uint32_t sz, char *out)
{
    char tmp[16];
    if (sz < 1024) {
        hs_itoa((int)sz, tmp);
        strcpy(out, tmp);
        strcpy(out + hs_strlen(out), "B");
    } else if (sz < 1024u * 1024u) {
        hs_itoa((int)(sz / 1024), tmp);
        strcpy(out, tmp);
        strcpy(out + hs_strlen(out), "K");
    } else {
        hs_itoa((int)(sz / (1024u * 1024u)), tmp);
        strcpy(out, tmp);
        strcpy(out + hs_strlen(out), "M");
    }
}

static void f_clear_filter(void)
{
    f_filter_len = 0;
    f_filter[0] = 0;
}

static void f_go_up(void)
{
    char np[FS_PATH_MAX];
    strcpy(np, f_path);
    int len = hs_strlen(np);
    while (len > 1 && np[len - 1] == '/') len--;
    np[len] = 0;
    int i = len - 1;
    while (i > 0 && np[i] != '/') i--;
    if (i <= 0) { np[0] = '/'; np[1] = 0; }
    else np[i] = 0;
    strcpy(f_path, np);
    f_clear_filter();
    f_sel = 0;
    f_scroll = 0;
    f_refresh();
}

static void f_prompt_start(int mode, const char *initial)
{
    f_prompt = mode;
    f_prompt_len = 0;
    f_prompt_buf[0] = 0;
    f_del_confirm = 0;
    if (initial) {
        int n = hs_strlen(initial);
        if (n > FS_PROMPT_MAX - 1) n = FS_PROMPT_MAX - 1;
        for (int i = 0; i < n; i++) f_prompt_buf[i] = initial[i];
        f_prompt_buf[n] = 0;
        f_prompt_len = n;
    }
}

static void f_refresh(void)
{
    for (int i = 0; i < FS_MAX_ITEMS; i++) f_mark[i] = 0;
    f_count = ext2_64_list_dir_entries_ex(f_path, f_items, f_types, f_sizes, FS_MAX_ITEMS);
    if (f_count < 0) f_count = 0;

    /* 过滤 */
    f_match_count = 0;
    for (int i = 0; i < f_count; i++) {
        if (f_match_entry(f_items[i], f_types[i]))
            f_matched[f_match_count++] = i;
    }
    /* 插入排序：目录优先 + 名称升序 (条目数 <=128, 开销可忽略) */
    for (int i = 1; i < f_match_count; i++) {
        int key = f_matched[i];
        int j = i - 1;
        while (j >= 0 && f_cmp_items(f_matched[j], key) > 0) {
            f_matched[j + 1] = f_matched[j];
            j--;
        }
        f_matched[j + 1] = key;
    }

    if (f_sel >= f_match_count) f_sel = f_match_count - 1;
    if (f_sel < 0) f_sel = 0;
    int max_scroll = f_match_count - f_visible_rows();
    if (max_scroll < 0) max_scroll = 0;
    if (f_scroll > max_scroll) f_scroll = max_scroll;
    if (f_scroll < 0) f_scroll = 0;

    int dirs = 0;
    for (int i = 0; i < f_count; i++) if (f_types[i]) dirs++;
    char buf[72]; char tmp[8];
    strcpy(buf, "items: ");
    hs_itoa(f_match_count, tmp);
    strcpy(buf + hs_strlen(buf), tmp);
    strcpy(buf + hs_strlen(buf), "  dirs: ");
    hs_itoa(dirs, tmp);
    strcpy(buf + hs_strlen(buf), tmp);
    if (f_filter_len) {
        strcpy(buf + hs_strlen(buf), "  filter: ");
        hs_strcpyn(buf + hs_strlen(buf), f_filter, 32);
    }
    hs_strcpyn(f_status, buf, 63);
}

static void f_render(hs_window_t *w)
{
    void *fb = gfx64_get_framebuffer();
    int W = g_shell.screen_width, H = g_shell.screen_height;
    int cx = w->x + 6, cy = w->y + 26;
    int cw = w->width - 12, ch = w->height - 26 - 40;
    int row_h = FS_ROW_H;

    /* 路径栏 */
    gfx64_fill_rounded_rect(cx, cy, cw, 20, 4, g_shell.colors.panel_bg);
    {
        char shown[80];
        hs_strcpyn(shown, f_path, 70);
        font64_draw_string(fb, W, H, cx + 6, cy + 3, shown, g_shell.colors.panel_fg, 12);
    }
    /* USB挂载状态按钮(路径栏右侧) */
    {
        extern int usb64_dev_count(void);
        extern int storage_hal_get_device(void);
        extern int usb64_mounted_flag(void);
        int has_usb = usb64_dev_count() > 0;
        int on_usb = storage_hal_get_device() == 1;
        const char *txt = has_usb ? (on_usb ? "[USB: mounted]" : "[USB: click to mount]") : "[USB: not detected]";
        int tw = 12 * (hs_strlen(txt)) + 4;
        int bx = cx + cw - tw - 4;
        if (on_usb) gfx64_fill_rounded_rect(bx, cy, tw, 20, 4, g_shell.colors.accent);
        font64_draw_string(fb, W, H, bx + 4, cy + 3, txt,
                           on_usb ? 0xFFFFFF : g_shell.colors.panel_fg, 12);
    }

    /* 网格模式: 大图标 + 名称 */
    if (f_view_mode == 1) {
        int gs = 76, cols = cw / gs;
        if (cols < 1) cols = 1;
        int rows = (ch - 52) / (gs + 8);
        if (rows < 1) rows = 1;
        int gy = cy + 26;
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                int gi = f_scroll + (r * cols + c);
                if (gi >= f_match_count) break;
                int item = f_matched[gi];
                int ix = cx + 6 + c * gs;
                int iy2 = gy + r * (gs + 8);
                if (gi == f_sel)
                    gfx64_fill_rounded_rect(ix - 4, iy2 - 4, gs, gs, 8, g_shell.colors.accent);
                extern void hs_icon_folder(int x, int y, int size, uint32_t color);
                extern void hs_icon_doc(int x, int y, int size, uint32_t color);
                extern void hs_icon_media(int x, int y, int size, uint32_t color);
                extern void hs_icon_file(int x, int y, int size, uint32_t color);
                uint32_t ic = (gi == f_sel) ? 0xFFFFFF : g_shell.colors.accent;
                if (f_types[item]) hs_icon_folder(ix, iy2, 30, ic);
                else {
                    int len = hs_strlen(f_items[item]);
                    const char *nm = f_items[item];
                    int is_media = 0, is_doc = 0;
                    if (len >= 4) {
                        const char *ext = nm + len - 4;
                        if (strcmp(ext, ".mp3") == 0 || strcmp(ext, ".mp4") == 0 ||
                            strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0 ||
                            strcmp(ext, ".wav") == 0) is_media = 1;
                        if (strcmp(ext, ".txt") == 0 || strcmp(ext, ".md") == 0 ||
                            strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0 ||
                            strcmp(ext, ".doc") == 0) is_doc = 1;
                    }
                    if (is_media) hs_icon_media(ix, iy2, 30, ic);
                    else if (is_doc) hs_icon_doc(ix, iy2, 30, ic);
                    else hs_icon_file(ix, iy2, 30, ic);
                }
                if (f_mark[item])
                    gfx64_fill_rounded_rect(ix - 2, iy2 - 2, 12, 12, 3, 0xFFD040);
                char gd[32];
                /* 单元格宽 76px: 长文件名缩小字号, 避免与相邻图标名称重叠 */
                int nlen = hs_strlen(f_items[item]);
                int nsz = (nlen > 12) ? 10 : 12;
                hs_strcpyn(gd, f_items[item], nsz == 10 ? 12 : 14);
                font64_draw_string(fb, W, H, ix - 2, iy2 + 34, gd,
                                   (gi == f_sel) ? 0xFFFFFF : g_shell.colors.window_fg, nsz);
            }
        }
    } else {
        /* 列表模式 (名称 + 大小) */
        int list_y = cy + 26;
        int visible = f_visible_rows();
        if (visible < 1) visible = 1;
        for (int r = 0; r < visible; r++) {
            int li = f_scroll + r;
            if (li >= f_match_count) break;
            int item = f_matched[li];
            int iy = list_y + r * row_h;
            if (li == f_sel)
                gfx64_fill_rect(cx, iy, cw, row_h, g_shell.colors.accent);
            char disp[70];
            hs_strcpyn(disp, f_items[item], 42);
            if (f_types[item]) {
                int dl = hs_strlen(disp);
                if (dl < 68) { disp[dl] = '/'; disp[dl + 1] = 0; }
            }
            /* 文件类型矢量图标(目录/文档/媒体/未知) */
            {
                extern void hs_icon_folder(int x, int y, int size, uint32_t color);
                extern void hs_icon_doc(int x, int y, int size, uint32_t color);
                extern void hs_icon_media(int x, int y, int size, uint32_t color);
                extern void hs_icon_file(int x, int y, int size, uint32_t color);
                uint32_t ic = (li == f_sel) ? 0xFFFFFF : g_shell.colors.accent;
                if (f_types[item]) {
                    hs_icon_folder(cx + 3, iy + 1, 14, ic);
                } else {
                    int len = hs_strlen(f_items[item]);
                    const char *nm = f_items[item];
                    int is_media = 0, is_doc = 0;
                    if (len >= 4) {
                        const char *ext = nm + len - 4;
                        if (strcmp(ext, ".mp3") == 0 || strcmp(ext, ".mp4") == 0 ||
                            strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0 ||
                            strcmp(ext, ".wav") == 0) is_media = 1;
                        if (strcmp(ext, ".txt") == 0 || strcmp(ext, ".md") == 0 ||
                            strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0 ||
                            strcmp(ext, ".doc") == 0) is_doc = 1;
                    }
                    if (is_media) hs_icon_media(cx + 3, iy + 1, 14, ic);
                    else if (is_doc) hs_icon_doc(cx + 3, iy + 1, 14, ic);
                    else hs_icon_file(cx + 3, iy + 1, 14, ic);
                }
            }
            font64_draw_string(fb, W, H, cx + 22, iy + 2, disp,
                               (li == f_sel) ? 0xFFFFFF : g_shell.colors.window_fg, 12);
            /* 标记指示 */
            if (f_mark[item])
                font64_draw_string(fb, W, H, cx + cw - 68, iy + 2, "*",
                                   (li == f_sel) ? 0xFFFFFF : 0xFFD040, 12);
            /* 右侧大小 / <DIR> */
            char szbuf[20];
            if (f_types[item]) strcpy(szbuf, "<DIR>");
            else f_fmt_size(f_sizes[item], szbuf);
            font64_draw_string(fb, W, H, cx + cw - 52, iy + 2, szbuf,
                               (li == f_sel) ? 0xFFFFFF : g_shell.colors.text_secondary, 12);
        }
    }

    /* 属性浮层 */
    if (f_props) {
        int px = cx + 10, py = cy + 30, pw = cw - 20, ph = 96;
        char pb[120];
        gfx64_fill_rounded_rect(px, py, pw, ph, 8, g_shell.colors.panel_bg);
        gfx64_draw_rounded_rect(px, py, pw, ph, 8, g_shell.colors.accent);
        font64_draw_string(fb, W, H, px + 10, py + 8, "Properties",
                           g_shell.colors.accent, 14);
        font64_draw_string(fb, W, H, px + pw - 108, py + 8,
                           "any key = close", g_shell.colors.text_secondary, 10);
        strcpy(pb, "Name: ");
        hs_strcpyn(pb + hs_strlen(pb), f_props_name, 40);
        font64_draw_string(fb, W, H, px + 10, py + 30, pb, g_shell.colors.panel_fg, 12);
        strcpy(pb, "Type: ");
        strcpy(pb + hs_strlen(pb), f_props_isdir ? "Folder" : "File");
        font64_draw_string(fb, W, H, px + 10, py + 46, pb, g_shell.colors.panel_fg, 12);
        strcpy(pb, "Size: ");
        char szb[20];
        f_fmt_size(f_props_size, szb);
        strcpy(pb + hs_strlen(pb), szb);
        font64_draw_string(fb, W, H, px + 10, py + 62, pb, g_shell.colors.panel_fg, 12);
        strcpy(pb, "Path: ");
        hs_strcpyn(pb + hs_strlen(pb), f_props_path, 45);
        font64_draw_string(fb, W, H, px + 10, py + 78, pb,
                           g_shell.colors.text_secondary, 11);
    }

    /* 底部输入框 / 删除确认 (放在状态栏上方, 避免覆盖两行状态文本) */
    int by = cy + ch - 24;
    if (f_del_confirm) {
        gfx64_fill_rounded_rect(cx, by, cw, 22, 4, 0xC03030);
        char lb[120];
        strcpy(lb, "Delete ");
        hs_strcpyn(lb + hs_strlen(lb), f_del_name, 20);
        strcpy(lb + hs_strlen(lb), " ?  Enter=Delete  N=Cancel");
        font64_draw_string(fb, W, H, cx + 8, by + 4, lb, 0xFFFFFF, 12);
    } else if (f_prompt != FS_PROMPT_NONE) {
        const char *label = "New folder: ";
        if (f_prompt == FS_PROMPT_MKFILE) label = "New file: ";
        else if (f_prompt == FS_PROMPT_RENAME) label = "Rename: ";
        else if (f_prompt == FS_PROMPT_GOTO) label = "Goto path: ";
        gfx64_fill_rounded_rect(cx, by, cw, 22, 4, g_shell.colors.panel_bg);
        gfx64_draw_rounded_rect(cx, by, cw, 22, 4, g_shell.colors.accent);
        font64_draw_string(fb, W, H, cx + 6, by + 4, label, g_shell.colors.panel_fg, 12);
        int ix = cx + 6 + 92;
        char shown[70];
        int maxn = (cw - 100) / 7;
        if (maxn < 4) maxn = 4;
        if (f_prompt_len > maxn) hs_strcpyn(shown, f_prompt_buf + (f_prompt_len - maxn), maxn);
        else hs_strcpyn(shown, f_prompt_buf, maxn);
        font64_draw_string(fb, W, H, ix, by + 4, shown, g_shell.colors.window_fg, 12);
        int cxcur = ix + 7 * (f_prompt_len > maxn ? maxn : f_prompt_len);
        if (cxcur > cx + cw - 10) cxcur = cx + cw - 10;
        gfx64_fill_rect(cxcur, by + 4, 2, 13, g_shell.colors.accent);
    }

    /* 状态栏 (两行) */
    font64_draw_string(fb, W, H, cx, cy + ch + 4, f_status,
                       g_shell.colors.text_secondary, 10);
    if (f_status2[0])
        font64_draw_string(fb, W, H, cx, cy + ch + 16, f_status2,
                           g_shell.colors.accent, 12);
}

static int f_is_text_file(const char *nm)
{
    static const char *exts[] = {
        ".txt", ".md", ".hsqml", ".conf", ".cfg", ".json", ".log", ".ini",
        ".sh", ".py", ".js", ".rs", ".go", ".asm", ".yaml", ".html",
        ".xml", ".csv", ".c", ".h", ".s", ".S", 0
    };
    int len = hs_strlen(nm);
    if (len < 2) return 0;
    for (int i = 0; exts[i]; i++) {
        int el = hs_strlen(exts[i]);
        if (len >= el && strcmp(nm + len - el, exts[i]) == 0) return 1;
    }
    return 0;
}

static void f_open_item(int idx)
{
    if (idx < 0 || idx >= f_match_count) return;
    int item = f_matched[idx];
    char full[FS_PATH_MAX];
    if (f_join(full, sizeof(full), f_path, f_items[item]) != 0) {
        hs_strcpyn(f_status2, "Path too long", FS_PROMPT_MAX);
        return;
    }

    if (f_types[item]) {
        strcpy(f_path, full);
        f_clear_filter();
        f_sel = 0;
        f_scroll = 0;
        f_refresh();
        return;
    }

    /* 文本类文件 → 代码编辑器 */
    if (f_is_text_file(f_items[item])) {
        if (hs_editor_open_file(full)) {
            hs_strcpyn(f_status2, "Opened in editor", FS_PROMPT_MAX);
            return;
        }
        hs_strcpyn(f_status2, "Editor failed; read-only view", FS_PROMPT_MAX);
    }

    /* 其它文件 → 只读查看器 */
    uint32_t fsz = 0;
    uint8_t fdir = 0;
    ext2_64_stat(full, &fsz, &fdir);

    /* V0.55.0 P0: PNG 图片 → 解码后按像素绘制 */
    if (fsz >= 8 && fsz <= sizeof(v_png_file)) {
        int pn = ext2_64_read_file(full, (uint8_t *)v_png_file, sizeof(v_png_file));
        if (pn > 8 && (uint8_t)v_png_file[0] == 0x89 &&
            v_png_file[1] == 'P' && v_png_file[2] == 'N' && v_png_file[3] == 'G') {
            if (v_png.rgba) png64_free(&v_png);
            if (png64_decode((uint8_t *)v_png_file, pn, &v_png) == 0) {
                v_is_png = 1;
                hs_strcpyn(v_name, f_items[item], 60);
                if (!v_win) {
                    v_win = hs_window_create(HS_WINDOW_FLOATING, "Image", -1, -1, 520, 420);
                    if (v_win) v_win->render = hs_viewer_render;
                }
                if (v_win) { hs_window_show(v_win); v_win->focus = 1; }
                hs_strcpyn(f_status2, "Image decoded", FS_PROMPT_MAX);
                hpdesk_mark_dirty();
                return;
            }
            /* 解码失败则继续走文本查看 */
        }
    }
    v_is_png = 0;
    int n = ext2_64_read_file(full, (uint8_t *)v_data, sizeof(v_data) - 1);
    if (n < 0) {
        hs_strcpyn(f_status2, "Read failed", FS_PROMPT_MAX);
        return;
    }
    v_len = n;
    v_data[n] = 0;
    v_scroll = 0;
    v_truncated = (fsz > (uint32_t)n) ? 1 : 0;
    hs_strcpyn(v_name, f_items[item], 60);
    if (!v_win) {
        v_win = hs_window_create(HS_WINDOW_FLOATING, "Viewer", -1, -1, 460, 400);
        if (v_win) v_win->render = hs_viewer_render;
    }
    if (v_win) {
        hs_window_show(v_win);
        v_win->focus = 1;
        hs_strcpyn(f_status2, "Read-only view", FS_PROMPT_MAX);
    }
}

/* ============================================================================
 * 多选 / 剪贴板 / 属性
 * ========================================================================== */
static void f_clear_marks(void)
{
    for (int i = 0; i < FS_MAX_ITEMS; i++) f_mark[i] = 0;
}

static int f_copy_file(const char *src, const char *dst)
{
    uint32_t sz = 0;
    uint8_t isdir = 0;
    if (ext2_64_stat(src, &sz, &isdir) != 0 || isdir) return -1;
    if (sz > FS_COPY_BUF) return -2;
    int n = ext2_64_read_file(src, f_copy_buf, FS_COPY_BUF);
    if (n < 0) return -1;
    int w = ext2_64_write_file(dst, f_copy_buf, (uint32_t)n);
    return (w == n) ? 0 : -1;
}

/* 目录复制 (BFS 队列, 避免深层递归占用内核栈) */
static int f_copy_tree(const char *src, const char *dst)
{
    static char qs[8][FS_PATH_MAX];
    static char qd[8][FS_PATH_MAX];
    int qh = 0, qt = 0;

    if (ext2_64_mkdir(dst) != 0) return -1;
    strcpy(qs[qt], src);
    strcpy(qd[qt], dst);
    qt++;

    while (qh < qt) {
        char s[FS_PATH_MAX], d[FS_PATH_MAX];
        strcpy(s, qs[qh]);
        strcpy(d, qd[qh]);
        qh++;

        static char names[FS_COPY_ENTRIES][256];
        uint8_t types[FS_COPY_ENTRIES];
        uint32_t sizes[FS_COPY_ENTRIES];
        int n = ext2_64_list_dir_entries_ex(s, names, types, sizes, FS_COPY_ENTRIES);
        if (n < 0) return -1;
        if (n >= FS_COPY_ENTRIES) return -3;

        for (int i = 0; i < n; i++) {
            char sp[FS_PATH_MAX], dp[FS_PATH_MAX];
            if (f_join(sp, FS_PATH_MAX, s, names[i]) != 0 ||
                f_join(dp, FS_PATH_MAX, d, names[i]) != 0) return -1;
            if (types[i]) {
                if (qt >= 8) return -3;
                strcpy(qs[qt], sp);
                strcpy(qd[qt], dp);
                qt++;
            } else {
                int rc = f_copy_file(sp, dp);
                if (rc != 0) return rc;
            }
        }
    }
    return 0;
}

static void f_do_copy(int cut)
{
    int count = 0;
    f_props = 0;
    for (int i = 0; i < f_match_count && count < FS_CLIP_MAX; i++) {
        int item = f_matched[i];
        if (!f_mark[item]) continue;
        if (f_join(f_clip_paths[count], FS_PATH_MAX, f_path, f_items[item]) != 0) continue;
        hs_strcpyn(f_clip_names[count], f_items[item], 60);
        count++;
    }
    if (count == 0 && f_match_count > 0) {
        int item = f_matched[f_sel];
        if (f_join(f_clip_paths[0], FS_PATH_MAX, f_path, f_items[item]) != 0) {
            hs_strcpyn(f_status2, "Path too long", FS_PROMPT_MAX);
            return;
        }
        hs_strcpyn(f_clip_names[0], f_items[item], 60);
        count = 1;
    }
    if (count == 0) {
        hs_strcpyn(f_status2, "Nothing selected", FS_PROMPT_MAX);
        return;
    }
    f_clip_count = count;
    f_clip_mode = cut ? 2 : 1;

    char msg[48]; char tmp[8];
    strcpy(msg, cut ? "Cut " : "Copied ");
    hs_itoa(count, tmp);
    strcpy(msg + hs_strlen(msg), tmp);
    strcpy(msg + hs_strlen(msg), " item(s)");
    hs_strcpyn(f_status2, msg, FS_PROMPT_MAX);
}

static void f_do_paste(void)
{
    if (f_clip_mode == 0 || f_clip_count == 0) {
        hs_strcpyn(f_status2, "Clipboard is empty", FS_PROMPT_MAX);
        return;
    }

    int ok = 0;
    for (int i = 0; i < f_clip_count; i++) {
        char dst[FS_PATH_MAX];
        if (f_join(dst, FS_PATH_MAX, f_path, f_clip_names[i]) != 0) break;

        uint32_t sz = 0;
        uint8_t isdir = 0;
        if (ext2_64_stat(dst, &sz, &isdir) == 0) {
            hs_strcpyn(f_status2, "Paste failed: target exists", FS_PROMPT_MAX);
            return;
        }
        if (ext2_64_stat(f_clip_paths[i], &sz, &isdir) != 0) {
            hs_strcpyn(f_status2, "Paste failed: source missing", FS_PROMPT_MAX);
            return;
        }

        int rc = isdir ? f_copy_tree(f_clip_paths[i], dst)
                       : f_copy_file(f_clip_paths[i], dst);
        if (rc == -2) { hs_strcpyn(f_status2, "Paste failed: file too large", FS_PROMPT_MAX); return; }
        if (rc == -3) { hs_strcpyn(f_status2, "Paste failed: too many entries", FS_PROMPT_MAX); return; }
        if (rc != 0) { hs_strcpyn(f_status2, "Paste failed", FS_PROMPT_MAX); return; }

        if (f_clip_mode == 2) {
            if (isdir) ext2_64_delete_tree(f_clip_paths[i]);
            else ext2_64_delete_file(f_clip_paths[i]);
        }
        ok++;
    }

    if (ok == f_clip_count) {
        char msg[48]; char tmp[8];
        strcpy(msg, "Pasted ");
        hs_itoa(ok, tmp);
        strcpy(msg + hs_strlen(msg), tmp);
        strcpy(msg + hs_strlen(msg), " item(s)");
        hs_strcpyn(f_status2, msg, FS_PROMPT_MAX);
        f_clip_count = 0;
        f_clip_mode = 0;
    }
    f_refresh();
    f_clear_marks();
}

static void f_props_open(void)
{
    if (f_match_count <= 0) return;
    int item = f_matched[f_sel];
    hs_strcpyn(f_props_name, f_items[item], 60);
    if (f_join(f_props_path, FS_PATH_MAX, f_path, f_items[item]) != 0) {
        hs_strcpyn(f_status2, "Path too long", FS_PROMPT_MAX);
        return;
    }
    f_props_size = f_sizes[item];
    f_props_isdir = f_types[item];
    uint32_t sz2 = f_props_size;
    uint8_t d2 = 0;
    ext2_64_stat(f_props_path, &sz2, &d2);
    f_props_size = sz2;
    f_props = 1;
}

static void v_handle_key(int key)
{
    int rows = 1;
    if (v_win) {
        rows = (v_win->height - 26 - 28) / 16;
        if (rows < 1) rows = 1;
    }
    int max_scroll = 0;
    {
        int l = 0, count = 0;
        while (l < v_len) { if (v_data[l] == '\n') count++; l++; }
        if (v_len && v_data[v_len - 1] != '\n') count++;
        max_scroll = count - rows;
        if (max_scroll < 0) max_scroll = 0;
    }
    if (key == KEY_EXT_UP) v_scroll--;
    else if (key == KEY_EXT_DOWN) v_scroll++;
    else if (key == KEY_EXT_PGUP) v_scroll -= rows;
    else if (key == KEY_EXT_PGDN) v_scroll += rows;
    else if (key == KEY_EXT_HOME) v_scroll = 0;
    else if (key == KEY_EXT_END) v_scroll = max_scroll;
    if (v_scroll < 0) v_scroll = 0;
    if (v_scroll > max_scroll) v_scroll = max_scroll;
}

void hs_viewer_render(hs_window_t *w)
{
    void *fb = gfx64_get_framebuffer();
    int W = g_shell.screen_width, H = g_shell.screen_height;
    int cx = w->x + 6, cy = w->y + 24;

    /* V0.55.0 P0: PNG 位图等比缩放绘制 */
    if (v_is_png && v_png.rgba) {
        font64_draw_string(fb, W, H, cx, cy, v_name, g_shell.colors.accent, 13);
        int aw = w->width - 16;
        int ah = w->height - 52;
        if (aw < 8 || ah < 8) return;
        int dw = aw, dh = (int)((long)v_png.height * aw / (long)v_png.width);
        if (dh > ah) { dh = ah; dw = (int)((long)v_png.width * ah / (long)v_png.height); }
        int dx = w->x + 8 + (aw - dw) / 2;
        int dy = w->y + 44 + (ah - dh) / 2;
        gfx64_blit_rgba(dx, dy, dw, dh, v_png.rgba, (int)v_png.width, (int)v_png.height);
        return;
    }

    int cw = w->width - 12;
    font64_draw_string(fb, W, H, cx, cy, v_name, g_shell.colors.accent, 13);
    cy += 22;
    int rows = (w->height - 26 - 28) / 16;
    if (rows < 1) rows = 1;

    /* 定位到第 v_scroll 行的起始字节 */
    int i = 0;
    for (int l = 0; l < v_scroll && i < v_len; l++) {
        while (i < v_len && v_data[i] != '\n') i++;
        if (i < v_len) i++;
    }
    int maxc = (cw - 8) / 7;
    if (maxc < 8) maxc = 8;
    if (maxc > 110) maxc = 110;
    for (int r = 0; r < rows && i < v_len; r++) {
        char lb[128]; int lc = 0;
        while (i < v_len && v_data[i] != '\n' && lc < maxc)
            lb[lc++] = v_data[i++];
        if (i < v_len && v_data[i] == '\n') i++;
        lb[lc] = 0;
        font64_draw_string(fb, W, H, cx, cy + r * 16, lb,
                           g_shell.colors.window_fg, 12);
    }
    char st[64];
    strcpy(st, "line ");
    char tmp[12];
    hs_itoa(v_scroll + 1, tmp);
    strcpy(st + hs_strlen(st), tmp);
    if (v_truncated) strcpy(st + hs_strlen(st), "  (truncated: file too large)");
    else strcpy(st + hs_strlen(st), "  Up/Down/PgUp/PgDn to scroll");
    font64_draw_string(fb, W, H, cx, w->y + w->height - 18, st,
                       g_shell.colors.text_secondary, 10);
}

void hs_files_cycle_view(void) { f_view_mode = !f_view_mode; f_scroll = 0; }

void hs_files_click(int x, int y)
{
    extern void vga_puts(const char *s);
    if (!f_win || !f_win->visible) return;
    /* V0.13.39: 路径栏右侧 USB 按钮点击 */
    {
        extern int usb64_dev_count(void);
        extern int storage_hal_get_device(void);
        extern int usb64_mount_usb(void);
        extern int usb64_umount_usb(void);
        int cx0 = f_win->x + 6, cy0 = f_win->y + 26;
        int cw0 = f_win->width - 12;
        extern int usb64_dev_count(void);
        int has_usb = usb64_dev_count() > 0;
        int on_usb = storage_hal_get_device() == 1;
        const char *txt = has_usb ? (on_usb ? "[USB: mounted]" : "[USB: click to mount]") : "[USB: not detected]";
        int tw = 12 * (hs_strlen(txt)) + 4;
        int bx = cx0 + cw0 - tw - 4;
        if (x >= bx && x <= bx + tw && y >= cy0 && y <= cy0 + 20) {
            int rc = on_usb ? usb64_umount_usb() : usb64_mount_usb();
            hs_strcpyn(f_status2, rc == 0 ? (on_usb ? "USB unmounted" : "USB mounted") : "USB op failed", 60);
            f_path[0] = '/'; f_path[1] = 0;
            f_refresh();
            return;
        }
    }
    /* 路径栏点击(USB 按钮之外) → 打开“转到路径”输入框 */
    {
        int cy0 = f_win->y + 26;
        if (y >= cy0 && y <= cy0 + 20 && x >= f_win->x + 6) {
            f_prompt_start(FS_PROMPT_GOTO, f_path);
            hs_strcpyn(f_status2, "Enter to go, Tab to cancel", FS_PROMPT_MAX);
            return;
        }
    }

    int cy = f_win->y + 26 + 26;
    int rows = f_visible_rows();
    if (f_view_mode == 0) {
        int rel = (y - cy) / FS_ROW_H;
        if (rel >= 0 && rel < rows) {
            int gi = f_scroll + rel;
            if (gi >= 0 && gi < f_match_count) {
                /* 双击检测: 同一项 400ms 内再次点击 → 打开 */
                extern int hpdesk_get_tick(void);
                int now = hpdesk_get_tick();
                int same = (f_sel == gi);
                f_sel = gi;
                if (same && now - f_dclick_tick < 400) {
                    f_dclick_tick = 0;
                    f_open_item(gi);
                    return;
                }
                f_dclick_tick = now;
                if (f_sel < f_scroll) f_scroll = f_sel;
                if (f_sel >= f_scroll + rows) f_scroll = f_sel - rows + 1;
            }
        }
    } else {
        int gs = 76, cols = (f_win->width - 12) / gs;
        if (cols < 1) cols = 1;
        int rel = ((x - (f_win->x + 6 + 6)) / gs) + ((y - cy) / (gs + 8)) * cols;
        int gi = f_scroll + rel;
        if (rel >= 0 && gi >= 0 && gi < f_match_count) {
            extern int hpdesk_get_tick(void);
            int now = hpdesk_get_tick();
            int same = (f_sel == gi);
            f_sel = gi;
            if (same && now - f_dclick_tick < 400) {
                f_dclick_tick = 0;
                f_open_item(gi);
                return;
            }
            f_dclick_tick = now;
        }
    }
}

int hs_files_hit(int x, int y)
{
    if (!f_win || !f_win->visible) return 0;
    if (x < f_win->x + 6 || x > f_win->x + f_win->width - 6) return 0;
    if (y < f_win->y + 26 || y > f_win->y + f_win->height - 20) return 0;
    return 1;
}

void hs_files_do_action(int action)
{
    /* 0=Open 1=Delete 2=Refresh 3=Up 4=NewFolder 5=Rename */
    if (action == 0) { f_open_item(f_sel); }
    else if (action == 1) {
        if (f_sel >= 0 && f_sel < f_match_count) {
            int item = f_matched[f_sel];
            if (f_join(f_del_path, FS_PATH_MAX, f_path, f_items[item]) != 0) {
                hs_strcpyn(f_status2, "Path too long", FS_PROMPT_MAX);
                return;
            }
            hs_strcpyn(f_del_name, f_items[item], 60);
            f_prompt = FS_PROMPT_NONE;
            f_del_confirm = 1;
            hs_strcpyn(f_status2, "Enter to delete, N to cancel", FS_PROMPT_MAX);
        }
    }
    else if (action == 2) {
        f_prompt = FS_PROMPT_NONE;
        f_del_confirm = 0;
        f_refresh();
        hs_strcpyn(f_status2, "Refreshed", FS_PROMPT_MAX);
    }
    else if (action == 3) {
        f_del_confirm = 0;
        f_prompt = FS_PROMPT_NONE;
        f_go_up();
    }
    else if (action == 4) {  /* 新建文件夹 */
        f_prompt_start(FS_PROMPT_MKDIR, "newfolder");
        hs_strcpyn(f_status2, "Enter to create, Tab to cancel", FS_PROMPT_MAX);
    }
    else if (action == 5) {  /* 重命名选中项 */
        if (f_sel >= 0 && f_sel < f_match_count) {
            int item = f_matched[f_sel];
            hs_strcpyn(f_old_name, f_items[item], 60);
            f_prompt_start(FS_PROMPT_RENAME, f_old_name);
            hs_strcpyn(f_status2, "Enter to confirm, Tab to cancel", FS_PROMPT_MAX);
        }
    }
}

static int f_name_bad(const char *s)
{
    if (!s[0]) return 1;
    if (strcmp(s, ".") == 0 || strcmp(s, "..") == 0) return 1;
    for (const char *p = s; *p; p++) {
        if (*p == '/' || *p == '\\') return 1;
    }
    return 0;
}

static void f_select_name(const char *name)
{
    for (int i = 0; i < f_match_count; i++) {
        int item = f_matched[i];
        if (strcmp(f_items[item], name) == 0) {
            f_sel = i;
            int rows = f_visible_rows();
            if (f_sel < f_scroll) f_scroll = f_sel;
            if (f_sel >= f_scroll + rows) f_scroll = f_sel - rows + 1;
            if (f_scroll < 0) f_scroll = 0;
            return;
        }
    }
}

static void f_prompt_commit(void)
{
    if (f_prompt == FS_PROMPT_MKDIR) {
        char name[FS_PROMPT_MAX];
        if (f_prompt_len == 0) strcpy(name, "newfolder");
        else strcpy(name, f_prompt_buf);
        if (f_name_bad(name)) {
            hs_strcpyn(f_status2, "Invalid name (empty or contains /)", FS_PROMPT_MAX);
            return;
        }
        char full[FS_PATH_MAX];
        if (f_join(full, FS_PATH_MAX, f_path, name) != 0) {
            hs_strcpyn(f_status2, "Path too long", FS_PROMPT_MAX);
            return;
        }
        uint32_t sz = 0;
        uint8_t isdir = 0;
        if (ext2_64_stat(full, &sz, &isdir) == 0) {
            hs_strcpyn(f_status2, "Name already exists", FS_PROMPT_MAX);
            return;
        }
        int rc = ext2_64_mkdir(full);
        f_prompt = FS_PROMPT_NONE;
        if (rc == 0) {
            hs_strcpyn(f_status2, "Folder created", FS_PROMPT_MAX);
            f_refresh();
            f_select_name(name);
        } else {
            hs_strcpyn(f_status2, "Create failed (disk full or denied)", FS_PROMPT_MAX);
        }
    } else if (f_prompt == FS_PROMPT_MKFILE) {
        char name[FS_PROMPT_MAX];
        if (f_prompt_len == 0) strcpy(name, "newfile.txt");
        else strcpy(name, f_prompt_buf);
        if (f_name_bad(name)) {
            hs_strcpyn(f_status2, "Invalid name (empty or contains /)", FS_PROMPT_MAX);
            return;
        }
        char full[FS_PATH_MAX];
        if (f_join(full, FS_PATH_MAX, f_path, name) != 0) {
            hs_strcpyn(f_status2, "Path too long", FS_PROMPT_MAX);
            return;
        }
        uint32_t sz = 0;
        uint8_t isdir = 0;
        if (ext2_64_stat(full, &sz, &isdir) == 0) {
            hs_strcpyn(f_status2, "Name already exists", FS_PROMPT_MAX);
            return;
        }
        int rc = ext2_64_create_file(full);
        f_prompt = FS_PROMPT_NONE;
        if (rc == 0) {
            hs_strcpyn(f_status2, "File created", FS_PROMPT_MAX);
            f_refresh();
            f_select_name(name);
        } else {
            hs_strcpyn(f_status2, "Create failed (disk full or denied)", FS_PROMPT_MAX);
        }
    } else if (f_prompt == FS_PROMPT_RENAME) {
        if (f_prompt_len == 0) {
            f_prompt = FS_PROMPT_NONE;
            hs_strcpyn(f_status2, "Rename cancelled", FS_PROMPT_MAX);
            return;
        }
        if (f_name_bad(f_prompt_buf)) {
            hs_strcpyn(f_status2, "Invalid name (empty or contains /)", FS_PROMPT_MAX);
            return;
        }
        if (strcmp(f_prompt_buf, f_old_name) == 0) {
            f_prompt = FS_PROMPT_NONE;
            hs_strcpyn(f_status2, "Name unchanged", FS_PROMPT_MAX);
            return;
        }
        char oldfull[FS_PATH_MAX], newfull[FS_PATH_MAX];
        if (f_join(oldfull, FS_PATH_MAX, f_path, f_old_name) != 0 ||
            f_join(newfull, FS_PATH_MAX, f_path, f_prompt_buf) != 0) {
            hs_strcpyn(f_status2, "Path too long", FS_PROMPT_MAX);
            return;
        }
        uint32_t sz = 0;
        uint8_t isdir = 0;
        if (ext2_64_stat(newfull, &sz, &isdir) == 0) {
            hs_strcpyn(f_status2, "Name already exists", FS_PROMPT_MAX);
            return;
        }
        int rc = ext2_64_rename(oldfull, newfull);
        f_prompt = FS_PROMPT_NONE;
        if (rc == 0) {
            hs_strcpyn(f_status2, "Renamed", FS_PROMPT_MAX);
            f_refresh();
            f_select_name(f_prompt_buf);
        } else {
            hs_strcpyn(f_status2, rc == -2 ? "Rename failed: conflict" : "Rename failed",
                       FS_PROMPT_MAX);
        }
    } else if (f_prompt == FS_PROMPT_GOTO) {
        if (f_prompt_len == 0) {
            f_prompt = FS_PROMPT_NONE;
            hs_strcpyn(f_status2, "Goto cancelled", FS_PROMPT_MAX);
            return;
        }
        char p[FS_PATH_MAX];
        if (f_prompt_buf[0] != '/') {
            p[0] = '/';
            if (f_prompt_len + 1 >= FS_PATH_MAX) {
                hs_strcpyn(f_status2, "Path too long", FS_PROMPT_MAX);
                return;
            }
            strcpy(p + 1, f_prompt_buf);
        } else {
            if (f_prompt_len >= FS_PATH_MAX) {
                hs_strcpyn(f_status2, "Path too long", FS_PROMPT_MAX);
                return;
            }
            strcpy(p, f_prompt_buf);
        }
        int len = hs_strlen(p);
        while (len > 1 && p[len - 1] == '/') { len--; p[len] = 0; }
        uint32_t sz = 0;
        uint8_t isdir = 0;
        if (ext2_64_stat(p, &sz, &isdir) != 0) {
            hs_strcpyn(f_status2, "No such path", FS_PROMPT_MAX);
            return;
        }
        if (!isdir) {
            hs_strcpyn(f_status2, "Not a directory", FS_PROMPT_MAX);
            return;
        }
        strcpy(f_path, p);
        f_prompt = FS_PROMPT_NONE;
        f_clear_filter();
        f_sel = 0;
        f_scroll = 0;
        f_refresh();
        hs_strcpyn(f_status2, "Goto OK", FS_PROMPT_MAX);
    }
}

static void f_delete_commit(void)
{
    uint32_t sz = 0;
    uint8_t isdir = 0;
    int rc = -1;
    if (ext2_64_stat(f_del_path, &sz, &isdir) == 0) {
        rc = isdir ? ext2_64_delete_tree(f_del_path) : ext2_64_delete_file(f_del_path);
    }
    f_del_confirm = 0;
    if (rc == 0) {
        char msg[96];
        strcpy(msg, "Deleted ");
        hs_strcpyn(msg + hs_strlen(msg), f_del_name, 40);
        hs_strcpyn(f_status2, msg, FS_PROMPT_MAX);
    } else {
        hs_strcpyn(f_status2, rc == -2 ? "Delete failed: too deep" : "Delete failed",
                   FS_PROMPT_MAX);
    }
    f_refresh();
}

void hs_files_key(int key)
{
    /* 属性浮层: 任意键关闭 */
    if (f_props) { f_props = 0; return; }

    /* 删除确认优先 */
    if (f_del_confirm) {
        if (key == '\n' || key == '\r' || key == 0x1C) {
            f_delete_commit();
        } else if (key == 'n' || key == 'N' || key == '\t') {
            f_del_confirm = 0;
            hs_strcpyn(f_status2, "Delete cancelled", FS_PROMPT_MAX);
        }
        return;
    }

    /* 输入框优先: 可打印字符入缓冲区 */
    if (f_prompt != FS_PROMPT_NONE) {
        if (key == '\b' || key == 0x7F || key == 0x0E) {
            if (f_prompt_len > 0) {
                f_prompt_len--;
                f_prompt_buf[f_prompt_len] = 0;
            }
            return;
        }
        if (key >= 0x20 && key <= 0x7E) {
            if (f_prompt_len < FS_PROMPT_MAX - 1) {
                f_prompt_buf[f_prompt_len++] = (char)key;
                f_prompt_buf[f_prompt_len] = 0;
            }
            return;
        }
        if (key == '\n' || key == '\r' || key == 0x1C) {
            f_prompt_commit();
            return;
        }
        if (key == '\t') {
            f_prompt = FS_PROMPT_NONE;
            hs_strcpyn(f_status2, "Cancelled", FS_PROMPT_MAX);
            return;
        }
        return;
    }

    /* 快捷键:
     * F2 重命名 / F3 属性 / F4 转到路径 / F5 刷新 / F6 复制 / F7 新建文件夹
     * F8 剪切 / F9 粘贴 / F10 新建文件 / Delete 删除 / Insert 标记 */
    if (key == 0x3C) { hs_files_do_action(5); return; }
    if (key == 0x3D) { f_props_open(); return; }
    if (key == 0x3E) {
        f_prompt_start(FS_PROMPT_GOTO, f_path);
        hs_strcpyn(f_status2, "Enter to go, Tab to cancel", FS_PROMPT_MAX);
        return;
    }
    if (key == 0x3F) { hs_files_do_action(2); return; }
    if (key == 0x40) { f_do_copy(0); return; }
    if (key == 0x41) { hs_files_do_action(4); return; }
    if (key == 0x42) { f_do_copy(1); return; }
    if (key == 0x43) { f_do_paste(); return; }
    if (key == 0x44) {
        f_prompt_start(FS_PROMPT_MKFILE, "newfile.txt");
        hs_strcpyn(f_status2, "Enter to create, Tab to cancel", FS_PROMPT_MAX);
        return;
    }
    if (key == KEY_EXT_DEL) { hs_files_do_action(1); return; }
    if (key == KEY_EXT_INS) {
        if (f_match_count > 0) {
            int item = f_matched[f_sel];
            f_mark[item] = !f_mark[item];
            hs_strcpyn(f_status2, f_mark[item] ? "Marked" : "Unmarked", FS_PROMPT_MAX);
        }
        return;
    }

    if (key == '\t') {
        f_view_mode = !f_view_mode;
        f_scroll = 0;
        return;
    }

    /* 可打印字符 → 过滤 */
    if (key >= 0x20 && key <= 0x7E) {
        if (f_filter_len < 31) {
            f_filter[f_filter_len++] = (char)key;
            f_filter[f_filter_len] = 0;
            f_sel = 0;
            f_scroll = 0;
            f_refresh();
        }
        return;
    }
    if (key == '\b' || key == 0x0E || key == 0x7F) {
        if (f_filter_len > 0) {
            f_filter[--f_filter_len] = 0;
            f_refresh();
            return;
        }
        /* 无过滤时退格 = 上级目录 */
        f_go_up();
        return;
    }

    int rows = f_visible_rows();
    int max_scroll = f_match_count - rows;
    if (max_scroll < 0) max_scroll = 0;

    if (key == KEY_EXT_UP) {
        if (f_sel > 0) f_sel--;
    } else if (key == KEY_EXT_DOWN) {
        if (f_sel < f_match_count - 1) f_sel++;
    } else if (key == KEY_EXT_PGUP) {
        f_sel -= rows;
        if (f_sel < 0) f_sel = 0;
    } else if (key == KEY_EXT_PGDN) {
        f_sel += rows;
        if (f_sel >= f_match_count) f_sel = f_match_count - 1;
        if (f_sel < 0) f_sel = 0;
    } else if (key == KEY_EXT_HOME) {
        f_sel = 0;
        f_scroll = 0;
        return;
    } else if (key == KEY_EXT_END) {
        f_sel = f_match_count - 1;
        if (f_sel < 0) f_sel = 0;
        f_scroll = max_scroll;
        return;
    } else if (key == '\n' || key == '\r' || key == 0x1C) {
        f_open_item(f_sel);
        return;
    } else {
        return;
    }

    /* 保持选中项在可视区 */
    if (f_sel < f_scroll) f_scroll = f_sel;
    if (f_sel >= f_scroll + rows) f_scroll = f_sel - rows + 1;
    if (f_scroll > max_scroll) f_scroll = max_scroll;
    if (f_scroll < 0) f_scroll = 0;
}

static void f_handle_key(int key)
{
    hs_files_key(key);
}

/* ============================================================================
 * 任务管理器
 * ========================================================================== */
static hs_window_t *m_win = 0;
/* V0.13.46: 任务管理器结束进程按键状态 */
static int m_kill_mode = 0;      /* 0=普通, 1=输入PID */
static int m_kill_pid = 0;
static int m_tick = 0;

/* V0.13.39: 实时曲线历史 (CPU 模拟 / 网络真实) */
#define M_HIST 72
static int m_cpu_hist[M_HIST];
static int m_rx_hist[M_HIST];
static int m_tx_hist[M_HIST];
static int m_hist_count = 0;

static void m_hist_push(int *hist, int val)
{
    for (int i = M_HIST - 1; i > 0; i--) hist[i] = hist[i - 1];
    hist[0] = val;
    if (m_hist_count < M_HIST) m_hist_count++;
}

static void m_draw_curve(void *fb, int W, int H, int x, int y, int w, int h,
                         int *hist, uint32_t color, int maxv)
{
    /* 背景 + 边框 */
    gfx64_fill_rect(x, y, w, h, 0x000000);
    gfx64_draw_rect(x, y, w, h, g_shell.colors.border);
    /* 中位参考线 */
    gfx64_draw_line(x, y + h / 2, x + w, y + h / 2, 0x222222);
    /* 折线 (右起最新) */
    int n = (m_hist_count > w / 2) ? w / 2 : m_hist_count;
    int prev_x = -1, prev_y = -1;
    for (int i = 0; i < n; i++) {
        int v = hist[i];
        if (v > maxv) v = maxv;
        if (v < 0) v = 0;
        int px = x + w - 2 - i * 2;
        int py = y + h - 2 - (v * (h - 4)) / maxv;
        if (prev_x >= 0) gfx64_draw_line(prev_x, prev_y, px, py, color);
        prev_x = px; prev_y = py;
    }
}

static void m_render(hs_window_t *w)
{
    void *fb = gfx64_get_framebuffer();
    int W = g_shell.screen_width, H = g_shell.screen_height;
    int cx = w->x + 6, cy = w->y + 26;
    int cw = w->width - 12;

    m_tick++;
    /* 标题 */
    font64_draw_string(fb, W, H, cx, cy, "Task Manager", g_shell.colors.accent, 16);
    cy += 24;

    /* V0.13.39: 真实系统摘要 (PMM/进程/驱动) */
    extern void pmm64_get_stats(uint64_t *total, uint64_t *used, uint64_t *free);
    extern int process64_get_count(void);
    extern int drv_get_count(void);
    uint64_t mt = 0, mu = 0, mf = 0;
    pmm64_get_stats(&mt, &mu, &mf);
    char buf[96]; char tmp[16];
    int pcnt = process64_get_count();
    int dcount = drv_get_count();
    strcpy(buf, "MEM: ");
    hs_uitoa64(mu / 1024, tmp); strcpy(buf + hs_strlen(buf), tmp);
    strcpy(buf + hs_strlen(buf), "/");
    hs_uitoa64(mt / 1024, tmp); strcpy(buf + hs_strlen(buf), tmp);
    strcpy(buf + hs_strlen(buf), " KB   PROC: ");
    hs_itoa(pcnt, tmp); strcpy(buf + hs_strlen(buf), tmp);
    strcpy(buf + hs_strlen(buf), "   DRV: ");
    hs_itoa(dcount, tmp); strcpy(buf + hs_strlen(buf), tmp);
    font64_draw_string(fb, W, H, cx, cy, buf, g_shell.colors.window_fg, 12);
    cy += 18;

    /* 实时曲线: CPU + 网络 */
    int cpu = hs_process_get_cpu_usage();
    uint64_t tx = 0, rx = 0; uint32_t txr = 0, rxr = 0;
    e1000_get_net_stats(&tx, &rx, &txr, &rxr);
    m_hist_push(m_cpu_hist, cpu);
    m_hist_push(m_rx_hist, (int)(rxr / 1024));   /* KB/s */
    m_hist_push(m_tx_hist, (int)(txr / 1024));

    int gh = 52, gw = cw - 80;
    m_draw_curve(fb, W, H, cx, cy, gw, gh, m_cpu_hist, 0x52C41A, 100);
    font64_draw_string(fb, W, H, cx + gw + 6, cy + 6, "CPU%", 0x52C41A, 10);
    m_draw_curve(fb, W, H, cx, cy + gh + 4, gw, gh, m_rx_hist, 0x4A9EEC, 256);
    font64_draw_string(fb, W, H, cx + gw + 6, cy + gh + 10, "RX", 0x4A9EEC, 10);
    m_draw_curve(fb, W, H, cx, cy + 2 * (gh + 4), gw, gh, m_tx_hist, 0xE0A44E, 256);
    font64_draw_string(fb, W, H, cx + gw + 6, cy + 2 * (gh + 4) + 10, "TX", 0xE0A44E, 10);
    cy += 3 * gh + 12;

    /* 分隔线 */
    gfx64_fill_rect(cx, cy, cw, 1, g_shell.colors.border);
    cy += 6;

    /* 表头 (V0.13.46: 加PID与状态列) */
    font64_draw_string(fb, W, H, cx, cy, "PID  NAME          CPU   MEM     STATE",
                       g_shell.colors.text_secondary, 10);
    cy += 14;

    /* V0.13.39: 真实用户进程 + 内核进程, CPU 降序 */
    extern int user_process_list(void *list, int max);
    extern void vga_puts(const char *s);
    hs_process_monitor_update();
    int pcount = 0;
    hs_process_info_t *procs = hs_process_get_list(&pcount);

    /* 简化排序: 模拟进程按 CPU 降序 (8 个冒泡) */
    for (int i = 0; i < pcount; i++) {
        for (int j = i + 1; j < pcount; j++) {
            if (procs[j].cpu_usage > procs[i].cpu_usage) {
                hs_process_info_t t2 = procs[i];
                procs[i] = procs[j];
                procs[j] = t2;
            }
        }
    }

    if (procs && pcount > 0) {
        for (int i = 0; i < pcount && cy < w->y + w->height - 28; i++) {
            char line[96];
            /* PID */
            hs_itoa(procs[i].pid, tmp);
            strcpy(line, tmp);
            while (hs_strlen(line) < 6) strcpy(line + hs_strlen(line), " ");
            /* NAME */
            strcpy(line + hs_strlen(line), procs[i].name);
            while (hs_strlen(line) < 20) strcpy(line + hs_strlen(line), " ");
            /* CPU */
            hs_itoa(procs[i].cpu_usage, tmp);
            strcpy(line + hs_strlen(line), tmp);
            strcpy(line + hs_strlen(line), "%");
            while (hs_strlen(line) < 28) strcpy(line + hs_strlen(line), " ");
            /* MEM */
            hs_itoa(procs[i].mem_usage, tmp);
            strcpy(line + hs_strlen(line), tmp);
            while (hs_strlen(line) < 38) strcpy(line + hs_strlen(line), " ");
            /* STATE */
            if (procs[i].pid >= 100 && procs[i].state == 0) {
                strcpy(line + hs_strlen(line), "RUN");
            } else if (procs[i].pid >= 100) {
                strcpy(line + hs_strlen(line), "idle");
            } else {
                strcpy(line + hs_strlen(line), "sys");
            }
            /* 运行中进程高亮 */
            int col = g_shell.colors.window_fg;
            if (procs[i].pid >= 100 && procs[i].state == 0) col = 0x52C41A;
            font64_draw_string(fb, W, H, cx, cy, line, col, 12);
            cy += 15;
        }
    }

    /* V0.13.46: 结束进程提示条 */
    cy += 2;
    if (m_kill_mode) {
        char kbuf[48];
        strcpy(kbuf, "End PID: ");
        hs_itoa(m_kill_pid, tmp);
        strcpy(kbuf + hs_strlen(kbuf), tmp);
        strcpy(kbuf + hs_strlen(kbuf), " (Enter=end, Esc=cancel)");
        font64_draw_string(fb, W, H, cx, cy, kbuf, 0xE0A44E, 10);
    } else {
        font64_draw_string(fb, W, H, cx, cy, "Press K then PID + Enter to end a process",
                           g_shell.colors.text_secondary, 10);
    }
}

/* ============================================================================
 * 对外接口
 * ========================================================================== */
hs_window_t *hs_terminal_create(void)
{
    if (t_win && t_win->visible) { t_win->focus = 1; return t_win; }
    t_win = hs_window_create(HS_WINDOW_FLOATING, "Terminal", -1, -1, 520, 340);
    if (t_win) {
        t_win->render = t_render;
        t_put("Hepu Terminal v0.56.1");
        t_put("Type 'help' for commands.");
        extern void vga_puts(const char *s);
        vga_puts("[HSAPPS] terminal created\n");
    } else {
        extern void vga_puts(const char *s);
        vga_puts("[HSAPPS] terminal create FAILED\n");
    }
    return t_win;
}

hs_window_t *hs_files_create(void)
{
    if (f_win && f_win->visible) { f_win->focus = 1; return f_win; }
    f_win = hs_window_create(HS_WINDOW_FLOATING, "Files", -1, -1, 460, 360);
    if (f_win) {
        f_win->render = f_render;
        strcpy(f_path, "/");
        f_refresh();
    }
    return f_win;
}

hs_window_t *hs_taskmgr_create(void)
{
    if (m_win && m_win->visible) { m_win->focus = 1; return m_win; }
    m_win = hs_window_create(HS_WINDOW_FLOATING, "Task Manager", -1, -1, 520, 380);
    if (m_win) m_win->render = m_render;
    return m_win;
}


static void m_kill_do(void)
{
    extern int user_process_kill(int pid);
    if (m_kill_pid > 0) {
        user_process_kill(m_kill_pid);
        m_kill_pid = 0;
    }
    m_kill_mode = 0;
}

/* ============================================================================
 * 代码编辑器窗口 (V0.54.7)
 * ----------------------------------------------------------------------------
 * 轻量文本编辑器: 16KB 平铺缓冲区 / 行号 / 光标 / 方向键 / Ctrl+S 保存。
 * 默认打开 /etc/hepushell/config.hsqml；Ctrl+S 原地写回 ext2。
 * ========================================================================== */
#define ED_CAP        16384
#define ED_LINE_H     16
#define ED_GUTTER     42

static hs_window_t *e_win = 0;
static char e_buf[ED_CAP + 1];
static int  e_len = 0;
static int  e_cur = 0;
static int  e_scroll = 0;
static int  e_modified = 0;
static int  e_loaded = 0;
static char e_path[96] = "/etc/hepushell/config.hsqml";
static char e_msg[64] = "Ctrl+S / F5 save | Ctrl+W close";

static int ed_line_start(int pos)
{
    while (pos > 0 && e_buf[pos - 1] != '\n') pos--;
    return pos;
}

static int ed_line_end(int pos)
{
    while (pos < e_len && e_buf[pos] != '\n') pos++;
    return pos;
}

static int ed_line_of(int pos)
{
    int line = 0;
    for (int i = 0; i < pos; i++)
        if (e_buf[i] == '\n') line++;
    return line;
}

static void ed_insert(char c)
{
    if (e_len >= ED_CAP) { hs_strcpyn(e_msg, "Buffer full", 64); return; }
    for (int i = e_len; i > e_cur; i--) e_buf[i] = e_buf[i - 1];
    e_buf[e_cur] = c;
    e_len++;
    e_cur++;
    e_buf[e_len] = 0;
    e_modified = 1;
}

static void ed_delete_before(void)
{
    if (e_cur <= 0) return;
    for (int i = e_cur - 1; i < e_len - 1; i++) e_buf[i] = e_buf[i + 1];
    e_len--;
    e_cur--;
    e_buf[e_len] = 0;
    e_modified = 1;
}

static void ed_move_vert(int dir)
{
    int ls = ed_line_start(e_cur);
    int col = e_cur - ls;
    if (dir < 0) {
        if (ls == 0) { e_cur = 0; return; }
        int prev_end = ls - 1;
        int prev_start = ed_line_start(prev_end);
        int prev_len = prev_end - prev_start;
        e_cur = prev_start + (col > prev_len ? prev_len : col);
    } else {
        int le = ed_line_end(e_cur);
        if (le >= e_len) { e_cur = e_len; return; }
        int next_start = le + 1;
        int next_end = ed_line_end(next_start);
        int next_len = next_end - next_start;
        e_cur = next_start + (col > next_len ? next_len : col);
    }
}

static void ed_load(void)
{
    extern int ext2_64_read_file(const char *path, uint8_t *buf, uint32_t max);
    int n = ext2_64_read_file(e_path, (uint8_t *)e_buf, ED_CAP);
    if (n < 0) {
        e_len = 0;
        e_buf[0] = 0;
        hs_strcpyn(e_msg, "New file (F5 to save)", 64);
    } else {
        e_len = n;
        e_buf[n] = 0;
        hs_strcpyn(e_msg, "Loaded", 64);
    }
    e_cur = 0;
    e_scroll = 0;
    e_modified = 0;
}

static void ed_save(void)
{
    extern int ext2_64_write_file(const char *path, const uint8_t *data, uint32_t size);
    int rc = ext2_64_write_file(e_path, (const uint8_t *)e_buf, (uint32_t)e_len);
    /* 成功返回写入字节数 (>=0)，失败返回 -1 */
    if (rc >= 0) {
        hs_strcpyn(e_msg, "Saved", 64);
        e_modified = 0;
    } else {
        hs_strcpyn(e_msg, "Save failed", 64);
    }
}

static void ed_handle_key(int key)
{
    if (key == 0x3F) { ed_save(); return; }                 /* F5 保存 */
    if (key >= 32 && key <= 126) { ed_insert((char)key); return; }
    if (key == '\n' || key == 13 || key == 0x1C) { ed_insert('\n'); return; }
    if (key == '\b' || key == 0x0E || key == 127) { ed_delete_before(); return; }
    if (key == 0x90) { ed_move_vert(-1); return; }          /* Up    */
    if (key == 0x91) { ed_move_vert(1);  return; }          /* Down  */
    if (key == 0x92) { e_cur = ed_line_start(e_cur); return; } /* Left  */
    if (key == 0x93) { e_cur = ed_line_end(e_cur);   return; } /* Right */
    if (key == 0x96) { e_cur = ed_line_start(e_cur); return; } /* Home  */
    if (key == 0x97) { e_cur = ed_line_end(e_cur);   return; } /* End   */
}

static void e_render(hs_window_t *w)
{
    void *fb = gfx64_get_framebuffer();
    int W = g_shell.screen_width, H = g_shell.screen_height;
    int cx = w->x + 8;
    int cy = w->y + 26;
    uint32_t accent = g_shell.colors.accent;
    uint32_t fg = g_shell.colors.window_fg;
    uint32_t dim = g_shell.colors.text_secondary;
    int vis = (w->height - 82) / ED_LINE_H;
    if (vis < 1) vis = 1;

    /* 标题: 路径 + 修改标记 */
    char title[128];
    hs_strcpyn(title, e_path, 100);
    if (e_modified) hs_strcpyn(title + hs_strlen(title), "  *", 8);
    font64_draw_string(fb, W, H, cx, cy, title, accent, 13);
    cy += 18;
    font64_draw_string(fb, W, H, cx, cy, e_msg, dim, 10);
    cy += 16;

    /* 保持光标行可见 */
    int cline = ed_line_of(e_cur);
    if (cline < e_scroll) e_scroll = cline;
    if (cline >= e_scroll + vis) e_scroll = cline - vis + 1;
    if (e_scroll < 0) e_scroll = 0;

    /* 定位滚动起点 */
    int pos = 0;
    for (int l = 0; l < e_scroll && pos < e_len; l++) {
        while (pos < e_len && e_buf[pos] != '\n') pos++;
        if (pos < e_len) pos++;
    }

    int maxc = (w->width - ED_GUTTER - 16) / 7;
    if (maxc < 8) maxc = 8;

    int cursor_drawn = 0;
    for (int row = 0; row < vis; row++) {
        int lstart = pos;
        int lend = ed_line_end(pos);
        char num[8];
        hs_itoa(e_scroll + row + 1, num);
        font64_draw_string(fb, W, H, cx, cy + row * ED_LINE_H, num, dim, 11);

        char lb[168];
        int lc = 0;
        int i = lstart;
        while (i < lend && lc < maxc && lc < 167) lb[lc++] = e_buf[i++];
        lb[lc] = 0;
        if (lc > 0)
            font64_draw_string(fb, W, H, cx + ED_GUTTER, cy + row * ED_LINE_H, lb, fg, 12);

        if (!cursor_drawn && e_cur >= lstart && e_cur <= lend) {
            int col = e_cur - lstart;
            if (col > maxc) col = maxc;
            gfx64_fill_rect(cx + ED_GUTTER + col * 7, cy + row * ED_LINE_H + 2,
                            2, 13, accent);
            cursor_drawn = 1;
        }

        pos = lend;
        if (pos >= e_len) break;
        pos++;
    }
}

hs_window_t *hs_editor_create(void)
{
    if (e_win && e_win->visible) {
        e_win->focus = 1;
        return e_win;
    }
    if (!e_win) {
        e_win = hs_window_create(HS_WINDOW_FLOATING, "Editor", -1, -1, 640, 440);
        if (e_win) e_win->render = e_render;
    }
    if (e_win) {
        if (!e_loaded) { ed_load(); e_loaded = 1; }
        hs_window_show(e_win);
        e_win->focus = 1;
    }
    return e_win;
}

hs_window_t *hs_editor_open_file(const char *path)
{
    if (!path || !path[0]) return 0;
    if (hs_strlen(path) >= (int)sizeof(e_path)) return 0;
    if (!e_win) {
        e_win = hs_window_create(HS_WINDOW_FLOATING, "Editor", -1, -1, 640, 440);
        if (e_win) e_win->render = e_render;
    }
    if (!e_win) return 0;
    /* 同一文件且未保存时不重新载入，避免丢掉编辑内容 */
    if (!(e_modified && strcmp(e_path, path) == 0)) {
        strcpy(e_path, path);
        ed_load();
        e_loaded = 1;
    }
    hs_window_show(e_win);
    e_win->focus = 1;
    return e_win;
}

int hs_apps_editor_save(void)
{
    if (!e_win || !e_win->visible) return 0;
    if (hs_window_get_focused() != e_win) return 0;
    ed_save();
    return 1;
}

int hs_apps_editor_close(void)
{
    if (!e_win || !e_win->visible) return 0;
    if (hs_window_get_focused() != e_win) return 0;
    hs_window_hide(e_win);
    return 1;
}

/* 键盘转发: 聚焦窗口为应用时消费按键 */
int hs_apps_handle_key(int key)
{
    hs_window_t *foc = hs_window_get_focused();
    if (!foc) return 0;
    if (t_win && foc == t_win) { t_handle_key(key); return 1; }
    if (f_win && foc == f_win) { f_handle_key(key); return 1; }
    if (v_win && foc == v_win) { v_handle_key(key); return 1; }
    if (e_win && foc == e_win) { ed_handle_key(key); return 1; }
    if (m_win && foc == m_win) {
        /* 任务管理器: K 进入结束模式, 数字累积PID, Enter执行 */
        if (m_kill_mode) {
            if (key >= '0' && key <= '9') {
                m_kill_pid = m_kill_pid * 10 + (key - '0');
                if (m_kill_pid > 9999) m_kill_pid = 9999;
            } else if (key == 13 || key == '\n') {
                m_kill_do();
            } else if (key == 27) {
                m_kill_mode = 0;
                m_kill_pid = 0;
            }
        } else if (key == 'k' || key == 'K') {
            m_kill_mode = 1;
            m_kill_pid = 0;
        }
        return 1;
    }
    return 0;
}

/* 滚轮转发: 终端滚动 */
void hs_apps_handle_wheel(int delta)
{
    hs_window_t *foc = hs_window_get_focused();

    /* 文件管理器: 滚轮滚动列表 */
    if (f_win && f_win->visible && foc == f_win) {
        int rows = f_visible_rows();
        int max_scroll = f_match_count - rows;
        if (max_scroll < 0) max_scroll = 0;
        if (delta > 0) f_scroll -= 3;
        else if (delta < 0) f_scroll += 3;
        if (f_scroll < 0) f_scroll = 0;
        if (f_scroll > max_scroll) f_scroll = max_scroll;
        if (f_sel < f_scroll) f_sel = f_scroll;
        if (f_sel >= f_scroll + rows) f_sel = f_scroll + rows - 1;
        return;
    }

    /* 查看器: 滚轮按行滚动 */
    if (v_win && v_win->visible && foc == v_win) {
        if (delta > 0) v_handle_key(KEY_EXT_UP);
        else if (delta < 0) v_handle_key(KEY_EXT_DOWN);
        return;
    }

    if (!t_win || !t_win->visible) return;
    if (delta > 0) {
        if (t_line_count > 8) {
            t_scroll++;
            if (t_scroll > t_line_count - 8) t_scroll = t_line_count - 8;
        }
    } else if (delta < 0) {
        if (t_scroll > 0) t_scroll--;
    }
}
