/* ============================================================================
 * 赫普OS - 增强版文本编辑器/代码查看器
 * V8.54: 添加C语言语法高亮、文件统计、搜索功能
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 系统调用 */
extern int hepu_open(const char *path, int flags);
extern int hepu_read(int fd, void *buf, int count);
extern int hepu_write(int fd, const void *buf, int count);
extern int hepu_close(int fd);
extern void hepu_exit(int status);
extern int hepu_sleep(uint32_t ms);

/* 编辑器状态 */
#define MAX_LINES 2000
#define MAX_LINE_LEN 512
#define MAX_FILE_SIZE (MAX_LINES * MAX_LINE_LEN)

typedef struct {
    char lines[MAX_LINES][MAX_LINE_LEN];
    int  line_count;
    int  current_line;
    int  current_col;
    int  modified;
    char filename[256];
    int  file_loaded;
    int  total_chars;
    int  total_words;
    int  code_lines;
    int  comment_lines;
    int  blank_lines;
} editor_t;

static editor_t editor;

/* ANSI颜色码 */
#define COLOR_RESET   "\033[0m"
#define COLOR_KEYWORD "\033[1;34m"  /* 蓝色粗体 - 关键字 */
#define COLOR_TYPE    "\033[1;36m"  /* 青色粗体 - 类型 */
#define COLOR_STRING  "\033[0;32m"  /* 绿色 - 字符串 */
#define COLOR_COMMENT "\033[0;33m"  /* 黄色 - 注释 */
#define COLOR_NUMBER  "\033[0;31m"  /* 红色 - 数字 */
#define COLOR_PREPROC "\033[1;35m"  /* 紫色粗体 - 预处理 */
#define COLOR_LINENO  "\033[0;90m"  /* 灰色 - 行号 */
#define COLOR_HEADER  "\033[1;37m"  /* 白色粗体 - 标题 */
#define COLOR_STATUS  "\033[0;36m"  /* 青色 - 状态栏 */

/* C语言关键字 */
static const char *c_keywords[] = {
    "if", "else", "for", "while", "do", "switch", "case", "default",
    "break", "continue", "return", "goto", "sizeof", "typedef",
    "struct", "union", "enum", "static", "extern", "volatile",
    "const", "register", "auto", "inline", "restrict", "_Bool",
    "_Complex", "_Imaginary", "NULL", NULL
};

/* C语言类型 */
static const char *c_types[] = {
    "int", "char", "float", "double", "void", "short", "long",
    "unsigned", "signed", "int8_t", "int16_t", "int32_t", "int64_t",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t", "size_t",
    "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t", "bool",
    "true", "false", NULL
};

/* 简单字符串函数 */
static int strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void strcpy(char *dst, const char *src) {
    int i = 0;
    while (src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_alnum(char c) {
    return is_alpha(c) || is_digit(c);
}

static void print_str(const char *s) {
    hepu_write(1, s, strlen(s));
}

static void print_char(char c) {
    hepu_write(1, &c, 1);
}

static void print_int(int n) {
    char buf[16];
    int i = 0, neg = 0;
    char temp[16];
    if (n < 0) { neg = 1; n = -n; }
    if (n == 0) buf[i++] = '0';
    else { while (n > 0) { temp[i++] = '0' + (n % 10); n /= 10; } }
    int j = 0;
    if (neg) buf[j++] = '-';
    while (i > 0) buf[j++] = temp[--i];
    buf[j] = 0;
    print_str(buf);
}

/* 检查是否是关键字 */
static int is_keyword(const char *word) {
    for (int i = 0; c_keywords[i]; i++) {
        if (strcmp(word, c_keywords[i]) == 0) return 1;
    }
    return 0;
}

/* 检查是否是类型 */
static int is_type(const char *word) {
    for (int i = 0; c_types[i]; i++) {
        if (strcmp(word, c_types[i]) == 0) return 1;
    }
    return 0;
}

/* 带语法高亮渲染一行 */
static void render_line_highlighted(const char *line) {
    int i = 0;
    int in_string = 0;
    int in_char = 0;
    int in_comment = 0;
    int len = strlen(line);

    /* 检查整行注释 */
    int j = 0;
    while (j < len && is_space(line[j])) j++;
    if (j < len - 1 && line[j] == '/' && line[j+1] == '/') {
        print_str(COLOR_COMMENT);
        print_str(line);
        print_str(COLOR_RESET);
        return;
    }

    while (i < len) {
        char c = line[i];

        /* 字符串 */
        if (c == '"' && !in_char) {
            in_string = !in_string;
            print_str(COLOR_STRING);
            print_char(c);
            if (!in_string) print_str(COLOR_RESET);
            i++;
            continue;
        }
        if (in_string) {
            print_char(c);
            i++;
            continue;
        }

        /* 字符 */
        if (c == '\'' && !in_string) {
            in_char = !in_char;
            print_str(COLOR_STRING);
            print_char(c);
            if (!in_char) print_str(COLOR_RESET);
            i++;
            continue;
        }
        if (in_char) {
            print_char(c);
            i++;
            continue;
        }

        /* 行注释 */
        if (c == '/' && i + 1 < len && line[i+1] == '/') {
            print_str(COLOR_COMMENT);
            while (i < len) { print_char(line[i]); i++; }
            print_str(COLOR_RESET);
            return;
        }

        /* 预处理指令 */
        if (c == '#' && i == j) {
            print_str(COLOR_PREPROC);
            while (i < len) { print_char(line[i]); i++; }
            print_str(COLOR_RESET);
            return;
        }

        /* 数字 */
        if (is_digit(c)) {
            print_str(COLOR_NUMBER);
            while (i < len && (is_digit(line[i]) || line[i] == 'x' ||
                   line[i] == 'X' || line[i] == 'a' || line[i] == 'b' ||
                   line[i] == 'c' || line[i] == 'd' || line[i] == 'e' ||
                   line[i] == 'f' || line[i] == 'A' || line[i] == 'B' ||
                   line[i] == 'C' || line[i] == 'D' || line[i] == 'E' ||
                   line[i] == 'F' || line[i] == 'u' || line[i] == 'l' ||
                   line[i] == 'U' || line[i] == 'L' || line[i] == '.')) {
                print_char(line[i]);
                i++;
            }
            print_str(COLOR_RESET);
            continue;
        }

        /* 标识符/关键字 */
        if (is_alpha(c)) {
            char word[64];
            int wlen = 0;
            while (i < len && is_alnum(line[i]) && wlen < 63) {
                word[wlen++] = line[i++];
            }
            word[wlen] = 0;

            if (is_keyword(word)) {
                print_str(COLOR_KEYWORD);
                print_str(word);
                print_str(COLOR_RESET);
            } else if (is_type(word)) {
                print_str(COLOR_TYPE);
                print_str(word);
                print_str(COLOR_RESET);
            } else {
                print_str(word);
            }
            continue;
        }

        print_char(c);
        i++;
    }
}

/* 初始化编辑器 */
static void editor_init(void) {
    editor.line_count = 0;
    editor.current_line = 0;
    editor.current_col = 0;
    editor.modified = 0;
    editor.file_loaded = 0;
    editor.filename[0] = 0;
    editor.total_chars = 0;
    editor.total_words = 0;
    editor.code_lines = 0;
    editor.comment_lines = 0;
    editor.blank_lines = 0;
    editor.lines[0][0] = 0;
    editor.line_count = 1;
}

/* 统计文件信息 */
static void editor_stats(void) {
    editor.total_chars = 0;
    editor.total_words = 0;
    editor.code_lines = 0;
    editor.comment_lines = 0;
    editor.blank_lines = 0;

    for (int i = 0; i < editor.line_count; i++) {
        const char *line = editor.lines[i];
        int len = strlen(line);
        editor.total_chars += len + 1;

        /* 统计单词 */
        int in_word = 0;
        for (int j = 0; j < len; j++) {
            if (is_alnum(line[j]) && !in_word) {
                editor.total_words++;
                in_word = 1;
            } else if (!is_alnum(line[j])) {
                in_word = 0;
            }
        }

        /* 统计行类型 */
        int j = 0;
        while (j < len && is_space(line[j])) j++;
        if (j == len) {
            editor.blank_lines++;
        } else if (j < len - 1 && line[j] == '/' && line[j+1] == '/') {
            editor.comment_lines++;
        } else if (line[j] == '*' || (j < len - 1 && line[j] == '/' && line[j+1] == '*')) {
            editor.comment_lines++;
        } else {
            editor.code_lines++;
        }
    }
}

/* 加载文件 */
static int editor_load_file(const char *filename) {
    int fd = hepu_open(filename, 0);
    if (fd < 0) return -1;

    static char buffer[MAX_FILE_SIZE];
    int bytes_read = hepu_read(fd, buffer, MAX_FILE_SIZE - 1);
    hepu_close(fd);

    if (bytes_read <= 0) return -1;
    buffer[bytes_read] = 0;

    int line = 0, col = 0;
    for (int i = 0; i < bytes_read && line < MAX_LINES; i++) {
        if (buffer[i] == '\n') {
            editor.lines[line][col] = 0;
            line++;
            col = 0;
            if (line < MAX_LINES) editor.lines[line][0] = 0;
        } else if (buffer[i] != '\r' && col < MAX_LINE_LEN - 1) {
            editor.lines[line][col++] = buffer[i];
            editor.lines[line][col] = 0;
        }
    }

    editor.line_count = line + 1;
    strcpy(editor.filename, filename);
    editor.file_loaded = 1;
    editor.modified = 0;
    editor_stats();
    return 0;
}

/* 搜索功能 */
static int editor_search(const char *keyword) {
    if (!keyword || !keyword[0]) return 0;
    int count = 0;
    int klen = strlen(keyword);

    print_str("\n");
    print_str(COLOR_HEADER);
    print_str("=== Search Results ===\n");
    print_str(COLOR_RESET);
    print_str("Keyword: ");
    print_str(keyword);
    print_str("\n\n");

    for (int i = 0; i < editor.line_count; i++) {
        const char *line = editor.lines[i];
        int len = strlen(line);
        for (int j = 0; j <= len - klen; j++) {
            int match = 1;
            for (int k = 0; k < klen; k++) {
                if (line[j+k] != keyword[k]) { match = 0; break; }
            }
            if (match) {
                count++;
                print_str(COLOR_LINENO);
                print_str("  L");
                print_int(i + 1);
                print_str(": ");
                print_str(COLOR_RESET);
                /* 显示匹配行（截断过长） */
                int display_len = len;
                if (display_len > 70) display_len = 70;
                for (int d = 0; d < display_len; d++) print_char(line[d]);
                if (len > 70) print_str("...");
                print_str("\n");
                break;
            }
        }
    }

    print_str("\n");
    print_str(COLOR_STATUS);
    print_str("Found ");
    print_int(count);
    print_str(" match(es)\n");
    print_str(COLOR_RESET);
    return count;
}

/* 渲染编辑器 */
static void editor_render(void) {
    print_str(COLOR_HEADER);
    print_str("╔══════════════════════════════════════════════════════════════╗\n");
    print_str("║  HepuOS Code Editor v2.0                                     ║\n");
    print_str("╚══════════════════════════════════════════════════════════════╝\n");
    print_str(COLOR_RESET);

    /* 文件信息 */
    print_str(COLOR_STATUS);
    print_str("┌─ File Info ──────────────────────────────────────────────────┐\n");
    print_str("│ Name: ");
    print_str(COLOR_RESET);
    if (editor.filename[0]) print_str(editor.filename);
    else print_str("[New File]");
    print_str(COLOR_STATUS);
    print_str("\n│ Lines: ");
    print_str(COLOR_RESET);
    print_int(editor.line_count);
    print_str(COLOR_STATUS);
    print_str("  Chars: ");
    print_str(COLOR_RESET);
    print_int(editor.total_chars);
    print_str(COLOR_STATUS);
    print_str("  Words: ");
    print_str(COLOR_RESET);
    print_int(editor.total_words);
    print_str(COLOR_STATUS);
    print_str("\n│ Code: ");
    print_str(COLOR_RESET);
    print_int(editor.code_lines);
    print_str(COLOR_STATUS);
    print_str("  Comments: ");
    print_str(COLOR_RESET);
    print_int(editor.comment_lines);
    print_str(COLOR_STATUS);
    print_str("  Blank: ");
    print_str(COLOR_RESET);
    print_int(editor.blank_lines);
    print_str(COLOR_STATUS);
    print_str("\n└───────────────────────────────────────────────────────────────┘\n");
    print_str(COLOR_RESET);

    /* 显示内容（前40行） */
    print_str("\n");
    int end_line = editor.line_count;
    if (end_line > 40) end_line = 40;

    for (int i = 0; i < end_line; i++) {
        /* 行号 */
        print_str(COLOR_LINENO);
        if (i < 9) print_str("   ");
        else if (i < 99) print_str("  ");
        else if (i < 999) print_str(" ");
        print_int(i + 1);
        print_str(" │ ");
        print_str(COLOR_RESET);

        /* 行内容（带语法高亮） */
        render_line_highlighted(editor.lines[i]);
        print_str("\n");
    }

    if (editor.line_count > 40) {
        print_str(COLOR_STATUS);
        print_str("\n... (");
        print_int(editor.line_count - 40);
        print_str(" more lines)\n");
        print_str(COLOR_RESET);
    }

    /* 底部状态栏 */
    print_str(COLOR_STATUS);
    print_str("\n┌───────────────────────────────────────────────────────────────┐\n");
    print_str("│ Ctrl+S=Save  Ctrl+Q=Quit  Ctrl+F=Find  Syntax: C/C++ Highlight │\n");
    print_str("└───────────────────────────────────────────────────────────────┘\n");
    print_str(COLOR_RESET);
}

/* 主函数 */
int main(int argc, char **argv) {
    editor_init();

    const char *search_kw = NULL;
    const char *filename = NULL;

    /* 解析参数 */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 's' && i + 1 < argc) {
            search_kw = argv[++i];
        } else if (!filename) {
            filename = argv[i];
        }
    }

    /* 加载文件 */
    if (filename) {
        if (editor_load_file(filename) != 0) {
            print_str("Could not open: ");
            print_str(filename);
            print_str("\nCreating new file.\n");
            hepu_sleep(1500);
        }
    }

    /* 搜索模式 */
    if (search_kw) {
        editor_search(search_kw);
        hepu_exit(0);
        return 0;
    }

    /* 正常渲染 */
    editor_render();

    print_str("\n(Interactive editing requires keyboard input system)\n");
    hepu_exit(0);
    return 0;
}
