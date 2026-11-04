/* ============================================================================
 * 赫普OS - 十六进制编辑器（V8.72新增）
 * 用于系统调试、文件二进制查看/编辑
 * ========================================================================== */

#ifndef _HEX_EDITOR_H
#define _HEX_EDITOR_H

#include <stdint.h>

/* 编辑器模式 */
typedef enum {
    HEX_MODE_VIEW = 0,      /* 查看模式 */
    HEX_MODE_EDIT_HEX,      /* 编辑十六进制 */
    HEX_MODE_EDIT_ASCII     /* 编辑ASCII */
} hex_mode_t;

/* 光标位置 */
typedef struct {
    uint32_t offset;        /* 文件偏移 */
    int      nibble;        /* 0=高4位, 1=低4位 */
} hex_cursor_t;

/* 编辑器状态 */
typedef struct {
    char     filename[256];
    uint8_t *data;
    uint32_t size;
    uint32_t capacity;
    hex_cursor_t cursor;
    uint32_t scroll_offset; /* 滚动偏移（行号） */
    hex_mode_t mode;
    int      modified;
    int      initialized;
    int      bytes_per_line; /* 每行字节数，默认16 */
} hex_editor_t;

/* 初始化十六进制编辑器 */
int hex_editor_init(void);

/* 关闭编辑器 */
void hex_editor_shutdown(void);

/* 打开文件 */
int hex_editor_open(const char *filename);

/* 保存文件 */
int hex_editor_save(void);

/* 另存为 */
int hex_editor_save_as(const char *filename);

/* 关闭文件 */
void hex_editor_close(void);

/* 移动光标 */
int hex_editor_move_cursor(int dx, int dy);

/* 跳转到偏移 */
int hex_editor_goto_offset(uint32_t offset);

/* 输入字节 */
int hex_editor_input_byte(uint8_t value);

/* 删除字节 */
int hex_editor_delete_byte(void);

/* 插入字节 */
int hex_editor_insert_byte(uint8_t value);

/* 切换模式 */
void hex_editor_toggle_mode(void);

/* 滚动 */
int hex_editor_scroll(int lines);

/* 搜索字节序列 */
int hex_editor_search(const uint8_t *pattern, int len);

/* 替换字节序列 */
int hex_editor_replace(const uint8_t *old_pat, int old_len,
                       const uint8_t *new_pat, int new_len);

/* 获取当前字节 */
uint8_t hex_editor_get_byte(uint32_t offset);

/* 设置当前字节 */
int hex_editor_set_byte(uint32_t offset, uint8_t value);

/* 绘制编辑器UI */
void hex_editor_draw_ui(int x, int y, int width, int height);

/* 处理键盘输入 */
void hex_editor_handle_key(char key);

/* 获取编辑器状态 */
int hex_editor_get_state(hex_editor_t *state);

#endif /* _HEX_EDITOR_H */
