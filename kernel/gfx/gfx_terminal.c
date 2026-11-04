/*
 * V7.94: 图形模式终端（支持中文TrueType字体）
 *
 * 用于Shell和调试输出，替代VGA文本模式以支持中文显示。
 * 基本功能：文字输出、自动换行、滚动、光标位置。
 *
 * 注意：这是简化版，输入处理仍由键盘驱动完成。
 */

#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include "graphics.h"

/* serial_puts声明 */
extern void serial_puts(unsigned short port, const char *s);

/* 终端配置 */
#define TERM_FONT_SIZE      16
#define TERM_LINE_HEIGHT    20
#define TERM_MARGIN_X       8
#define TERM_MARGIN_Y       8
#define TERM_BG_COLOR       0x1E1E2E  /* 深色背景 */
#define TERM_FG_COLOR       0xCDD6F4  /* 浅色文字 */
#define TERM_CURSOR_COLOR   0x89B4FA  /* 光标颜色 */

/* 终端状态 */
static int term_x = TERM_MARGIN_X;
static int term_y = TERM_MARGIN_Y;
static int term_initialized = 0;
static int term_width = 0;
static int term_height = 0;
static int term_cols = 0;
static int term_rows = 0;

/* 初始化图形终端 */
void gfx_terminal_init(void)
{
    if (term_initialized) return;

    uint32_t w, h, bpp;
    gfx_get_mode(&w, &h, &bpp);
    term_width = w;
    term_height = h;
    term_cols = (term_width - 2 * TERM_MARGIN_X) / (TERM_FONT_SIZE / 2);
    term_rows = (term_height - 2 * TERM_MARGIN_Y) / TERM_LINE_HEIGHT;

    /* 清屏 */
    gfx_fill_rect(0, 0, term_width, term_height, TERM_BG_COLOR);

    term_x = TERM_MARGIN_X;
    term_y = TERM_MARGIN_Y;
    term_initialized = 1;

    serial_puts(0x3F8, "[TERM] graphics terminal initialized\n");
}

/* 清屏 */
void gfx_terminal_clear(void)
{
    if (!term_initialized) gfx_terminal_init();
    gfx_fill_rect(0, 0, term_width, term_height, TERM_BG_COLOR);
    term_x = TERM_MARGIN_X;
    term_y = TERM_MARGIN_Y;
}

/* 滚动一行 */
static void term_scroll(void)
{
    /* 向上滚动一行 */
    int scroll_h = TERM_LINE_HEIGHT;
    uint32_t *fb = (uint32_t *)gfx_get_fb_vaddr();
    if (!fb) return;

    int pitch = term_width;
    for (int y = 0; y < term_height - scroll_h; y++) {
        memcpy(&fb[y * pitch], &fb[(y + scroll_h) * pitch], pitch * 4);
    }
    /* 清空底部 */
    gfx_fill_rect(0, term_height - scroll_h, term_width, scroll_h, TERM_BG_COLOR);

    term_y -= TERM_LINE_HEIGHT;
}

/* 输出单个字符（支持UTF-8） */
static void term_put_char(char c)
{
    static char utf8_buf[4] = {0};
    static int utf8_len = 0;
    static int utf8_needed = 0;

    if (c == '\n') {
        term_x = TERM_MARGIN_X;
        term_y += TERM_LINE_HEIGHT;
        utf8_len = 0;
        utf8_needed = 0;
        if (term_y + TERM_LINE_HEIGHT > term_height - TERM_MARGIN_Y) {
            term_scroll();
        }
        return;
    }

    if (c == '\r') {
        term_x = TERM_MARGIN_X;
        return;
    }

    if (c == '\t') {
        term_x += (TERM_FONT_SIZE / 2) * 4;
        return;
    }

    /* UTF-8解码 */
    if (c & 0x80) {
        if (utf8_len == 0) {
            if ((c & 0xE0) == 0xC0) utf8_needed = 2;
            else if ((c & 0xF0) == 0xE0) utf8_needed = 3;
            else if ((c & 0xF8) == 0xF0) utf8_needed = 4;
            else utf8_needed = 1;
        }
        utf8_buf[utf8_len++] = c;
        if (utf8_len < utf8_needed) return;

        /* 完整UTF-8字符，绘制 */
        char tmp[5] = {utf8_buf[0], utf8_buf[1], utf8_buf[2], utf8_buf[3], 0};
        gfx_draw_string_font(term_x, term_y, tmp, TERM_FG_COLOR, TERM_FONT_SIZE);

        /* 计算字符宽度（中文约为size*3/2，ASCII约为size/2） */
        int char_w = (utf8_needed > 1) ? (TERM_FONT_SIZE * 3 / 2) : (TERM_FONT_SIZE / 2);
        term_x += char_w;

        utf8_len = 0;
        utf8_needed = 0;
    } else {
        /* ASCII字符 */
        char tmp[2] = {c, 0};
        gfx_draw_string_font(term_x, term_y, tmp, TERM_FG_COLOR, TERM_FONT_SIZE);
        term_x += TERM_FONT_SIZE / 2;
    }

    /* 自动换行 */
    if (term_x > term_width - TERM_MARGIN_X) {
        term_x = TERM_MARGIN_X;
        term_y += TERM_LINE_HEIGHT;
        if (term_y + TERM_LINE_HEIGHT > term_height - TERM_MARGIN_Y) {
            term_scroll();
        }
    }
}

/* 输出字符串 */
void gfx_terminal_puts(const char *s)
{
    if (!term_initialized) gfx_terminal_init();
    if (!s) return;

    while (*s) {
        term_put_char(*s++);
    }
}

/* 格式化输出 */
void gfx_terminal_printf(const char *fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gfx_terminal_puts(buf);
}

/* 获取终端是否初始化 */
int gfx_terminal_is_active(void)
{
    return term_initialized;
}
