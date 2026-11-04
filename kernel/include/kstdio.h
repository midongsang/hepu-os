#ifndef HEPU_KSTDIO_H
#define HEPU_KSTDIO_H

#include <stdint.h>
#include <stddef.h>

/* va_list 需要 stdarg.h（编译器内置支持，无libc依赖） */
#include <stdarg.h>

/* 格式化输出到缓冲区
 * 支持：%d %i %u %x %X %c %s %p %%
 * 标志：-左对齐, 0零填充, 宽度, l长整型 */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
int snprintf(char *buf, size_t size, const char *fmt, ...);

#endif /* HEPU_KSTDIO_H */
