/* ============================================================================
 * 赫普OS - 早期控制台输出 (V0.99新增)
 * 实现：在VGA初始化前的调试输出，直接写VGA文本缓冲区
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "early_console.h"

#define VGA_BUFFER 0xB8000
#define VGA_WIDTH  80
#define VGA_HEIGHT 25

static uint16_t *early_buffer = (uint16_t *)VGA_BUFFER;
static uint8_t  early_row = 0;
static uint8_t  early_col = 0;
static uint8_t  early_color = 0x07;  /* 灰字黑底 */
static uint8_t  early_initialized = 0;

/* 初始化早期控制台 */
void early_console_init(void)
{
    /* 清屏 */
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        early_buffer[i] = (early_color << 8) | ' ';
    }
    early_row = 0;
    early_col = 0;
    early_initialized = 1;
}

/* 设置颜色 */
void early_console_set_color(uint8_t fg, uint8_t bg)
{
    early_color = (bg << 4) | (fg & 0x0F);
}

/* 输出一个字符 */
void early_console_putc(char c)
{
    if (!early_initialized) early_console_init();

    if (c == '\n') {
        early_col = 0;
        early_row++;
    } else if (c == '\r') {
        early_col = 0;
    } else if (c == '\t') {
        early_col = (early_col + 8) & ~7;
    } else {
        uint16_t *ptr = early_buffer + early_row * VGA_WIDTH + early_col;
        *ptr = (early_color << 8) | (uint8_t)c;
        early_col++;
    }

    if (early_col >= VGA_WIDTH) {
        early_col = 0;
        early_row++;
    }

    /* 滚动 */
    if (early_row >= VGA_HEIGHT) {
        for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
            early_buffer[i] = early_buffer[i + VGA_WIDTH];
        }
        for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++) {
            early_buffer[i] = (early_color << 8) | ' ';
        }
        early_row = VGA_HEIGHT - 1;
    }
}

/* 输出字符串 */
void early_console_puts(const char *str)
{
    if (!str) return;
    while (*str) {
        early_console_putc(*str++);
    }
}

/* 输出32位十六进制数 */
void early_console_puthex(uint32_t val)
{
    early_console_puts("0x");
    for (int i = 7; i >= 0; i--) {
        uint8_t nibble = (val >> (i * 4)) & 0xF;
        char c = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
        early_console_putc(c);
    }
}

/* 输出32位十进制数 */
void early_console_putdec(uint32_t val)
{
    if (val == 0) {
        early_console_putc('0');
        return;
    }
    char buf[11];
    int pos = 0;
    while (val > 0) {
        buf[pos++] = '0' + (val % 10);
        val /= 10;
    }
    while (pos > 0) {
        early_console_putc(buf[--pos]);
    }
}

/* 获取当前光标位置 */
void early_console_get_pos(uint8_t *row, uint8_t *col)
{
    if (row) *row = early_row;
    if (col) *col = early_col;
}

/* 设置光标位置 */
void early_console_set_pos(uint8_t row, uint8_t col)
{
    if (row < VGA_HEIGHT) early_row = row;
    if (col < VGA_WIDTH) early_col = col;
}

/* 清屏 */
void early_console_clear(void)
{
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        early_buffer[i] = (early_color << 8) | ' ';
    }
    early_row = 0;
    early_col = 0;
}
