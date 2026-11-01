/* ============================================================================
 * 赫普OS - 增强版终端模拟器实现（V8.77新增）
 * 命令历史、ANSI颜色、滚动缓冲、标签页、自动补全
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include "../include/terminal_enhanced.h"
#include "../include/vga.h"
#include "../include/kheap.h"
#include "../include/graphics.h"

/* 全局终端状态 */
static terminal_t term;

/* ANSI颜色映射到RGB */
static const uint32_t ansi_colors[16] = {
    0x000000, 0xAA0000, 0x00AA00, 0xAA5500,
    0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA,
    0x555555, 0xFF5555, 0x55FF55, 0xFFFF55,
    0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF
};

/* 内置命令列表（用于自动补全） */
static const char *builtin_commands[] = {
    "help", "clear", "ls", "cd", "pwd", "cat", "echo",
    "mkdir", "rm", "cp", "mv", "touch", "ps", "kill",
    "top", "free", "df", "uname", "date", "whoami",
    "history", "exit", "reboot", "shutdown", "ping",
    "ifconfig", "netstat", "vim", "nano", "gcc", "make",
    NULL
};

/* 初始化终端 */
int terminal_init(int width, int height)
{
    if (term.initialized) return 0;
    memset(&term, 0, sizeof(terminal_t));
    term.initialized = 1;
    term.width = width;
    term.height = height;
    term.font_width = 8;
    term.font_height = 16;
    term.current_fg = TERM_COLOR_WHITE;
    term.current_bg = TERM_COLOR_BLACK;
    term.tab_count = 0;
    term.current_tab = 0;
    terminal_new_tab("Shell");
    vga_printf("[TERM] Enhanced terminal initialized (%dx%d)\n", width, height);
    return 0;
}

void terminal_shutdown(void)
{
    if (!term.initialized) return;
    for (int i = 0; i < term.tab_count; i++) {
        if (term.tabs[i].buffer) kfree(term.tabs[i].buffer);
        for (int j = 0; j < term.tabs[i].history_count; j++)
            if (term.tabs[i].history[j]) kfree(term.tabs[i].history[j]);
        if (term.tabs[i].history) kfree(term.tabs[i].history);
    }
    term.initialized = 0;
}

/* 创建新标签页 */
int terminal_new_tab(const char *title)
{
    if (term.tab_count >= 8) return -1;
    term_tab_t *tab = &term.tabs[term.tab_count];
    memset(tab, 0, sizeof(term_tab_t));
    strncpy(tab->title, title ? title : "Shell", 31);
    tab->width = term.width;
    tab->height = term.height;
    tab->scrollback_lines = 100;
    tab->buffer = (term_char_t *)kmalloc(
        (tab->height + tab->scrollback_lines) * tab->width * sizeof(term_char_t));
    if (!tab->buffer) return -2;
    memset(tab->buffer, 0,
           (tab->height + tab->scrollback_lines) * tab->width * sizeof(term_char_t));
    tab->history = (char **)kmalloc(50 * sizeof(char *));
    tab->history_count = 0;
    tab->history_index = -1;
    tab->cursor_x = 0;
    tab->cursor_y = 0;
    tab->scroll_offset = 0;
    tab->active = 1;
    term.current_tab = term.tab_count;
    term.tab_count++;

    /* 输出欢迎信息 */
    terminal_puts("\033[1;32mHepu OS Enhanced Terminal\033[0m\n");
    terminal_puts("输入 'help' 查看可用命令\n\n");
    terminal_puts("\033[1;34muser@hepu\033[0m:\033[1;36m~\033[0m$ ");
    return term.tab_count - 1;
}

int terminal_close_tab(int index)
{
    if (index < 0 || index >= term.tab_count) return -1;
    if (term.tab_count <= 1) return -2;
    term_tab_t *tab = &term.tabs[index];
    if (tab->buffer) kfree(tab->buffer);
    for (int i = 0; i < tab->history_count; i++)
        if (tab->history[i]) kfree(tab->history[i]);
    if (tab->history) kfree(tab->history);
    for (int i = index; i < term.tab_count - 1; i++)
        term.tabs[i] = term.tabs[i + 1];
    term.tab_count--;
    if (term.current_tab >= term.tab_count)
        term.current_tab = term.tab_count - 1;
    return 0;
}

void terminal_switch_tab(int index)
{
    if (index >= 0 && index < term.tab_count)
        term.current_tab = index;
}

term_tab_t *terminal_get_current_tab(void)
{
    return &term.tabs[term.current_tab];
}

/* 滚动一行 */
static void scroll_up(term_tab_t *tab)
{
    /* 上移所有行 */
    int total = tab->height + tab->scrollback_lines;
    memmove(tab->buffer, tab->buffer + tab->width,
            (total - 1) * tab->width * sizeof(term_char_t));
    /* 清空最后一行 */
    memset(tab->buffer + (total - 1) * tab->width, 0,
           tab->width * sizeof(term_char_t));
    if (tab->scroll_offset > 0)
        tab->scroll_offset--;
}

/* 输出字符 */
void terminal_putchar(char c)
{
    term_tab_t *tab = terminal_get_current_tab();
    if (!tab || !tab->buffer) return;

    /* ANSI转义序列处理 */
    if (term.ansi_state) {
        terminal_parse_ansi(c);
        return;
    }

    if (c == '\033') {
        term.ansi_state = 1;
        term.ansi_pos = 0;
        return;
    }

    if (c == '\n') {
        tab->cursor_x = 0;
        tab->cursor_y++;
        if (tab->cursor_y >= tab->height) {
            scroll_up(tab);
            tab->cursor_y = tab->height - 1;
        }
        return;
    }

    if (c == '\r') {
        tab->cursor_x = 0;
        return;
    }

    if (c == '\t') {
        tab->cursor_x = (tab->cursor_x + 8) & ~7;
        if (tab->cursor_x >= tab->width) {
            tab->cursor_x = 0;
            tab->cursor_y++;
            if (tab->cursor_y >= tab->height) {
                scroll_up(tab);
                tab->cursor_y = tab->height - 1;
            }
        }
        return;
    }

    if (c == '\b') {
        if (tab->cursor_x > 0) tab->cursor_x--;
        return;
    }

    /* 普通字符 */
    if (tab->cursor_x >= tab->width) {
        tab->cursor_x = 0;
        tab->cursor_y++;
        if (tab->cursor_y >= tab->height) {
            scroll_up(tab);
            tab->cursor_y = tab->height - 1;
        }
    }

    int idx = (tab->scroll_offset + tab->cursor_y) * tab->width + tab->cursor_x;
    tab->buffer[idx].ch = c;
    tab->buffer[idx].fg = term.current_fg;
    tab->buffer[idx].bg = term.current_bg;
    tab->buffer[idx].bold = term.current_bold;
    tab->buffer[idx].underline = term.current_underline;
    tab->buffer[idx].reverse = term.current_reverse;
    tab->cursor_x++;
}

void terminal_puts(const char *str)
{
    if (!str) return;
    while (*str) terminal_putchar(*str++);
}

void terminal_printf(const char *fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    terminal_puts(buf);
}

/* 解析ANSI转义序列 */
void terminal_parse_ansi(char c)
{
    if (term.ansi_state == 1) {
        if (c == '[') {
            term.ansi_state = 2;
            term.ansi_pos = 0;
        } else {
            term.ansi_state = 0;
        }
        return;
    }

    if (term.ansi_state == 2) {
        if (c >= '0' && c <= '9' || c == ';') {
            if (term.ansi_pos < 31)
                term.ansi_buffer[term.ansi_pos++] = c;
        } else {
            term.ansi_buffer[term.ansi_pos] = 0;
            /* 处理命令 */
            if (c == 'm') {
                /* SGR - 设置图形属性 */
                char *token = term.ansi_buffer;
                while (*token) {
                    int code = 0;
                    while (*token >= '0' && *token <= '9') {
                        code = code * 10 + (*token - '0');
                        token++;
                    }
                    if (*token == ';') token++;
                    /* 应用代码 */
                    if (code == 0) terminal_reset_attr();
                    else if (code == 1) term.current_bold = 1;
                    else if (code == 4) term.current_underline = 1;
                    else if (code == 7) term.current_reverse = 1;
                    else if (code >= 30 && code <= 37)
                        term.current_fg = code - 30;
                    else if (code >= 40 && code <= 47)
                        term.current_bg = code - 40;
                    else if (code >= 90 && code <= 97)
                        term.current_fg = code - 90 + 8;
                    else if (code >= 100 && code <= 107)
                        term.current_bg = code - 100 + 8;
                }
            } else if (c == 'J') {
                /* 清除屏幕 */
                terminal_clear();
            } else if (c == 'K') {
                /* 清除行 */
                terminal_clear_line();
            }
            term.ansi_state = 0;
        }
    }
}

void terminal_clear(void)
{
    term_tab_t *tab = terminal_get_current_tab();
    if (!tab || !tab->buffer) return;
    memset(tab->buffer, 0,
           (tab->height + tab->scrollback_lines) * tab->width * sizeof(term_char_t));
    tab->cursor_x = 0;
    tab->cursor_y = 0;
    tab->scroll_offset = 0;
}

void terminal_clear_line(void)
{
    term_tab_t *tab = terminal_get_current_tab();
    if (!tab || !tab->buffer) return;
    int idx = (tab->scroll_offset + tab->cursor_y) * tab->width;
    memset(tab->buffer + idx, 0, tab->width * sizeof(term_char_t));
}

void terminal_move_cursor(int x, int y)
{
    term_tab_t *tab = terminal_get_current_tab();
    if (!tab) return;
    if (x >= 0 && x < tab->width) tab->cursor_x = x;
    if (y >= 0 && y < tab->height) tab->cursor_y = y;
}

void terminal_scroll(int lines)
{
    term_tab_t *tab = terminal_get_current_tab();
    if (!tab) return;
    tab->scroll_offset += lines;
    if (tab->scroll_offset < 0) tab->scroll_offset = 0;
    if (tab->scroll_offset > tab->scrollback_lines)
        tab->scroll_offset = tab->scrollback_lines;
}

void terminal_set_color(term_color_t fg, term_color_t bg)
{
    term.current_fg = fg;
    term.current_bg = bg;
}

void terminal_reset_attr(void)
{
    term.current_fg = TERM_COLOR_WHITE;
    term.current_bg = TERM_COLOR_BLACK;
    term.current_bold = 0;
    term.current_underline = 0;
    term.current_reverse = 0;
}

/* 命令历史 */
void terminal_add_history(const char *cmd)
{
    term_tab_t *tab = terminal_get_current_tab();
    if (!tab || !cmd || !*cmd) return;
    /* 避免重复 */
    if (tab->history_count > 0 &&
        strcmp(tab->history[tab->history_count - 1], cmd) == 0)
        return;
    if (tab->history_count >= 50) {
        if (tab->history[0]) kfree(tab->history[0]);
        for (int i = 0; i < 49; i++)
            tab->history[i] = tab->history[i + 1];
        tab->history_count--;
    }
    tab->history[tab->history_count] = (char *)kmalloc(strlen(cmd) + 1);
    strcpy(tab->history[tab->history_count], cmd);
    tab->history_count++;
    tab->history_index = tab->history_count;
}

const char *terminal_history_prev(void)
{
    term_tab_t *tab = terminal_get_current_tab();
    if (!tab || tab->history_count == 0) return NULL;
    if (tab->history_index > 0)
        tab->history_index--;
    return tab->history[tab->history_index];
}

const char *terminal_history_next(void)
{
    term_tab_t *tab = terminal_get_current_tab();
    if (!tab) return NULL;
    if (tab->history_index < tab->history_count - 1) {
        tab->history_index++;
        return tab->history[tab->history_index];
    }
    tab->history_index = tab->history_count;
    return "";
}

/* 自动补全 */
int terminal_autocomplete(char *input, int pos)
{
    if (!input || pos == 0) return 0;
    /* 提取当前单词 */
    int start = pos - 1;
    while (start > 0 && input[start - 1] != ' ') start--;
    char word[64];
    int wlen = pos - start;
    if (wlen >= 64) wlen = 63;
    memcpy(word, input + start, wlen);
    word[wlen] = 0;

    /* 查找匹配 */
    int matches = 0;
    const char *match = NULL;
    for (int i = 0; builtin_commands[i]; i++) {
        if (strncmp(builtin_commands[i], word, wlen) == 0) {
            matches++;
            match = builtin_commands[i];
        }
    }

    if (matches == 1 && match) {
        /* 唯一匹配，补全 */
        int remaining = strlen(match) - wlen;
        if (remaining > 0 && pos + remaining < 255) {
            memmove(input + pos + remaining, input + pos,
                    strlen(input + pos) + 1);
            memcpy(input + pos, match + wlen, remaining);
            return remaining;
        }
    }
    return 0;
}

/* 执行命令 */
int terminal_execute(const char *command)
{
    if (!command || !*command) return 0;
    terminal_add_history(command);

    /* 解析命令 */
    char cmd[256];
    strncpy(cmd, command, 255);
    cmd[255] = 0;

    /* 去除首尾空格 */
    char *start = cmd;
    while (*start == ' ') start++;
    char *end = start + strlen(start) - 1;
    while (end > start && *end == ' ') *end-- = 0;

    if (strcmp(start, "help") == 0) {
        terminal_puts("\033[1;33m可用命令:\033[0m\n");
        terminal_puts("  help       - 显示帮助信息\n");
        terminal_puts("  clear      - 清除屏幕\n");
        terminal_puts("  ls         - 列出文件\n");
        terminal_puts("  cd <dir>   - 切换目录\n");
        terminal_puts("  pwd        - 显示当前目录\n");
        terminal_puts("  cat <file> - 显示文件内容\n");
        terminal_puts("  echo <msg> - 输出消息\n");
        terminal_puts("  ps         - 显示进程\n");
        terminal_puts("  top        - 系统监控\n");
        terminal_puts("  free       - 内存信息\n");
        terminal_puts("  df         - 磁盘信息\n");
        terminal_puts("  uname      - 系统信息\n");
        terminal_puts("  date       - 显示日期\n");
        terminal_puts("  history    - 命令历史\n");
        terminal_puts("  exit       - 退出终端\n");
    } else if (strcmp(start, "clear") == 0) {
        terminal_clear();
        return 0;
    } else if (strcmp(start, "pwd") == 0) {
        terminal_puts("/home/user\n");
    } else if (strcmp(start, "uname") == 0) {
        terminal_puts("Hepu OS 8.77 x86\n");
    } else if (strcmp(start, "date") == 0) {
        terminal_puts("2026-10-06 15:00:00\n");
    } else if (strcmp(start, "whoami") == 0) {
        terminal_puts("user\n");
    } else if (strcmp(start, "history") == 0) {
        term_tab_t *tab = terminal_get_current_tab();
        for (int i = 0; i < tab->history_count; i++) {
            terminal_printf("  %d  %s\n", i + 1, tab->history[i]);
        }
    } else if (strncmp(start, "echo ", 5) == 0) {
        terminal_puts(start + 5);
        terminal_putchar('\n');
    } else if (strcmp(start, "ls") == 0) {
        terminal_puts("\033[1;34mDesktop\033[0m  ");
        terminal_puts("\033[1;34mDocuments\033[0m  ");
        terminal_puts("\033[1;34mDownloads\033[0m  ");
        terminal_puts("\033[1;32mREADME.txt\033[0m  ");
        terminal_puts("\033[1;33mhepu-os.zip\033[0m\n");
    } else {
        terminal_printf("\033[1;31m命令未找到: %s\033[0m\n", start);
        terminal_puts("输入 'help' 查看可用命令\n");
    }
    return 0;
}

/* 处理输入字符 */
void terminal_input_char(char c)
{
    term_tab_t *tab = terminal_get_current_tab();
    if (!tab) return;

    if (c == '\n' || c == '\r') {
        terminal_putchar('\n');
        tab->input_buffer[tab->input_pos] = 0;
        terminal_execute(tab->input_buffer);
        tab->input_pos = 0;
        tab->input_buffer[0] = 0;
        terminal_puts("\033[1;34muser@hepu\033[0m:\033[1;36m~\033[0m$ ");
        return;
    }

    if (c == '\b' || c == 0x7F) {
        if (tab->input_pos > 0) {
            tab->input_pos--;
            tab->input_buffer[tab->input_pos] = 0;
            terminal_putchar('\b');
            terminal_putchar(' ');
            terminal_putchar('\b');
        }
        return;
    }

    if (c == '\t') {
        int added = terminal_autocomplete(tab->input_buffer, tab->input_pos);
        if (added > 0) {
            for (int i = 0; i < added; i++)
                terminal_putchar(tab->input_buffer[tab->input_pos + i]);
            tab->input_pos += added;
        }
        return;
    }

    if (c >= 0x20 && c < 0x7F && tab->input_pos < 255) {
        tab->input_buffer[tab->input_pos++] = c;
        terminal_putchar(c);
    }
}

/* 处理特殊键 */
void terminal_handle_key(int key)
{
    term_tab_t *tab = terminal_get_current_tab();
    if (!tab) return;

    /* 上箭头 - 历史上一条 */
    if (key == 0x101) {
        const char *prev = terminal_history_prev();
        if (prev) {
            /* 清除当前输入 */
            while (tab->input_pos > 0) {
                terminal_putchar('\b');
                terminal_putchar(' ');
                terminal_putchar('\b');
                tab->input_pos--;
            }
            strncpy(tab->input_buffer, prev, 255);
            tab->input_pos = strlen(prev);
            terminal_puts(prev);
        }
    }
    /* 下箭头 - 历史下一条 */
    else if (key == 0x102) {
        const char *next = terminal_history_next();
        while (tab->input_pos > 0) {
            terminal_putchar('\b');
            terminal_putchar(' ');
            terminal_putchar('\b');
            tab->input_pos--;
        }
        if (next && *next) {
            strncpy(tab->input_buffer, next, 255);
            tab->input_pos = strlen(next);
            terminal_puts(next);
        } else {
            tab->input_buffer[0] = 0;
            tab->input_pos = 0;
        }
    }
    /* PageUp - 向上滚动 */
    else if (key == 0x103) {
        terminal_scroll(3);
    }
    /* PageDown - 向下滚动 */
    else if (key == 0x104) {
        terminal_scroll(-3);
    }
    /* Ctrl+L - 清除屏幕 */
    else if (key == 0x0C) {
        terminal_clear();
        terminal_puts("\033[1;34muser@hepu\033[0m:\033[1;36m~\033[0m$ ");
    }
    /* Ctrl+U - 清除当前行 */
    else if (key == 0x15) {
        while (tab->input_pos > 0) {
            terminal_putchar('\b');
            terminal_putchar(' ');
            terminal_putchar('\b');
            tab->input_pos--;
        }
        tab->input_buffer[0] = 0;
    }
}

/* 绘制标签栏 */
void terminal_draw_tab_bar(int x, int y, int width)
{
    gfx_color_t bg = {40, 40, 50, 255};
    gfx_color_t active_bg = {60, 60, 80, 255};
    gfx_color_t fg = {200, 200, 200, 255};
    gfx_color_t active_fg = {255, 255, 255, 255};

    gfx_fill_rect(x, y, width, 24, bg);
    for (int i = 0; i < term.tab_count; i++) {
        int tx = x + i * 100;
        if (i == term.current_tab) {
            gfx_fill_rect(tx + 2, y + 2, 96, 20, active_bg);
            gfx_draw_string_font(tx + 8, y + 6, term.tabs[i].title,
                                 active_fg, 11);
        } else {
            gfx_draw_string_font(tx + 8, y + 6, term.tabs[i].title,
                                 fg, 11);
        }
    }
    /* 新建标签按钮 */
    gfx_draw_string_font(x + term.tab_count * 100 + 10, y + 6,
                         "+ 新建", (gfx_color_t){150,150,150,255}, 11);
}

/* 绘制终端 */
void terminal_draw(int x, int y, int pixel_width, int pixel_height)
{
    term_tab_t *tab = terminal_get_current_tab();
    if (!tab || !tab->buffer) return;

    /* 背景 */
    gfx_fill_rect(x, y, pixel_width, pixel_height,
                  (gfx_color_t){0, 0, 0, 255});

    /* 绘制字符 */
    for (int row = 0; row < tab->height; row++) {
        for (int col = 0; col < tab->width; col++) {
            int idx = (tab->scroll_offset + row) * tab->width + col;
            term_char_t *tc = &tab->buffer[idx];
            if (!tc->ch) continue;

            uint32_t fg_color = ansi_colors[tc->fg % 16];
            uint32_t bg_color = ansi_colors[tc->bg % 16];

            /* 反色 */
            if (tc->reverse) {
                uint32_t tmp = fg_color;
                fg_color = bg_color;
                bg_color = tmp;
            }

            int px = x + col * term.font_width;
            int py = y + row * term.font_height;

            /* 背景 */
            if (tc->bg != TERM_COLOR_BLACK || tc->reverse) {
                gfx_fill_rect(px, py, term.font_width, term.font_height,
                              (gfx_color_t){(bg_color>>16)&0xFF,
                                            (bg_color>>8)&0xFF,
                                            bg_color&0xFF, 255});
            }

            /* 字符 */
            gfx_color_t fc = {(fg_color>>16)&0xFF,
                              (fg_color>>8)&0xFF,
                              fg_color&0xFF, 255};
            char buf[2] = {tc->ch, 0};
            gfx_draw_string_font(px, py, buf, fc, 12);

            /* 下划线 */
            if (tc->underline) {
                gfx_draw_line(px, py + term.font_height - 1,
                              px + term.font_width, py + term.font_height - 1,
                              fc);
            }
        }
    }

    /* 光标 */
    int cx = x + tab->cursor_x * term.font_width;
    int cy = y + tab->cursor_y * term.font_height;
    gfx_fill_rect(cx, cy, 2, term.font_height,
                  (gfx_color_t){200, 200, 200, 255});
}

int terminal_get_state(terminal_t *state)
{
    if (!state) return -1;
    memcpy(state, &term, sizeof(terminal_t));
    return 0;
}
