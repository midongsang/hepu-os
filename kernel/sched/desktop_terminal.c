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

static void term_exec(const char *cmd)
{
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
        term_puts("Commands: help ls echo clear version whoami date\n");
    } else if (strncmp(cmd, "ls", 2) == 0) {
        term_puts("dev  initrd  proc  sys  bin\n");
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
