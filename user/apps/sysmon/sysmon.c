/* ============================================================================
 * 赫普OS - 系统监控应用
 * 显示CPU、内存、进程、网络状态
 * V8.39
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 系统调用 */
extern int hepu_write(int fd, const void *buf, int count);
extern void hepu_exit(int status);
extern int hepu_sysinfo(void *info);
extern int hepu_getpid(void);
extern int hepu_sleep(uint32_t ms);

/* 系统信息结构 */
typedef struct {
    uint32_t total_memory;
    uint32_t used_memory;
    uint32_t free_memory;
    uint32_t total_pages;
    uint32_t used_pages;
    uint32_t uptime;
    uint32_t process_count;
    uint32_t cpu_usage;
    uint32_t irq_count;
    char     kernel_version[64];
} sys_info_t;

/* 进程信息 */
typedef struct {
    int      pid;
    char     name[32];
    uint32_t state;
    uint32_t memory_usage;
    uint32_t cpu_time;
    uint32_t priority;
} process_info_t;

/* 简单的字符串函数 */
static int strlen(const char *s)
{
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void print_str(const char *s)
{
    hepu_write(1, s, strlen(s));
}

static void print_char(char c)
{
    hepu_write(1, &c, 1);
}

static void int_to_str(int n, char *buf)
{
    int i = 0;
    int negative = 0;
    char temp[16];

    if (n < 0) {
        negative = 1;
        n = -n;
    }

    if (n == 0) {
        buf[i++] = '0';
    } else {
        while (n > 0) {
            temp[i++] = '0' + (n % 10);
            n /= 10;
        }
    }

    int j = 0;
    if (negative) buf[j++] = '-';
    while (i > 0) {
        buf[j++] = temp[--i];
    }
    buf[j] = 0;
}

static void print_int(int n)
{
    char buf[16];
    int_to_str(n, buf);
    print_str(buf);
}

static void print_hex(uint32_t n)
{
    char buf[16];
    const char *hex = "0123456789abcdef";
    int i = 7;

    for (int j = 0; j < 8; j++) buf[j] = '0';
    buf[8] = 0;

    while (n > 0 && i >= 0) {
        buf[i--] = hex[n & 0xF];
        n >>= 4;
    }
    print_str(buf);
}

/* 打印进度条 */
static void print_bar(int percent, int width)
{
    print_char('[');
    int filled = (percent * width) / 100;
    for (int i = 0; i < width; i++) {
        if (i < filled) {
            if (percent > 90) print_char('#');
            else if (percent > 70) print_char('+');
            else print_char('=');
        } else {
            print_char('-');
        }
    }
    print_char(']');
    print_char(' ');
    print_int(percent);
    print_str("%\n");
}

/* 清屏 */
static void clear_screen(void)
{
    print_str("\033[2J\033[H");
}

/* 主函数 */
int main(int argc, char **argv)
{
    sys_info_t info;
    int iterations = 0;
    int max_iterations = 10;  /* 默认显示10次 */

    if (argc > 1) {
        /* 解析参数 */
        const char *arg = argv[1];
        if (arg[0] == '-' && arg[1] == 'n') {
            if (argc > 2) {
                max_iterations = 0;
                for (int i = 0; arg[2 + i] >= '0' && arg[2 + i] <= '9'; i++) {
                    max_iterations = max_iterations * 10 + (arg[2 + i] - '0');
                }
            }
        }
    }

    print_str("=== HepuOS System Monitor ===\n");
    print_str("Press Ctrl+C to exit\n\n");

    while (max_iterations == 0 || iterations < max_iterations) {
        clear_screen();

        /* 获取系统信息 */
        hepu_sysinfo(&info);

        /* 打印标题 */
        print_str("=== HepuOS System Monitor ===\n");
        print_str("Kernel: ");
        print_str(info.kernel_version);
        print_str("\n");
        print_str("Uptime: ");
        print_int(info.uptime);
        print_str(" seconds\n\n");

        /* CPU使用率 */
        print_str("CPU Usage:  ");
        print_bar(info.cpu_usage, 30);

        /* 内存使用 */
        int mem_percent = 0;
        if (info.total_memory > 0) {
            mem_percent = (info.used_memory * 100) / info.total_memory;
        }
        print_str("Memory:     ");
        print_bar(mem_percent, 30);
        print_str("  Total: ");
        print_int(info.total_memory / 1024);
        print_str(" KB, Used: ");
        print_int(info.used_memory / 1024);
        print_str(" KB, Free: ");
        print_int(info.free_memory / 1024);
        print_str(" KB\n");

        /* 进程信息 */
        print_str("\nProcesses: ");
        print_int(info.process_count);
        print_str(" running\n");

        /* 中断信息 */
        print_str("IRQs: ");
        print_int(info.irq_count);
        print_str("\n");

        /* 内存页信息 */
        print_str("Pages: ");
        print_int(info.used_pages);
        print_str("/");
        print_int(info.total_pages);
        print_str(" used\n");

        /* 分隔线 */
        print_str("\n----------------------------------------\n");
        print_str("Refresh: ");
        print_int(iterations + 1);
        if (max_iterations > 0) {
            print_str("/");
            print_int(max_iterations);
        }
        print_str("\n");

        iterations++;

        /* 等待1秒 */
        hepu_sleep(1000);
    }

    print_str("\nMonitor exited.\n");
    hepu_exit(0);
    return 0;
}
