/* ============================================================================
 * 赫普OS 64位 - 用户态C库（ulib）
 * 提供系统调用封装和常用libc函数
 * V0.10.149: 初始版本
 * ========================================================================== */

#ifndef ULIB_H
#define ULIB_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

/* 系统调用号（与内核syscall64_full.c一致） */
#define SYS_WRITE   1
#define SYS_READ    2
#define SYS_OPEN    3
#define SYS_CLOSE   4
#define SYS_EXIT    6
#define SYS_FORK    7
#define SYS_EXECVE  11
#define SYS_BRK     12
#define SYS_TIME    13
#define SYS_SLEEP   14
#define SYS_GETPID  20

/* 标准文件描述符 */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* 系统调用封装（内联汇编，x86_64 syscall指令） */
static inline uint64_t syscall0(uint64_t nr)
{
    register uint64_t ret asm("rax");
    asm volatile("syscall" : "=a"(ret) : "a"(nr) : "rcx", "r11", "memory");
    return ret;
}

static inline uint64_t syscall1(uint64_t nr, uint64_t arg1)
{
    register uint64_t ret asm("rax");
    asm volatile("syscall" : "=a"(ret) : "a"(nr), "D"(arg1) : "rcx", "r11", "memory");
    return ret;
}

static inline uint64_t syscall2(uint64_t nr, uint64_t arg1, uint64_t arg2)
{
    register uint64_t ret asm("rax");
    asm volatile("syscall" : "=a"(ret) : "a"(nr), "D"(arg1), "S"(arg2) : "rcx", "r11", "memory");
    return ret;
}

static inline uint64_t syscall3(uint64_t nr, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
    register uint64_t ret asm("rax");
    asm volatile("syscall" : "=a"(ret) : "a"(nr), "D"(arg1), "S"(arg2), "d"(arg3) : "rcx", "r11", "memory");
    return ret;
}

/* 系统调用函数 */
static inline int write(int fd, const void *buf, size_t count)
{
    return (int)syscall3(SYS_WRITE, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}

static inline int read(int fd, void *buf, size_t count)
{
    return (int)syscall3(SYS_READ, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}

static inline int open(const char *pathname, int flags)
{
    return (int)syscall2(SYS_OPEN, (uint64_t)pathname, (uint64_t)flags);
}

static inline int close(int fd)
{
    return (int)syscall1(SYS_CLOSE, (uint64_t)fd);
}

static inline void exit(int status)
{
    syscall1(SYS_EXIT, (uint64_t)status);
    while (1) { asm volatile("hlt"); }
}

static inline int fork(void)
{
    return (int)syscall0(SYS_FORK);
}

static inline int waitpid(int pid, int *status, int options)
{
    return (int)syscall3(56, (uint64_t)pid, (uint64_t)status, (uint64_t)options);
}

static inline int execve(const char *pathname, char *const argv[], char *const envp[])
{
    return (int)syscall3(SYS_EXECVE, (uint64_t)pathname, (uint64_t)argv, (uint64_t)envp);
}

static inline void *sbrk(intptr_t increment)
{
    return (void *)syscall1(SYS_BRK, (uint64_t)increment);
}

static inline uint64_t time(void)
{
    return syscall0(SYS_TIME);
}

static inline void sleep(uint64_t seconds)
{
    syscall1(SYS_SLEEP, seconds);
}

static inline int getpid(void)
{
    return (int)syscall0(SYS_GETPID);
}

/* 字符串函数 */
static inline size_t strlen(const char *s)
{
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static inline int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

static inline int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n > 0 && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

static inline char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++)) {}
    return dest;
}

static inline char *strncpy(char *dest, const char *src, size_t n)
{
    char *d = dest;
    while (n > 0 && *src) { *d++ = *src++; n--; }
    while (n > 0) { *d++ = '\0'; n--; }
    return dest;
}

static inline char *strcat(char *dest, const char *src)
{
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++)) {}
    return dest;
}

static inline char *strchr(const char *s, int c)
{
    while (*s) { if (*s == (char)c) return (char *)s; s++; }
    return (c == '\0') ? (char *)s : NULL;
}

/* 内存函数 */
static inline void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

static inline void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dest;
}

static inline void *memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

static inline int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    while (n--) { if (*p1 != *p2) return *p1 - *p2; p1++; p2++; }
    return 0;
}

/* 转换函数 */
static inline int atoi(const char *s)
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

static inline char *itoa(int value, char *str, int base)
{
    char *rc;
    char *ptr;
    char *low;
    if (base < 2 || base > 36) { *str = '\0'; return str; }
    rc = ptr = str;
    if (value < 0 && base == 10) { *ptr++ = '-'; }
    low = ptr;
    do {
        *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[value % base];
        value /= base;
    } while (value);
    *ptr-- = '\0';
    while (low < ptr) {
        char tmp = *low;
        *low++ = *ptr;
        *ptr-- = tmp;
    }
    return rc;
}

/* 输出函数 */
static inline void puts(const char *s)
{
    write(STDOUT_FILENO, s, strlen(s));
    write(STDOUT_FILENO, "\n", 1);
}

static inline void putchar(char c)
{
    write(STDOUT_FILENO, &c, 1);
}

/* 简单的printf（支持%d, %s, %c, %x, %%） */
static inline void printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[32];
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 'd': {
                    int val = va_arg(ap, int);
                    itoa(val, buf, 10);
                    write(STDOUT_FILENO, buf, strlen(buf));
                    break;
                }
                case 'u': {
                    unsigned int val = va_arg(ap, unsigned int);
                    itoa((int)val, buf, 10);
                    write(STDOUT_FILENO, buf, strlen(buf));
                    break;
                }
                case 'x': {
                    unsigned int val = va_arg(ap, unsigned int);
                    itoa((int)val, buf, 16);
                    write(STDOUT_FILENO, buf, strlen(buf));
                    break;
                }
                case 's': {
                    const char *s = va_arg(ap, const char *);
                    if (s) write(STDOUT_FILENO, s, strlen(s));
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(ap, int);
                    write(STDOUT_FILENO, &c, 1);
                    break;
                }
                case '%':
                    write(STDOUT_FILENO, "%", 1);
                    break;
                default:
                    write(STDOUT_FILENO, "%", 1);
                    write(STDOUT_FILENO, fmt, 1);
                    break;
            }
        } else {
            write(STDOUT_FILENO, fmt, 1);
        }
        fmt++;
    }
    va_end(ap);
}

#endif /* ULIB_H */
