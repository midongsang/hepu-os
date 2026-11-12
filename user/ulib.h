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

/* 系统调用号：唯一事实源 = kernel/include/syscall.h，用户态禁止再抄一份。
 *
 * 旧版在这里硬编码 1=WRITE / 3=OPEN / 6=EXIT / 7=FORK / 20=GETPID，
 * 与内核真实表（WRITE=10, OPEN=8, EXIT=5, FORK=43, GETPID=7）整体错位
 * —— 所有 /bin 程序一启动就刷 "Unimplemented syscall: 1"、"exited(pid=0)",
 * ps.elf 直接 #PF。这就是"程序打包了却跑不起来"的根因。
 */
#include "syscall.h"

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

/* V0.57.3: 第 4/5 参数走 r10/r8（Linux x86_64 约定），clone 需要 5 个参数 */
static inline uint64_t syscall4(uint64_t nr, uint64_t arg1, uint64_t arg2,
                                uint64_t arg3, uint64_t arg4)
{
    register uint64_t ret asm("rax");
    register uint64_t r10 asm("r10") = arg4;
    asm volatile("syscall" : "=a"(ret)
                 : "a"(nr), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10)
                 : "rcx", "r11", "memory");
    return ret;
}

static inline uint64_t syscall5(uint64_t nr, uint64_t arg1, uint64_t arg2,
                                uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    register uint64_t ret asm("rax");
    register uint64_t r10 asm("r10") = arg4;
    register uint64_t r8  asm("r8")  = arg5;
    asm volatile("syscall" : "=a"(ret)
                 : "a"(nr), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8)
                 : "rcx", "r11", "memory");
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

/* V0.57.8：结束当前用户线程（不是整进程）。
 * 内核把这条执行流摘掉后直接返回到同进程的下一条就绪线程；
 * 若没有兄弟线程，调用会普通返回，线程继续跑（不会误杀进程）。 */
static inline void thread_exit(void)
{
    syscall1(SYS_HEPU_THREAD_EXIT, 0);
}

static inline int fork(void)
{
    return (int)syscall0(SYS_FORK);
}

static inline int waitpid(int pid, int *status, int options)
{
    return (int)syscall3(SYS_WAITPID, (uint64_t)pid, (uint64_t)status, (uint64_t)options);
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
/* V0.57.2: 必须 noinline。
 *
 * 原来写 `static inline`，-O2 内联 + constprop 之后 %s 的 va_arg 取值落到
 * 0x70000000（栈顶之外）→ #PF(rip=printf 内 mov (%rdx),%rsi)。原因：variadic
 * 函数只有在生成独立栈帧、把寄存器参数 spill 进 register save area 之后，
 * va_start 才拿得到正确地址；inline 掉就没有这块区域。
 * ps.elf 一打印就崩、而只用 puts 的 hello.elf 正常，差别就在这里。
 */
static __attribute__((noinline, unused)) void printf(const char *fmt, ...)
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
