/* ============================================================================
 * 赫普OS - 共享字符串/内存操作库（架构无关）
 * 32位和64位内核共用，避免代码重复
 * ========================================================================== */
#ifndef LIB_SHARED_H
#define LIB_SHARED_H

#include <stdint.h>
#include <stddef.h>

/* 字符串操作 */
size_t shared_strlen(const char *s);
int shared_strcmp(const char *s1, const char *s2);
int shared_strncmp(const char *s1, const char *s2, size_t n);
char *shared_strcpy(char *dest, const char *src);
char *shared_strncpy(char *dest, const char *src, size_t n);
char *shared_strcat(char *dest, const char *src);
char *shared_strchr(const char *s, int c);
char *shared_strrchr(const char *s, int c);

/* 内存操作 */
void *shared_memset(void *s, int c, size_t n);
void *shared_memcpy(void *dest, const void *src, size_t n);
void *shared_memmove(void *dest, const void *src, size_t n);
int shared_memcmp(const void *s1, const void *s2, size_t n);
void *shared_memchr(const void *s, int c, size_t n);

/* 数字转换 */
int shared_atoi(const char *s);
char *shared_itoa(int value, char *str, int base);

#endif /* LIB_SHARED_H */
