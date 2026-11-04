/* ============================================================================
 * 赫普OS - 十六进制编辑器实现（V8.72新增）
 * 二进制文件查看/编辑、搜索替换、系统调试
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../include/hex_editor.h"
#include "../include/vga.h"
#include "../include/kheap.h"
#include "../include/graphics.h"

/* 全局编辑器状态 */
static hex_editor_t editor;

/* 初始化十六进制编辑器 */
int hex_editor_init(void)
{
    if (editor.initialized) return 0;

    memset(&editor, 0, sizeof(hex_editor_t));
    editor.mode = HEX_MODE_VIEW;
    editor.bytes_per_line = 16;
    editor.initialized = 1;

    vga_puts("[HEX-EDITOR] Initialized\n");
    return 0;
}

/* 关闭编辑器 */
void hex_editor_shutdown(void)
{
    if (!editor.initialized) return;
    hex_editor_close();
    editor.initialized = 0;
}

/* 打开文件 */
int hex_editor_open(const char *filename)
{
    if (!editor.initialized) hex_editor_init();
    if (!filename) return -1;

    hex_editor_close();

    /* 简化：模拟打开文件
     * 实际应通过VFS读取文件内容 */
    strncpy(editor.filename, filename, 255);
    editor.filename[255] = 0;

    /* 分配缓冲区（模拟4KB文件） */
    editor.size = 4096;
    editor.capacity = 8192;
    editor.data = (uint8_t *)kmalloc(editor.capacity);
    if (!editor.data) return -2;

    /* 填充测试数据 */
    for (uint32_t i = 0; i < editor.size; i++) {
        editor.data[i] = (uint8_t)(i & 0xFF);
    }

    editor.cursor.offset = 0;
    editor.cursor.nibble = 0;
    editor.scroll_offset = 0;
    editor.modified = 0;

    vga_printf("[HEX-EDITOR] Opened: %s (%d bytes)\n", filename, editor.size);
    return 0;
}

/* 保存文件 */
int hex_editor_save(void)
{
    if (!editor.data || editor.size == 0) return -1;

    /* 简化：模拟保存
     * 实际应通过VFS写入文件 */
    editor.modified = 0;
    vga_printf("[HEX-EDITOR] Saved: %s (%d bytes)\n",
               editor.filename, editor.size);
    return 0;
}

/* 另存为 */
int hex_editor_save_as(const char *filename)
{
    if (!filename) return -1;
    strncpy(editor.filename, filename, 255);
    editor.filename[255] = 0;
    return hex_editor_save();
}

/* 关闭文件 */
void hex_editor_close(void)
{
    if (editor.data) {
        kfree(editor.data);
        editor.data = NULL;
    }
    editor.size = 0;
    editor.capacity = 0;
    editor.modified = 0;
    editor.filename[0] = 0;
}

/* 移动光标 */
int hex_editor_move_cursor(int dx, int dy)
{
    if (!editor.data) return -1;

    int new_offset = editor.cursor.offset + dx + dy * editor.bytes_per_line;
    if (new_offset < 0) new_offset = 0;
    if ((uint32_t)new_offset >= editor.size) new_offset = editor.size - 1;

    editor.cursor.offset = new_offset;

    /* 自动滚动 */
    uint32_t cursor_line = editor.cursor.offset / editor.bytes_per_line;
    if (cursor_line < editor.scroll_offset)
        editor.scroll_offset = cursor_line;
    else if (cursor_line >= editor.scroll_offset + 20)
        editor.scroll_offset = cursor_line - 19;

    return 0;
}

/* 跳转到偏移 */
int hex_editor_goto_offset(uint32_t offset)
{
    if (!editor.data) return -1;
    if (offset >= editor.size) offset = editor.size - 1;

    editor.cursor.offset = offset;
    editor.cursor.nibble = 0;

    uint32_t cursor_line = offset / editor.bytes_per_line;
    editor.scroll_offset = cursor_line;
    return 0;
}

/* 输入字节 */
int hex_editor_input_byte(uint8_t value)
{
    if (!editor.data || editor.mode == HEX_MODE_VIEW) return -1;
    if (editor.cursor.offset >= editor.size) return -2;

    if (editor.mode == HEX_MODE_EDIT_HEX) {
        if (editor.cursor.nibble == 0) {
            editor.data[editor.cursor.offset] =
                (editor.data[editor.cursor.offset] & 0x0F) | (value << 4);
            editor.cursor.nibble = 1;
        } else {
            editor.data[editor.cursor.offset] =
                (editor.data[editor.cursor.offset] & 0xF0) | (value & 0x0F);
            editor.cursor.nibble = 0;
            hex_editor_move_cursor(1, 0);
        }
    } else {
        editor.data[editor.cursor.offset] = value;
        hex_editor_move_cursor(1, 0);
    }

    editor.modified = 1;
    return 0;
}

/* 删除字节 */
int hex_editor_delete_byte(void)
{
    if (!editor.data || editor.size == 0) return -1;
    if (editor.cursor.offset >= editor.size) return -2;

    for (uint32_t i = editor.cursor.offset; i < editor.size - 1; i++) {
        editor.data[i] = editor.data[i + 1];
    }
    editor.size--;
    editor.modified = 1;
    return 0;
}

/* 插入字节 */
int hex_editor_insert_byte(uint8_t value)
{
    if (!editor.data) return -1;
    if (editor.size >= editor.capacity) return -2;

    for (uint32_t i = editor.size; i > editor.cursor.offset; i--) {
        editor.data[i] = editor.data[i - 1];
    }
    editor.data[editor.cursor.offset] = value;
    editor.size++;
    editor.modified = 1;
    return 0;
}

/* 切换模式 */
void hex_editor_toggle_mode(void)
{
    if (editor.mode == HEX_MODE_VIEW)
        editor.mode = HEX_MODE_EDIT_HEX;
    else if (editor.mode == HEX_MODE_EDIT_HEX)
        editor.mode = HEX_MODE_EDIT_ASCII;
    else
        editor.mode = HEX_MODE_VIEW;
}

/* 滚动 */
int hex_editor_scroll(int lines)
{
    int new_scroll = (int)editor.scroll_offset + lines;
    if (new_scroll < 0) new_scroll = 0;
    uint32_t max_lines = (editor.size + editor.bytes_per_line - 1) /
                         editor.bytes_per_line;
    if ((uint32_t)new_scroll >= max_lines) new_scroll = max_lines - 1;
    editor.scroll_offset = new_scroll;
    return 0;
}

/* 搜索字节序列 */
int hex_editor_search(const uint8_t *pattern, int len)
{
    if (!editor.data || !pattern || len <= 0) return -1;

    for (uint32_t i = 0; i + len <= editor.size; i++) {
        if (memcmp(editor.data + i, pattern, len) == 0) {
            hex_editor_goto_offset(i);
            return i;
        }
    }
    return -1;
}

/* 替换字节序列 */
int hex_editor_replace(const uint8_t *old_pat, int old_len,
                       const uint8_t *new_pat, int new_len)
{
    if (!editor.data || !old_pat || !new_pat) return -1;

    int offset = hex_editor_search(old_pat, old_len);
    if (offset < 0) return -1;

    if (new_len == old_len) {
        memcpy(editor.data + offset, new_pat, new_len);
    } else if (new_len < old_len) {
        memcpy(editor.data + offset, new_pat, new_len);
        for (uint32_t i = offset + new_len; i < editor.size - (old_len - new_len); i++) {
            editor.data[i] = editor.data[i + old_len - new_len];
        }
        editor.size -= (old_len - new_len);
    } else {
        if (editor.size + (new_len - old_len) > editor.capacity) return -2;
        for (uint32_t i = editor.size + (new_len - old_len) - 1;
             i >= (uint32_t)(offset + new_len); i--) {
            editor.data[i] = editor.data[i - (new_len - old_len)];
        }
        memcpy(editor.data + offset, new_pat, new_len);
        editor.size += (new_len - old_len);
    }

    editor.modified = 1;
    return offset;
}

/* 获取字节 */
uint8_t hex_editor_get_byte(uint32_t offset)
{
    if (!editor.data || offset >= editor.size) return 0;
    return editor.data[offset];
}

/* 设置字节 */
int hex_editor_set_byte(uint32_t offset, uint8_t value)
{
    if (!editor.data || offset >= editor.size) return -1;
    editor.data[offset] = value;
    editor.modified = 1;
    return 0;
}

/* 将字节转换为可打印ASCII字符 */
static char byte_to_ascii(uint8_t b)
{
    if (b >= 0x20 && b <= 0x7E) return (char)b;
    return '.';
}

/* 绘制编辑器UI */
void hex_editor_draw_ui(int x, int y, int width, int height)
{
    gfx_color_t bg = {20, 20, 30, 255};
    gfx_color_t fg = {200, 200, 200, 255};
    gfx_color_t accent = {100, 200, 255, 255};
    gfx_color_t cursor_bg = {50, 100, 200, 255};
    gfx_color_t offset_color = {150, 150, 100, 255};
    gfx_color_t ascii_color = {100, 255, 100, 255};
    gfx_color_t gray = {100, 100, 100, 255};

    /* 背景 */
    gfx_fill_rect(x, y, width, height, bg);

    if (!editor.data) {
        gfx_draw_string_font(x + width/2 - 80, y + height/2,
                             "未打开文件 (按O打开)", fg, 16);
        return;
    }

    /* 标题栏 */
    char title[300];
    const char *mode_str = (editor.mode == HEX_MODE_VIEW) ? "查看" :
                           (editor.mode == HEX_MODE_EDIT_HEX) ? "编辑HEX" : "编辑ASCII";
    snprintf(title, sizeof(title), "十六进制编辑器 - %s [%s]%s",
             editor.filename, mode_str, editor.modified ? " *" : "");
    gfx_draw_string_font(x + 10, y + 8, title, fg, 14);

    /* 列标题 */
    int header_y = y + 35;
    gfx_draw_string_font(x + 10, header_y, "偏移", offset_color, 12);
    gfx_draw_string_font(x + 70, header_y, "00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F",
                         accent, 12);
    gfx_draw_string_font(x + 70 + 16 * 22, header_y, "ASCII", ascii_color, 12);

    /* 数据区域 */
    int data_y = header_y + 20;
    int line_height = 16;
    int visible_lines = (height - data_y - 30) / line_height;

    for (int line = 0; line < visible_lines; line++) {
        uint32_t offset = (editor.scroll_offset + line) * editor.bytes_per_line;
        if (offset >= editor.size) break;

        int cur_y = data_y + line * line_height;

        /* 偏移地址 */
        char offset_str[16];
        snprintf(offset_str, sizeof(offset_str), "%08X", offset);
        gfx_draw_string_font(x + 10, cur_y, offset_str, offset_color, 12);

        /* 十六进制数据 */
        char hex_str[64];
        char ascii_str[20];
        int hex_pos = 0;
        int ascii_pos = 0;

        for (int i = 0; i < editor.bytes_per_line; i++) {
            uint32_t cur_offset = offset + i;
            if (cur_offset >= editor.size) {
                hex_str[hex_pos++] = ' ';
                hex_str[hex_pos++] = ' ';
                ascii_str[ascii_pos++] = ' ';
            } else {
                uint8_t byte = editor.data[cur_offset];
                const char *hex_digits = "0123456789ABCDEF";
                hex_str[hex_pos++] = hex_digits[(byte >> 4) & 0xF];
                hex_str[hex_pos++] = hex_digits[byte & 0xF];
                ascii_str[ascii_pos++] = byte_to_ascii(byte);

                /* 高亮光标 */
                if (cur_offset == editor.cursor.offset) {
                    int char_x = x + 70 + i * 22;
                    gfx_fill_rect(char_x - 2, cur_y - 2, 20, 14, cursor_bg);
                }
            }
            hex_str[hex_pos++] = ' ';
        }
        hex_str[hex_pos] = 0;
        ascii_str[ascii_pos] = 0;

        gfx_draw_string_font(x + 70, cur_y, hex_str, fg, 12);
        gfx_draw_string_font(x + 70 + 16 * 22, cur_y, ascii_str, ascii_color, 12);
    }

    /* 底部状态栏 */
    int status_y = y + height - 25;
    char status[200];
    snprintf(status, sizeof(status),
             "偏移: 0x%08X (%d)  字节: 0x%02X (%d)  大小: %d  行: %d/%d",
             editor.cursor.offset, editor.cursor.offset,
             editor.data ? editor.data[editor.cursor.offset] : 0,
             editor.data ? editor.data[editor.cursor.offset] : 0,
             editor.size,
             editor.scroll_offset + 1,
             (editor.size + editor.bytes_per_line - 1) / editor.bytes_per_line);
    gfx_draw_string_font(x + 10, status_y, status, gray, 11);

    /* 快捷键提示 */
    gfx_draw_string_font(x + width - 250, status_y,
                         "方向键移动 Tab切换模式 Ctrl+S保存", gray, 10);
}

/* 处理键盘输入 */
void hex_editor_handle_key(char key)
{
    if (!editor.initialized) return;

    switch (key) {
        case 0x1B:  /* ESC */
            if (editor.mode != HEX_MODE_VIEW)
                editor.mode = HEX_MODE_VIEW;
            break;
        case '\t':  /* Tab：切换模式 */
            hex_editor_toggle_mode();
            break;
        case 0x01:  /* Ctrl+A：开头 */
            hex_editor_goto_offset(0);
            break;
        case 0x05:  /* Ctrl+E：结尾 */
            if (editor.size > 0)
                hex_editor_goto_offset(editor.size - 1);
            break;
        case 0x13:  /* Ctrl+S：保存 */
            hex_editor_save();
            break;
        case 0x08:  /* Backspace：删除 */
            hex_editor_delete_byte();
            break;
        case 0x7F:  /* Delete */
            hex_editor_delete_byte();
            break;
        default:
            /* 十六进制输入 */
            if (editor.mode == HEX_MODE_EDIT_HEX) {
                if (key >= '0' && key <= '9')
                    hex_editor_input_byte(key - '0');
                else if (key >= 'a' && key <= 'f')
                    hex_editor_input_byte(key - 'a' + 10);
                else if (key >= 'A' && key <= 'F')
                    hex_editor_input_byte(key - 'A' + 10);
            } else if (editor.mode == HEX_MODE_EDIT_ASCII) {
                if (key >= 0x20 && key <= 0x7E)
                    hex_editor_input_byte((uint8_t)key);
            }
            break;
    }
}

/* 获取编辑器状态 */
int hex_editor_get_state(hex_editor_t *state)
{
    if (!state) return -1;
    memcpy(state, &editor, sizeof(hex_editor_t));
    return 0;
}
