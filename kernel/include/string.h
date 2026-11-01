#ifndef HEPU_STRING_H
#define HEPU_STRING_H

#include <stdint.h>
#include <stddef.h>

/* 内存操作 */
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int   memcmp(const void *s1, const void *s2, size_t n);
void *memchr(const void *s, int c, size_t n);

/* 字符串操作 */
size_t strlen(const char *s);
char  *strcpy(char *dest, const char *src);
char  *strncpy(char *dest, const char *src, size_t n);
char  *strcat(char *dest, const char *src);
char  *strncat(char *dest, const char *src, size_t n);
int    strcmp(const char *s1, const char *s2);
int    strncmp(const char *s1, const char *s2, size_t n);
int    strcasecmp(const char *s1, const char *s2);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *haystack, const char *needle);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);

/* 转换 */
int    atoi(const char *s);
long   atol(const char *s);
int    abs(int x);

/* 字符分类 */
int    isdigit(int c);
int    isalpha(int c);
int    isalnum(int c);
int    isspace(int c);
int    isupper(int c);
int    islower(int c);
int    toupper(int c);
int    tolower(int c);

/* 内核专用 */
void  *kmemcpy(void *dest, const void *src, size_t n);
void  *kmemset(void *s, int c, size_t n);

#endif /* HEPU_STRING_H */
