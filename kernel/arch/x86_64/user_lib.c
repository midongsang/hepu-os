/* ============================================================================
 * 赫普OS 64位 - 用户态系统调用封装库
 * 提供用户态程序调用内核系统调用的接口
 * V0.10.73: 初始版本，支持write/read/open/close/exit/getpid/brk/time
 * V0.10.102: 添加内存/字符串辅助函数
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 系统调用号（与syscall64_full.c一致） */
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

/*
 * 系统调用入口（内联汇编）
 * 系统调用号在rax，参数在rdi, rsi, rdx, r10, r8, r9
 * 返回值在rax
 */
static inline uint64_t syscall0(uint64_t nr)
{
    register uint64_t ret asm("rax");
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(nr)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline uint64_t syscall1(uint64_t nr, uint64_t arg1)
{
    register uint64_t ret asm("rax");
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(nr), "D"(arg1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline uint64_t syscall2(uint64_t nr, uint64_t arg1, uint64_t arg2)
{
    register uint64_t ret asm("rax");
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(nr), "D"(arg1), "S"(arg2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline uint64_t syscall3(uint64_t nr, uint64_t arg1, uint64_t arg2, uint64_t arg3)
{
    register uint64_t ret asm("rax");
    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(nr), "D"(arg1), "S"(arg2), "d"(arg3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

/* 用户态API */

/* 写入文件 */
int64_t sys_write(int fd, const void *buf, size_t count)
{
    return (int64_t)syscall3(SYS_WRITE, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}

/* 读取文件 */
int64_t sys_read(int fd, void *buf, size_t count)
{
    return (int64_t)syscall3(SYS_READ, (uint64_t)fd, (uint64_t)buf, (uint64_t)count);
}

/* 打开文件 */
int64_t sys_open(const char *path, int flags, int mode)
{
    return (int64_t)syscall3(SYS_OPEN, (uint64_t)path, (uint64_t)flags, (uint64_t)mode);
}

/* 关闭文件 */
int64_t sys_close(int fd)
{
    return (int64_t)syscall1(SYS_CLOSE, (uint64_t)fd);
}

/* 退出进程 */
void sys_exit(int exit_code)
{
    syscall1(SYS_EXIT, (uint64_t)exit_code);
    while (1) { asm volatile("hlt"); }
}

/* 获取进程ID */
int64_t sys_getpid(void)
{
    return (int64_t)syscall0(SYS_GETPID);
}

/* 获取当前时间（秒数，从午夜开始） */
int64_t sys_time(void)
{
    return (int64_t)syscall0(SYS_TIME);
}

/* 调整程序break地址 */
void *sys_brk(void *addr)
{
    return (void *)syscall1(SYS_BRK, (uint64_t)addr);
}

/* 休眠（毫秒） */
int64_t sys_sleep(uint64_t milliseconds)
{
    return (int64_t)syscall1(SYS_SLEEP, milliseconds);
}

/* 创建子进程 */
int64_t sys_fork(void)
{
    return (int64_t)syscall0(SYS_FORK);
}

/* 执行程序 */
int64_t sys_execve(const char *path, char *const argv[], char *const envp[])
{
    return (int64_t)syscall3(SYS_EXECVE, (uint64_t)path, (uint64_t)argv, (uint64_t)envp);
}

/* 辅助函数：字符串长度 */
static size_t user_strlen(const char *s)
{
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

/* 输出字符串到stdout */
void user_puts(const char *s)
{
    sys_write(STDOUT_FILENO, s, user_strlen(s));
}

/* 输出字符到stdout */
void user_putchar(char c)
{
    sys_write(STDOUT_FILENO, &c, 1);
}

/* 输出数字（十进制） */
void user_putint(int64_t num)
{
    char buf[32];
    int i = 0;
    int negative = 0;

    if (num < 0) {
        negative = 1;
        num = -num;
    }

    if (num == 0) {
        user_putchar('0');
        return;
    }

    while (num > 0 && i < 31) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }

    if (negative) {
        user_putchar('-');
    }

    while (i > 0) {
        user_putchar(buf[--i]);
    }
}

/* 内存复制 */
void *user_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

/* 内存设置 */
void *user_memset(void *dst, int val, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) {
        d[i] = (uint8_t)val;
    }
    return dst;
}

/* 内存比较 */
int user_memcmp(const void *s1, const void *s2, size_t n)
{
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }
    return 0;
}

/* 字符串比较 */
int user_strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const uint8_t *)s1 - *(const uint8_t *)s2;
}

/* 字符串复制 */
char *user_strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++)) {}
    return dst;
}

/* 字符串连接 */
char *user_strcat(char *dst, const char *src)
{
    char *d = dst;
    while (*d) d++;
    while ((*d++ = *src++)) {}
    return dst;
}

/* 字符串查找字符 */
char *user_strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) {
            return (char *)s;
        }
        s++;
    }
    if (c == 0) {
        return (char *)s;
    }
    return NULL;
}

/* 数字转字符串 */
char *user_itoa(int64_t num, char *buf, int base)
{
    char *p = buf;
    int negative = 0;

    if (num < 0 && base == 10) {
        negative = 1;
        num = -num;
    }

    if (num == 0) {
        *p++ = '0';
        *p = 0;
        return buf;
    }

    char tmp[32];
    int i = 0;
    while (num > 0 && i < 31) {
        int digit = num % base;
        tmp[i++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        num /= base;
    }

    if (negative) {
        *p++ = '-';
    }

    while (i > 0) {
        *p++ = tmp[--i];
    }
    *p = 0;
    return buf;
}

/* 字符串转数字 */
int64_t user_atoi(const char *s)
{
    int64_t result = 0;
    int negative = 0;

    while (*s == ' ' || *s == '\t') s++;

    if (*s == '-') {
        negative = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }

    return negative ? -result : result;
}
