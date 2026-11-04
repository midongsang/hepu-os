#ifndef EARLY_CONSOLE_H
#define EARLY_CONSOLE_H

#include <stdint.h>

/* 初始化早期控制台 */
void early_console_init(void);

/* 设置颜色 */
void early_console_set_color(uint8_t fg, uint8_t bg);

/* 输出 */
void early_console_putc(char c);
void early_console_puts(const char *str);
void early_console_puthex(uint32_t val);
void early_console_putdec(uint32_t val);

/* 光标控制 */
void early_console_get_pos(uint8_t *row, uint8_t *col);
void early_console_set_pos(uint8_t row, uint8_t col);

/* 清屏 */
void early_console_clear(void);

#endif
