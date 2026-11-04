/* ============================================================================
 * 赫普OS - 增强版文本编辑器（V8.76新增）
 * 语法高亮、搜索替换、行号、多文件
 * ========================================================================== */

#ifndef _TEXT_EDITOR_H
#define _TEXT_EDITOR_H

#include <stdint.h>

/* 编辑器模式 */
typedef enum {
    EDITOR_MODE_NORMAL = 0,
    EDITOR_MODE_INSERT,
    EDITOR_MODE_COMMAND,
    EDITOR_MODE_SEARCH,
    EDITOR_MODE_REPLACE
} editor_mode_t;

/* 语法类型 */
typedef enum {
    SYNTAX_PLAIN = 0,
    SYNTAX_C,
    SYNTAX_HEADER,
    SYNTAX_MAKEFILE,
    SYNTAX_PYTHON,
    SYNTAX_SHELL,
    SYNTAX_MARKDOWN,
    SYNTAX_JSON,
    SYNTAX_XML
} syntax_type_t;

/* 文本行 */
typedef struct {
    char    *text;
    int      length;
    int      capacity;
} editor_line_t;

/* 搜索匹配 */
typedef struct {
    int line;
    int col;
    int length;
} search_match_t;

/* 编辑器状态 */
typedef struct {
    char          filename[256];
    editor_line_t *lines;
    int            line_count;
    int            capacity;
    int            cursor_line;
    int            cursor_col;
    int            scroll_line;
    int            scroll_col;
    editor_mode_t  mode;
    syntax_type_t  syntax;
    int            modified;
    int            initialized;
    char           command_buffer[128];
    char           search_buffer[128];
    char           replace_buffer[128];
    search_match_t matches[100];
    int            match_count;
    int            current_match;
    int            tab_width;
    int            show_line_numbers;
    int            show_status;
    int            auto_indent;
} text_editor_t;

/* 初始化编辑器 */
int text_editor_init(void);

/* 关闭编辑器 */
void text_editor_shutdown(void);

/* 打开文件 */
int text_editor_open(const char *filename);

/* 保存文件 */
int text_editor_save(void);

/* 另存为 */
int text_editor_save_as(const char *filename);

/* 新建文件 */
int text_editor_new(void);

/* 关闭文件 */
void text_editor_close(void);

/* 输入字符 */
int text_editor_input_char(char c);

/* 处理特殊键 */
int text_editor_handle_special(int key);

/* 移动光标 */
void text_editor_move_cursor(int dx, int dy);

/* 插入字符 */
void text_editor_insert_char(char c);

/* 删除字符 */
void text_editor_delete_char(void);

/* 退格 */
void text_editor_backspace(void);

/* 换行 */
void text_editor_newline(void);

/* 搜索 */
int text_editor_search(const char *pattern);

/* 替换 */
int text_editor_replace(const char *old, const char *new_str, int all);

/* 下一个匹配 */
void text_editor_next_match(void);

/* 上一个匹配 */
void text_editor_prev_match(void);

/* 检测语法类型 */
syntax_type_t text_editor_detect_syntax(const char *filename);

/* 获取行文本 */
const char *text_editor_get_line(int line);

/* 获取行数 */
int text_editor_get_line_count(void);

/* 绘制UI */
void text_editor_draw_ui(int x, int y, int width, int height);

/* 处理键盘 */
void text_editor_handle_key(char key);

/* 获取状态 */
int text_editor_get_state(text_editor_t *state);

#endif /* _TEXT_EDITOR_H */
