/* ============================================================================
 * 赫普OS 32位 - hpdesk 图形终端（S1 最小生产力）
 *
 * 32 位 hpdesk 没有编 hs_apps.c，终端入口是 hpdesk32_deps.c 里的空壳。
 * 这里补一个真正可用的 32 位终端：F12 或 autostart 打开窗口，键盘输入走
 * legacy keyboard_try_getchar，文件读写走 32 位 VFS（和 shell 同一条路径）。
 *
 * S1 验收：edit /s1.txt -> 输入两行 -> .save -> 重启 -> cat /s1.txt 还在。
 * ========================================================================== */
#include <stdint.h>
#include <string.h>
#include "../x86_64/hpdesk.h"
#include "keyboard.h"
#include "vfs.h"

extern void *gfx64_get_framebuffer(void);
extern void  gfx64_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void  font64_draw_string(void *fb, int sw, int sh, int x, int y,
                                const char *s, uint32_t color, int size);
extern void  hpdesk_mark_dirty(void);
extern void  gfx64_clip_set(int x, int y, int w, int h);

#define S1_LINES 18
#define S1_LINE_MAX 72
#define S1_INPUT_MAX 72
#define S1_EDIT_MAX 2048

static hs_window_t *s1_win = 0;
static char s1_lines[S1_LINES][S1_LINE_MAX + 1];
static int  s1_line_count = 0;
static char s1_input[S1_INPUT_MAX + 1];
static int  s1_in_len = 0;

static int  s1_ed_active = 0;
static int  s1_ed_len = 0;
static char s1_ed_path[128];
static char s1_ed_buf[S1_EDIT_MAX];

static void s1_put(const char *s)
{
    while (s && *s) {
        if (s1_line_count >= S1_LINES) {
            for (int i = 1; i < S1_LINES; i++)
                memcpy(s1_lines[i - 1], s1_lines[i], S1_LINE_MAX + 1);
            s1_line_count = S1_LINES - 1;
        }
        int n = 0;
        while (s[n] && n < S1_LINE_MAX) n++;
        for (int i = 0; i < n; i++) s1_lines[s1_line_count][i] = s[i];
        s1_lines[s1_line_count][n] = 0;
        s1_line_count++;
        s += n;
    }
    hpdesk_mark_dirty();
}

static void s1_fill(uint32_t *fb, int sw, int sh, int x, int y, int w, int h, uint32_t c)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > sw ? sw : x + w, y1 = y + h > sh ? sh : y + h;
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++)
            fb[(size_t)yy * sw + xx] = c | 0xFF000000u;
}

static void s1_render(hs_window_t *w)
{
    void *fb = gfx64_get_framebuffer();
    int sw = g_shell.screen_width, sh = g_shell.screen_height;
    gfx64_clip_set(w->x, w->y, w->width, w->height);
    int x = w->x + 8, y = w->y + 28, rh = 15;
    int rows = (w->height - 28 - 28) / rh;
    if (rows > S1_LINES) rows = S1_LINES;
    s1_fill((uint32_t *)fb, sw, sh, w->x + 3, w->y + 22, w->width - 6, w->height - 26, 0x000000);
    int start = s1_line_count - rows;
    if (start < 0) start = 0;
    for (int r = 0; r < rows; r++) {
        int li = start + r;
        if (li >= 0 && li < s1_line_count)
            font64_draw_string(fb, sw, sh, x, y + r * rh, s1_lines[li], 0x33FF33, 12);
    }
    char prompt[S1_INPUT_MAX + 4];
    prompt[0] = '>'; prompt[1] = ' ';
    int n = 0;
    while (s1_input[n] && n < S1_INPUT_MAX) { prompt[n + 2] = s1_input[n]; n++; }
    prompt[n + 2] = 0;
    int iy = w->y + w->height - 24;
    s1_fill((uint32_t *)fb, sw, sh, w->x + 3, iy - 2, w->width - 6, 24, 0x0A0F0A);
    font64_draw_string(fb, sw, sh, x, iy, prompt, 0x66FF66, 12);
}

static int s1_ed_load(const char *path)
{
    int fd, n;
    s1_ed_len = 0;
    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) return 0;
    while (s1_ed_len < S1_EDIT_MAX - 1) {
        n = vfs_read(fd, s1_ed_buf + s1_ed_len, S1_EDIT_MAX - 1 - s1_ed_len);
        if (n <= 0) break;
        s1_ed_len += n;
    }
    vfs_close(fd);
    return s1_ed_len;
}

static int s1_ed_save(void)
{
    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);
    int fd = vfs_open(s1_ed_path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) return -1;
    int off = 0;
    while (off < s1_ed_len) {
        int w = vfs_write(fd, s1_ed_buf + off, (uint32_t)(s1_ed_len - off));
        if (w <= 0) { vfs_close(fd); return -1; }
        off += w;
    }
    vfs_close(fd);
    if (vfs_sync() != 0) return -1;
    serial_puts(0x3F8, "[S1T] save path="); serial_puts(0x3F8, s1_ed_path);
    serial_puts(0x3F8, " len="); serial_puthex(0x3F8, (uint32_t)s1_ed_len);
    serial_puts(0x3F8, "\n");
    return 0;
}

static void s1_ed_begin(const char *path)
{
    if (!path || !*path) { s1_put("edit: need a file name"); return; }
    int i = 0;
    while (path[i] && i < (int)sizeof(s1_ed_path) - 1) { s1_ed_path[i] = path[i]; i++; }
    s1_ed_path[i] = 0;
    s1_ed_load(s1_ed_path);
    s1_ed_active = 1;
    char b[96];
    b[0] = 'e'; b[1] = 'd'; b[2] = 'i'; b[3] = 't'; b[4] = ':'; b[5] = ' ';
    int n = 0; while (s1_ed_path[n] && n < 70) { b[6 + n] = s1_ed_path[n]; n++; }
    b[6 + n] = 0;
    s1_put(b);
    s1_put("lines append; .save | .quit | .list");
}

static void s1_ed_line(const char *line)
{
    if (strcmp(line, ".save") == 0) {
        s1_put(s1_ed_save() == 0 ? "edit: saved" : "edit: SAVE FAIL");
        return;
    }
    if (strcmp(line, ".quit") == 0) { s1_ed_active = 0; s1_put("edit: quit"); return; }
    if (strcmp(line, ".list") == 0) {
        if (!s1_ed_len) { s1_put("(empty)"); return; }
        int start = 0;
        for (int i = 0; i <= s1_ed_len; i++) {
            if (i == s1_ed_len || s1_ed_buf[i] == '\n') {
                char b[S1_LINE_MAX + 1];
                int n = i - start; if (n > S1_LINE_MAX) n = S1_LINE_MAX;
                for (int j = 0; j < n; j++) b[j] = s1_ed_buf[start + j];
                b[n] = 0; s1_put(b);
                start = i + 1;
            }
        }
        return;
    }
    if (!line[0]) return;
    int n = strlen(line);
    if (s1_ed_len + n + 1 >= S1_EDIT_MAX) { s1_put("edit: buffer full"); return; }
    for (int i = 0; i < n; i++) s1_ed_buf[s1_ed_len++] = line[i];
    s1_ed_buf[s1_ed_len++] = '\n';
    s1_put("edit: +");
}

static void s1_cat(const char *path)
{
    static char b[2048];
    int fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) { s1_put("cat: not found"); return; }
    int n = vfs_read(fd, b, sizeof(b) - 1);
    vfs_close(fd);
    if (n <= 0) { s1_put("(empty)"); return; }
    int start = 0;
    for (int i = 0; i <= n; i++) {
        if (i == n || b[i] == '\n') {
            char line[S1_LINE_MAX + 1];
            int m = i - start; if (m > S1_LINE_MAX) m = S1_LINE_MAX;
            for (int j = 0; j < m; j++) line[j] = b[start + j];
            line[m] = 0; s1_put(line);
            start = i + 1;
        }
    }
}

/* === S1: 终端基础文件工具（cp/mv/rm/ls/mkdir） ===
 * 全部走 32 位 VFS，和 shell 的 fs 命令同一后端；只接受绝对/根相对路径。 */
static int s1_cmd_ls(const char *path)
{
    vfs_dirent_t ents[24];
    int n, i, shown = 0;
    const char *p = (path && *path) ? path : "/";
    n = vfs_readdir(p, ents, 24);
    if (n <= 0) { s1_put("(empty)"); return 0; }
    char line[128]; int len = 0;
    for (i = 0; i < n; i++) {
        const char *nm = ents[i].name;
        int l = (int)strlen(nm);
        if (len + l + 3 >= (int)sizeof(line)) break;
        for (int j = 0; j < l; j++) line[len++] = nm[j];
        if (ents[i].type == VFS_DIRECTORY) line[len++] = '/';
        line[len++] = ' '; line[len++] = ' ';
        shown++;
        if (len > 90) { line[len] = 0; s1_put(line); len = 0; }
    }
    if (len) { line[len] = 0; s1_put(line); }
    if (!shown) s1_put("(empty)");
    return shown;
}

static int s1_cmd_cp(const char *src, const char *dst)
{
    static char cbuf[2048];
    int in, out, n, total = 0;
    if (!src || !dst || !*src || !*dst) { s1_put("cp: need src dst"); return -1; }
    in = vfs_open(src, VFS_O_RDONLY);
    if (in < 0) { s1_put("cp: source not found"); return -1; }
    out = vfs_open(dst, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (out < 0) { vfs_close(in); s1_put("cp: create dst FAIL"); return -1; }
    while ((n = vfs_read(in, cbuf, sizeof(cbuf))) > 0) {
        int off = 0;
        while (off < n) {
            int w = vfs_write(out, cbuf + off, (uint32_t)(n - off));
            if (w <= 0) { vfs_close(in); vfs_close(out); s1_put("cp: write FAIL"); return -1; }
            off += w; total += w;
        }
    }
    vfs_close(in); vfs_close(out);
    if (vfs_sync() != 0) { s1_put("cp: sync FAIL"); return -1; }
    char b[96]; b[0]='c'; b[1]='p'; b[2]=':'; b[3]=' ';
    int k=0; while (dst[k] && k<80) { b[4+k]=dst[k]; k++; } b[4+k]=0;
    s1_put(b);
    return 0;
}

static int s1_cmd_rm(const char *path)
{
    if (!path || !*path) { s1_put("rm: need path"); return -1; }
    if (vfs_unlink(path) == 0 || vfs_rmdir(path) == 0) { s1_put("rm: done"); return 0; }
    s1_put("rm: not found or dir not empty");
    return -1;
}

static void s1_cmd_mkdir(const char *path)
{
    if (!path || !*path) { s1_put("mkdir: need path"); return; }
    if (vfs_mkdir(path) == 0) s1_put("mkdir: done");
    else s1_put("mkdir: FAIL");
}

static void s1_cmd_mv(const char *src, const char *dst)
{
    if (s1_cmd_cp(src, dst) != 0) return;
    if (vfs_unlink(src) != 0) { s1_put("mv: unlink src FAIL"); return; }
    s1_put("mv: done");
}

static void s1_cmd_run(const char *name)
{
    extern int cmd_run_auto(const char *);
    if (!name || !*name) { s1_put("run: need program name"); return; }
    s1_put("run: starting (serial has output)");
    int rc = cmd_run_auto(name);
    char b[64]; b[0]='r'; b[1]='u'; b[2]='n'; b[3]=':'; b[4]=' '; b[5]='r'; b[6]='c'; b[7]='=';
    int v=(rc<0?-rc:rc), i=8; char t[8]; int j=0;
    if (!v) t[j++]='0';
    while (v && j<8) { t[j++] = (char)('0' + (v % 10)); v /= 10; }
    while (j>0) b[i++] = t[--j];
    if (rc < 0) { b[i++] = ' '; b[i++] = '('; b[i++] = 'E'; b[i++] = ')'; }
    b[i]=0; s1_put(b);
}

static void s1_run_line(const char *line)
{
    if (s1_ed_active) { s1_ed_line(line); return; }
    if (!line || !*line) return;
    if (strncmp(line, "edit ", 5) == 0) { s1_ed_begin(line + 5); return; }
    if (strncmp(line, "cat ", 4) == 0) { s1_cat(line + 4); return; }
    if (strcmp(line, "ls") == 0) { s1_cmd_ls("/"); return; }
    if (strncmp(line, "ls ", 3) == 0) { s1_cmd_ls(line + 3); return; }
    if (strncmp(line, "cp ", 3) == 0) {
        const char *a = line + 3; const char *b = a;
        while (*b && *b != ' ') b++;
        if (!*b) { s1_put("cp: need src dst"); return; }
        char src[96]; int n = (int)(b-a);
        if (n > 90) n = 90;
        for (int i = 0; i < n; i++) src[i] = a[i];
        src[n] = 0;
        while (*b == ' ') b++;
        s1_cmd_cp(src, b); return;
    }
    if (strncmp(line, "mv ", 3) == 0) {
        const char *a = line + 3; const char *b = a;
        while (*b && *b != ' ') b++;
        if (!*b) { s1_put("mv: need src dst"); return; }
        char src[96]; int n = (int)(b-a);
        if (n > 90) n = 90;
        for (int i = 0; i < n; i++) src[i] = a[i];
        src[n] = 0;
        while (*b == ' ') b++;
        s1_cmd_mv(src, b); return;
    }
    if (strncmp(line, "rm ", 3) == 0) { s1_cmd_rm(line + 3); return; }
    if (strncmp(line, "mkdir ", 6) == 0) { s1_cmd_mkdir(line + 6); return; }
    if (strncmp(line, "run ", 4) == 0) { s1_cmd_run(line + 4); return; }
    if (strcmp(line, "help") == 0) {
        s1_put("help: edit | cat | ls | cp | mv | rm | mkdir | run | clear");
        return;
    }
    if (strcmp(line, "clear") == 0) { s1_line_count = 0; return; }
    char b[96];
    b[0] = '?'; b[1] = ' '; b[2] = 0;
    int n = 0; while (line[n] && n < 80) { b[2 + n] = line[n]; n++; }
    b[2 + n] = 0; s1_put(b);
}

static int s1_pending_open = 0;
extern int hpdesk_owner_guard(const char *what);

void s1_terminal_create(void)
{
    /* hs_window_create() 只允许桌面 owner 线程调用。F12 是在 shell 的
     * keyboard_getchar 里先被看到的，那里直接建窗会被 owner guard 拒绝，
     * 窗口永远不出现。shell 侧只登记请求，等 hpdesk_render 的 owner 帧
     * 调 s1_terminal_poll 时再真正创建。 */
    if (!hpdesk_owner_guard("s1-terminal")) {
        s1_pending_open = 1;
        hpdesk_mark_dirty();
        return;
    }
    s1_pending_open = 0;
    if (s1_win && s1_win->id >= 0 && s1_win->visible) { s1_win->focus = 1; return; }
    s1_win = hs_window_create(HS_WINDOW_FLOATING, "Terminal", -1, -1, 520, 340);
    if (s1_win) {
        s1_win->render = s1_render;
        s1_put("Hepu Terminal 32-bit (S1)");
        s1_put("F12/autostart opened; type help");
    }
}

void s1_terminal_run_line_public(const char *line)
{
    s1_terminal_create();
    s1_run_line(line);
    hpdesk_mark_dirty();
}

int s1_terminal_is_open(void)
{
    return s1_win && s1_win->visible;
}

void s1_terminal_poll(void)
{
    if (s1_pending_open && hpdesk_owner_guard("s1-poll")) s1_terminal_create();
    /* V3.87-E8: IRQ1 顶半部只把扫描码塞进 raw_buf，扫描码→ASCII 在 softirq。
     * hpdesk 的渲染循环不经过 shell 的 keyboard_getchar，必须在这里主动跑
     * 底半部，否则 VNC/真键盘的按键永远停在 raw_buf 里，界面像“没收到键”。 */
    extern void softirq_run(void);
    softirq_run();
    if (!s1_terminal_is_open()) return;
    char c;
    while (keyboard_try_getchar(&c)) {

        if (c == (char)0x94) { s1_terminal_create(); continue; }  /* F12 */
        if (!s1_win) continue;
        if (c == '\n' || c == '\r') {
            s1_input[s1_in_len] = 0;
            char echo[S1_INPUT_MAX + 4];
            echo[0] = '>'; echo[1] = ' ';
            int n = 0; while (s1_input[n] && n < S1_INPUT_MAX) { echo[n + 2] = s1_input[n]; n++; }
            echo[n + 2] = 0; s1_put(echo);
            s1_run_line(s1_input);
            s1_in_len = 0; s1_input[0] = 0;
        } else if (c == 0x08) {
            if (s1_in_len > 0) s1_in_len--;
        } else if (c >= 0x20 && c < 0x7F && s1_in_len < S1_INPUT_MAX - 1) {
            s1_input[s1_in_len++] = c;
        }
        hpdesk_mark_dirty();
    }
}
