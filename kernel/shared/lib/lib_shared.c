/* ============================================================================
 * 赫普OS - 共享字符串/内存操作库实现（架构无关）
 * 32位和64位内核共用，避免代码重复
 * ========================================================================== */

#include "lib_shared.h"

/* 字符串长度 */
size_t shared_strlen(const char *s)
{
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

/* 字符串比较 */
int shared_strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/* 有限字符串比较 */
int shared_strncmp(const char *s1, const char *s2, size_t n)
{
    if (n == 0) return 0;
    while (n-- > 0 && *s1 && (*s1 == *s2)) {
        if (n == 0) break;
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/* 字符串复制 */
char *shared_strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

/* 有限字符串复制 */
char *shared_strncpy(char *dest, const char *src, size_t n)
{
    char *d = dest;
    while (n > 0 && *src) {
        *d++ = *src++;
        n--;
    }
    while (n > 0) {
        *d++ = '\0';
        n--;
    }
    return dest;
}

/* 字符串拼接 */
char *shared_strcat(char *dest, const char *src)
{
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

/* 查找字符 */
char *shared_strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    if (c == '\0') return (char *)s;
    return 0;
}

/* 反向查找字符 */
char *shared_strrchr(const char *s, int c)
{
    const char *last = 0;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == '\0') return (char *)s;
    return (char *)last;
}

/* 内存设置 */
void *shared_memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

/* 内存复制 */
void *shared_memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dest;
}

/* 安全内存复制（支持重叠） */
void *shared_memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

/* 内存比较 */
int shared_memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

/* 内存查找 */
void *shared_memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    while (n--) {
        if (*p == (unsigned char)c) return (void *)p;
        p++;
    }
    return 0;
}

/* 字符串转整数 */
int shared_atoi(const char *s)
{
    int result = 0;
    int sign = 1;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }
    return result * sign;
}

/* 整数转字符串 */
char *shared_itoa(int value, char *str, int base)
{
    char *rc = str;
    char *ptr = str;
    unsigned int v;
    if (base < 2 || base > 36) { *str = '\0'; return str; }
    v = (value < 0 && base == 10) ? -value : value;
    do {
        int d = v % base;
        *ptr++ = (d < 10) ? '0' + d : 'a' + d - 10;
    } while (v /= base);
    if (value < 0 && base == 10) *ptr++ = '-';
    *ptr-- = '\0';
    while (rc < ptr) {
        char t = *rc; *rc = *ptr; *ptr = t;
        rc++; ptr--;
    }
    return str;
}
