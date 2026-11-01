#ifndef HEPU_STRING_H
#define HEPU_STRING_H

#include <stdint.h>
#include <stddef.h>

/* 字符串长度与复制 */
size_t strlen(const char *s);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strcat(char *dst, const char *src);
char *strncat(char *dst, const char *src, size_t n);

/* 字符串比较 */
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);

/* 字符串查找 */
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);

/* 内存操作 */
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int val, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memchr(const void *s, int c, size_t n);

/* 转换 */
int atoi(const char *s);
long atol(const char *s);
int abs(int x);

/* 字符分类 */
int isdigit(int c);
int isalpha(int c);
int isalnum(int c);
int isspace(int c);
int isupper(int c);
int islower(int c);
int toupper(int c);
int tolower(int c);

#endif /* HEPU_STRING_H */
