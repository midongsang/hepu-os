/* ============================================================================
 * 赫普OS - 增强版终端模拟器（V8.77新增）
 * 命令历史、ANSI颜色、滚动缓冲、标签页、自动补全
 * ========================================================================== */

#ifndef _TERMINAL_ENHANCED_H
#define _TERMINAL_ENHANCED_H

#include <stdint.h>

/* 终端颜色 */
typedef enum {
    TERM_COLOR_BLACK = 0,
    TERM_COLOR_RED,
    TERM_COLOR_GREEN,
    TERM_COLOR_YELLOW,
    TERM_COLOR_BLUE,
    TERM_COLOR_MAGENTA,
    TERM_COLOR_CYAN,
    TERM_COLOR_WHITE,
    TERM_COLOR_BRIGHT_BLACK,
    TERM_COLOR_BRIGHT_RED,
    TERM_COLOR_BRIGHT_GREEN,
    TERM_COLOR_BRIGHT_YELLOW,
    TERM_COLOR_BRIGHT_BLUE,
    TERM_COLOR_BRIGHT_MAGENTA,
    TERM_COLOR_BRIGHT_CYAN,
    TERM_COLOR_BRIGHT_WHITE
} term_color_t;

/* 终端字符（带属性） */
typedef struct {
    char        ch;
    term_color_t fg;
    term_color_t bg;
    uint8_t     bold;
    uint8_t     underline;
    uint8_t     reverse;
} term_char_t;

/* 终端标签页 */
typedef struct {
    char        title[32];
    term_char_t *buffer;      /* 字符缓冲 */
    int         width;
    int         height;
    int         cursor_x;
    int         cursor_y;
    int         scroll_offset;
    int         scrollback_lines;  /* 回滚行数 */
    char        **history;         /* 命令历史 */
    int         history_count;
    int         history_index;
    char        input_buffer[256];
    int         input_pos;
    int         active;
} term_tab_t;

/* 终端状态 */
typedef struct {
    term_tab_t  tabs[8];
    int         tab_count;
    int         current_tab;
    int         initialized;
    int         width;
    int         height;
    int         font_width;
    int         font_height;
    /* ANSI解析状态 */
    int         ansi_state;
    char        ansi_buffer[32];
    int         ansi_pos;
    term_color_t current_fg;
    term_color_t current_bg;
    uint8_t     current_bold;
    uint8_t     current_underline;
    uint8_t     current_reverse;
} terminal_t;

/* 初始化终端 */
int terminal_init(int width, int height);

/* 关闭终端 */
void terminal_shutdown(void);

/* 创建新标签页 */
int terminal_new_tab(const char *title);

/* 关闭标签页 */
int terminal_close_tab(int index);

/* 切换标签页 */
void terminal_switch_tab(int index);

/* 输出字符 */
void terminal_putchar(char c);

/* 输出字符串 */
void terminal_puts(const char *str);

/* 输出格式化字符串 */
void terminal_printf(const char *fmt, ...);

/* 处理输入字符 */
void terminal_input_char(char c);

/* 处理特殊键 */
void terminal_handle_key(int key);

/* 执行命令 */
int terminal_execute(const char *command);

/* 清除屏幕 */
void terminal_clear(void);

/* 清除当前行 */
void terminal_clear_line(void);

/* 移动光标 */
void terminal_move_cursor(int x, int y);

/* 滚动 */
void terminal_scroll(int lines);

/* 设置颜色 */
void terminal_set_color(term_color_t fg, term_color_t bg);

/* 重置属性 */
void terminal_reset_attr(void);

/* 解析ANSI转义序列 */
void terminal_parse_ansi(char c);

/* 自动补全 */
int terminal_autocomplete(char *input, int pos);

/* 命令历史 */
void terminal_add_history(const char *cmd);
const char *terminal_history_prev(void);
const char *terminal_history_next(void);

/* 绘制终端 */
void terminal_draw(int x, int y, int pixel_width, int pixel_height);

/* 绘制标签栏 */
void terminal_draw_tab_bar(int x, int y, int width);

/* 获取当前标签页 */
term_tab_t *terminal_get_current_tab(void);

/* 获取终端状态 */
int terminal_get_state(terminal_t *state);

#endif /* _TERMINAL_ENHANCED_H */
