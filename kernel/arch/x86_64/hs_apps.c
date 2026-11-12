/* ============================================================================
 * hs_apps.c — 应用窗口实现 (V0.13.10 重建)
 * 终端 / 文件管理器 / 任务管理器
 * 渲染由 hpdesk 窗口系统调用 (w->render 回调), 键盘经 hs_apps_handle_key
 * ========================================================================== */

#include "hpdesk.h"
#include "png64.h"
#include "jpeg64.h"
#include "hs_apps.h"
#include "hsservices.h"
#include "hsinput.h"   /* KEY_EXT_* 扩展键统一码 */
#include "../../shared/drivers/driver_framework.h"

/* 外部图形/系统接口 */
extern void gfx64_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void gfx64_fill_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
/* 桌面缩放入口(见 hpdesk.c), 窗口创建尺寸必须与桌面同一套 DPI 语义 */
extern int hs_ui_sx(int v);
extern int hs_ui_sy(int v);
extern int hs_win_k(hs_window_t *w, int base_w, int v);
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
extern int ext2_64_delete_dir(const char *path);
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
        t_put("Hepu OS v0.57.0 (x86_64)");
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
    /* 终端内容按窗口自身尺寸缩放(基准 520x340)。窗口被 DPI 放大时内容跟着放大,
     * 不会出现"窗口变大、内容只占左上角"的留白 —— 这与 Settings 的做法一致,
     * 区别是这里用窗口自身尺寸做比例, 不依赖全局缩放因子, 更耐改。 */
#define TX(v) hs_win_k(w, 520, v)
#define TY(v) ((v) * w->height / 340)
    int W = g_shell.screen_width, H = g_shell.screen_height;
    int cx = w->x + TX(6), cy = w->y + TY(26);
    int ch = w->height - TY(26) - TY(24);
    int row_h = TY(16);
    /* 预留 20px 给底部输入行, 避免最后一行输出与提示行重叠 */
    int visible = (ch - TY(20)) / row_h;
    if (visible < 1) visible = 1;

    /* V0.13.39: 经典终端配色 — 黑底绿字(固定, 不随主题) */
    uint32_t TERM_BG = 0x000000;
    uint32_t TERM_FG = 0x33FF33;
    uint32_t TERM_HI = 0x66FF66;
    gfx64_fill_rect(w->x + TX(3), w->y + TY(24), w->width - TX(6), w->height - TY(28), TERM_BG);
    gfx64_fill_rect(w->x + TX(3), w->y + TY(22) + ch, w->width - TX(6), TY(26), 0x0A0F0A);

    /* 输出区: 从 t_scroll 偏移显示 */
    int start = t_line_count - visible - t_scroll;
    if (start < 0) start = 0;
    for (int r = 0; r < visible; r++) {
        int li = start + r;
        if (li < t_line_count) {
            font64_draw_string(fb, W, H, cx, cy + r * row_h,
                               t_lines[li], TERM_FG, TY(12));
        }
    }

    /* 输入行 */
    int iy = cy + ch - TY(20);
    char prompt[72];
    strcpy(prompt, "> ");
    hs_strcpyn(prompt + 2, t_input, TERM_INPUT_MAX);
    font64_draw_string(fb, W, H, cx, iy, prompt, TERM_HI, TY(12));

    /* 光标 (闪烁块, 2 帧周期) */
    t_frame++;
    if ((t_frame & 1) == 0) {
        int tx = cx + (2 + t_in_len) * TX(6);
        gfx64_fill_rect(tx, iy + TY(1), TX(7), TY(13), TERM_FG);
    }
#undef TX
#undef TY
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
#define FS_PROMPT_SEARCH 5

/* 用户自定义快捷目录: 钉在路径栏下面的那排胶囊, 持久化到 /hs-quick.txt */
#define FS_QUICK_MAX 6
#define FS_QUICK_CFG "/hs-quick.txt"

#define FS_CLIP_MAX 8
#define FS_COPY_BUF (128 * 1024)
#define FS_COPY_ENTRIES 64

static hs_window_t *f_win = 0;
#define FS_BASE_W 460

/* Files 窗口内容按自身尺寸缩放(基准 460x360)。
 * 窗口被 DPI 放大时内容同比例放大, 基准尺寸下恒等于原值。
 * 行高也走这里 —— 渲染、f_visible_rows()、命中测试必须共用同一个行高,
 * 否则会出现"看得见点不到/点错行"。 */
static int f_sc(int v) { return hs_win_k(f_win, FS_BASE_W, v); }
#define FS(v) f_sc(v)
static int f_row_h(void) { return f_sc(FS_ROW_H); }
static char f_path[FS_PATH_MAX] = "/";
static char f_items[FS_MAX_ITEMS][256];
static uint8_t f_types[FS_MAX_ITEMS];
static uint32_t f_sizes[FS_MAX_ITEMS];
static int f_count = 0;
static int f_sel = 0;
static int f_scroll = 0;
static char f_status[64] = "";
/* 过滤 / 视图 / 菜单 / 查看器 */
static char f_quick[FS_QUICK_MAX][FS_PATH_MAX];
static int  f_quick_n = 0;
static char f_filter[32] = "";
static int f_filter_len = 0;
static int f_matched[FS_MAX_ITEMS];   /* 过滤/排序后的显示索引 */
static int f_match_count = 0;
static int f_view_mode = 0;           /* 0=列表, 1=网格 */
/* 排序: 0=名称 1=大小 2=类型; desc=1 时反向。目录恒优先(两种方向都保持目录在前) */
static int f_sort_key = 0;
static int f_sort_desc = 0;
static int f_kind(int item);
static void f_job_report(void);
static void f_tree_reveal(void);
static void f_tree_sync(void);
static int f_tree_row_at(int x, int y);
static int f_job_push_dir(const char *src, const char *dst);
/* V0.55.1: 文件菜单状态当前未被使用(统一走桌面菜单), 先标注保留意图 */
static int f_fs_menu __attribute__((unused)) = 0;   /* 0=桌面菜单, 1=文件菜单 */
static int f_dclick_tick = 0;         /* 双击检测: 上次点击 tick */
static char f_status2[FS_PROMPT_MAX + 1] = "";

/* P3: 左树右列表 —— 左栏是"目录层级树", 只列目录, 可展开/折叠/点击跳转。
 * 树节点用扁平数组表示(路径 + 缩进深度), 展开即在当前位置之后插入子节点,
 * 折叠即删掉其后所有更深的节点 —— 比指针树好维护, 也不用动态内存。 */
#define FS_TREE_MAX 64
#define FS_TREE_CHILD 64
#define FS_TREE_W 140
static char f_tree_path[FS_TREE_MAX][FS_PATH_MAX];
static int  f_tree_depth[FS_TREE_MAX];
static int  f_tree_exp[FS_TREE_MAX];
static int  f_tree_n;
static int  f_tree_sel;
/* 列举子目录的临时缓冲(不能复用 f_items, 否则会把右栏列表冲掉) */
static char     f_tnm[FS_TREE_CHILD][256];   /* 跨步必须 256: ext2_64_list_dir_entries_ex 的约定 */
static uint8_t  f_ttp[FS_TREE_CHILD];
static uint32_t f_tsz[FS_TREE_CHILD];

/* P3: 多标签 —— 每个标签记住自己的目录与视图状态, 切换时互相还原。
 * 标签栏画在标题栏里(不占内容区高度), 免得动已经耦合紧密的列表几何。 */
#define FS_TAB_MAX 4
static char f_tab_path[FS_TAB_MAX][FS_PATH_MAX];
static int  f_tab_scroll[FS_TAB_MAX], f_tab_sel[FS_TAB_MAX];
static int  f_tab_view[FS_TAB_MAX], f_tab_skey[FS_TAB_MAX], f_tab_sdesc[FS_TAB_MAX];
static int  f_tab_count, f_tab_cur;

/* P3: 拖拽移动状态 —— 按住列表/网格某一项, 拖到目录项上松手 = 移进去。
 *   0=空闲 1=按住但未越过阈值 2=拖拽中(只有此时才画拖影、才改光标形态) */
static int f_drag_state;
static int f_drag_item;
static int f_drag_x0, f_drag_y0, f_drag_x, f_drag_y;
static int f_drop_item;
static int f_drop_tree;   /* 拖到左树某一行时的目标节点, -1=不在树上 */

/* FS P2: 分片拷贝/移动任务状态 (实现见 f_do_paste 之前; 结构需前置供 f_render 使用) */
#define F_JOB_Q 24
#define F_JOB_PER_FRAME 1
static struct {
    int active;
    int kind;                        /* 1=copy 2=move */
    int ok, fail, done;
    char why[24];
    int qh, qt;                      /* 目录 BFS 队列 */
    char qs[F_JOB_Q][FS_PATH_MAX];
    char qd[F_JOB_Q][FS_PATH_MAX];
    int cur_n, cur_i;
    char names[FS_COPY_ENTRIES][256];
    uint8_t types[FS_COPY_ENTRIES];
    uint32_t sizes[FS_COPY_ENTRIES];
    int top_i, top_n;
    int topdir_n;
    char tdsrc[FS_CLIP_MAX][FS_PATH_MAX];   /* 顶层目录源(移动时整树删除) */
    /* 删除任务: 先遍历删文件, 记下沿途目录, 最后逆序删空目录 */
    int phase;                              /* 0=遍历 1=回收目录 */
    int ddn;
    char ddel[48][FS_PATH_MAX];
    char ts[FS_CLIP_MAX][FS_PATH_MAX];
    char td[FS_CLIP_MAX][FS_PATH_MAX];
    char tn[FS_CLIP_MAX][64];
    char cur_name[64];
} f_job;
static void f_job_step(void);
static void f_job_step_delete(void);
static void f_job_start_delete(const char *const *paths, const char *const *names, int n);
static void f_job_cancel(void);
static int f_job_cancel_hit(int x, int y);

static hs_window_t *v_win = 0;
/* Viewer(460x400) 与 Image(520x420) 共用同一个 render, 基准宽由打开时记录 */
static int v_base_w = 460;
#define VC(v) (v * (v_win ? v_win->width : v_base_w) / (v_base_w ? v_base_w : 460))
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
static int f_del_multi = 0;          /* >0: 待删项数(多选), 0 则删当前选中项 */
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
    int ch = h - FS(26) - FS(40);          /* 内容区高度 (状态栏 40px 已扣除) */
    /* 20 路径栏 + 18 位置条 + 16 列头 + 26 底部输入/确认条 + 余量 */
    int rows = (ch - FS(86)) / f_row_h();
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

/* 目录优先，再按当前排序列；同列值相等时退回名称，保证顺序稳定 */
static int f_cmp_items(int ia, int ib)
{
    int r = 0;
    if (f_types[ia] != f_types[ib]) return f_types[ib] - f_types[ia];
    if (f_sort_key == 1 && f_sizes[ia] != f_sizes[ib])
        r = (int)f_sizes[ia] - (int)f_sizes[ib];
    else if (f_sort_key == 2) {
        int ka = f_kind(ia), kb = f_kind(ib);
        if (ka != kb) r = ka - kb;
    }
    if (r == 0) r = f_name_cmp(f_items[ia], f_items[ib]);
    return f_sort_desc ? -r : r;
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
    f_tree_sync();     /* 先让已展开的节点跟上磁盘(新建/删除后树才看得见变化) */
    f_tree_reveal();   /* 树自动展开到当前目录, 否则进去以后看不出自己在哪 */
}


/* 文件类型判定: 0=目录 1=图片 2=视频 3=音频 4=文档 5=其它。
 * 旧实现只有 media/doc 两类, 且按"末 4 字符"取扩展名 —— 对 .c/.h/.md 这种 2~3 字符
 * 扩展名会把文件名尾巴一起比, 永远不命中。这里统一按"最后一个点"定位。 */
static int f_kind(int item)
{
    const char *nm;
    int len, dot = -1, i;
    if (f_types[item]) return 0;
    nm = f_items[item];
    len = hs_strlen(nm);
    for (i = len - 1; i >= 0 && i >= len - 6; i--)
        if (nm[i] == '.') { dot = i; break; }
    if (dot < 0) return 5;
    nm += dot;
    if (strcmp(nm, ".png") == 0 || strcmp(nm, ".jpg") == 0 ||
        strcmp(nm, ".bmp") == 0 || strcmp(nm, ".gif") == 0) return 1;
    if (strcmp(nm, ".mp4") == 0 || strcmp(nm, ".mov") == 0 ||
        strcmp(nm, ".mkv") == 0) return 2;
    if (strcmp(nm, ".mp3") == 0 || strcmp(nm, ".wav") == 0 ||
        strcmp(nm, ".flac") == 0) return 3;
    if (strcmp(nm, ".txt") == 0 || strcmp(nm, ".md") == 0 ||
        strcmp(nm, ".doc") == 0 || strcmp(nm, ".pdf") == 0 ||
        strcmp(nm, ".c") == 0   || strcmp(nm, ".h") == 0) return 4;
    return 5;
}


/* ===================== 网格视图缩略图 =====================
 * 一屏网格可能有十来张图, 逐帧解码会把渲染线程拖住, 所以:
 *   1) 解码一次 → 立刻缩成 56x40 存进缓存槽, 原始大图 free 掉;
 *   2) 每帧最多补一张, 首次进入目录时缩略图"逐格冒出来"而不是整体卡一下;
 *   3) 槽满就按 round-robin 覆盖(屏幕上同时可见的图远比槽多)。
 * 文件大小上限 128KB, 超过不解码直接回退图标 —— 解码 2MB 级 JPEG 的峰值内存
 * (宽*高*4) 在 1GB 机器上不划算, 也不符合"缩略图"这个用途。 */
#define F_THUMB_MAX  16
#define F_THUMB_W    56
#define F_THUMB_H    40
#define F_THUMB_SRC_MAX (512 * 1024)

typedef struct {
    char path[FS_PATH_MAX];
    int  used;
    uint8_t pix[F_THUMB_W * F_THUMB_H * 4];
} f_thumb_t;

static f_thumb_t f_thumbs[F_THUMB_MAX];
static int f_thumb_rr = 0;
static uint8_t f_thumb_src[F_THUMB_SRC_MAX];



/* 是否为可出缩略图的图片(png/jpg) */
static int f_thumb_ext_ok(const char *name)
{
    int n = hs_strlen(name);
    for (int i = n - 1; i >= 0 && n - i <= 5; i--) {
        if (name[i] == '.') {
            const char *e = name + i;
            if (strcmp(e, ".png") == 0) return 1;
            if (strcmp(e, ".jpg") == 0) return 2;
            if (strcmp(e, ".jpeg") == 0) return 2;
            return 0;
        }
    }
    return 0;
}

static int f_thumb_find(const char *path)
{
    for (int i = 0; i < F_THUMB_MAX; i++)
        if (f_thumbs[i].used && strcmp(f_thumbs[i].path, path) == 0) return i;
    return -1;
}

/* 把任意尺寸 RGBA 最近邻缩进槽里(等比, 居中留边) */
static void f_thumb_downscale(f_thumb_t *t, const uint8_t *src, int sw, int sh)
{
    int tw = F_THUMB_W, th = F_THUMB_H;
    int scale_w, scale_h;
    /* 等比缩放: 以"更受限"的那一边为准, 另一边按比例算 —— 两个轴都必须落在格子内,
     * 否则写 pix 会越界(首版把两个轴都初始化成原尺寸, 只改其中一个 → #PF) */
    if (sw * th > sh * tw) { scale_w = tw; scale_h = sh * tw / sw; }   /* 宽受限 */
    else                   { scale_h = th; scale_w = sw * th / sh; }   /* 高受限 */
    if (scale_w > tw) scale_w = tw;
    if (scale_h > th) scale_h = th;
    if (scale_w < 1) scale_w = 1;
    if (scale_h < 1) scale_h = 1;
    int offx = (tw - scale_w) / 2, offy = (th - scale_h) / 2;

    /* 底板: 深色, 让透明图和浅色图都有边界感 */
    for (int i = 0; i < tw * th; i++) {
        t->pix[i * 4 + 0] = 0x1c; t->pix[i * 4 + 1] = 0x1b;
        t->pix[i * 4 + 2] = 0x1f; t->pix[i * 4 + 3] = 0xFF;
    }
    for (int y = 0; y < scale_h; y++) {
        int sy = y * sh / scale_h;
        for (int x = 0; x < scale_w; x++) {
            int sx = x * sw / scale_w;
            const uint8_t *s = src + ((size_t)sy * sw + sx) * 4;
            uint8_t *d = t->pix + (((size_t)(y + offy)) * tw + (x + offx)) * 4;
            uint8_t a = s[3];
            if (a == 0xFF) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 0xFF; }
            else if (a == 0) { /* 保持底板 */ }
            else {   /* 简易混合到底板上 */
                d[0] = (uint8_t)((s[0] * a + 0x1c * (255 - a)) / 255);
                d[1] = (uint8_t)((s[1] * a + 0x1b * (255 - a)) / 255);
                d[2] = (uint8_t)((s[2] * a + 0x1f * (255 - a)) / 255);
                d[3] = 0xFF;
            }
        }
    }
}

/* 读文件头判断真实格式(不少素材是 "xxx.png" 其实是 JPEG), 并读出宽高:
 * 宽高用来挡掉超大图 —— 解码峰值是 w*h*4, 2048x2048 就是 16MB,
 * 对缩略图这种用途不值当。 */
static uint32_t f_be32(const uint8_t *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static uint16_t f_be16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0]<<8)|p[1]); }

#define F_THUMB_MAX_SIDE 1400

/* 返回 1=png 2=jpeg, 0=不认识; w/h 输出原始尺寸 */
static int f_img_sniff(const uint8_t *b, int n, int *w, int *h)
{
    *w = 0; *h = 0;
    if (n >= 24 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') {
        *w = (int)f_be32(b + 16); *h = (int)f_be32(b + 20);
        return 1;
    }
    if (n >= 4 && b[0] == 0xFF && b[1] == 0xD8) {
        int i = 2;
        while (i + 9 < n) {
            if (b[i] != 0xFF) { i++; continue; }
            uint8_t m = b[i + 1];
            if (m == 0xC0 || m == 0xC1 || m == 0xC2) {   /* SOF0/1/2 */
                *h = (int)f_be16(b + i + 5);
                *w = (int)f_be16(b + i + 7);
                return 2;
            }
            if (m == 0x01 || m == 0xD8 || (m >= 0xD0 && m <= 0xD7)) { i += 2; continue; }
            int len = (int)f_be16(b + i + 2);
            if (len <= 0) break;
            i += 2 + len;
        }
        return 2;   /* 是 JPEG 但没读到 SOF, 后面解码会报错 */
    }
    return 0;
}

/* 为某个完整路径生成缩略图, 返回槽号, 失败 -1 */
static int f_thumb_make(const char *path, const char *name)
{
    (void)name;
    uint32_t sz = 0;
    if (ext2_64_stat(path, &sz, 0) != 0) return -1;
    if (sz == 0 || sz > F_THUMB_SRC_MAX) return -1;

    int n = ext2_64_read_file(path, f_thumb_src, F_THUMB_SRC_MAX);
    if (n <= 8) return -1;

    int iw = 0, ih = 0;
    int kind = f_img_sniff(f_thumb_src, n, &iw, &ih);
    if (kind == 0) return -1;
    if (iw <= 0 || ih <= 0) return -1;
    if (iw > F_THUMB_MAX_SIDE || ih > F_THUMB_MAX_SIDE) return -1;

    const uint8_t *src = 0; int sw = 0, sh = 0;
    png64_image_t pimg; pimg.width = 0; pimg.height = 0; pimg.rgba = 0;
    jpeg64_image_t jimg; jimg.width = 0; jimg.height = 0; jimg.rgba = 0;
    if (kind == 1) {
        if (png64_decode(f_thumb_src, n, &pimg) != 0 || !pimg.rgba) return -1;
        src = pimg.rgba; sw = (int)pimg.width; sh = (int)pimg.height;
    } else {
        if (jpeg64_decode(f_thumb_src, n, &jimg) != 0 || !jimg.rgba) return -1;
        src = jimg.rgba; sw = (int)jimg.width; sh = (int)jimg.height;
    }
    if (!src || sw <= 0 || sh <= 0) {
        if (pimg.rgba) png64_free(&pimg);
        if (jimg.rgba) jpeg64_free(&jimg);
        return -1;
    }

    int slot = f_thumb_find(path);
    if (slot < 0) { slot = f_thumb_rr; f_thumb_rr = (f_thumb_rr + 1) % F_THUMB_MAX; }
    hs_strcpyn(f_thumbs[slot].path, path, FS_PATH_MAX);
    f_thumb_downscale(&f_thumbs[slot], src, sw, sh);
    f_thumbs[slot].used = 1;

    if (pimg.rgba) png64_free(&pimg);
    if (jimg.rgba) jpeg64_free(&jimg);
    return slot;
}


/* 光标命中项(列表/网格共用), 几何与 f_render 严格一致; 未命中返回 -1 */
static int f_item_at(int x, int y)
{
    if (!f_win || !f_win->visible) return -1;
    int cy = f_win->y + FS(26);
    int cx = f_win->x + FS(6) + FS(FS_TREE_W) + FS(8);
    int cw = f_win->width - FS(12) - FS(FS_TREE_W) - FS(8);
    if (x < cx || x > cx + cw) return -1;
    if (f_view_mode == 0) {
        int list_y = cy + FS(60);
        if (y < list_y) return -1;
        int r = (y - list_y) / f_row_h();
        if (r < 0 || r >= f_visible_rows()) return -1;
        int gi = f_scroll + r;
        if (gi >= f_match_count) return -1;
        return f_matched[gi];
    }
    int gs = FS(76), cols = cw / gs;
    if (cols < 1) cols = 1;
    int gy = cy + FS(44);
    if (y < gy) return -1;
    int r = (y - gy) / (gs + FS(8));
    int c = (x - cx - FS(6)) / gs;
    if (c < 0 || c >= cols) return -1;
    int gi = f_scroll + r * cols + c;
    if (gi >= f_match_count) return -1;
    return f_matched[gi];
}

/* 直接"移动进指定目录": 复用粘贴的分片任务骨架, 但源不来自剪贴板 */
static void f_job_start_move_to(const char *src, const char *name, const char *dstdir)
{
    f_job.active = 0; f_job.kind = 2;
    f_job.ok = 0; f_job.fail = 0; f_job.done = 0; f_job.why[0] = 0;
    f_job.qh = 0; f_job.qt = 0; f_job.cur_n = 0; f_job.cur_i = 0;
    f_job.top_i = 0; f_job.top_n = 0; f_job.topdir_n = 0; f_job.cur_name[0] = 0;

    char dst[FS_PATH_MAX];
    if (f_join(dst, FS_PATH_MAX, dstdir, name) != 0) {
        f_job.fail++; hs_strcpyn(f_job.why, "path too long", 24);
    } else {
        uint32_t sz = 0; uint8_t isdir = 0;
        if (ext2_64_stat(dst, &sz, &isdir) == 0) {
            f_job.fail++; hs_strcpyn(f_job.why, "target exists", 24);
        } else if (ext2_64_stat(src, &sz, &isdir) != 0) {
            f_job.fail++; hs_strcpyn(f_job.why, "source missing", 24);
        } else if (isdir) {
            if (ext2_64_mkdir(dst) != 0) { f_job.fail++; hs_strcpyn(f_job.why, "mkdir failed", 24); }
            else if (f_job_push_dir(src, dst) != 0) { f_job.fail++; hs_strcpyn(f_job.why, "too many dirs", 24); }
            else {
                f_job.ok++;
                if (f_job.topdir_n < FS_CLIP_MAX) {
                    hs_strcpyn(f_job.tdsrc[f_job.topdir_n], src, FS_PATH_MAX);
                    f_job.topdir_n++;
                }
            }
        } else {
            hs_strcpyn(f_job.ts[0], src, FS_PATH_MAX);
            hs_strcpyn(f_job.td[0], dst, FS_PATH_MAX);
            hs_strcpyn(f_job.tn[0], name, 64);
            f_job.top_n = 1;
        }
    }

    if (f_job.top_n == 0 && f_job.qt == 0) { f_job_report(); return; }
    f_job.active = 1;
    hs_strcpyn(f_status2, "Moving...", FS_PROMPT_MAX);
}

/* 按下: 只有落在某一项上才接管(返回 1 → 上层别再把这次按下当普通点击) */
int hs_files_drag_begin(int x, int y)
{
    if (!f_win || !f_win->visible) return 0;
    if (f_job.active) return 0;
    int it = f_item_at(x, y);
    if (it < 0) return 0;
    f_drag_state = 1;
    f_drag_item = it;
    f_drop_item = -1;
    f_drop_tree = -1;
    f_drag_x0 = x; f_drag_y0 = y;
    f_drag_x = x;  f_drag_y = y;
    return 1;
}

void hs_files_drag_move(int x, int y)
{
    if (f_drag_state == 0) return;
    f_drag_x = x; f_drag_y = y;
    if (f_drag_state == 1) {
        int dx = x - f_drag_x0, dy = y - f_drag_y0;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx + dy < FS(8)) return;      /* 抖动不算拖拽 */
            f_drag_state = 2;
        f_drop_tree = -1;
        hs_strcpyn(f_status2, "Drop on a folder to move", FS_PROMPT_MAX);
    }
    /* 目标优先看左树(树里全是目录), 再看右栏列表里的目录行 */
    int tr = f_tree_row_at(x, y);
    if (tr >= 0) {
        f_drop_tree = tr;
        f_drop_item = -1;
    } else {
        f_drop_tree = -1;
        int it = f_item_at(x, y);
        f_drop_item = (it >= 0 && it != f_drag_item && f_kind(it) == 0) ? it : -1;
    }
    hpdesk_mark_dirty();
}

/* 松手: 返回 1 表示这次按下-抬起已被拖拽消费, 上层别再当点击 */
int hs_files_drag_end(int x, int y)
{
    (void)x; (void)y;
    if (f_drag_state == 0) return 0;
    int was_drag = (f_drag_state == 2);
    int src = f_drag_item, drop = f_drop_item, dtree = f_drop_tree;
    f_drag_state = 0; f_drop_item = -1; f_drop_tree = -1;
    if (!was_drag) return 0;

    /* 拖到左树节点: 目标是树里的那个目录(可能根本不在右栏的父目录里) */
    if (dtree >= 0 && dtree < f_tree_n) {
        char srcp[FS_PATH_MAX];
        if (strcmp(f_tree_path[dtree], f_path) == 0) {
            hs_strcpyn(f_status2, "Already in this folder", FS_PROMPT_MAX);
            f_refresh();
            return 1;
        }
        if (f_join(srcp, FS_PATH_MAX, f_path, f_items[src]) != 0)
            hs_strcpyn(f_status2, "Path too long", FS_PROMPT_MAX);
        else
            f_job_start_move_to(srcp, f_items[src], f_tree_path[dtree]);
        f_refresh();
        return 1;
    }

    if (drop >= 0 && drop != src) {
        char dstdir[FS_PATH_MAX], srcp[FS_PATH_MAX];
        if (f_join(dstdir, FS_PATH_MAX, f_path, f_items[drop]) != 0 ||
            f_join(srcp, FS_PATH_MAX, f_path, f_items[src]) != 0) {
            hs_strcpyn(f_status2, "Path too long", FS_PROMPT_MAX);
        } else {
            f_job_start_move_to(srcp, f_items[src], dstdir);
        }
    } else {
        hs_strcpyn(f_status2, "Move cancelled", FS_PROMPT_MAX);
    }
    f_refresh();
    return 1;
}


/* ---- 多标签 ---- */
static void f_tab_label(int i, char *out, int cap)
{
    const char *p = f_tab_path[i];
    if (!p[0] || (p[0] == '/' && p[1] == 0)) { hs_strcpyn(out, "Root", cap); return; }
    const char *last = p;
    for (const char *q = p; *q; q++) if (*q == '/') last = q + 1;
    hs_strcpyn(out, last[0] ? last : "Root", cap);
}

static void f_tab_store(void)
{
    if (f_tab_cur < 0 || f_tab_cur >= f_tab_count) return;
    hs_strcpyn(f_tab_path[f_tab_cur], f_path, FS_PATH_MAX);
    f_tab_scroll[f_tab_cur] = f_scroll;
    f_tab_sel[f_tab_cur]    = f_sel;
    f_tab_view[f_tab_cur]   = f_view_mode;
    f_tab_skey[f_tab_cur]   = f_sort_key;
    f_tab_sdesc[f_tab_cur]  = f_sort_desc;
}

static void f_tab_restore(void)
{
    hs_strcpyn(f_path, f_tab_path[f_tab_cur], FS_PATH_MAX);
    f_view_mode = f_tab_view[f_tab_cur];
    f_sort_key  = f_tab_skey[f_tab_cur];
    f_sort_desc = f_tab_sdesc[f_tab_cur];
    f_refresh();
    /* refresh 之后条目数可能变了, 把游标钳回有效范围 */
    if (f_sel >= f_count) f_sel = -1;
    int maxs = f_match_count - f_visible_rows();
    if (maxs < 0) maxs = 0;
    f_scroll = f_tab_scroll[f_tab_cur];
    if (f_scroll > maxs) f_scroll = maxs;
    if (f_scroll < 0) f_scroll = 0;
    hpdesk_mark_dirty();
}

static void f_tab_switch(int i)
{
    if (i < 0 || i >= f_tab_count || i == f_tab_cur) return;
    f_tab_store();
    f_tab_cur = i;
    f_tab_restore();
}

static void f_tab_new(void)
{
    f_tab_store();
    if (f_tab_count >= FS_TAB_MAX) {
        hs_strcpyn(f_status2, "Max 4 tabs", FS_PROMPT_MAX);
        return;
    }
    int i = f_tab_count++;
    hs_strcpyn(f_tab_path[i], "/", FS_PATH_MAX);
    f_tab_scroll[i] = 0; f_tab_sel[i] = -1;
    f_tab_view[i] = f_view_mode;
    f_tab_skey[i] = f_sort_key; f_tab_sdesc[i] = f_sort_desc;
    f_tab_cur = i;
    f_tab_restore();
}

static void f_tab_close(void)
{
    if (f_tab_count <= 1) { hs_strcpyn(f_status2, "Last tab", FS_PROMPT_MAX); return; }
    for (int k = f_tab_cur; k + 1 < f_tab_count; k++) {
        hs_strcpyn(f_tab_path[k], f_tab_path[k + 1], FS_PATH_MAX);
        f_tab_scroll[k] = f_tab_scroll[k + 1];
        f_tab_sel[k]    = f_tab_sel[k + 1];
        f_tab_view[k]   = f_tab_view[k + 1];
        f_tab_skey[k]   = f_tab_skey[k + 1];
        f_tab_sdesc[k]  = f_tab_sdesc[k + 1];
    }
    f_tab_count--;
    if (f_tab_cur >= f_tab_count) f_tab_cur = f_tab_count - 1;
    f_tab_restore();
}

/* 标签栏几何: 标题栏内, 从 "Files" 字样右侧起, 与绘制共用同一公式 */
static int f_tab_x0(void) { return f_win->x + FS(56); }
static int f_tab_xmax(void) { return f_win->x + f_win->width - FS(80); }
static int f_tab_top(void) { return f_win->y + FS(6); }
static int f_tab_w(int i)
{
    char lb[20];
    f_tab_label(i, lb, sizeof lb);
    return FS(6) * hs_strlen(lb) + FS(14);
}

/* 标题栏标签点击: 返回 1 = 已消费(单击切换 / 双击关闭 / 点 "+" 新建) */
int hs_files_tab_click(int x, int y)
{
    if (!f_win || !f_win->visible) return 0;
    if (f_tab_count < 1) return 0;
    int ty = f_tab_top(), th = FS(14);
    if (y < ty || y > ty + th) return 0;
    int tx = f_tab_x0(), xmax = f_tab_xmax();
    for (int i = 0; i < f_tab_count; i++) {
        int tw = f_tab_w(i);
        if (tx + tw > xmax) break;
        if (x >= tx && x <= tx + tw) {
            extern uint64_t pit64_get_ms(void);
            static int lasti = -1;
            static uint64_t lastms = 0;
            uint64_t now = pit64_get_ms();
            if (lasti == i && now - lastms < 400) {
                lasti = -1;
                f_tab_close();
                return 1;
            }
            lasti = i; lastms = now;
            f_tab_switch(i);
            return 1;
        }
        tx += tw + FS(4);
    }
    if (tx + FS(14) <= xmax && x >= tx && x <= tx + FS(14)) { f_tab_new(); return 1; }
    return 0;
}


/* ---- 左树 ---- */
static int f_tree_rows(void)
{
    int ch = f_win->height - FS(26) - FS(40);
    int h = ch - FS(44) - FS(30);
    if (h < FS(16)) h = FS(16);
    return h / FS(16);
}
static int f_tree_top(void) { return f_win->y + FS(26) + FS(44); }
static void f_tree_note_shift(int at, int delta)
{
    if (f_tree_sel >= at) f_tree_sel += delta;
}

static void f_tree_insert(int at, const char *p, int d)
{
    if (f_tree_n >= FS_TREE_MAX) return;
    for (int k = f_tree_n; k > at; k--) {
        hs_strcpyn(f_tree_path[k], f_tree_path[k - 1], FS_PATH_MAX);
        f_tree_depth[k] = f_tree_depth[k - 1];
        f_tree_exp[k]   = f_tree_exp[k - 1];
    }
    hs_strcpyn(f_tree_path[at], p, FS_PATH_MAX);
    f_tree_depth[at] = d;
    f_tree_exp[at] = 0;
    f_tree_n++;
    f_tree_note_shift(at, 1);
}

static void f_tree_expand(int i)
{
    if (i < 0 || i >= f_tree_n || f_tree_exp[i]) return;
    int n = ext2_64_list_dir_entries_ex(f_tree_path[i], f_tnm, f_ttp, f_tsz, FS_TREE_CHILD);
    if (n <= 0) { f_tree_exp[i] = 1; return; }
    int at = i + 1, added = 0;
    for (int k = 0; k < n; k++) {
        if (!f_ttp[k]) continue;                       /* 只收目录 */
        char sub[FS_PATH_MAX];
        if (f_join(sub, FS_PATH_MAX, f_tree_path[i], f_tnm[k]) != 0) continue;
        f_tree_insert(at + added, sub, f_tree_depth[i] + 1);
        added++;
    }
    f_tree_exp[i] = 1;
}

static void f_tree_collapse(int i)
{
    if (i < 0 || i >= f_tree_n) return;
    int d = f_tree_depth[i];
    int k = i + 1;
    while (k < f_tree_n && f_tree_depth[k] > d) k++;
    int cut = k - (i + 1);
    for (int m = i + 1; m + cut < f_tree_n; m++) {
        hs_strcpyn(f_tree_path[m], f_tree_path[m + cut], FS_PATH_MAX);
        f_tree_depth[m] = f_tree_depth[m + cut];
        f_tree_exp[m]   = f_tree_exp[m + cut];
    }
    f_tree_n -= cut;
    f_tree_exp[i] = 0;
    f_tree_sel = i;   /* 留在被折叠的节点上, 别把高亮清空让人找不着位置 */
}

static void f_tree_init(void)
{
    f_tree_n = 0; f_tree_sel = -1;
    hs_strcpyn(f_tree_path[0], "/", FS_PATH_MAX);
    f_tree_depth[0] = 0; f_tree_exp[0] = 0;
    f_tree_n = 1;
    f_tree_expand(0);
}

/* 目录内容变过(新建/删除/移动完成)后重新对一遍已展开节点的子目录,
 * 否则新建的目录在树里看不见 —— 得先折叠再展开才刷新出来 */
static void f_tree_sync(void)
{
    for (int i = f_tree_n - 1; i >= 0; i--) {
        if (!f_tree_exp[i]) continue;
        int n = ext2_64_list_dir_entries_ex(f_tree_path[i], f_tnm, f_ttp, f_tsz, FS_TREE_CHILD);
        if (n < 0) continue;
        int cnt = 0;
        for (int k = 0; k < n; k++) if (f_ttp[k]) cnt++;
        int have = 0, d = f_tree_depth[i] + 1;
        for (int k = i + 1; k < f_tree_n && f_tree_depth[k] >= d; k++)
            if (f_tree_depth[k] == d) have++;
        if (cnt != have) { f_tree_collapse(i); f_tree_expand(i); }
    }
}

/* 把树展开到 f_path: 从根逐级往下找"是 f_path 前缀"的子节点 */
static void f_tree_reveal(void)
{
    if (f_tree_n <= 0) return;
    int idx = 0;
    f_tree_sel = 0;
    for (int guard = 0; guard < 24; guard++) {
        if (strcmp(f_tree_path[idx], f_path) == 0) { f_tree_sel = idx; return; }
        if (!f_tree_exp[idx]) f_tree_expand(idx);
        int d = f_tree_depth[idx] + 1;
        int found = -1;
        for (int k = idx + 1; k < f_tree_n && f_tree_depth[k] >= d; k++) {
            if (f_tree_depth[k] != d) continue;
            int l = hs_strlen(f_tree_path[k]);
            int fl = hs_strlen(f_path);
            /* 节点路径必须是 f_path 的"按分量前缀": 完全相等也算命中。
             * 原来要求 fl > l, 于是 /usr 匹配不上 /usr 节点, /usr/share 也
             * 走不到 share —— 高亮会停在上一层, 看起来就是"树没跟到位置" */
            if (fl >= l && strncmp(f_path, f_tree_path[k], l) == 0 &&
                (fl == l || f_path[l] == '/')) { found = k; break; }
        }
        if (found < 0) return;
        idx = found;
        f_tree_sel = idx;
    }
}

/* 坐标落在树的第几行(未落上返回 -1) */
static int f_tree_row_at(int x, int y)
{
    if (!f_win || !f_win->visible) return -1;
    int tx = f_win->x + FS(6), tw = FS(FS_TREE_W);
    if (x < tx || x > tx + tw) return -1;
    int top = f_tree_top(), rh = FS(16);
    if (y < top) return -1;
    int r = (y - top) / rh;
    if (r < 0 || r >= f_tree_rows() || r >= f_tree_n) return -1;
    return r;
}

static void f_tree_label(int i, char *out, int cap)
{
    const char *p = f_tree_path[i];
    if (!p[0] || (p[0] == '/' && p[1] == 0)) { hs_strcpyn(out, "/", cap); return; }
    const char *last = p;
    for (const char *q = p; *q; q++) if (*q == '/') last = q + 1;
    hs_strcpyn(out, last[0] ? last : "/", cap);
}

/* 左栏点击: 返回 1 = 已消费。点目录 = 跳过去并展开; 点已展开的 = 折叠 */
int hs_files_tree_click(int x, int y)
{
    int r = f_tree_row_at(x, y);
    if (r < 0) return 0;

    if (f_tree_exp[r]) {
        f_tree_collapse(r);
    } else {
        f_tree_expand(r);
        hs_strcpyn(f_path, f_tree_path[r], FS_PATH_MAX);
        f_sel = -1; f_scroll = 0;
        f_refresh();
        f_tree_sel = r;
    }
    hpdesk_mark_dirty();
    return 1;
}

static void f_quick_label(int i, char *out, int cap)
{
    const char *p = f_quick[i];
    if (!p[0] || (p[0] == '/' && p[1] == 0)) { hs_strcpyn(out, "/", cap); return; }
    const char *last = p;
    for (const char *q = p; *q; q++) if (*q == '/') last = q + 1;
    hs_strcpyn(out, last[0] ? last : "/", cap);
}

static void f_quick_defaults(void)
{
    static const char *d[4] = {"/", "/usr", "/fonts", "/usr/share"};
    f_quick_n = 0;
    for (int i = 0; i < 4; i++) hs_strcpyn(f_quick[f_quick_n++], d[i], FS_PATH_MAX);
}

static void f_quick_save(void)
{
    extern int ext2_64_write_file(const char *path, const uint8_t *data, uint32_t size);
    static char out[FS_QUICK_MAX * FS_PATH_MAX];
    int n = 0;
    for (int i = 0; i < f_quick_n && n < (int)sizeof out - FS_PATH_MAX; i++) {
        int l = hs_strlen(f_quick[i]);
        for (int k = 0; k < l; k++) out[n++] = f_quick[i][k];
        out[n++] = '\n';
    }
    ext2_64_write_file(FS_QUICK_CFG, (const uint8_t *)out, (uint32_t)n);
}

static void f_quick_load(void)
{
    extern int ext2_64_read_file(const char *path, uint8_t *buf, uint32_t max);
    static uint8_t buf[FS_QUICK_MAX * FS_PATH_MAX + 256];
    int n = ext2_64_read_file(FS_QUICK_CFG, buf, (uint32_t)(sizeof buf - 1));
    if (n <= 0) { f_quick_defaults(); return; }
    buf[n] = 0;
    f_quick_n = 0;
    int i = 0;
    while (i < n && f_quick_n < FS_QUICK_MAX) {
        while (i < n && (buf[i] == '\n' || buf[i] == '\r' || buf[i] == ' ')) i++;
        int st = i;
        while (i < n && buf[i] != '\n' && buf[i] != '\r') i++;
        int len = i - st;
        if (len < 1 || len >= FS_PATH_MAX) continue;
        if (buf[st] != '/') continue;
        int k;
        for (k = 0; k < len; k++) f_quick[f_quick_n][k] = (char)buf[st + k];
        f_quick[f_quick_n][k] = 0;
        f_quick_n++;
    }
    if (f_quick_n == 0) f_quick_defaults();
}

/* 把当前目录钉上; 已经在里面就把这一项取消 —— 一个按钮既是加也是删 */
static void f_quick_toggle_here(void)
{
    for (int i = 0; i < f_quick_n; i++) {
        if (strcmp(f_quick[i], f_path) == 0) {
            for (int k = i; k + 1 < f_quick_n; k++)
                hs_strcpyn(f_quick[k], f_quick[k + 1], FS_PATH_MAX);
            f_quick_n--;
            f_quick_save();
            hs_strcpyn(f_status2, "Unpinned from shortcuts", FS_PROMPT_MAX);
            return;
        }
    }
    if (f_quick_n >= FS_QUICK_MAX) {
        for (int k = 0; k + 1 < FS_QUICK_MAX; k++)
            hs_strcpyn(f_quick[k], f_quick[k + 1], FS_PATH_MAX);
        f_quick_n = FS_QUICK_MAX - 1;
    }
    hs_strcpyn(f_quick[f_quick_n++], f_path, FS_PATH_MAX);
    f_quick_save();
    hs_strcpyn(f_status2, "Pinned to shortcuts", FS_PROMPT_MAX);
}

/* 搜索框内容实时同步到过滤串: 边打边筛, 不用等回车 */
static void f_search_apply(void)
{
    hs_strcpyn(f_filter, f_prompt_buf, 32);
    f_filter_len = hs_strlen(f_filter);
    f_sel = 0;
    f_scroll = 0;
    f_refresh();
}

static void f_render(hs_window_t *w)
{
    /* FS P2: 有分片任务在跑就推进一步, 并保持 dirty 让下一帧继续 —— 这样
     * 拷贝期间画面/光标都不冻, 进度条也能动 */
    if (f_job.active) { f_job_step(); hpdesk_mark_dirty(); }
    void *fb = gfx64_get_framebuffer();
    int W = g_shell.screen_width, H = g_shell.screen_height;
    int cx = w->x + FS(6), cy = w->y + FS(26);
    int cw = w->width - FS(12), ch = w->height - FS(26) - FS(40);
    int row_h = f_row_h();

    /* 路径栏 */
    gfx64_fill_rounded_rect(cx, cy, cw, FS(20), FS(4), g_shell.colors.panel_bg);
    {
        char shown[80];
        hs_strcpyn(shown, f_path, 70);
        font64_draw_string(fb, W, H, cx + FS(6), cy + FS(3), shown, g_shell.colors.panel_fg, FS(12));
    }
    /* USB挂载状态按钮(路径栏右侧) */
    {
        extern int usb64_dev_count(void);
        extern int storage_hal_get_device(void);
        extern int usb64_mounted_flag(void);
        int has_usb = usb64_dev_count() > 0;
        int on_usb = storage_hal_get_device() == 1;
        const char *txt = has_usb ? (on_usb ? "[USB: mounted]" : "[USB: click to mount]") : "[USB: not detected]";
        int tw = FS(12) * (hs_strlen(txt)) + FS(4);
        int bx = cx + cw - tw - FS(4);
        if (on_usb) gfx64_fill_rounded_rect(bx, cy, tw, FS(20), FS(4), g_shell.colors.accent);
        font64_draw_string(fb, W, H, bx + FS(4), cy + FS(3), txt,
                           on_usb ? 0xFFFFFF : g_shell.colors.panel_fg, FS(12));
        /* 搜索入口: 就在 USB 按钮左边 */
        int sw = FS(7) * 6 + FS(10);
        int sx = bx - sw - FS(6);
        if (sx > cx + FS(6)) {
            /* 用强调色描边: panel_bg 底 + border 线在深色面板上等于看不见 */
            gfx64_fill_rounded_rect(sx, cy, sw, FS(20), FS(4),
                                    f_filter_len ? g_shell.colors.accent : g_shell.colors.panel_bg);
            gfx64_draw_rounded_rect(sx, cy, sw, FS(20), FS(4), g_shell.colors.accent);
            font64_draw_string(fb, W, H, sx + FS(5), cy + FS(3), "Search",
                               f_filter_len ? 0xFFFFFF : g_shell.colors.panel_fg, FS(10));
        }
    }

    /* 快捷目录条: 用户自己钉的常用位置; 末尾按钮把当前目录钉上/取消钉 */
    int loc_y = cy + FS(22);
    {
        int lx = cx + FS(2);
        for (int i = 0; i < f_quick_n; i++) {
            char lb[20];
            f_quick_label(i, lb, sizeof lb);
            int on = (strcmp(f_path, f_quick[i]) == 0);
            int lw = FS(7) * hs_strlen(lb) + FS(10);
            if (lx + lw > cx + cw - FS(26)) break;
            gfx64_fill_rounded_rect(lx, loc_y, lw, FS(16), FS(4),
                                    on ? g_shell.colors.accent : g_shell.colors.panel_bg);
            font64_draw_string(fb, W, H, lx + FS(5), loc_y + FS(3), lb,
                               on ? 0xFFFFFF : g_shell.colors.panel_fg, FS(10));
            lx += lw + FS(4);
        }
        int here = 0;
        for (int i = 0; i < f_quick_n; i++)
            if (strcmp(f_path, f_quick[i]) == 0) here = 1;
        if (lx + FS(16) <= cx + cw - FS(4)) {
            gfx64_fill_rounded_rect(lx, loc_y, FS(16), FS(16), FS(4),
                                    g_shell.colors.panel_bg);
            gfx64_draw_rounded_rect(lx, loc_y, FS(16), FS(16), FS(4),
                                    g_shell.colors.border);
            font64_draw_string(fb, W, H, lx + FS(5), loc_y + FS(3),
                               (char *)(here ? "-" : "+"),
                               g_shell.colors.panel_fg, FS(10));
        }
    }
    /* 左树右列表: 右栏起点/宽度 (树宽 + 分隔线) */
    int px = cx + FS(FS_TREE_W) + FS(8);
    int pw = cw - FS(FS_TREE_W) - FS(8);
    /* 左栏目录树 */
    {
        int tx = cx, tw = FS(FS_TREE_W);
        int top = f_tree_top(), rh = FS(16);
        int ch = f_win->height - FS(26) - FS(40);
        int rows = f_tree_rows();
        /* 竖分隔线: 只画"树与列表并排"的那一段(从树的第一行开始)。
         * 原来从 cy+20 起, 会横穿路径栏/面包屑, 把最右边那个胶囊切一刀 */
        {
            int sep_h = (cy + ch - FS(6)) - top;
            if (sep_h > 0)
                gfx64_fill_rect(tx + tw + FS(3), top, 1, sep_h, g_shell.colors.border);
        }
        for (int r = 0; r < rows; r++) {
            if (r >= f_tree_n) break;
            int iy = top + r * rh;
            char lb[24];
            f_tree_label(r, lb, sizeof lb);
            if (r == f_tree_sel)
                gfx64_fill_rounded_rect(tx, iy, tw - FS(4), rh - 1, FS(3), g_shell.colors.accent);
            if (f_drag_state == 2 && r == f_drop_tree)
                gfx64_draw_rounded_rect(tx, iy, tw - FS(4), rh - 1, FS(3), 0xFFFFFF);
            uint32_t fc = (r == f_tree_sel) ? 0x211F26 : g_shell.colors.panel_fg;
            /* 折叠态用 '>' 展开态用 'v', 和列头排序箭头同一套字符做法 */
            const char *ar = f_tree_exp[r] ? "v" : ">";
            /* 箭头必须跟着缩进一起右移, 否则子项的箭头会孤零零留在最左边,
             * 和名字隔开二三十像素, 层级完全看不出来 */
            int rind = f_tree_depth[r] * FS(9);
            font64_draw_string(fb, W, H, tx + FS(2) + rind, iy + 1, (char *)ar,
                               (r == f_tree_sel) ? 0x211F26 : g_shell.colors.text_secondary, FS(9));
            font64_draw_string(fb, W, H, tx + FS(14) + rind, iy + 1,
                               lb, fc, FS(9));
        }
    }

    /* 多标签栏: 画在标题栏里(坐标在内容区之上, 不影响列表几何) */
    {
        int ty = f_tab_top(), th = FS(14);
        int tx = f_tab_x0(), xmax = f_tab_xmax();
        for (int i = 0; i < f_tab_count; i++) {
            char lb[20];
            f_tab_label(i, lb, sizeof lb);
            int tw = f_tab_w(i);
            if (tx + tw > xmax) break;
            int on = (i == f_tab_cur);
            gfx64_fill_rounded_rect(tx, ty, tw, th, FS(3),
                                    on ? g_shell.colors.accent : g_shell.colors.panel_bg);
            if (!on) gfx64_draw_rounded_rect(tx, ty, tw, th, FS(3), g_shell.colors.border);
            font64_draw_string(fb, W, H, tx + FS(7), ty + FS(2), lb,
                               on ? 0x211F26 : g_shell.colors.panel_fg, FS(9));
            tx += tw + FS(4);
        }
        if (tx + FS(14) <= xmax) {
            gfx64_fill_rounded_rect(tx, ty, FS(14), th, FS(3), g_shell.colors.panel_bg);
            gfx64_draw_rounded_rect(tx, ty, FS(14), th, FS(3), g_shell.colors.border);
            font64_draw_string(fb, W, H, tx + FS(4), ty + FS(2), "+",
                               g_shell.colors.accent, FS(10));
        }
    }

    /* 列表列头: 点击切换排序(名称/大小/类型) */
    int hdr_y = cy + FS(44);
    int list_y = hdr_y + FS(16);
    if (f_view_mode == 0) {
        uint32_t hc = g_shell.colors.text_secondary;
        int col_size = px + pw - FS(86);
        int col_type = px + pw - FS(34);
        font64_draw_string(fb, W, H, px + FS(22), hdr_y, "Name",
                           f_sort_key == 0 ? g_shell.colors.accent : hc, FS(10));
        font64_draw_string(fb, W, H, col_size, hdr_y, "Size",
                           f_sort_key == 1 ? g_shell.colors.accent : hc, FS(10));
        font64_draw_string(fb, W, H, col_type, hdr_y, "Type",
                           f_sort_key == 2 ? g_shell.colors.accent : hc, FS(10));
        /* 当前排序列下的小横线: 有横线=升序, 粗线=降序不好区分, 用箭头文字代替 */
        if (f_sort_desc) {
            const char *arrow = "v";
            int ax = (f_sort_key == 0) ? px + FS(22) + FS(34)
                   : (f_sort_key == 1) ? col_size + FS(32) : col_type + FS(30);
            font64_draw_string(fb, W, H, ax, hdr_y, (char *)arrow,
                               g_shell.colors.accent, FS(9));
        }
        gfx64_fill_rect(px, hdr_y + FS(14), pw, 1, g_shell.colors.border);
    }

    /* 网格模式: 大图标 + 名称 */
    if (f_view_mode == 1) {
        int gs = FS(76), cols = pw / gs;
        if (cols < 1) cols = 1;
        int rows = (ch - FS(52)) / (gs + FS(8));
        if (rows < 1) rows = 1;
        int gy = cy + FS(44);
        int thumb_budget = 1;   /* 每帧最多补一张, 避免一次把渲染线程拖住 */
        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                int gi = f_scroll + (r * cols + c);
                if (gi >= f_match_count) break;
                int item = f_matched[gi];
                int ix = px + FS(6) + c * gs;
                int iy2 = gy + r * (gs + FS(8));
                if (gi == f_sel)
                    gfx64_fill_rounded_rect(ix - FS(4), iy2 - FS(4), gs, gs, FS(8), g_shell.colors.accent);
                if (f_drag_state == 2 && item == f_drop_item)
                    gfx64_draw_rounded_rect(ix - FS(4), iy2 - FS(4), gs, gs, FS(8), 0xFFFFFF);
                extern void hs_file_type_icon(int x, int y, int size, uint32_t color, int kind);
                uint32_t ic = (gi == f_sel) ? 0x211F26 : g_shell.colors.accent;
                /* 图片文件优先用缩略图; 未缓存的格子先画图标, 本帧预算内补一张 */
                int thumb = -1;
                if (f_kind(item) == 1) {
                    char fp[FS_PATH_MAX];
                    if (f_join(fp, FS_PATH_MAX, f_path, f_items[item]) == 0) {
                        thumb = f_thumb_find(fp);
                        if (thumb < 0 && thumb_budget > 0) {
                            thumb_budget--;
                            thumb = f_thumb_make(fp, f_items[item]);
                        }
                    }
                }
                if (thumb >= 0) {
                    gfx64_blit_rgba(ix - FS(5), iy2 - FS(6), FS(56), FS(40),
                                    f_thumbs[thumb].pix, F_THUMB_W, F_THUMB_H);
                    /* 四边描边: 浅色/透明图直接压在深色底上会看不出边界 */
                    gfx64_fill_rect(ix - FS(5), iy2 - FS(6), FS(56), 1, g_shell.colors.border);
                    gfx64_fill_rect(ix - FS(5), iy2 - FS(6) + FS(40) - 1, FS(56), 1, g_shell.colors.border);
                    gfx64_fill_rect(ix - FS(5), iy2 - FS(6), 1, FS(40), g_shell.colors.border);
                    gfx64_fill_rect(ix - FS(5) + FS(56) - 1, iy2 - FS(6), 1, FS(40), g_shell.colors.border);
                }
                else
                    hs_file_type_icon(ix, iy2, FS(30), ic, f_kind(item));
                if (f_mark[item])
                    gfx64_fill_rounded_rect(ix - FS(2), iy2 - FS(2), FS(12), FS(12), FS(3), 0xFFD040);
                char gd[32];
                /* 单元格宽 76px: 长文件名缩小字号; 仍放不下就截断并以 ".." 收尾
                 * (旧版硬砍到 12 字符, "wallpaper.png" 会显示成 "wallpaper.pn") */
                int nlen = hs_strlen(f_items[item]);
                int nsz = (nlen > 10) ? 10 : 12;
                int cap = (nsz == 10) ? 14 : 16;
                if (nlen >= cap) {
                    hs_strcpyn(gd, f_items[item], cap);
                    int l = hs_strlen(gd);
                    if (l >= 3) { gd[l - 1] = '.'; gd[l - 2] = '.'; }
                } else {
                    hs_strcpyn(gd, f_items[item], cap + 1);
                }
                font64_draw_string(fb, W, H, ix - FS(2), iy2 + FS(38), gd,
                                   (gi == f_sel) ? 0xFFFFFF : g_shell.colors.window_fg, nsz);
            }
        }
    } else {
        /* 空状态: 区分"目录本身是空的"与"被过滤条件筛掉了" —— 两者在旧版都是纯黑一片,
         * 用户会以为目录没刷新出来。 */
        if (f_match_count == 0) {
            const char *msg = f_filter_len ? "No matches for filter" : "This folder is empty";
            font64_draw_string(fb, W, H, px + FS(24), list_y + FS(6), (char *)msg,
                               g_shell.colors.text_secondary, FS(12));
        }
        /* 列表模式 (名称 + 大小 + 类型) */
        int visible = f_visible_rows();
        if (visible < 1) visible = 1;
        for (int r = 0; r < visible; r++) {
            int li = f_scroll + r;
            if (li >= f_match_count) break;
            int item = f_matched[li];
            int iy = list_y + r * row_h;
            if (li == f_sel)
                gfx64_fill_rect(px, iy, pw, row_h, g_shell.colors.accent);
            /* drop 目标用白色描边: 若目标恰是当前选中项, accent 描边压在同色底上会看不见 */
            if (f_drag_state == 2 && item == f_drop_item)
                gfx64_draw_rounded_rect(px, iy + 1, pw, row_h - 2, FS(3), 0xFFFFFF);
            char disp[70];
            hs_strcpyn(disp, f_items[item], 42);
            if (f_types[item]) {
                int dl = hs_strlen(disp);
                if (dl < 68) { disp[dl] = '/'; disp[dl + 1] = 0; }
            }
            /* 文件类型矢量图标(目录/文档/媒体/未知) */
            {
                extern void hs_file_type_icon(int x, int y, int size, uint32_t color, int kind);
                /* 选中行底色是 accent(浅紫), 图标再用白色会糊成实心块 —— 换深色描线 */
                uint32_t ic = (li == f_sel) ? 0x211F26 : g_shell.colors.accent;
                hs_file_type_icon(px + FS(3), iy + 1, FS(14), ic, f_kind(item));
            }
            font64_draw_string(fb, W, H, px + FS(22), iy + FS(2), disp,
                               (li == f_sel) ? 0xFFFFFF : g_shell.colors.window_fg, FS(12));
            /* 标记指示(多选): 放到行首, 避免与 Size/Type 两列抢位置 */
            if (f_mark[item])
                font64_draw_string(fb, W, H, px + FS(1), iy + FS(2), "*",
                                   (li == f_sel) ? 0xFFFFFF : 0xFFD040, FS(12));
            /* Size 列 */
            char szbuf[20];
            if (f_types[item]) strcpy(szbuf, "<DIR>");
            else f_fmt_size(f_sizes[item], szbuf);
            font64_draw_string(fb, W, H, px + pw - FS(86), iy + FS(2), szbuf,
                               (li == f_sel) ? 0xFFFFFF : g_shell.colors.text_secondary, FS(12));
            /* Type 列: 与列头 col_type 对齐 */
            {
                static const char *kname[6] = {"DIR", "IMG", "VID", "AUD", "DOC", "FIL"};
                int k = f_kind(item);
                if (k < 0 || k > 5) k = 5;
                font64_draw_string(fb, W, H, px + pw - FS(34), iy + FS(2), (char *)kname[k],
                                   (li == f_sel) ? 0xFFFFFF : g_shell.colors.text_secondary, FS(10));
            }
        }
    }

    /* 属性浮层 */
    if (f_props) {
        int px = px + FS(10), py = cy + FS(30), pw = pw - FS(20), ph = FS(96);
        char pb[120];
        gfx64_fill_rounded_rect(px, py, pw, ph, FS(8), g_shell.colors.panel_bg);
        gfx64_draw_rounded_rect(px, py, pw, ph, FS(8), g_shell.colors.accent);
        font64_draw_string(fb, W, H, px + FS(10), py + FS(8), "Properties",
                           g_shell.colors.accent, FS(14));
        font64_draw_string(fb, W, H, px + pw - FS(108), py + FS(8),
                           "any key = close", g_shell.colors.text_secondary, FS(10));
        strcpy(pb, "Name: ");
        hs_strcpyn(pb + hs_strlen(pb), f_props_name, 40);
        font64_draw_string(fb, W, H, px + FS(10), py + FS(30), pb, g_shell.colors.panel_fg, FS(12));
        strcpy(pb, "Type: ");
        strcpy(pb + hs_strlen(pb), f_props_isdir ? "Folder" : "File");
        font64_draw_string(fb, W, H, px + FS(10), py + FS(46), pb, g_shell.colors.panel_fg, FS(12));
        strcpy(pb, "Size: ");
        char szb[20];
        f_fmt_size(f_props_size, szb);
        strcpy(pb + hs_strlen(pb), szb);
        font64_draw_string(fb, W, H, px + FS(10), py + FS(62), pb, g_shell.colors.panel_fg, FS(12));
        strcpy(pb, "Path: ");
        hs_strcpyn(pb + hs_strlen(pb), f_props_path, 45);
        font64_draw_string(fb, W, H, px + FS(10), py + FS(78), pb,
                           g_shell.colors.text_secondary, FS(11));
    }

    /* 拖影: 跟着光标的文件名小牌子 */
    if (f_drag_state == 2) {
        char lbl[28];
        hs_strcpyn(lbl, f_items[f_drag_item], 24);
        int tw = FS(6) * hs_strlen(lbl) + FS(16);
        int tx = f_drag_x + FS(10), ty = f_drag_y + FS(8);
        if (tx + tw > W - FS(4)) tx = W - tw - FS(4);
        if (ty + FS(18) > H - FS(4)) ty = H - FS(18) - FS(4);
        gfx64_fill_rounded_rect(tx, ty, tw, FS(18), FS(4), g_shell.colors.panel_bg);
        gfx64_draw_rounded_rect(tx, ty, tw, FS(18), FS(4), g_shell.colors.accent);
        font64_draw_string(fb, W, H, tx + FS(6), ty + FS(3), lbl,
                           g_shell.colors.panel_fg, FS(11));
    }

    /* 底部输入框 / 删除确认 (放在状态栏上方, 避免覆盖两行状态文本) */
    int by = cy + ch - FS(24);
    if (f_del_confirm) {
        gfx64_fill_rounded_rect(cx, by, cw, FS(22), FS(4), 0xC03030);
        char lb[120];
        strcpy(lb, "Delete ");
        if (f_del_multi > 0) {
            char t8[8];
            hs_itoa(f_del_multi, t8);
            strcpy(lb + hs_strlen(lb), t8);
            strcpy(lb + hs_strlen(lb), " marked items");
        } else {
            hs_strcpyn(lb + hs_strlen(lb), f_del_name, 20);
        }
        strcpy(lb + hs_strlen(lb), " ?  Enter=Delete  N=Cancel");
        font64_draw_string(fb, W, H, cx + FS(8), by + FS(4), lb, 0xFFFFFF, FS(12));
    } else if (f_prompt != FS_PROMPT_NONE) {
        const char *label = "New folder: ";
        if (f_prompt == FS_PROMPT_MKFILE) label = "New file: ";
        else if (f_prompt == FS_PROMPT_RENAME) label = "Rename: ";
        else if (f_prompt == FS_PROMPT_GOTO) label = "Goto path: ";
        else if (f_prompt == FS_PROMPT_SEARCH) label = "Search: ";
        gfx64_fill_rounded_rect(cx, by, cw, FS(22), FS(4), g_shell.colors.panel_bg);
        gfx64_draw_rounded_rect(cx, by, cw, FS(22), FS(4), g_shell.colors.accent);
        font64_draw_string(fb, W, H, cx + FS(6), by + FS(4), label, g_shell.colors.panel_fg, FS(12));
        int ix = cx + FS(6) + FS(92);
        char shown[70];
        int maxn = (cw - FS(100)) / FS(7);
        if (maxn < 4) maxn = 4;
        if (f_prompt_len > maxn) hs_strcpyn(shown, f_prompt_buf + (f_prompt_len - maxn), maxn);
        else hs_strcpyn(shown, f_prompt_buf, maxn);
        font64_draw_string(fb, W, H, ix, by + FS(4), shown, g_shell.colors.window_fg, FS(12));
        int cxcur = ix + FS(7) * (f_prompt_len > maxn ? maxn : f_prompt_len);
        if (cxcur > cx + cw - FS(10)) cxcur = cx + cw - FS(10);
        gfx64_fill_rect(cxcur, by + FS(4), FS(2), FS(13), g_shell.colors.accent);
    }

    /* 状态栏 (两行) */
    font64_draw_string(fb, W, H, cx, cy + ch + FS(4), f_status,
                       g_shell.colors.text_secondary, FS(10));
    if (f_job.active) {
        char msg[80], tmp[8];
        strcpy(msg, f_job.kind == 3 ? "Deleting " : (f_job.kind == 2 ? "Moving " : "Copying "));
        hs_itoa(f_job.ok, tmp);
        strcpy(msg + hs_strlen(msg), tmp);
        strcpy(msg + hs_strlen(msg), " items  ");
        hs_strcpyn(msg + hs_strlen(msg), f_job.cur_name, 40);
        font64_draw_string(fb, W, H, cx, cy + ch + FS(16), msg, g_shell.colors.accent, FS(12));

        /* 右端 Cancel 按钮(热区由 f_job_cancel_hit 共用同一套几何) */
        int cbw = FS(52), cbx = cx + cw - cbw, cby = cy + ch + FS(12);
        gfx64_fill_rounded_rect(cbx, cby, cbw, FS(17), FS(4), g_shell.colors.accent);
        font64_draw_string(fb, W, H, cbx + FS(7), cby + FS(2), "Cancel",
                           g_shell.colors.panel_fg, FS(11));

        /* 窗口底部等长动画条: 无总条目数时用 done 做相位, 表明还在推进 */
        int ty = cy + ch + FS(30);
        int sw = FS(90);
        int track = cw;
        int span = track - sw;
        int sx = cx + (span > FS(8) ? (int)(((unsigned)f_job.done * FS(45)) % (unsigned)span) : 0);
        gfx64_fill_rect(cx, ty, track, FS(2), g_shell.colors.border);
        gfx64_fill_rect(sx, ty, sw, FS(2), g_shell.colors.accent);
    }
    else if (f_status2[0])
        font64_draw_string(fb, W, H, cx, cy + ch + FS(16), f_status2,
                           g_shell.colors.accent, FS(12));
    else
        font64_draw_string(fb, W, H, cx, cy + ch + FS(16),
                           "F2 Rename  F3 Props  F5 Refresh  F6 Copy  F8 Cut  F9 Paste",
                           g_shell.colors.text_secondary, FS(9));
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
                    v_base_w = 520;
                v_win = hs_window_create(HS_WINDOW_FLOATING, "Image", -1, -1,
                                    hs_ui_sx(520), hs_ui_sy(420));
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
    v_base_w = 460;
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
        v_win = hs_window_create(HS_WINDOW_FLOATING, "Viewer", -1, -1,
                        hs_ui_sx(460), hs_ui_sy(400));
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

/* ============================================================================
 * FS P2: 分片拷贝/移动任务
 * 此前 f_do_paste 在渲染线程里同步跑完整个 copy_tree, 拷大目录时桌面连光标
 * 都僵住。这里改成状态机: 每帧只处理固定数量的条目, 帧与帧之间正常 present,
 * 于是进度条会动、光标能跟手、还能随时取消。任务仍是单线程 (与 ext2 无并发)。
 * ========================================================================== */

static void f_job_report(void);      /* 前向: 结束时汇报 */

static void f_job_finish(void)
{
    f_job.active = 0;
    /* 移动: 顶层目录的子树此刻已全部拷完, 整体删源 */
    if (f_job.kind == 2) {
        for (int i = 0; i < f_job.topdir_n; i++)
            ext2_64_delete_tree(f_job.tdsrc[i]);
    }
    f_job_report();
}

static int f_job_push_dir(const char *s, const char *d)
{
    if (f_job.qt >= F_JOB_Q) return -1;
    hs_strcpyn(f_job.qs[f_job.qt], s, FS_PATH_MAX);
    hs_strcpyn(f_job.qd[f_job.qt], d, FS_PATH_MAX);
    f_job.qt++;
    return 0;
}

/* 取下一个条目并处理(K 个/帧) */
static void f_job_step(void)
{
    if (!f_job.active) return;
    if (f_job.kind == 3) { f_job_step_delete(); return; }
    for (int k = 0; k < F_JOB_PER_FRAME && f_job.active; k++) {
        /* 1) 顶层文件 */
        if (f_job.top_i < f_job.top_n) {
            int i = f_job.top_i++;
            hs_strcpyn(f_job.cur_name, f_job.tn[i], 64);
            int rc = f_copy_file(f_job.ts[i], f_job.td[i]);
            if (rc == -2) { f_job.fail++; hs_strcpyn(f_job.why, "file too large", 24); }
            else if (rc != 0) { f_job.fail++; hs_strcpyn(f_job.why, "copy failed", 24); }
            else {
                f_job.ok++;
                if (f_job.kind == 2) ext2_64_delete_file(f_job.ts[i]);
            }
            f_job.done++;
            continue;
        }
        /* 2) 目录队列 */
        if (f_job.cur_n == 0) {
            if (f_job.qh >= f_job.qt) { f_job_finish(); return; }
            int n = ext2_64_list_dir_entries_ex(f_job.qs[f_job.qh], f_job.names,
                                                f_job.types, f_job.sizes, FS_COPY_ENTRIES);
            if (n < 0 || n >= FS_COPY_ENTRIES) {
                f_job.fail++;
                hs_strcpyn(f_job.why, n < 0 ? "list failed" : "dir > 64 entries", 24);
                f_job.qh++; f_job.cur_n = 0;
                continue;
            }
            if (n == 0) { f_job.qh++; f_job.cur_n = 0; continue; }   /* 空目录, 已在建时落盘 */
            f_job.cur_n = n; f_job.cur_i = 0;
            continue;
        }
        int i = f_job.cur_i++;
        {
            char sp[FS_PATH_MAX], dp[FS_PATH_MAX];
            hs_strcpyn(f_job.cur_name, f_job.names[i], 64);
            if (f_join(sp, FS_PATH_MAX, f_job.qs[f_job.qh], f_job.names[i]) != 0 ||
                f_join(dp, FS_PATH_MAX, f_job.qd[f_job.qh], f_job.names[i]) != 0) {
                f_job.fail++; hs_strcpyn(f_job.why, "path too long", 24);
            } else if (f_job.types[i]) {
                if (ext2_64_mkdir(dp) != 0) { f_job.fail++; hs_strcpyn(f_job.why, "mkdir failed", 24); }
                else if (f_job_push_dir(sp, dp) == 0) { f_job.ok++; }
                else { f_job.fail++; hs_strcpyn(f_job.why, "too many dirs", 24); }
            } else {
                int rc = f_copy_file(sp, dp);
                if (rc == -2) { f_job.fail++; hs_strcpyn(f_job.why, "file too large", 24); }
                else if (rc != 0) { f_job.fail++; hs_strcpyn(f_job.why, "copy failed", 24); }
                else {
                    f_job.ok++;
                    if (f_job.kind == 2) ext2_64_delete_file(sp);
                }
            }
            f_job.done++;
        }
        if (f_job.cur_i >= f_job.cur_n) { f_job.qh++; f_job.cur_n = 0; }
    }
}


/* 删除分片: 一份任务要处理"多个顶层项, 其中若干是目录树"两件事, 所以分两阶段
 *   phase 0: 逐个顶层项; 目录入遍历队列, 边遍历边删文件, 顺手把每个目录记进 ddel
 *   phase 1: 队列空了, 从 ddel 末尾往前 delete_dir (子目录先于父目录)
 * 每个文件/目录只占一帧, 大目录树删除也有进度、可取消 */
static void f_job_step_delete(void)
{
    for (int k = 0; k < F_JOB_PER_FRAME && f_job.active; k++) {
        if (f_job.phase == 0) {
            /* ① 顶层文件 / 目录 */
            if (f_job.top_i < f_job.top_n) {
                int i = f_job.top_i++;
                hs_strcpyn(f_job.cur_name, f_job.tn[i], 64);
                uint32_t sz = 0; uint8_t isdir = 0;
                if (ext2_64_stat(f_job.ts[i], &sz, &isdir) != 0) {
                    f_job.fail++; hs_strcpyn(f_job.why, "not found", 24);
                } else if (isdir) {
                    if (f_job_push_dir(f_job.ts[i], f_job.ts[i]) != 0) {
                        f_job.fail++; hs_strcpyn(f_job.why, "too many dirs", 24);
                    }
                } else {
                    int rc = ext2_64_delete_file(f_job.ts[i]);
                    if (rc == 0) f_job.ok++; else { f_job.fail++; hs_strcpyn(f_job.why, "delete failed", 24); }
                }
                f_job.done++;
                continue;
            }
            /* ② 从遍历队列取目录展开 */
            if (f_job.cur_n == 0) {
                if (f_job.qh >= f_job.qt) { f_job.phase = 1; continue; }
                int n = ext2_64_list_dir_entries_ex(f_job.qs[f_job.qh], f_job.names,
                                                    f_job.types, f_job.sizes, FS_COPY_ENTRIES);
                if (n < 0 || n >= FS_COPY_ENTRIES) {
                    f_job.fail++;
                    hs_strcpyn(f_job.why, n < 0 ? "list failed" : "dir > 64 entries", 24);
                    f_job.qh++; f_job.cur_n = 0;
                    continue;
                }
                if (n == 0) {
                    if (f_job.ddn < 48) hs_strcpyn(f_job.ddel[f_job.ddn++], f_job.qs[f_job.qh], FS_PATH_MAX);
                    f_job.qh++; f_job.cur_n = 0;
                    continue;
                }
                f_job.cur_n = n; f_job.cur_i = 0;
                continue;
            }
            int i = f_job.cur_i++;
            {
                char sp[FS_PATH_MAX];
                hs_strcpyn(f_job.cur_name, f_job.names[i], 64);
                if (f_join(sp, FS_PATH_MAX, f_job.qs[f_job.qh], f_job.names[i]) != 0) {
                    f_job.fail++; hs_strcpyn(f_job.why, "path too long", 24);
                } else if (f_job.types[i]) {
                    if (f_job_push_dir(sp, sp) != 0) { f_job.fail++; hs_strcpyn(f_job.why, "too many dirs", 24); }
                } else {
                    int rc = ext2_64_delete_file(sp);
                    if (rc == 0) f_job.ok++; else { f_job.fail++; hs_strcpyn(f_job.why, "delete failed", 24); }
                }
                f_job.done++;
            }
            if (f_job.cur_i >= f_job.cur_n) {
                if (f_job.ddn < 48) hs_strcpyn(f_job.ddel[f_job.ddn++], f_job.qs[f_job.qh], FS_PATH_MAX);
                f_job.qh++; f_job.cur_n = 0;
            }
            continue;
        }
        /* ③ 回收: 逆序删目录 */
        if (f_job.ddn <= 0) { f_job_finish(); return; }
        f_job.ddn--;
        hs_strcpyn(f_job.cur_name, f_job.ddel[f_job.ddn], 64);
        {
            int rc = ext2_64_delete_dir(f_job.ddel[f_job.ddn]);
            if (rc == 0) f_job.ok++; else { f_job.fail++; hs_strcpyn(f_job.why, "rmdir failed", 24); }
        }
    }
}

/* 启动删除任务 */
static void f_job_start_delete(const char *const *paths, const char *const *names, int n)
{
    f_job.active = 0; f_job.kind = 3;
    f_job.ok = 0; f_job.fail = 0; f_job.done = 0; f_job.why[0] = 0;
    f_job.qh = 0; f_job.qt = 0; f_job.cur_n = 0; f_job.cur_i = 0;
    f_job.top_i = 0; f_job.top_n = 0; f_job.topdir_n = 0; f_job.cur_name[0] = 0;
    f_job.phase = 0; f_job.ddn = 0;
    for (int i = 0; i < n && i < FS_CLIP_MAX; i++) {
        hs_strcpyn(f_job.ts[i], paths[i], FS_PATH_MAX);
        hs_strcpyn(f_job.tn[i], names[i], 64);
        f_job.td[i][0] = 0;
    }
    f_job.top_n = (n < FS_CLIP_MAX) ? n : FS_CLIP_MAX;
    if (f_job.top_n == 0) { f_job_report(); return; }
    f_job.active = 1;
    hs_strcpyn(f_status2, "Deleting...", FS_PROMPT_MAX);
}

/* 启动: 剪贴板里逐个预检, 目录 mkdir 后入队, 文件进顶层表 */
static void f_job_start(int kind)
{
    f_job.active = 0; f_job.kind = kind;
    f_job.ok = 0; f_job.fail = 0; f_job.done = 0; f_job.why[0] = 0;
    f_job.qh = 0; f_job.qt = 0; f_job.cur_n = 0; f_job.cur_i = 0;
    f_job.top_i = 0; f_job.top_n = 0; f_job.topdir_n = 0; f_job.cur_name[0] = 0;

    for (int i = 0; i < f_clip_count; i++) {
        char dst[FS_PATH_MAX];
        if (f_join(dst, FS_PATH_MAX, f_path, f_clip_names[i]) != 0) {
            f_job.fail++; hs_strcpyn(f_job.why, "path too long", 24); continue;
        }
        uint32_t sz = 0; uint8_t isdir = 0;
        if (ext2_64_stat(dst, &sz, &isdir) == 0) {
            f_job.fail++; hs_strcpyn(f_job.why, "target exists", 24); continue;
        }
        if (ext2_64_stat(f_clip_paths[i], &sz, &isdir) != 0) {
            f_job.fail++; hs_strcpyn(f_job.why, "source missing", 24); continue;
        }
        if (isdir) {
            if (ext2_64_mkdir(dst) != 0) { f_job.fail++; hs_strcpyn(f_job.why, "mkdir failed", 24); continue; }
            if (f_job_push_dir(f_clip_paths[i], dst) != 0) { f_job.fail++; hs_strcpyn(f_job.why, "too many dirs", 24); continue; }
            f_job.ok++;
            if (f_job.topdir_n < FS_CLIP_MAX) {
                hs_strcpyn(f_job.tdsrc[f_job.topdir_n], f_clip_paths[i], FS_PATH_MAX);
                f_job.topdir_n++;
            }
        } else {
            int t = f_job.top_n;
            if (t >= FS_CLIP_MAX) { f_job.fail++; hs_strcpyn(f_job.why, "too many items", 24); continue; }
            hs_strcpyn(f_job.ts[t], f_clip_paths[i], FS_PATH_MAX);
            hs_strcpyn(f_job.td[t], dst, FS_PATH_MAX);
            hs_strcpyn(f_job.tn[t], f_clip_names[i], 64);
            f_job.top_n++;
        }
    }

    if (f_job.top_n == 0 && f_job.qt == 0) { f_job_report(); return; }
    f_job.active = 1;
    f_job.cur_n = 0; f_job.cur_i = 0; f_job.qh = 0;
    hs_strcpyn(f_status2, kind == 2 ? "Moving..." : "Copying...", FS_PROMPT_MAX);
}

static void f_job_cancel(void)
{
    if (!f_job.active) return;
    f_job.active = 0;
    char msg[96]; char tmp[8];
    strcpy(msg, "Cancelled after ");
    hs_itoa(f_job.ok, tmp); strcpy(msg + hs_strlen(msg), tmp);
    strcpy(msg + hs_strlen(msg), " items");
    hs_strcpyn(f_status2, msg, FS_PROMPT_MAX);
    f_refresh();
}

static void f_job_report(void)
{
    char msg[96]; char tmp[8];
    strcpy(msg, f_job.kind == 3 ? "Deleted " : (f_job.kind == 2 ? "Moved " : "Copied "));
    hs_itoa(f_job.ok, tmp); strcpy(msg + hs_strlen(msg), tmp);
    strcpy(msg + hs_strlen(msg), f_job.ok == 1 ? " item" : " items");
    if (f_job.fail > 0) {
        strcpy(msg + hs_strlen(msg), ", ");
        hs_itoa(f_job.fail, tmp);
        strcpy(msg + hs_strlen(msg), tmp);
        strcpy(msg + hs_strlen(msg), " failed");
        if (f_job.why[0]) {
            strcpy(msg + hs_strlen(msg), " (");
            strcpy(msg + hs_strlen(msg), f_job.why);
            strcpy(msg + hs_strlen(msg), ")");
        }
    }
    hs_strcpyn(f_status2, msg, FS_PROMPT_MAX);
    if (f_job.fail == 0 && f_job.kind != 3) { f_clip_count = 0; f_clip_mode = 0; }
    f_refresh();
}

static void f_do_paste(void)
{
    if (f_clip_mode == 0 || f_clip_count == 0) {
        hs_strcpyn(f_status2, "Clipboard is empty", FS_PROMPT_MAX);
        return;
    }
    if (f_job.active) {
        hs_strcpyn(f_status2, "Busy: a file job is running", FS_PROMPT_MAX);
        return;
    }
    /* P2: 不再同步跑完整个目录树, 改为启动分片任务(每帧推进, 可取消) */
    f_job_start(f_clip_mode);
}

/* 取消热区: 与渲染用同一套几何 */
static int f_job_cancel_hit(int x, int y)
{
    if (!f_job.active) return 0;
    int cx = f_win->x + FS(6), cy = f_win->y + FS(26);
    int cw = f_win->width - FS(12), ch = f_win->height - FS(26) - FS(40);
    int cbx = cx + cw - FS(52), cby = cy + ch + FS(12);
    return (x >= cbx && y >= cby && x <= cx + cw && y <= cby + FS(17));
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
    int cx = w->x + VC(6), cy = w->y + VC(24);

    /* V0.55.0 P0: PNG 位图等比缩放绘制 */
    if (v_is_png && v_png.rgba) {
        font64_draw_string(fb, W, H, cx, cy, v_name, g_shell.colors.accent, VC(13));
        int aw = w->width - VC(16);
        int ah = w->height - VC(52);
        if (aw < 8 || ah < 8) return;
        int dw = aw, dh = (int)((long)v_png.height * aw / (long)v_png.width);
        if (dh > ah) { dh = ah; dw = (int)((long)v_png.width * ah / (long)v_png.height); }
        int dx = w->x + VC(8) + (aw - dw) / 2;
        int dy = w->y + VC(44) + (ah - dh) / 2;
        gfx64_blit_rgba(dx, dy, dw, dh, v_png.rgba, (int)v_png.width, (int)v_png.height);
        return;
    }

    int cw = w->width - VC(12);
    font64_draw_string(fb, W, H, cx, cy, v_name, g_shell.colors.accent, VC(13));
    cy += VC(22);
    int rows = (w->height - VC(26) - VC(28)) / VC(16);
    if (rows < 1) rows = 1;

    /* 定位到第 v_scroll 行的起始字节 */
    int i = 0;
    for (int l = 0; l < v_scroll && i < v_len; l++) {
        while (i < v_len && v_data[i] != '\n') i++;
        if (i < v_len) i++;
    }
    int maxc = (cw - VC(8)) / VC(7);   /* 7 = 半角字符宽 */
    if (maxc < 8) maxc = 8;            /* 8 = 字符列下限 */
    if (maxc > 110) maxc = 110;        /* 110 = 字符列上限(受 lb[128] 容量约束) */
    for (int r = 0; r < rows && i < v_len; r++) {
        char lb[128]; int lc = 0;
        while (i < v_len && v_data[i] != '\n' && lc < maxc)
            lb[lc++] = v_data[i++];
        if (i < v_len && v_data[i] == '\n') i++;
        lb[lc] = 0;
        font64_draw_string(fb, W, H, cx, cy + r * VC(16), lb,
                           g_shell.colors.window_fg, VC(12));
    }
    char st[64];
    strcpy(st, "line ");
    char tmp[12];
    hs_itoa(v_scroll + 1, tmp);
    strcpy(st + hs_strlen(st), tmp);
    if (v_truncated) strcpy(st + hs_strlen(st), "  (truncated: file too large)");
    else strcpy(st + hs_strlen(st), "  Up/Down/PgUp/PgDn to scroll");
    font64_draw_string(fb, W, H, cx, w->y + w->height - VC(18), st,
                       g_shell.colors.text_secondary, VC(10));
}

void hs_files_cycle_view(void) { f_view_mode = !f_view_mode; f_scroll = 0; }


void hs_files_click(int x, int y)
{
    if (f_job_cancel_hit(x, y)) { f_job_cancel(); return; }
    extern void vga_puts(const char *s);
    if (!f_win || !f_win->visible) return;
    /* V0.13.39: 路径栏右侧 USB 按钮点击 */
    {
        extern int usb64_dev_count(void);
        extern int storage_hal_get_device(void);
        extern int usb64_mount_usb(void);
        extern int usb64_umount_usb(void);
        /* 几何必须和渲染用同一套 FS 口径: 原来这里是裸像素, 缩放一开
         * 点击区就和画出来的按钮错位 */
        int cx0 = f_win->x + FS(6), cy0 = f_win->y + FS(26);
        int cw0 = f_win->width - FS(12);
        extern int usb64_dev_count(void);
        int has_usb = usb64_dev_count() > 0;
        int on_usb = storage_hal_get_device() == 1;
        const char *txt = has_usb ? (on_usb ? "[USB: mounted]" : "[USB: click to mount]") : "[USB: not detected]";
        int tw = FS(12) * (hs_strlen(txt)) + FS(4);
        int bx = cx0 + cw0 - tw - FS(4);
        /* 搜索按钮(USB 左边): 打开搜索输入框, 边打边筛 */
        int sw = FS(7) * 6 + FS(10);
        int sx = bx - sw - FS(6);
        if (x >= sx && x <= sx + sw && y >= cy0 && y <= cy0 + 20) {
            f_prompt_start(FS_PROMPT_SEARCH, f_filter);
            hs_strcpyn(f_status2, "Type to filter, Enter to keep, Tab to clear", FS_PROMPT_MAX);
            return;
        }
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
        int cy0 = f_win->y + FS(26);
        if (y >= cy0 && y <= cy0 + FS(20) && x >= f_win->x + FS(6)) {
            f_prompt_start(FS_PROMPT_GOTO, f_path);
            hs_strcpyn(f_status2, "Enter to go, Tab to cancel", FS_PROMPT_MAX);
            return;
        }
    }

    /* 快捷目录条: 命中范围与渲染同源(含末尾的钉/取消钉按钮) */
    {
        int cx0 = f_win->x + FS(6);
        int cw0 = f_win->width - FS(12);
        int cy0 = f_win->y + FS(26) + FS(22);
        int lx = cx0 + FS(2);
        if (y >= cy0 && y <= cy0 + FS(16)) {
            for (int i = 0; i < f_quick_n; i++) {
                char lb[20];
                f_quick_label(i, lb, sizeof lb);
                int lw = FS(7) * hs_strlen(lb) + FS(10);
                if (lx + lw > cx0 + cw0 - FS(26)) break;
                if (x >= lx && x <= lx + lw) {
                    hs_strcpyn(f_path, f_quick[i], FS_PATH_MAX - 1);
                    f_scroll = 0; f_sel = 0;
                    f_refresh();
                    return;
                }
                lx += lw + FS(4);
            }
            if (lx + FS(16) <= cx0 + cw0 - FS(4) &&
                x >= lx && x <= lx + FS(16)) {
                f_quick_toggle_here();
                f_refresh();
                return;
            }
            return;   /* 点在空白处: 吞掉, 避免落进下方列表误选 */
        }
    }
    /* 左栏目录树最先判: 树与"列头"同高起算, 不先拦会被列头排序吃掉 */
    {
        extern int hs_files_tree_click(int x, int y);
        if (hs_files_tree_click(x, y)) return;
    }

    /* 列头: 点击切排序(同列再次点击切换升降) */
    if (f_view_mode == 0) {
        int cx0 = f_win->x + FS(6) + FS(FS_TREE_W) + FS(8);  /* 只覆盖右栏 */
        int cw0 = f_win->width - FS(12) - FS(FS_TREE_W) - FS(8);
        int hy = f_win->y + FS(26) + FS(44);
        if (y >= hy && y <= hy + FS(16)) {
            int key = -1;
            if (x >= cx0 + cw0 - FS(34)) key = 2;
            else if (x >= cx0 + cw0 - FS(86)) key = 1;
            else if (x >= cx0) key = 0;
            if (key >= 0) {
                uint8_t keep[FS_MAX_ITEMS];
                for (int i = 0; i < FS_MAX_ITEMS; i++) keep[i] = f_mark[i];
                if (f_sort_key == key) f_sort_desc = !f_sort_desc;
                else { f_sort_key = key; f_sort_desc = 0; }
                f_refresh();
                for (int i = 0; i < FS_MAX_ITEMS; i++) f_mark[i] = keep[i];
                f_scroll = 0;
            }
            return;
        }
    }

    int cy = f_win->y + FS(26) + FS(60);
    int rows = f_visible_rows();
    int px = f_win->x + FS(6) + FS(FS_TREE_W) + FS(8);
    int pw = f_win->width - FS(12) - FS(FS_TREE_W) - FS(8);
    if (x < px || x > px + pw) return;
    if (f_view_mode == 0) {
        int rel = (y - cy) / f_row_h();
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
        int gs = 76, cols = (f_win->width - 12 - 140 - 8) / gs;
        if (cols < 1) cols = 1;
        cy = f_win->y + FS(26) + FS(44);
        int rel = ((x - (f_win->x + 6 + 140 + 8 + 6)) / gs) + ((y - cy) / (gs + 8)) * cols;
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

/* ============================================================================
 * 右键菜单 / 光标形态(V0.57.1)
 * ========================================================================== */
void hs_files_menu_action(int id)
{
    if (id >= 0 && id <= 5) { hs_files_do_action(id); return; }
    if (id == 6) { f_do_copy(0); return; }
    if (id == 7) { f_do_copy(1); return; }
    if (id == 8) { f_do_paste(); return; }
    if (id == 9) { f_props_open(); return; }
    if (id == 10) {
        f_prompt_start(FS_PROMPT_MKFILE, "newfile.txt");
        hs_strcpyn(f_status2, "Enter to create, Tab to cancel", FS_PROMPT_MAX);
        return;
    }
}

/* 在点击点弹出文件操作菜单。标签内容随"有选中项 / 剪贴板非空"变化,
 * 免得给用户看一堆此刻点了没反应的项目。 */
int hs_files_open_ctx(int x, int y)
{
    extern void hs_ctx_open(int x, int y, const char *const *labels,
                            const int *ids, int n, int owner);
    const char *labs[9];
    int ids[9];
    int n = 0;
    int has_sel = (f_match_count > 0);
    if (has_sel) {
        labs[n] = "Open";        ids[n++] = 0;
        labs[n] = "Rename";      ids[n++] = 5;
        labs[n] = "Copy";        ids[n++] = 6;
        labs[n] = "Cut";         ids[n++] = 7;
        labs[n] = "Delete";      ids[n++] = 1;
        labs[n] = "Properties";  ids[n++] = 9;
    }
    if (f_clip_count > 0) {
        labs[n] = f_clip_mode == 2 ? "Paste (move)" : "Paste (copy)";
        ids[n++] = 8;
    }
    labs[n] = "New folder";      ids[n++] = 4;
    labs[n] = "Refresh";         ids[n++] = 2;
    hs_ctx_open(x, y, labs, ids, n, 1);
    return 1;
}

/* 光标形态: -1=不在本窗口, 0=箭头, 1=手型(可点), 2=文本 */
int hs_files_cursor_kind(int x, int y)
{
    if (!f_win || !f_win->visible) return -1;
    if (x < f_win->x || x > f_win->x + f_win->width) return -1;
    if (y < f_win->y || y > f_win->y + f_win->height) return -1;
    if (f_drag_state == 2) return 3;                   /* 拖拽中: 四向移动光标 */
    if (y <= f_win->y + FS(26)) return 0;              /* 标题栏 */
    int cy0 = f_win->y + FS(26);
    int ch = f_win->height - FS(26) - FS(40);
    int by = cy0 + ch - FS(24);
    if (y >= by && y <= by + FS(22))
        return (f_prompt != FS_PROMPT_NONE || f_del_confirm) ? 2 : 0;
    if (y <= cy0 + FS(20)) return 1;                   /* 路径栏 */
    return 1;                                          /* 位置条/列头/列表 */
}

void hs_files_do_action(int action)
{
    /* 0=Open 1=Delete 2=Refresh 3=Up 4=NewFolder 5=Rename */
    if (action == 0) { f_open_item(f_sel); }
    else if (action == 1) {
        /* 有标记 → 批量删除全部标记项; 无标记 → 删当前选中项 */
        int marks = 0;
        for (int i = 0; i < f_count; i++) if (f_mark[i]) marks++;
        if (marks > 0) {
            f_del_multi = marks;
            f_del_name[0] = 0;
            f_prompt = FS_PROMPT_NONE;
            f_del_confirm = 1;
            char msg[64], tmp[8];
            strcpy(msg, "Enter to delete ");
            hs_itoa(marks, tmp);
            strcpy(msg + hs_strlen(msg), tmp);
            strcpy(msg + hs_strlen(msg), " marked items");
            hs_strcpyn(f_status2, msg, FS_PROMPT_MAX);
            return;
        }
        if (f_sel >= 0 && f_sel < f_match_count) {
            int item = f_matched[f_sel];
            if (f_join(f_del_path, FS_PATH_MAX, f_path, f_items[item]) != 0) {
                hs_strcpyn(f_status2, "Path too long", FS_PROMPT_MAX);
                return;
            }
            hs_strcpyn(f_del_name, f_items[item], 60);
            f_prompt = FS_PROMPT_NONE;
            f_del_confirm = 1;
            f_del_multi = 0;
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
    } else if (f_prompt == FS_PROMPT_SEARCH) {
        f_prompt = FS_PROMPT_NONE;
        f_search_apply();
        char m[64], t[8];
        hs_strcpyn(m, "Search: ", 12);
        if (f_filter_len == 0) hs_strcpyn(m, "Search cleared", 20);
        else {
            hs_strcpyn(m + hs_strlen(m), f_filter, 24);
            strcpy(m + hs_strlen(m), "  ");
            hs_itoa(f_match_count, t);
            strcpy(m + hs_strlen(m), t);
            strcpy(m + hs_strlen(m), " found");
        }
        hs_strcpyn(f_status2, m, FS_PROMPT_MAX);
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
    /* 批量与单项统一走分片删除任务: 大目录树不再同步阻塞, 有进度也能取消 */
    static char pbuf[FS_CLIP_MAX][FS_PATH_MAX];
    static char nbuf[FS_CLIP_MAX][64];
    static const char *pl[FS_CLIP_MAX];
    static const char *nl[FS_CLIP_MAX];
    int n = 0;

    if (f_del_multi > 0) {
        for (int i = 0; i < f_count && n < FS_CLIP_MAX; i++) {
            if (!f_mark[i]) continue;
            if (f_join(pbuf[n], FS_PATH_MAX, f_path, f_items[i]) != 0) continue;
            hs_strcpyn(nbuf[n], f_items[i], 64);
            pl[n] = pbuf[n]; nl[n] = nbuf[n];
            n++;
        }
    } else {
        if (f_join(pbuf[0], FS_PATH_MAX, f_path, f_del_name) != 0) return;
        hs_strcpyn(nbuf[0], f_del_name, 64);
        pl[0] = pbuf[0]; nl[0] = nbuf[0];
        n = 1;
    }

    f_del_multi = 0;
    f_del_confirm = 0;
    if (f_job.active) { hs_strcpyn(f_status2, "Busy: a file job is running", FS_PROMPT_MAX); return; }
    f_job_start_delete(pl, nl, n);
}

void hs_files_key(int key)
{
    /* 多标签: F1 新建 / F10 关闭 (F2~F9 已各有用途) */
    if (key == 0x3B) { f_tab_new(); return; }      /* F1 */
    if (key == 0x44) { f_tab_close(); return; }    /* F10 */

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
            if (f_prompt == FS_PROMPT_SEARCH) f_search_apply();
            return;
        }
        if (key >= 0x20 && key <= 0x7E) {
            if (f_prompt_len < FS_PROMPT_MAX - 1) {
                f_prompt_buf[f_prompt_len++] = (char)key;
                f_prompt_buf[f_prompt_len] = 0;
            }
            if (f_prompt == FS_PROMPT_SEARCH) f_search_apply();
            return;
        }
        if (key == '\n' || key == '\r' || key == 0x1C) {
            f_prompt_commit();
            return;
        }
        if (key == '\t') {
            if (f_prompt == FS_PROMPT_SEARCH) { f_clear_filter(); f_refresh(); }
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
    if (f_job.active && key == 0x01) { f_job_cancel(); return; }   /* Esc */
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

/* V0.57.0: 本文件的颜色混合(刻度线用), 与 hpdesk.c 的 static helper 各自私有 */
static uint32_t hs_apps_mix(uint32_t a, uint32_t b, int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg2 = (b >> 8) & 0xFF, bb = b & 0xFF;
    return (uint32_t)((((ar * (100 - pct) + br * pct) / 100) << 16) |
                      (((ag * (100 - pct) + bg2 * pct) / 100) << 8) |
                      ((ab * (100 - pct) + bb * pct) / 100));
}

static void m_draw_curve(void *fb, int W, int H, int x, int y, int w, int h,
                         int *hist, uint32_t color, int maxv)
{
    /* V0.56.2: 背景用主题面板色 —— 原硬编码 0x000000 在浅色主题下是三块黑块 */
    gfx64_fill_rect(x, y, w, h, g_shell.colors.panel_bg);
    gfx64_draw_rect(x, y, w, h, g_shell.colors.border);
    /* V0.57.0: 25/50/75% 三条刻度线。draw_line 在窗口内容层不可见, 统一用 1px 填充 */
    for (int q = 1; q <= 3; q++) {
        int gy = y + h - 2 - (h - 4) * q / 4;
        gfx64_fill_rect(x + 2, gy, w - 4, 1,
                        hs_apps_mix(g_shell.colors.panel_bg, g_shell.colors.border, 55));
    }
    /* V0.56.2: 折线在窗口内容层上看不见, 改为柱状条(gfx64_fill_rect 已验证可用),
     * 每个采样 2px 宽、3px 步进, 右起最新 */
    int n = (m_hist_count > w / 3) ? w / 3 : m_hist_count;
    for (int i = 0; i < n; i++) {
        int v = hist[i];
        if (v > maxv) v = maxv;
        if (v < 0) v = 0;
        int px = x + w - 4 - i * 3;
        if (px < x + 2) break;
        /* V0.57.0: 0 值也留 1px 采样点。
         * 空闲系统的 CPU 真的是 0%(idle 100%), 原来 bh=0 就什么都不画,
         * 整块图看着是空的, 用户会以为"图表没接上数据"。留基线表示在采样。 */
        int bh = (v * (h - 4)) / maxv;
        if (bh < 1) bh = 1;
        uint32_t c = (v > 0) ? color
                             : hs_apps_mix(g_shell.colors.panel_bg, color, 40);
        gfx64_fill_rect(px, y + h - 2 - bh, 2, bh, c);
    }
}

static void m_render(hs_window_t *w)
{
    void *fb = gfx64_get_framebuffer();
    /* 内容按窗口自身尺寸缩放(基准 520x380)。列偏移 c_name/c_cpu/c_mem/c_state
     * 也走同一个比例, 这样放大后表头与数据行仍然共用一组列位置 —— 比例字体下
     * 靠空格拼字符串对齐必然随位数漂移, 只有共用列偏移才稳。 */
#define MC(v) hs_win_k(w, 520, v)
    int W = g_shell.screen_width, H = g_shell.screen_height;
    int cx = w->x + MC(6), cy = w->y + MC(26);
    int cw = w->width - MC(12);

    m_tick++;
    /* V0.56.2: 标题条已由窗口管理器绘制, 这里不再重复画一遍 "Task Manager" */

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
    font64_draw_string(fb, W, H, cx, cy, buf, g_shell.colors.window_fg, MC(12));
    cy += MC(18);

    /* V0.56.2: 整机占用摘要(用户看这里, 不必读曲线) */
    {
        extern int hs_process_get_cpu_usage(void);
        extern int hs_process_get_mem_usage(void);
        char sbuf[64];
        strcpy(sbuf, "CPU: ");
        hs_itoa(hs_process_get_cpu_usage(), tmp);
        strcpy(sbuf + hs_strlen(sbuf), tmp);
        strcpy(sbuf + hs_strlen(sbuf), "%    MEM: ");
        hs_itoa(hs_process_get_mem_usage(), tmp);
        strcpy(sbuf + hs_strlen(sbuf), tmp);
        strcpy(sbuf + hs_strlen(sbuf), "%    UP: ");
        /* V0.57.1: uptime 直接读 PIT 计数器。原来打算由 kstat 线程缓存后显示, 但 kstat
         * 目前只能登记挂起(见 process64_create_suspended), 显示它的缓存值会恒为 0 ——
         * 那就成了假数据, 所以这里不走缓存。 */
        extern uint64_t pit64_get_ms(void);
        hs_itoa((int)(pit64_get_ms() / 1000), tmp);
        strcpy(sbuf + hs_strlen(sbuf), tmp);
        strcpy(sbuf + hs_strlen(sbuf), "s");
        font64_draw_string(fb, W, H, cx, cy, sbuf, g_shell.colors.accent, MC(12));
        cy += MC(18);
    }

    /* 实时曲线: CPU + 网络 */
    int cpu = hs_process_get_cpu_usage();
    uint64_t tx = 0, rx = 0; uint32_t txr = 0, rxr = 0;
    e1000_get_net_stats(&tx, &rx, &txr, &rxr);
    m_hist_push(m_cpu_hist, cpu);
    m_hist_push(m_rx_hist, (int)(rxr / 1024));   /* KB/s */
    m_hist_push(m_tx_hist, (int)(txr / 1024));

    int gh = MC(38), gw = cw - MC(80);
    m_draw_curve(fb, W, H, cx, cy, gw, gh, m_cpu_hist, 0x52C41A, MC(100));
    font64_draw_string(fb, W, H, cx + gw + MC(6), cy + MC(6), "CPU%", 0x52C41A, MC(10));
    m_draw_curve(fb, W, H, cx, cy + gh + MC(4), gw, gh, m_rx_hist, 0x4A9EEC, MC(256));
    font64_draw_string(fb, W, H, cx + gw + MC(6), cy + gh + MC(10), "RX", 0x4A9EEC, MC(10));
    m_draw_curve(fb, W, H, cx, cy + MC(2) * (gh + MC(4)), gw, gh, m_tx_hist, 0xE0A44E, MC(256));
    font64_draw_string(fb, W, H, cx + gw + MC(6), cy + MC(2) * (gh + MC(4)) + MC(10), "TX", 0xE0A44E, MC(10));
    cy += MC(3) * gh + MC(12);

    /* 分隔线 */
    gfx64_fill_rect(cx, cy, cw, 1, g_shell.colors.border);
    cy += MC(6);

    /* 表头 (V0.13.46: 加PID与状态列)
     * V0.57.1: 改成分列绘制。旧实现把表头和整行都拼成一个含空格的字符串再一次性画,
     * 而 font64 是比例字体, 空格宽度与 "0"/"16"/"100%" 的宽度不一致 —— 于是每行列位置
     * 随数值位数漂移, 看着就是"没对齐"。现在表头与数据行共用同一组列偏移。 */
    /* 列距按内容宽度定, 不用百分比: 520 宽的窗口按百分比摊开会让后面两列顶到右边界,
     * 窗口被别的面板遮住时直接看不见。NAME 最长 12 字符(~84px), CPU 最长 "100%"(~28px)。 */
    int c_pid = cx;
    int c_name = cx + MC(38);
    int c_cpu = cx + MC(150);
    int c_mem = cx + MC(202);
    int c_state = cx + MC(250);
    {
        uint32_t hc = g_shell.colors.text_secondary;
        font64_draw_string(fb, W, H, c_pid, cy, "PID", hc, MC(10));
        font64_draw_string(fb, W, H, c_name, cy, "NAME", hc, MC(10));
        font64_draw_string(fb, W, H, c_cpu, cy, "CPU", hc, MC(10));
        font64_draw_string(fb, W, H, c_mem, cy, "MEM", hc, MC(10));
        font64_draw_string(fb, W, H, c_state, cy, "STATE", hc, MC(10));
    }
    cy += MC(14);

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
            char tmp2[16];
            /* 运行中进程高亮 */
            int col = g_shell.colors.window_fg;
            if (procs[i].pid >= 100 && procs[i].state == 0) col = 0x52C41A;

            hs_itoa(procs[i].pid, tmp2);
            font64_draw_string(fb, W, H, c_pid, cy, tmp2, col, MC(11));

            char nmbuf[16];
            int nl = 0;
            while (procs[i].name[nl] && nl < 12) { nmbuf[nl] = procs[i].name[nl]; nl++; }
            nmbuf[nl] = 0;
            font64_draw_string(fb, W, H, c_name, cy, nmbuf, col, MC(11));

            hs_itoa(procs[i].cpu_usage, tmp2);
            strcpy(tmp2 + hs_strlen(tmp2), "%");
            font64_draw_string(fb, W, H, c_cpu, cy, tmp2, col, MC(11));

            hs_itoa(procs[i].mem_usage, tmp2);
            font64_draw_string(fb, W, H, c_mem, cy, tmp2, col, MC(11));

            /* V0.57.1: 内核线程标 "kthr" 而不是混在 "sys" 里 —— idle/kbeat 这类线程
             * 有的在轮转、有的是登记挂起(见 process64_create_suspended), 统一写 sys 会
             * 让人以为它们都在跑。 */
            const char *st = "kthr";
            if (procs[i].pid == 0) st = "sys";
            else if (procs[i].pid >= 100 && procs[i].state == 0) st = "RUN";
            else if (procs[i].pid >= 100) st = "idle";
            font64_draw_string(fb, W, H, c_state, cy, st, col, MC(11));

            cy += MC(15);
        }
    }

    /* V0.57.0: 内核服务/驱动清单。
     * 进程表一直只有 idle(PID 0) —— 桌面与驱动都还没注册成调度进程, 表格看着像
     * "没接上数据"。这里把真正注册进驱动框架的驱动列出来, 让面板反映系统里确实
     * 在跑什么, 而不是用假进程凑数(进程模型补齐是另一件事, 见 FIXES 遗留)。 */
    {
        extern int drv_get_all(drv_desc_t **out, int max);
        static const char *svc_state[] = {"UNLOAD", "LOADED", "PROBED", "ACTIVE", "SUSP", "ERROR"};
        drv_desc_t *ds[DRV_MAX_COUNT];
        int dn = drv_get_all(ds, DRV_MAX_COUNT);
        if (dn > 0 && cy + MC(20) < w->y + w->height - MC(22)) {
            cy += MC(4);
            gfx64_fill_rect(cx, cy, cw, 1, g_shell.colors.border);
            cy += MC(6);
            font64_draw_string(fb, W, H, cx, cy, "KERNEL SERVICES (drivers loaded)",
                               g_shell.colors.text_secondary, MC(10));
            cy += MC(14);
            for (int i = 0; i < dn && cy < w->y + w->height - 22; i++) {
                char sline[96];
                strcpy(sline, ds[i]->name ? ds[i]->name : "?");
                while (hs_strlen(sline) < 20) strcpy(sline + hs_strlen(sline), " ");
                int stt = (int)ds[i]->state;
                strcpy(sline + hs_strlen(sline),
                       (stt >= 0 && stt < 6) ? svc_state[stt] : "?");
                font64_draw_string(fb, W, H, cx, cy, sline, g_shell.colors.window_fg, MC(11));
                cy += MC(14);
            }
        }
    }

    /* V0.13.46: 结束进程提示条 (V0.56.2: 固定贴窗口底部, 不再跟随列表溢出) */
    cy = w->y + w->height - MC(16);
    if (m_kill_mode) {
        char kbuf[48];
        strcpy(kbuf, "End PID: ");
        hs_itoa(m_kill_pid, tmp);
        strcpy(kbuf + hs_strlen(kbuf), tmp);
        strcpy(kbuf + hs_strlen(kbuf), " (Enter=end, Esc=cancel)");
        font64_draw_string(fb, W, H, cx, cy, kbuf, 0xE0A44E, MC(10));
    } else {
        font64_draw_string(fb, W, H, cx, cy, "Press K then PID + Enter to end a process",
                           g_shell.colors.text_secondary, MC(10));
    }
#undef MC
}


/* ============================================================================
 * 对外接口
 * ========================================================================== */
hs_window_t *hs_terminal_create(void)
{
    if (t_win && t_win->visible) { t_win->focus = 1; return t_win; }
    t_win = hs_window_create(HS_WINDOW_FLOATING, "Terminal", -1, -1,
                            hs_ui_sx(520), hs_ui_sy(340));
    if (t_win) {
        t_win->render = t_render;
        t_put("Hepu Terminal v0.57.0");
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
    /* 左树右列表: 窗口加宽到 620, 否则 140 的树一占, 右栏三列会挤 */
    f_win = hs_window_create(HS_WINDOW_FLOATING, "Files", -1, -1,
                            hs_ui_sx(620), hs_ui_sy(380));
    if (f_win) {
        f_win->render = f_render;
        strcpy(f_path, "/");
        /* 第一个标签: 标明自己并占住这个位置 */
        f_tab_count = 1;
        f_tab_cur = 0;
        hs_strcpyn(f_tab_path[0], "/", FS_PATH_MAX);
        f_tab_scroll[0] = 0; f_tab_sel[0] = -1;
        f_tab_view[0] = 0;  f_tab_skey[0] = 0; f_tab_sdesc[0] = 0;
        f_tree_init();
        f_quick_load();
        f_refresh();
    }
    return f_win;
}

hs_window_t *hs_taskmgr_create(void)
{
    if (m_win && m_win->visible) { m_win->focus = 1; return m_win; }
    m_win = hs_window_create(HS_WINDOW_FLOATING, "Task Manager", -1, -1,
                            hs_ui_sx(520), hs_ui_sy(380));
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

/* 编辑器内容按窗口自身尺寸缩放(基准 640x440) */
static int e_sc(int v) { return hs_win_k(e_win, 640, v); }
#define EC(v) e_sc(v)
static int ed_line_h(void) { return e_sc(ED_LINE_H); }
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
    int cx = w->x + EC(8);
    int cy = w->y + EC(26);
    uint32_t accent = g_shell.colors.accent;
    uint32_t fg = g_shell.colors.window_fg;
    uint32_t dim = g_shell.colors.text_secondary;
    int vis = (w->height - EC(82)) / ed_line_h();
    if (vis < 1) vis = 1;

    /* 标题: 路径 + 修改标记 */
    char title[128];
    hs_strcpyn(title, e_path, 100);
    if (e_modified) hs_strcpyn(title + hs_strlen(title), "  *", 8);
    font64_draw_string(fb, W, H, cx, cy, title, accent, EC(13));
    cy += EC(18);
    font64_draw_string(fb, W, H, cx, cy, e_msg, dim, EC(10));
    cy += EC(16);

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

    int maxc = (w->width - EC(ED_GUTTER) - EC(16)) / EC(7);   /* 7 = 半角字符宽 */
    if (maxc < 8) maxc = 8;                                   /* 8 = 字符列下限 */

    int cursor_drawn = 0;
    for (int row = 0; row < vis; row++) {
        int lstart = pos;
        int lend = ed_line_end(pos);
        char num[8];
        hs_itoa(e_scroll + row + 1, num);
        font64_draw_string(fb, W, H, cx, cy + row * ed_line_h(), num, dim, EC(11));

        char lb[168];
        int lc = 0;
        int i = lstart;
        while (i < lend && lc < maxc && lc < 167) lb[lc++] = e_buf[i++];
        lb[lc] = 0;
        if (lc > 0)
            font64_draw_string(fb, W, H, cx + EC(ED_GUTTER), cy + row * ed_line_h(), lb, fg, EC(12));

        if (!cursor_drawn && e_cur >= lstart && e_cur <= lend) {
            int col = e_cur - lstart;
            if (col > maxc) col = maxc;
            gfx64_fill_rect(cx + EC(ED_GUTTER) + col * EC(7), cy + row * ed_line_h() + EC(2),
                            EC(2), EC(13), accent);
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
        e_win = hs_window_create(HS_WINDOW_FLOATING, "Editor", -1, -1,
                            hs_ui_sx(640), hs_ui_sy(440));
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
        e_win = hs_window_create(HS_WINDOW_FLOATING, "Editor", -1, -1,
                            hs_ui_sx(640), hs_ui_sy(440));
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









