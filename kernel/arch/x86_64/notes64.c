/* ============================================================================
 * 赫普OS 64位 - 记事本应用
 * 支持文本编辑、文件打开/保存
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

extern void vga_puts(const char *s);
extern int ext2_64_read_file(const char *path, uint8_t *buffer, uint32_t max_size);
extern int ext2_64_write_file(const char *path, const uint8_t *data, uint32_t size);
extern int ext2_64_create_file(const char *path);

#define NOTES_MAX_TEXT 8192
#define NOTES_MAX_LINES 256
#define NOTES_MAX_LINE_LEN 128

typedef struct {
    char     text[NOTES_MAX_TEXT];
    int      text_len;
    char     filename[256];
    int      modified;
    int      initialized;
} notes_app_t;

static notes_app_t notes;

/* 字符串复制 */
static void notes_strcpy(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

/* 字符串长度 */
static int notes_strlen(const char *s)
{
    int len = 0;
    while (s[len]) len++;
    return len;
}

/* 初始化记事本 */
void notes64_init(void)
{
    notes.text[0] = 0;
    notes.text_len = 0;
    notes.filename[0] = 0;
    notes.modified = 0;
    notes.initialized = 1;

    /* 默认内容 */
    notes_strcpy(notes.text,
        "Welcome to HepuOS Notepad!\n"
        "\n"
        "This is a simple text editor.\n"
        "Use 'notes open <file>' to open a file.\n"
        "Use 'notes save <file>' to save.\n"
        "Use 'notes edit' to enter edit mode.\n"
        "\n"
        "HepuOS 64-bit v0.54.12\n",
        NOTES_MAX_TEXT);
    notes.text_len = notes_strlen(notes.text);

    vga_puts("[NOTES] Notepad initialized\n");
}

/* 打开文件 */
int notes64_open(const char *filename)
{
    if (!filename || !filename[0]) return -1;

    int size = ext2_64_read_file(filename, (uint8_t *)notes.text,
                                 NOTES_MAX_TEXT - 1);
    if (size <= 0) {
        vga_puts("[NOTES] Cannot open file: ");
        vga_puts(filename);
        vga_puts("\n");
        return -1;
    }

    notes.text[size] = 0;
    notes.text_len = size;
    notes_strcpy(notes.filename, filename, sizeof(notes.filename));
    notes.modified = 0;

    vga_puts("[NOTES] Opened: ");
    vga_puts(filename);
    vga_puts(" (");
    char buf[16];
    int n = size, i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
    vga_puts(buf);
    vga_puts(" bytes)\n");
    return 0;
}

/* 保存文件 */
int notes64_save(const char *filename)
{
    const char *save_name = filename ? filename : notes.filename;
    if (!save_name || !save_name[0]) {
        vga_puts("[NOTES] No filename specified\n");
        return -1;
    }

    /* 确保文件存在 */
    ext2_64_create_file(save_name);

    int result = ext2_64_write_file(save_name,
                                    (const uint8_t *)notes.text,
                                    notes.text_len);
    if (result != 0) {
        vga_puts("[NOTES] Failed to save\n");
        return -1;
    }

    notes_strcpy(notes.filename, save_name, sizeof(notes.filename));
    notes.modified = 0;

    vga_puts("[NOTES] Saved: ");
    vga_puts(save_name);
    vga_puts("\n");
    return 0;
}

/* 显示文本内容 */
void notes64_show(void)
{
    vga_puts("\n=== Notepad: ");
    if (notes.filename[0]) {
        vga_puts(notes.filename);
    } else {
        vga_puts("Untitled");
    }
    if (notes.modified) vga_puts(" [modified]");
    vga_puts(" ===\n");

    vga_puts(notes.text);

    vga_puts("\n=== End (");
    char buf[16];
    int n = notes.text_len, i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
    vga_puts(buf);
    vga_puts(" bytes) ===\n");
}

/* 追加文本 */
int notes64_append(const char *text)
{
    if (!text) return -1;
    int len = notes_strlen(text);
    if (notes.text_len + len >= NOTES_MAX_TEXT - 1) {
        vga_puts("[NOTES] Buffer full\n");
        return -1;
    }
    for (int i = 0; i < len; i++) {
        notes.text[notes.text_len++] = text[i];
    }
    notes.text[notes.text_len] = 0;
    notes.modified = 1;
    return 0;
}

/* 清空内容 */
void notes64_clear(void)
{
    notes.text[0] = 0;
    notes.text_len = 0;
    notes.modified = 1;
    vga_puts("[NOTES] Cleared\n");
}

/* 图形模式绘制记事本 */
void notes64_draw(void *fb, int fb_w, int fb_h,
                  int x, int y, int w, int h)
{
    if (!fb) return;

    extern void font64_draw_string(void *fb, int fw, int fh,
                                   int x, int y, const char *str,
                                   uint32_t color, uint16_t size);

    /* 标题 */
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 10,
                       "Notepad", 0x333333, 16);

    /* 文件名 */
    if (notes.filename[0]) {
        font64_draw_string(fb, fb_w, fb_h, x + 10, y + 32,
                           notes.filename, 0x666666, 16);
    }

    /* 分隔线 */
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 52,
                       "------------------------", 0x999999, 16);

    /* 显示前几行内容 */
    int line_y = y + 65;
    int line_start = 0;
    int lines_shown = 0;
    for (int i = 0; i < notes.text_len && line_y < y + h - 20; i++) {
        if (notes.text[i] == '\n' || i == notes.text_len - 1) {
            char line[128];
            int len = i - line_start;
            if (len > 127) len = 127;
            for (int j = 0; j < len; j++) {
                line[j] = notes.text[line_start + j];
            }
            line[len] = 0;
            font64_draw_string(fb, fb_w, fb_h, x + 10, line_y,
                               line, 0x333333, 16);
            line_y += 18;
            line_start = i + 1;
            lines_shown++;
            if (lines_shown >= 10) break;
        }
    }

    /* 底部状态栏 */
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + h - 25,
                       "Use 'notes' commands in shell", 0x999999, 16);
}
