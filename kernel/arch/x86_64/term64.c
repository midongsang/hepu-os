/* ============================================================================
 * 赫普OS 64位 - 终端增强模块
 * 添加更多Shell命令和功能
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "process64.h"

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);
extern void vga_clear(void);
extern void pmm64_get_stats(uint64_t *total, uint64_t *used, uint64_t *free);
extern int process64_get_count(void);

/* 文件管理器接口 */
extern const char* fm64_get_path(void);
extern int fm64_cd(const char *name);
extern void fm64_list(void);
extern int fm64_cat(const char *name);
extern int fm64_touch(const char *name);
extern int fm64_mkdir(const char *name);

/* 系统设置接口 */
extern void syssettings64_show(void);
extern int syssettings64_get_count(void);

/* 任务管理器接口 */
extern void taskmgr64_text(void);

/* 记事本接口 */
extern void notes64_show(void);
extern int notes64_open(const char *filename);
extern int notes64_save(const char *filename);

/* 格式化数字 */
static void format_num(uint64_t num, char *buf, int buf_size)
{
    if (!buf || buf_size < 16) return;
    int i = 0;
    if (num == 0) buf[i++] = '0';
    while (num > 0 && i < buf_size - 1) {
        buf[i++] = '0' + num % 10;
        num /= 10;
    }
    buf[i] = 0;
    /* 反转 */
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
}

/* 格式化内存大小 */
static void format_memory(uint64_t bytes, char *buf, int buf_size)
{
    if (!buf || buf_size < 16) return;
    if (bytes >= 1024 * 1024) {
        int mb = bytes / (1024 * 1024);
        format_num(mb, buf, buf_size);
        int len = 0;
        while (buf[len]) len++;
        if (len < buf_size - 3) {
            buf[len++] = 'M';
            buf[len++] = 'B';
            buf[len] = 0;
        }
    } else if (bytes >= 1024) {
        int kb = bytes / 1024;
        format_num(kb, buf, buf_size);
        int len = 0;
        while (buf[len]) len++;
        if (len < buf_size - 3) {
            buf[len++] = 'K';
            buf[len++] = 'B';
            buf[len] = 0;
        }
    } else {
        format_num(bytes, buf, buf_size);
    }
}

/* neofetch - 系统信息展示 */
void term64_cmd_neofetch(void)
{
    uint64_t total, used, free;
    pmm64_get_stats(&total, &used, &free);
    char buf[32];

    vga_puts("\n");
    vga_puts("       ___          \n");
    vga_puts("      /\\  \\         HepuOS 64-bit\n");
    vga_puts("     /  \\  \\        Version: 0.10.117\n");
    vga_puts("    / /\\ \\  \\       Kernel: x86_64 Long Mode\n");
    vga_puts("   / ____ \\  \\      Shell: HepuSH\n");
    vga_puts("  /_/    \\_\\__\\     CPU: AMD EPYC 9Y24\n");
    vga_puts("\n");

    vga_puts("  OS: HepuOS 0.10.117 x86_64\n");
    vga_puts("  Host: QEMU Virtual Machine\n");
    vga_puts("  Kernel: 64-bit Microkernel\n");
    vga_puts("  Shell: HepuSH 1.0\n");
    vga_puts("  Resolution: 1024x768 (VBE)\n");
    vga_puts("  Terminal: VGA Text Mode\n");

    vga_puts("  Memory: ");
    format_memory(used * 4096, buf, sizeof(buf));
    vga_puts(buf);
    vga_puts(" / ");
    format_memory(total * 4096, buf, sizeof(buf));
    vga_puts(buf);
    vga_puts("\n");

    vga_puts("  Processes: ");
    format_num(process64_get_count(), buf, sizeof(buf));
    vga_puts(buf);
    vga_puts("\n");

    vga_puts("  Settings: ");
    format_num(syssettings64_get_count(), buf, sizeof(buf));
    vga_puts(buf);
    vga_puts(" items\n");

    vga_puts("\n");
}

/* top - 系统监控 */
void term64_cmd_top(void)
{
    uint64_t total, used, free;
    pmm64_get_stats(&total, &used, &free);
    char buf[32];

    vga_puts("\n=== System Monitor ===\n");
    vga_puts("  CPU:  AMD EPYC 9Y24 (96 cores)\n");
    vga_puts("  Load: 0.00, 0.00, 0.00\n");

    vga_puts("  Mem:  ");
    format_memory(total * 4096, buf, sizeof(buf));
    vga_puts(buf);
    vga_puts(" total, ");
    format_memory(used * 4096, buf, sizeof(buf));
    vga_puts(buf);
    vga_puts(" used, ");
    format_memory(free * 4096, buf, sizeof(buf));
    vga_puts(buf);
    vga_puts(" free\n");

    /* 数据同源：调用 process64_snapshot 获取真实进程快照 */
    process64_stat_t procs[PROCESS64_MAX_SLOTS];
    int n = process64_snapshot(procs, PROCESS64_MAX_SLOTS);

    vga_puts("  Procs: ");
    format_num(n, buf, sizeof(buf));
    vga_puts(buf);
    vga_puts("\n\n");
    vga_puts("  PID  DOMAIN  NAME            STATE\n");
    for (int i = 0; i < n; i++) {
        vga_puts("  ");
        format_num(procs[i].pid, buf, sizeof(buf));
        vga_puts(buf);
        int len = 0; while (buf[len]) len++;
        for (int k = len; k < 4; k++) vga_puts(" ");
        vga_puts(" ");
        format_num(procs[i].domain_id, buf, sizeof(buf));
        vga_puts(buf);
        len = 0; while (buf[len]) len++;
        for (int k = len; k < 7; k++) vga_puts(" ");
        vga_puts(procs[i].name);
        int namelen = 0;
        while (procs[i].name[namelen]) namelen++;
        for (int k = namelen; k < 16; k++) vga_puts(" ");
        const char *st = "UNKNOWN";
        switch (procs[i].state) {
            case 1: st = "READY"; break;
            case 2: st = "RUNNING"; break;
            case 3: st = "BLOCKED"; break;
            case 4: st = "ZOMBIE"; break;
        }
        vga_puts(st);
        vga_puts("\n");
    }
    vga_puts("========================\n");
}

/* ver - 版本信息 */
void term64_cmd_ver(void)
{
    vga_puts("\nHepuOS Version 0.10.117\n");
    vga_puts("Kernel: 64-bit Microkernel\n");
    vga_puts("Architecture: x86_64\n");
    vga_puts("Boot Loader: HepuBoot (SeaBIOS)\n");
    vga_puts("Filesystem: ext2\n");
    vga_puts("Network: TCP/IPv4 (RTL8139)\n");
    vga_puts("Graphics: VBE/VGA\n");
    vga_puts("Build: 2026-10-10\n\n");
}

/* whoami */
void term64_cmd_whoami(void)
{
    vga_puts("root\n");
}

/* hostname */
void term64_cmd_hostname(void)
{
    vga_puts("hepuos\n");
}

/* pwd - 当前目录 */
void term64_cmd_pwd(void)
{
    vga_puts(fm64_get_path());
    vga_puts("\n");
}

/* cd - 切换目录 */
void term64_cmd_cd(int argc, char **argv)
{
    if (argc < 2) {
        fm64_cd("/");
        return;
    }
    fm64_cd(argv[1]);
}

/* settings - 系统设置 */
void term64_cmd_settings(void)
{
    syssettings64_show();
}

/* taskmgr - 任务管理器 */
void term64_cmd_taskmgr(void)
{
    taskmgr64_text();
}

/* notes - 记事本 */
void term64_cmd_notes(int argc, char **argv)
{
    if (argc >= 3) {
        if (argv[1][0] == 'o' && argv[1][1] == 'p' && argv[1][2] == 'e' && argv[1][3] == 'n') {
            notes64_open(argv[2]);
            return;
        }
        if (argv[1][0] == 's' && argv[1][1] == 'a' && argv[1][2] == 'v' && argv[1][3] == 'e') {
            notes64_save(argv[2]);
            return;
        }
    }
    notes64_show();
}

/* history - 命令历史（简化版） */
void term64_cmd_history(void)
{
    vga_puts("\n  1  help\n");
    vga_puts("  2  ls\n");
    vga_puts("  3  uname\n");
    vga_puts("  4  neofetch\n");
    vga_puts("  5  history\n\n");
}

/* 初始化终端增强模块 */
void term64_init(void)
{
    vga_puts("[TERM64] Terminal enhanced (10 new commands)\n");
}

/* 获取增强命令数量 */
int term64_get_command_count(void)
{
    return 10; /* neofetch, top, ver, whoami, hostname, pwd, cd, settings, taskmgr, notes */
}
