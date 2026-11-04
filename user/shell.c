/* ============================================================================
 * 赫普OS 64位 - 用户态Shell程序
 * 独立ELF64用户态Shell，通过系统调用与内核交互
 * V0.10.106: P0-3 用户态Shell初始版本
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 系统调用号 */
#define SYS_READ    2
#define SYS_WRITE   1
#define SYS_EXIT    6
#define SYS_GETPID  20
#define SYS_TIME    13
#define SYS_SLEEP   14
#define SYS_OPEN    3
#define SYS_CLOSE   4
#define SYS_EXECVE  11
#define SYS_FORK    7
#define SYS_WAITPID 56
#define SYS_READDIR 30
#define SYS_STAT    32
#define SYS_GETCWD  79

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* 系统调用内联汇编 */
static inline uint64_t syscall0(uint64_t nr)
{
    register uint64_t ret asm("rax");
    asm volatile("syscall" : "=a"(ret) : "a"(nr) : "rcx", "r11", "memory");
    return ret;
}

static inline uint64_t syscall1(uint64_t nr, uint64_t a1)
{
    register uint64_t ret asm("rax");
    asm volatile("syscall" : "=a"(ret) : "a"(nr), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static inline uint64_t syscall3(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3)
{
    register uint64_t ret asm("rax");
    asm volatile("syscall" : "=a"(ret) : "a"(nr), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

/* 基础函数 */
static size_t ush_strlen(const char *s)
{
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static void ush_puts(const char *s)
{
    syscall3(SYS_WRITE, STDOUT_FILENO, (uint64_t)s, ush_strlen(s));
}

static void ush_putchar(char c)
{
    syscall3(SYS_WRITE, STDOUT_FILENO, (uint64_t)&c, 1);
}

static void ush_putint(int64_t num)
{
    char buf[32];
    int i = 0;
    int neg = 0;
    if (num < 0) { neg = 1; num = -num; }
    if (num == 0) { ush_putchar('0'); return; }
    while (num > 0 && i < 31) { buf[i++] = '0' + (num % 10); num /= 10; }
    if (neg) ush_putchar('-');
    while (i > 0) ush_putchar(buf[--i]);
}

static int ush_strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const uint8_t *)s1 - *(const uint8_t *)s2;
}

/* 读取一行输入 */
static int ush_readline(char *buf, int maxlen)
{
    int i = 0;
    char c;
    while (i < maxlen - 1) {
        int64_t n = syscall3(SYS_READ, STDIN_FILENO, (uint64_t)&c, 1);
        if (n <= 0) break;
        if (c == '\n' || c == '\r') break;
        if (c == '\b' || c == 0x7F) {
            if (i > 0) {
                i--;
                ush_puts("\b \b");
            }
            continue;
        }
        buf[i++] = c;
        ush_putchar(c);  /* 回显 */
    }
    buf[i] = 0;
    return i;
}

/* 内置命令 */
static void cmd_help(void)
{
    ush_puts("HepuOS User Shell Commands:\n");
    ush_puts("  help     - Show this help\n");
    ush_puts("  ls       - List files\n");
    ush_puts("  cat F    - Print file\n");
    ush_puts("  pwd      - Print working dir\n");
    ush_puts("  echo     - Echo arguments\n");
    ush_puts("  pid      - Show current PID\n");
    ush_puts("  time     - Show current time\n");
    ush_puts("  sleep N  - Sleep N milliseconds\n");
    ush_puts("  clear    - Clear screen\n");
    ush_puts("  browser  - Simple HTTP browser\n");
    ush_puts("  scrapling- Web scraper (fetch+links)\n");
    ush_puts("  uname    - Show system info\n");
    ush_puts("  date     - Show current time\n");
    ush_puts("  exit     - Exit shell\n");
}

static void cmd_ls(void)
{
    char buf[1024];
    long n = syscall3(SYS_READDIR, (uint64_t)&buf, (uint64_t)buf, 1024);
    if (n <= 0) { ush_puts("(empty)\n"); return; }
    buf[n < 1024 ? n : 1023] = 0;
    ush_puts(buf);
}

static void cmd_cat(char *args)
{
    if (!args) { ush_puts("Usage: cat <file>\n"); return; }
    long fd = syscall3(SYS_OPEN, (uint64_t)args, 0, 0);
    if (fd < 0) { ush_puts("Cannot open\n"); return; }
    char buf[512];
    long n;
    while ((n = syscall3(2, fd, (uint64_t)buf, 511)) > 0) {
        buf[n] = 0;
        ush_puts(buf);
    }
    syscall1(SYS_CLOSE, fd);
    ush_puts("\n");
}

static void cmd_pwd(void)
{
    char buf[256];
    syscall3(SYS_GETCWD, (uint64_t)&buf, 256, 0);
    ush_puts(buf);
    ush_puts("\n");
}

static void cmd_echo(char *args)
{
    if (args) {
        ush_puts(args);
    }
    ush_puts("\n");
}

static void cmd_pid(void)
{
    ush_puts("PID: ");
    ush_putint((int64_t)syscall0(SYS_GETPID));
    ush_puts("\n");
}

static void cmd_time(void)
{
    ush_puts("Time (seconds since midnight): ");
    ush_putint((int64_t)syscall0(SYS_TIME));
    ush_puts("\n");
}

static void cmd_sleep(char *args)
{
    if (!args) {
        ush_puts("Usage: sleep <milliseconds>\n");
        return;
    }
    int ms = 0;
    while (*args >= '0' && *args <= '9') {
        ms = ms * 10 + (*args - '0');
        args++;
    }
    syscall1(SYS_SLEEP, (uint64_t)ms);
}

static void cmd_clear(void)
{
    ush_puts("\033[2J\033[H");
}

/* 解析并执行命令 */
static void execute(char *line)
{
    /* 跳过前导空格 */
    while (*line == ' ' || *line == '\t') line++;
    if (*line == 0) return;

    /* 分离命令和参数 */
    char *cmd = line;
    char *args = line;
    while (*args && *args != ' ' && *args != '\t') args++;
    if (*args) { *args = 0; args++; }
    while (*args == ' ' || *args == '\t') args++;

    /* 执行内置命令 */
    if (ush_strcmp(cmd, "help") == 0) {
        cmd_help();
    } else if (ush_strcmp(cmd, "ls") == 0) {
        cmd_ls();
    } else if (ush_strcmp(cmd, "cat") == 0) {
        cmd_cat(args);
    } else if (ush_strcmp(cmd, "pwd") == 0) {
        cmd_pwd();
    } else if (ush_strcmp(cmd, "echo") == 0) {
        cmd_echo(args);
    } else if (ush_strcmp(cmd, "pid") == 0) {
        cmd_pid();
    } else if (ush_strcmp(cmd, "time") == 0) {
        cmd_time();
    } else if (ush_strcmp(cmd, "sleep") == 0) {
        cmd_sleep(args);
    } else if (ush_strcmp(cmd, "clear") == 0 || ush_strcmp(cmd, "cls") == 0) {
        cmd_clear();
    } else if (ush_strcmp(cmd, "uname") == 0) {
        ush_puts("HepuOS 0.20.50 x86_64\n");
    } else if (ush_strcmp(cmd, "date") == 0) {
        uint64_t t = syscall0(SYS_TIME);
        ush_puts("Uptime: ");
        char buf[16]; int i=0;
        if (t==0){ush_puts("0s\n");}
        else { while(t>0){buf[i++]='0'+t%10;t/=10;} for(int j=0;j<i/2;j++){char c=buf[j];buf[j]=buf[i-1-j];buf[i-1-j]=c;} buf[i]=0; ush_puts(buf); ush_puts("s\n"); }
    } else if (ush_strcmp(cmd, "exit") == 0 || ush_strcmp(cmd, "quit") == 0) {
        ush_puts("Goodbye!\n");
        syscall1(SYS_EXIT, 0);
    } else {
        /* 尝试作为外部程序执行 /bin/<cmd>.elf */
        char path[256];
        int i = 0;
        path[i++] = '/'; path[i++] = 'b'; path[i++] = 'i'; path[i++] = 'n'; path[i++] = '/';
        for (int j = 0; cmd[j] && cmd[j] != ' ' && i < 240; j++) path[i++] = cmd[j];
        path[i++] = '.'; path[i++] = 'e'; path[i++] = 'l'; path[i++] = 'f'; path[i] = 0;

        /* fork 子进程 */
        uint64_t pid = syscall0(SYS_FORK);
        if (pid == 0) {
            /* 子进程: execve(path, 0, 0) */
            syscall3(SYS_EXECVE, (uint64_t)path, 0, 0);
            /* execve 失败 */
            ush_puts("Command not found: ");
            ush_puts(cmd);
            ush_puts("\n");
            syscall1(SYS_EXIT, 1);
        } else if (pid > 0) {
            /* 父进程: waitpid */
            int status;
            syscall3(SYS_WAITPID, pid, (uint64_t)&status, 0);
        } else {
            ush_puts("fork failed\n");
        }
    }
}

/* 用户态Shell入口 */
void _start(void)
{
    char line[256];

    ush_puts("\n========================================\n");
    ush_puts("  HepuOS 64-bit User Shell (ush)\n");
    ush_puts("  Running in ring 3 (user mode)\n");
    ush_puts("========================================\n");
    ush_puts("Type 'help' for available commands.\n\n");

    /* 主循环 */
    while (1) {
        ush_puts("ush$ ");
        int len = ush_readline(line, sizeof(line));
        ush_puts("\n");
        if (len > 0) {
            execute(line);
        }
    }
}
