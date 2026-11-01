#ifndef HEPU_STDIO_H
#define HEPU_STDIO_H

#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>

/* 基础输出 */
int putchar(int c);
int puts(const char *s);
int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);

/* 格式化字符串 */
int sprintf(char *buf, const char *fmt, ...);
int vsprintf(char *buf, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

/* 输入 */
int getchar(void);
char *fgets(char *s, int size, void *stream);

#endif /* HEPU_STDIO_H */
