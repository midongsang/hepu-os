/* ============================================================================
 * 赫普OS - 桌面环境：终端模拟器
 * 从 desktop.c 拆分（V6.46 代码重构，大文件拆分目标<500行）
 * 包含：终端输出缓冲、命令解析、自定义绘制、键盘事件、终端窗口启动
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "wm.h"
#include "vga_gfx.h"
#include "hepu.h"
#include "vfs.h"
#include "ext2.h"

/* 终端窗口指针（与desktop.c共享，由on_launch_terminal管理） */
static wm_window_t *terminal_win = NULL;

/* center_create 在 desktop.c 中定义（已改为全局） */
extern wm_window_t *center_create(const char *title, int w, int h);

/* V2.38: 动态 Terminal —— 输出缓冲 + 输入行 + 命令历史 */
#define TERM_BUF_SIZE 4096
#define TERM_HIST_MAX 16
#define TERM_INPUT_MAX 64
static char term_buf[TERM_BUF_SIZE];
static int  term_buf_len = 0;
static char term_input[TERM_INPUT_MAX];
static int  term_input_len = 0;
static char term_history[TERM_HIST_MAX][TERM_INPUT_MAX];
static int  term_hist_count = 0;
static int  term_hist_idx = -1;
static int  term_scroll = 0;  /* 输出滚动偏移 (行) */

static void term_puts(const char *s)
{
    while (*s && term_buf_len < TERM_BUF_SIZE - 1) {
        term_buf[term_buf_len++] = *s++;
    }
    term_buf[term_buf_len] = 0;
}

static void term_puthex(uint32_t v)
{
    char b[11]; int i = 0;
    b[i++] = '0'; b[i++] = 'x';
    if (v == 0) { b[i++] = '0'; }
    else {
        char t[8]; int j = 0;
        while (v && j < 8) { t[j++] = "0123456789ABCDEF"[v & 0xF]; v >>= 4; }
        while (j > 0) b[i++] = t[--j];
    }
    b[i] = 0; term_puts(b);
}

/* 2026-11-21: 终端以前是演示壳 —— ls 直接写死 "dev initrd proc sys bin"，
 * date 写死 "2026-08-30"，跟盘上内容毫无关系。把 ls / cat 接到真 VFS 上。 */
static int term_cmd_ls(void)
{
    extern int vfs_readdir(const char *, vfs_dirent_t *, int);
    vfs_dirent_t ents[32];
    int n, shown = 0, i;

    n = vfs_readdir("/", ents, 32);
    if (n <= 0) { term_puts("(empty)\n"); return 0; }
    for (i = 0; i < n; i++) {
        const char *nm = ents[i].name;
        if (nm[0] == '.' &&
            (nm[1] == 0 || (nm[1] == '.' && nm[2] == 0))) continue;
        term_puts(nm);
        term_puts(ents[i].type == VFS_DIRECTORY ? "/  " : "  ");
        shown++;
    }
    term_puts("\n");
    return shown;
}

static void term_cmd_cat(const char *name)
{
    extern int vfs_open(const char *, int);
    extern int vfs_read(int, void *, uint32_t);
    extern int vfs_close(int);
    static char cbuf[1024];
    int fd, n;

    if (!name || !*name) { term_puts("cat: need a name\n"); return; }
    fd = vfs_open(name, VFS_O_RDONLY);
    if (fd < 0) {
        term_puts("cat: "); term_puts(name); term_puts(": not found\n");
        return;
    }
    n = vfs_read(fd, cbuf, sizeof(cbuf) - 1);
    vfs_close(fd);
    if (n <= 0) { term_puts("(empty)\n"); return; }
    cbuf[n] = 0;
    term_puts(cbuf);
    term_puts("\n");
}

/* === S1: 终端基础文件工具 ===
 * cp/mv/rm/mkdir/df/sync 都走 VFS，和 shell 的 fs 命令共用同一后端。
 * 这里不做 shell 的路径补全/相对路径，只接受绝对路径，保持最小可用。 */
static int term_cmd_cp(const char *src, const char *dst)
{
    extern int vfs_open(const char *, int);
    extern int vfs_read(int, void *, uint32_t);
    extern int vfs_write(int, const void *, uint32_t);
    extern int vfs_close(int);
    static char cbuf[2048];
    int in, out, n, total = 0;
    if (!src || !dst || !*src || !*dst) { term_puts("cp: need src dst\n"); return -1; }
    in = vfs_open(src, VFS_O_RDONLY);
    if (in < 0) { term_puts("cp: source not found\n"); return -1; }
    out = vfs_open(dst, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (out < 0) { vfs_close(in); term_puts("cp: create dst FAIL\n"); return -1; }
    while ((n = vfs_read(in, cbuf, sizeof(cbuf))) > 0) {
        int off = 0;
        while (off < n) {
            int w = vfs_write(out, cbuf + off, (uint32_t)(n - off));
            if (w <= 0) { vfs_close(in); vfs_close(out); term_puts("cp: write FAIL\n"); return -1; }
            off += w; total += w;
        }
    }
    vfs_close(in); vfs_close(out);
    { extern int vfs_sync(void); vfs_sync(); }
    term_puts("cp: "); term_puts(src); term_puts(" -> "); term_puts(dst);
    term_puts(" ("); term_puthex((uint32_t)total); term_puts(" bytes)\n");
    return 0;
}

static int term_cmd_mv(const char *src, const char *dst)
{
    extern int vfs_unlink(const char *);
    if (term_cmd_cp(src, dst) != 0) return -1;
    if (vfs_unlink(src) != 0) { term_puts("mv: unlink src FAIL\n"); return -1; }
    term_puts("mv: done\n");
    return 0;
}

static int term_cmd_rm(const char *path)
{
    extern int vfs_unlink(const char *);
    extern int vfs_rmdir(const char *);
    if (!path || !*path) { term_puts("rm: need path\n"); return -1; }
    if (vfs_unlink(path) == 0 || vfs_rmdir(path) == 0) { term_puts("rm: done\n"); return 0; }
    term_puts("rm: not found or dir not empty\n");
    return -1;
}

static int term_cmd_mkdir(const char *path)
{
    extern int vfs_mkdir(const char *);
    if (!path || !*path) { term_puts("mkdir: need path\n"); return -1; }
    if (vfs_mkdir(path) == 0) { term_puts("mkdir: done\n"); return 0; }
    term_puts("mkdir: FAIL\n");
    return -1;
}

static void term_cmd_df(void)
{
    extern ext2_superblock_t *ext2_sb;
    extern uint32_t ext2_block_size;
    if (!ext2_sb || !ext2_block_size) { term_puts("df: fs not mounted\n"); return; }
    term_puts("total "); term_puthex(ext2_sb->s_blocks_count);
    term_puts(" blocks, free "); term_puthex(ext2_sb->s_free_blocks_count);
    term_puts(" blocks, "); term_puthex((ext2_sb->s_free_blocks_count * ext2_block_size) / 1024);
    term_puts(" KiB free\n");
}

/* 测试钩子：让内核自测验证"终端这一层真的读到了盘"，而不是写死字符串 */
int term_selftest_ls(void) { return term_cmd_ls(); }
const char *term_selftest_buf(void) { return term_buf; }
/* 清空输出缓冲：终端 buffer 在启动过程中已积累 banner 等文本，
 * 不重置会写满, term_puts 静默丢弃, 自测就会误判成 "cat 没读到内容" */
void term_selftest_reset(void) { term_buf_len = 0; term_buf[0] = 0; }

/* 返回本次 cat 追加到输出缓冲的字节数, 便于自测断言"真有内容" */
int term_selftest_cat(const char *name)
{
    int before = term_buf_len;
    term_cmd_cat(name);
    return term_buf_len - before;
}

/* === S1: 终端内最小行编辑器 ===
 * 不是 vi：输入 `edit <file>` 后逐行追加，`.save` 保存、`.quit` 退出、
 * `.list` 回显。所有落盘走 VFS + vfs_sync，和 shell 的 write 同一条路径。 */
#define ED_BUF_MAX 2048
static int  ed_active = 0;
static int  ed_len = 0;
static int  ed_dirty = 0;
static char ed_path[128];
static char ed_buf[ED_BUF_MAX];

static int ed_load(const char *path)
{
    extern int vfs_open(const char *, int);
    extern int vfs_read(int, void *, uint32_t);
    extern int vfs_close(int);
    int fd, n;
    ed_len = 0; ed_dirty = 0;
    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) return 0;          /* 新文件 */
    while (ed_len < ED_BUF_MAX - 1) {
        n = vfs_read(fd, ed_buf + ed_len, ED_BUF_MAX - 1 - ed_len);
        if (n <= 0) break;
        ed_len += n;
    }
    vfs_close(fd);
    return ed_len;
}

static int ed_save(void)
{
    extern int vfs_open(const char *, int);
    extern int vfs_write(int, const void *, uint32_t);
    extern int vfs_close(int);
    extern int vfs_sync(void);
    int fd, off = 0;
    fd = vfs_open(ed_path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC);
    if (fd < 0) return -1;
    while (off < ed_len) {
        int w = vfs_write(fd, ed_buf + off, (uint32_t)(ed_len - off));
        if (w <= 0) { vfs_close(fd); return -1; }
        off += w;
    }
    vfs_close(fd);
    if (vfs_sync() != 0) return -1;
    ed_dirty = 0;
    return 0;
}

static void ed_begin(const char *path)
{
    if (!path || !*path) { term_puts("edit: need a file name\n"); return; }
    int i = 0;
    while (path[i] && i < (int)sizeof(ed_path) - 1) { ed_path[i] = path[i]; i++; }
    ed_path[i] = 0;
    ed_load(ed_path);
    ed_active = 1;
    term_puts("[edit] "); term_puts(ed_path);
    term_puts(ed_len ? " (loaded)\n" : " (new)\n");
    term_puts("lines append; .save | .quit | .list\n");
}

static void ed_line(const char *line)
{
    if (strcmp(line, ".save") == 0) {
        if (ed_save() == 0) term_puts("[edit] saved\n");
        else term_puts("[edit] save FAIL\n");
        return;
    }
    if (strcmp(line, ".quit") == 0) {
        ed_active = 0;
        term_puts("[edit] quit\n");
        return;
    }
    if (strcmp(line, ".list") == 0) {
        if (ed_len <= 0) term_puts("(empty)\n");
        else { term_puts(ed_buf); term_puts("\n"); }
        return;
    }
    if (!line[0]) return;
    int n = 0; while (line[n]) n++;
    if (ed_len + n + 1 >= ED_BUF_MAX) { term_puts("[edit] buffer full\n"); return; }
    for (int i = 0; i < n; i++) ed_buf[ed_len++] = line[i];
    ed_buf[ed_len++] = '\n';
    ed_dirty = 1;
    term_puts("[edit] +\n");
}

/* 自测入口：走和键盘完全相同的 edit 状态机，写完立即落盘。 */
int term_selftest_edit(const char *path)
{
    ed_begin(path);
    /* 自测每次都从空缓冲区开始：ed_begin 会加载磁盘旧内容，
     * 但自测要验的是“写入两行”，不能把上次启动留下的内容再追加一遍。 */
    ed_len = 0; ed_dirty = 0; ed_buf[0] = 0;
    ed_line("HEPU_S1_EDIT");
    ed_line("SECOND_LINE");
    int ok = ed_save();
    ed_active = 0;
    return ok == 0 ? 0 : -1;
}

static void term_exec(const char *cmd)
{
    if (ed_active) { ed_line(cmd); return; }
    /* 记录历史 */
    if (cmd[0] && term_hist_count < TERM_HIST_MAX) {
        int i = 0; while (cmd[i] && i < TERM_INPUT_MAX-1) {
            term_history[term_hist_count][i] = cmd[i]; i++;
        }
        term_history[term_hist_count][i] = 0;
        term_hist_count++;
    }
    term_hist_idx = -1;

    /* 解析命令 */
    if (cmd[0] == 0) return;
    if (strcmp(cmd, "help") == 0) {
        term_puts("Commands: help ls cat <file> edit <file> cp mv rm mkdir df sync fs echo clear version whoami date\n");
    } else if (strncmp(cmd, "ls", 2) == 0 && (cmd[2] == 0 || cmd[2] == ' ')) {
        term_cmd_ls();
    } else if (strncmp(cmd, "cat ", 4) == 0) {
        term_cmd_cat(cmd + 4);
    } else if (strncmp(cmd, "fs", 2) == 0 && (cmd[2] == 0 || cmd[2] == ' ')) {
        const char *sub = cmd + 2;
        while (*sub == ' ') sub++;
        if (strncmp(sub, "ls", 2) == 0 && (sub[2] == 0 || sub[2] == ' ')) {
            term_cmd_ls();
        } else if (strncmp(sub, "cat ", 4) == 0) {
            term_cmd_cat(sub + 4);
        } else {
            term_puts("fs: support ls | cat <name>\n");
        }
    } else if (strncmp(cmd, "edit ", 5) == 0) {
        ed_begin(cmd + 5);
    } else if (strncmp(cmd, "cp ", 3) == 0) {
        const char *p = cmd + 3; const char *q = p;
        while (*q && *q != ' ') q++;
        if (!*q) { term_puts("cp: need src dst\n"); return; }
        char src[96], dst[96]; int n = (int)(q - p);
        if (n > 95) n = 95; memcpy(src, p, n); src[n] = 0;
        q++; while (*q == ' ') q++;
        n = 0; while (q[n] && n < 95) { dst[n] = q[n]; n++; } dst[n] = 0;
        term_cmd_cp(src, dst);
    } else if (strncmp(cmd, "mv ", 3) == 0) {
        const char *p = cmd + 3; const char *q = p;
        while (*q && *q != ' ') q++;
        if (!*q) { term_puts("mv: need src dst\n"); return; }
        char src[96], dst[96]; int n = (int)(q - p);
        if (n > 95) n = 95; memcpy(src, p, n); src[n] = 0;
        q++; while (*q == ' ') q++;
        n = 0; while (q[n] && n < 95) { dst[n] = q[n]; n++; } dst[n] = 0;
        term_cmd_mv(src, dst);
    } else if (strncmp(cmd, "rm ", 3) == 0) {
        term_cmd_rm(cmd + 3);
    } else if (strncmp(cmd, "mkdir ", 6) == 0) {
        term_cmd_mkdir(cmd + 6);
    } else if (strcmp(cmd, "df") == 0) {
        term_cmd_df();
    } else if (strcmp(cmd, "sync") == 0) {
        extern int vfs_sync(void);
        term_puts(vfs_sync() == 0 ? "sync: done\n" : "sync: FAIL\n");
    } else if (strncmp(cmd, "echo ", 5) == 0) {
        term_puts(cmd + 5);
        term_puts("\n");
    } else if (strcmp(cmd, "clear") == 0) {
        term_buf_len = 0;
        term_buf[0] = 0;
        term_scroll = 0;
    } else if (strcmp(cmd, "version") == 0) {
        extern const char *hepu_kernel_version;
        term_puts("Hepu OS ");
        term_puts(hepu_kernel_version);
        term_puts("\n");
    } else if (strcmp(cmd, "whoami") == 0) {
        term_puts("root\n");
    } else if (strcmp(cmd, "date") == 0) {
        term_puts("2026-08-30 (UTC+8)\n");
    } else {
        term_puts("command not found: ");
        term_puts(cmd);
        term_puts("\n");
    }
}

/* Terminal 自定义绘制 */
static void terminal_draw(wm_window_t *win)
{
    int x = win->x + 6;
    int y = win->y + 16;
    int line_h = 8;
    int max_lines = (win->h - 20) / line_h;

    /* 计算可见行 (从后往前, 支持滚动) */
    /* 先把输出按行拆分到临时行指针 */
    static const char *lines[64];
    int line_count = 0;
    lines[0] = term_buf;
    for (int i = 0; i < term_buf_len && line_count < 63; i++) {
        if (term_buf[i] == '\n') {
            term_buf[i] = 0;
            lines[++line_count] = &term_buf[i + 1];
        }
    }
    if (term_buf_len > 0 && term_buf[term_buf_len-1] != '\n')
        line_count++;
    else if (term_buf_len == 0)
        line_count = 0;

    int start = (term_scroll > 0) ? (line_count - max_lines + 1 - term_scroll) : (line_count - max_lines + 1);
    if (start < 0) start = 0;

    for (int i = start; i < line_count && (i - start) < max_lines - 1; i++) {
        if (lines[i] && lines[i][0])
            gfx_puts(x, y + (i - start) * line_h, lines[i], 0x0F);
    }

    /* 输入行 */
    int input_y = y + (max_lines - 1) * line_h;
    char prompt[TERM_INPUT_MAX + 4];
    prompt[0] = '$'; prompt[1] = ' ';
    int j = 0;
    while (j < term_input_len && j < TERM_INPUT_MAX - 1) {
        prompt[j + 2] = term_input[j]; j++;
    }
    prompt[j + 2] = 0;
    gfx_puts(x, input_y, prompt, 0x0F);
    /* 光标 */
    gfx_fillrect(x + (j + 2) * 6, input_y, 4, 7, 0x0F);

    /* 恢复换行符 (临时拆分时改了) */
    for (int i = 0; i < term_buf_len; i++) {
        if (term_buf[i] == 0 && i < term_buf_len - 1) term_buf[i] = '\n';
    }
}

/* Terminal 键盘事件 */
static void terminal_event(wm_window_t *win, wm_event_t *evt)
{
    (void)win;
    if (evt->type != WM_EVENT_KEY_DOWN) return;
    char ch = evt->key;
    if (ch == '\n' || ch == '\r') {
        term_input[term_input_len] = 0;
        term_puts("$ ");
        term_puts(term_input);
        term_puts("\n");
        term_exec(term_input);
        term_input_len = 0;
        term_input[0] = 0;
        term_scroll = 0;
    } else if (ch == 0x08) {  /* Backspace */
        if (term_input_len > 0) term_input_len--;
    } else if (ch >= 0x20 && ch < 0x7F && term_input_len < TERM_INPUT_MAX - 1) {
        term_input[term_input_len++] = ch;
    }
}

void on_launch_terminal(wm_window_t *win, wm_widget_t *wgt)
{
    (void)win; (void)wgt;
    if (!terminal_win || !terminal_win->used) {
        terminal_win = center_create("Terminal", 230, 130);
        if (terminal_win) {
            terminal_win->draw_cb = terminal_draw;
            terminal_win->event_cb = terminal_event;
            term_buf_len = 0;
            term_buf[0] = 0;
            term_input_len = 0;
            term_puts("Hepu OS Terminal v1.0\n");
            term_puts("Type 'help' for commands\n\n");
        }
    }
    if (terminal_win) wm_bring_to_front(terminal_win);
}
