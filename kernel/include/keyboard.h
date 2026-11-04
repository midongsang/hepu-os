#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

/* 键盘缓冲区大小 */
#define KEYBOARD_BUFFER_SIZE 256

/* 初始化键盘 */
void keyboard_init(void);
void keyboard_flush_raw(void);   /* V4.04-E49 */

/* 键盘中断处理 */
void keyboard_handler(void);

/* 从缓冲区读取一个字符 (阻塞) */
char keyboard_getchar(void);

/* 非阻塞读取一个字符 */
int keyboard_try_getchar(char *c);

/* 读取一行输入 */
int keyboard_readline(char *buf, int max_len);

/* 缓冲区是否有数据 */
int keyboard_has_data(void);

/* V2.22: 注入测试字符串到键盘缓冲区(Shell自检用) */
void keyboard_inject(const char *s);

#endif /* KEYBOARD_H */
