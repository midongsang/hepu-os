/* ============================================================================
 * 赫普OS - 增强版文本编辑器实现（V8.76新增）
 * 语法高亮、搜索替换、行号、多文件
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../include/text_editor.h"
#include "../include/vga.h"
#include "../include/kheap.h"
#include "../include/graphics.h"

/* 全局状态 */
static text_editor_t ed;

/* C语言关键字 */
static const char *c_keywords[] = {
    "int", "char", "float", "double", "void", "short", "long",
    "unsigned", "signed", "const", "static", "extern", "volatile",
    "struct", "union", "enum", "typedef", "return", "if", "else",
    "while", "for", "do", "switch", "case", "default", "break",
    "continue", "goto", "sizeof", "NULL", "true", "false",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "int8_t", "int16_t", "int32_t", "int64_t", "size_t", NULL
};

/* 初始化 */
int text_editor_init(void)
{
    if (ed.initialized) return 0;
    memset(&ed, 0, sizeof(text_editor_t));
    ed.initialized = 1;
    ed.mode = EDITOR_MODE_NORMAL;
    ed.syntax = SYNTAX_PLAIN;
    ed.tab_width = 4;
    ed.show_line_numbers = 1;
    ed.show_status = 1;
    ed.auto_indent = 1;
    ed.capacity = 1000;
    ed.lines = (editor_line_t *)kmalloc(ed.capacity * sizeof(editor_line_t));
    if (!ed.lines) return -1;
    memset(ed.lines, 0, ed.capacity * sizeof(editor_line_t));
    /* 创建空行 */
    ed.lines[0].text = (char *)kmalloc(256);
    ed.lines[0].text[0] = 0;
    ed.lines[0].length = 0;
    ed.lines[0].capacity = 256;
    ed.line_count = 1;
    vga_puts("[TEXT-EDITOR] Enhanced text editor initialized\n");
    return 0;
}

void text_editor_shutdown(void)
{
    if (!ed.initialized) return;
    text_editor_close();
    if (ed.lines) kfree(ed.lines);
    ed.initialized = 0;
}

/* 打开文件 */
int text_editor_open(const char *filename)
{
    if (!ed.initialized) text_editor_init();
    if (!filename) return -1;
    text_editor_close();
    strncpy(ed.filename, filename, 255);
    ed.syntax = text_editor_detect_syntax(filename);
    /* 简化：创建示例内容 */
    const char *sample[] = {
        "/* Hepu OS - 示例代码 */",
        "#include <stdint.h>",
        "",
        "int main(void) {",
        "    // 初始化系统",
        "    system_init();",
        "    ",
        "    while (1) {",
        "        process_events();",
        "    }",
        "    return 0;",
        "}",
        NULL
    };
    for (int i = 0; sample[i]; i++) {
        if (i >= ed.capacity) break;
        ed.lines[i].text = (char *)kmalloc(256);
        strncpy(ed.lines[i].text, sample[i], 255);
        ed.lines[i].length = strlen(sample[i]);
        ed.lines[i].capacity = 256;
        ed.line_count++;
    }
    ed.modified = 0;
    ed.cursor_line = 0;
    ed.cursor_col = 0;
    return 0;
}

/* 保存文件 */
int text_editor_save(void)
{
    if (!ed.filename[0]) return -1;
    /* 简化：模拟保存 */
    ed.modified = 0;
    vga_printf("[TEXT-EDITOR] Saved: %s (%d lines)\n",
               ed.filename, ed.line_count);
    return 0;
}

int text_editor_save_as(const char *filename)
{
    if (!filename) return -1;
    strncpy(ed.filename, filename, 255);
    return text_editor_save();
}

int text_editor_new(void)
{
    text_editor_close();
    strcpy(ed.filename, "未命名.txt");
    ed.lines[0].text = (char *)kmalloc(256);
    ed.lines[0].text[0] = 0;
    ed.lines[0].length = 0;
    ed.lines[0].capacity = 256;
    ed.line_count = 1;
    ed.syntax = SYNTAX_PLAIN;
    return 0;
}

void text_editor_close(void)
{
    for (int i = 0; i < ed.line_count; i++) {
        if (ed.lines[i].text) {
            kfree(ed.lines[i].text);
            ed.lines[i].text = NULL;
        }
    }
    ed.line_count = 0;
    ed.cursor_line = 0;
    ed.cursor_col = 0;
    ed.modified = 0;
    ed.filename[0] = 0;
}

/* 移动光标 */
void text_editor_move_cursor(int dx, int dy)
{
    ed.cursor_line += dy;
    if (ed.cursor_line < 0) ed.cursor_line = 0;
    if (ed.cursor_line >= ed.line_count)
        ed.cursor_line = ed.line_count - 1;

    ed.cursor_col += dx;
    if (ed.cursor_col < 0) ed.cursor_col = 0;
    int max_col = ed.lines[ed.cursor_line].length;
    if (ed.cursor_col > max_col) ed.cursor_col = max_col;

    /* 自动滚动 */
    if (ed.cursor_line < ed.scroll_line)
        ed.scroll_line = ed.cursor_line;
    else if (ed.cursor_line >= ed.scroll_line + 25)
        ed.scroll_line = ed.cursor_line - 24;
}

/* 插入字符 */
void text_editor_insert_char(char c)
{
    editor_line_t *line = &ed.lines[ed.cursor_line];
    if (ed.cursor_col >= line->capacity - 1) {
        /* 扩展行容量 */
        int new_cap = line->capacity * 2;
        char *new_text = (char *)kmalloc(new_cap);
        memcpy(new_text, line->text, line->length + 1);
        kfree(line->text);
        line->text = new_text;
        line->capacity = new_cap;
    }
    /* 后移字符 */
    for (int i = line->length; i >= ed.cursor_col; i--)
        line->text[i + 1] = line->text[i];
    line->text[ed.cursor_col] = c;
    line->length++;
    ed.cursor_col++;
    ed.modified = 1;
}

/* 删除字符 */
void text_editor_delete_char(void)
{
    editor_line_t *line = &ed.lines[ed.cursor_line];
    if (ed.cursor_col >= line->length) return;
    for (int i = ed.cursor_col; i < line->length; i++)
        line->text[i] = line->text[i + 1];
    line->length--;
    ed.modified = 1;
}

/* 退格 */
void text_editor_backspace(void)
{
    if (ed.cursor_col > 0) {
        ed.cursor_col--;
        text_editor_delete_char();
    } else if (ed.cursor_line > 0) {
        /* 合并到上一行 */
        editor_line_t *prev = &ed.lines[ed.cursor_line - 1];
        editor_line_t *cur = &ed.lines[ed.cursor_line];
        int prev_len = prev->length;
        if (prev_len + cur->length + 1 > prev->capacity) {
            int new_cap = prev_len + cur->length + 16;
            char *new_text = (char *)kmalloc(new_cap);
            memcpy(new_text, prev->text, prev_len + 1);
            kfree(prev->text);
            prev->text = new_text;
            prev->capacity = new_cap;
        }
        memcpy(prev->text + prev_len, cur->text, cur->length + 1);
        prev->length += cur->length;
        /* 删除当前行 */
        kfree(cur->text);
        for (int i = ed.cursor_line; i < ed.line_count - 1; i++)
            ed.lines[i] = ed.lines[i + 1];
        ed.line_count--;
        ed.cursor_line--;
        ed.cursor_col = prev_len;
        ed.modified = 1;
    }
}

/* 换行 */
void text_editor_newline(void)
{
    if (ed.line_count >= ed.capacity) return;
    editor_line_t *cur = &ed.lines[ed.cursor_line];
    /* 创建新行 */
    for (int i = ed.line_count; i > ed.cursor_line; i--)
        ed.lines[i] = ed.lines[i - 1];
    ed.line_count++;

    editor_line_t *new_line = &ed.lines[ed.cursor_line + 1];
    new_line->text = (char *)kmalloc(256);
    new_line->capacity = 256;
    new_line->length = cur->length - ed.cursor_col;
    memcpy(new_line->text, cur->text + ed.cursor_col,
           new_line->length + 1);
    cur->length = ed.cursor_col;
    cur->text[ed.cursor_col] = 0;

    /* 自动缩进 */
    if (ed.auto_indent && ed.cursor_col > 0) {
        int indent = 0;
        while (indent < ed.cursor_col && cur->text[indent] == ' ')
            indent++;
        if (indent > 0) {
            char *indented = (char *)kmalloc(256 + indent);
            for (int i = 0; i < indent; i++) indented[i] = ' ';
            memcpy(indented + indent, new_line->text,
                   new_line->length + 1);
            kfree(new_line->text);
            new_line->text = indented;
            new_line->length += indent;
            new_line->capacity = 256 + indent;
        }
    }

    ed.cursor_line++;
    ed.cursor_col = new_line->length;
    ed.modified = 1;
}

/* 搜索 */
int text_editor_search(const char *pattern)
{
    if (!pattern || !*pattern) return 0;
    ed.match_count = 0;
    ed.current_match = 0;
    for (int i = 0; i < ed.line_count && ed.match_count < 100; i++) {
        char *pos = ed.lines[i].text;
        while ((pos = strstr(pos, pattern)) != NULL) {
            ed.matches[ed.match_count].line = i;
            ed.matches[ed.match_count].col = pos - ed.lines[i].text;
            ed.matches[ed.match_count].length = strlen(pattern);
            ed.match_count++;
            pos++;
        }
    }
    if (ed.match_count > 0) {
        ed.cursor_line = ed.matches[0].line;
        ed.cursor_col = ed.matches[0].col;
    }
    return ed.match_count;
}

/* 替换 */
int text_editor_replace(const char *old, const char *new_str, int all)
{
    if (!old || !new_str) return 0;
    int count = 0;
    for (int i = 0; i < ed.line_count; i++) {
        char *pos = ed.lines[i].text;
        while ((pos = strstr(pos, old)) != NULL) {
            int old_len = strlen(old);
            int new_len = strlen(new_str);
            int offset = pos - ed.lines[i].text;
            /* 简单替换 */
            if (new_len == old_len) {
                memcpy(pos, new_str, new_len);
            } else {
                /* 需要移动 */
                char *new_text = (char *)kmalloc(
                    ed.lines[i].capacity + new_len);
                memcpy(new_text, ed.lines[i].text, offset);
                memcpy(new_text + offset, new_str, new_len);
                strcpy(new_text + offset + new_len,
                       pos + old_len);
                kfree(ed.lines[i].text);
                ed.lines[i].text = new_text;
                ed.lines[i].length += new_len - old_len;
                ed.lines[i].capacity += new_len;
                pos = new_text + offset + new_len;
            }
            count++;
            if (!all) break;
            pos += new_len;
        }
        if (!all && count > 0) break;
    }
    if (count > 0) ed.modified = 1;
    return count;
}

void text_editor_next_match(void)
{
    if (ed.match_count == 0) return;
    ed.current_match = (ed.current_match + 1) % ed.match_count;
    ed.cursor_line = ed.matches[ed.current_match].line;
    ed.cursor_col = ed.matches[ed.current_match].col;
}

void text_editor_prev_match(void)
{
    if (ed.match_count == 0) return;
    ed.current_match = (ed.current_match - 1 + ed.match_count) %
                       ed.match_count;
    ed.cursor_line = ed.matches[ed.current_match].line;
    ed.cursor_col = ed.matches[ed.current_match].col;
}

/* 检测语法类型 */
syntax_type_t text_editor_detect_syntax(const char *filename)
{
    if (!filename) return SYNTAX_PLAIN;
    const char *ext = strrchr(filename, '.');
    if (!ext) return SYNTAX_PLAIN;
    if (strcmp(ext, ".c") == 0) return SYNTAX_C;
    if (strcmp(ext, ".h") == 0) return SYNTAX_HEADER;
    if (strcmp(ext, ".py") == 0) return SYNTAX_PYTHON;
    if (strcmp(ext, ".sh") == 0) return SYNTAX_SHELL;
    if (strcmp(ext, ".md") == 0) return SYNTAX_MARKDOWN;
    if (strcmp(ext, ".json") == 0) return SYNTAX_JSON;
    if (strcmp(ext, ".xml") == 0) return SYNTAX_XML;
    if (strcmp(filename, "Makefile") == 0) return SYNTAX_MAKEFILE;
    return SYNTAX_PLAIN;
}

const char *text_editor_get_line(int line)
{
    if (line < 0 || line >= ed.line_count) return NULL;
    return ed.lines[line].text;
}

int text_editor_get_line_count(void) { return ed.line_count; }

/* 检查是否是关键字 */
static int is_keyword(const char *word)
{
    for (int i = 0; c_keywords[i]; i++)
        if (strcmp(word, c_keywords[i]) == 0) return 1;
    return 0;
}

/* 绘制带语法高亮的行 */
static void draw_syntax_line(int x, int y, const char *text,
                             int cursor_col, int is_current)
{
    gfx_color_t normal = {220, 220, 220, 255};
    gfx_color_t keyword = {100, 150, 255, 255};
    gfx_color_t comment = {100, 200, 100, 255};
    gfx_color_t string = {200, 150, 100, 255};
    gfx_color_t number = {200, 100, 200, 255};
    gfx_color_t cursor_bg = {60, 80, 120, 255};

    int len = strlen(text);
    int col = 0;
    char word[64];
    int word_len = 0;

    while (col < len) {
        char c = text[col];

        /* 注释 */
        if (c == '/' && col + 1 < len && text[col + 1] == '/') {
            gfx_draw_string_font(x + col * 8, y, text + col, comment, 12);
            break;
        }
        /* 字符串 */
        if (c == '"' || c == '\'') {
            char quote = c;
            int start = col;
            col++;
            while (col < len && text[col] != quote) col++;
            if (col < len) col++;
            char buf[256];
            int slen = col - start;
            if (slen > 255) slen = 255;
            memcpy(buf, text + start, slen);
            buf[slen] = 0;
            gfx_draw_string_font(x + start * 8, y, buf, string, 12);
            continue;
        }
        /* 数字 */
        if (c >= '0' && c <= '9') {
            int start = col;
            while (col < len &&
                   ((text[col] >= '0' && text[col] <= '9') ||
                    text[col] == 'x' || text[col] == 'X' ||
                    (text[col] >= 'a' && text[col] <= 'f') ||
                    (text[col] >= 'A' && text[col] <= 'F')))
                col++;
            char buf[64];
            int nlen = col - start;
            if (nlen > 63) nlen = 63;
            memcpy(buf, text + start, nlen);
            buf[nlen] = 0;
            gfx_draw_string_font(x + start * 8, y, buf, number, 12);
            continue;
        }
        /* 标识符/关键字 */
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            c == '_') {
            int start = col;
            word_len = 0;
            while (col < len &&
                   ((text[col] >= 'a' && text[col] <= 'z') ||
                    (text[col] >= 'A' && text[col] <= 'Z') ||
                    (text[col] >= '0' && text[col] <= '9') ||
                    text[col] == '_') && word_len < 63) {
                word[word_len++] = text[col++];
            }
            word[word_len] = 0;
            gfx_color_t color = is_keyword(word) ? keyword : normal;
            gfx_draw_string_font(x + start * 8, y, word, color, 12);
            continue;
        }
        /* 普通字符 */
        char buf[2] = {c, 0};
        gfx_draw_string_font(x + col * 8, y, buf, normal, 12);
        col++;
    }

    /* 光标 */
    if (is_current) {
        int cx = x + cursor_col * 8;
        gfx_fill_rect(cx, y, 2, 14, cursor_bg);
    }
}

/* 绘制UI */
void text_editor_draw_ui(int x, int y, int width, int height)
{
    gfx_color_t bg={25,25,30,255}, fg={220,220,220,255}, accent={100,180,255,255};
    gfx_color_t line_num_bg={35,35,40,255}, line_num_fg={100,100,120,255};
    gfx_color_t status_bg={40,40,50,255}, gray={130,130,130,255};
    gfx_color_t modified_color={220,180,80,255};

    gfx_fill_rect(x, y, width, height, bg);

    /* 标题栏 */
    char title[300];
    snprintf(title, sizeof(title), "文本编辑器 - %s%s",
             ed.filename[0] ? ed.filename : "未命名",
             ed.modified ? " *" : "");
    gfx_draw_string_font(x+10, y+6, title, accent, 14);

    /* 语法类型 */
    const char *syn_names[] = {"纯文本","C","头文件","Makefile",
                               "Python","Shell","Markdown","JSON","XML"};
    gfx_draw_string_font(x+width-80, y+8, syn_names[ed.syntax], gray, 11);

    /* 行号区域 */
    int ln_width = ed.show_line_numbers ? 40 : 0;
    int text_x = x + ln_width + 5;
    int text_y = y + 30;
    int line_height = 16;
    int visible_lines = (height - text_y - 25) / line_height;

    if (ed.show_line_numbers) {
        gfx_fill_rect(x, text_y, ln_width, height - text_y - 25,
                      line_num_bg);
        for (int i = 0; i < visible_lines; i++) {
            int line = ed.scroll_line + i;
            if (line >= ed.line_count) break;
            char ln[8];
            snprintf(ln, sizeof(ln), "%4d", line + 1);
            gfx_draw_string_font(x+2, text_y + i*line_height, ln,
                                 line == ed.cursor_line ? accent :
                                 line_num_fg, 11);
        }
    }

    /* 文本区域 */
    for (int i = 0; i < visible_lines; i++) {
        int line = ed.scroll_line + i;
        if (line >= ed.line_count) break;
        int ly = text_y + i * line_height;
        /* 当前行高亮 */
        if (line == ed.cursor_line) {
            gfx_fill_rect(text_x - 2, ly - 1, width - text_x - 5,
                          line_height - 1,
                          (gfx_color_t){35,40,55,255});
        }
        /* 搜索匹配高亮 */
        for (int m = 0; m < ed.match_count; m++) {
            if (ed.matches[m].line == line) {
                gfx_fill_rect(text_x + ed.matches[m].col * 8, ly,
                              ed.matches[m].length * 8, 14,
                              (gfx_color_t){80,60,20,255});
            }
        }
        draw_syntax_line(text_x, ly, ed.lines[line].text,
                         line == ed.cursor_line ? ed.cursor_col : -1,
                         line == ed.cursor_line);
    }

    /* 状态栏 */
    int status_y = y + height - 22;
    gfx_fill_rect(x, status_y, width, 22, status_bg);
    char status[200];
    const char *mode_names[] = {"普通","插入","命令","搜索","替换"};
    snprintf(status, sizeof(status),
             " %s | 行:%d/%d 列:%d | %s | %s",
             mode_names[ed.mode],
             ed.cursor_line + 1, ed.line_count,
             ed.cursor_col + 1,
             ed.modified ? "已修改" : "已保存",
             ed.match_count > 0 ? "找到匹配" : "");
    gfx_draw_string_font(x+10, status_y+5, status, fg, 11);

    /* 快捷键提示 */
    gfx_draw_string_font(x+width-200, status_y+5,
                         "Ctrl+S保存 Ctrl+F搜索", gray, 10);
}

/* 处理键盘 */
void text_editor_handle_key(char key)
{
    if (!ed.initialized) return;
    if (ed.mode == EDITOR_MODE_INSERT || ed.mode == EDITOR_MODE_NORMAL) {
        if (key >= 0x20 && key < 0x7F) {
            text_editor_insert_char(key);
        } else if (key == 0x08) {
            text_editor_backspace();
        } else if (key == 0x7F) {
            text_editor_delete_char();
        } else if (key == '\n' || key == '\r') {
            text_editor_newline();
        } else if (key == '\t') {
            for (int i = 0; i < ed.tab_width; i++)
                text_editor_insert_char(' ');
        } else if (key == 0x13) {  /* Ctrl+S */
            text_editor_save();
        }
    }
}

int text_editor_get_state(text_editor_t *state)
{
    if (!state) return -1;
    memcpy(state, &ed, sizeof(text_editor_t));
    return 0;
}
