/* ============================================================================
 * 赫普OS 64位 - Shell命令行解释器
 * 支持基本命令：help, ls, cat, echo, date, uname, ps, clear, reboot
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

#define SHELL_BUF_SIZE 256
#define SHELL_MAX_ARGS 8

extern void vga_puts(const char *s);
extern void vga_putchar(char c);
extern char keyboard64_getchar(void);
extern int keyboard64_haschar(void);
extern void rtc64_get_time(int *year, int *month, int *day,
                            int *hour, int *minute, int *second);
extern int ext2_64_list_dir(const char *path);
extern int ext2_64_read_file(const char *path, uint8_t *buf, uint32_t max_size);
extern int process64_count(void);
extern int user_process_switch(int pid);
extern void net64_poll(void);
extern uint32_t eth64_get_ip(void);

/* 终端增强命令 */
extern void term64_cmd_neofetch(void);
extern void term64_cmd_top(void);
extern void term64_cmd_ver(void);
extern void term64_cmd_whoami(void);
extern void term64_cmd_hostname(void);
extern void term64_cmd_pwd(void);
extern void term64_cmd_cd(int argc, char **argv);
extern void term64_cmd_settings(void);
extern void term64_cmd_taskmgr(void);
extern void term64_cmd_notes(int argc, char **argv);
extern void term64_cmd_history(void);

static void shell_memcpy(void *dst, const void *src, int n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}

static int shell_strlen(const char *s) {
    int i = 0;
    while (s[i]) i++;
    return i;
}

static int shell_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

static void shell_itoa(int n, char *s) {
    int i = 0, sign = n;
    char tmp[16];
    if (n < 0) n = -n;
    do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    if (sign < 0) tmp[i++] = '-';
    int j = 0;
    while (i > 0) s[j++] = tmp[--i];
    s[j] = 0;
}

/* 打印提示符 */
static void shell_prompt(void)
{
    vga_puts("hepu@os:~$ ");
}

/* 读取一行输入 */
static int shell_read_line(char *buf, int max)
{
    int pos = 0;
    while (pos < max - 1) {
        if (keyboard64_haschar()) {
            char c = keyboard64_getchar();
            if (c == '\n' || c == '\r') {
                vga_putchar('\n');
                buf[pos] = 0;
                return pos;
            } else if (c == '\b' || c == 0x7F) {
                if (pos > 0) {
                    pos--;
                    vga_putchar('\b');
                    vga_putchar(' ');
                    vga_putchar('\b');
                }
            } else if (c >= 0x20 && c < 0x7F) {
                buf[pos++] = c;
                vga_putchar(c);
            }
        }
        net64_poll();  /* 处理网络数据包 */
    }
    buf[pos] = 0;
    return pos;
}

/* 解析命令行参数 */
static int shell_parse_args(char *line, char **args, int max_args)
{
    int argc = 0;
    int in_word = 0;
    for (int i = 0; line[i] && argc < max_args; i++) {
        if (line[i] == ' ' || line[i] == '\t') {
            if (in_word) {
                line[i] = 0;
                in_word = 0;
            }
        } else {
            if (!in_word) {
                args[argc++] = &line[i];
                in_word = 1;
            }
        }
    }
    return argc;
}

/* 命令处理 */
static void shell_cmd_usertest(int argc, char **argv)
{
    vga_puts("[USER] Switching to user mode (PID 100)...\n");
    user_process_switch(100);
    vga_puts("[USER] Returned from user mode\n");
}

/* apic命令：切换到纯APIC模式（测试） */
extern int apic64_switch_to_apic_mode(void);
extern int apic64_is_apic_mode(void);
extern uint32_t apic64_get_id(void);
static void shell_cmd_apic(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "mode") == 0) {
        vga_puts("[APIC] Current mode: ");
        vga_puts(apic64_is_apic_mode() ? "APIC" : "PIC");
        vga_puts("\n");
        vga_puts("[APIC] Local APIC ID: ");
        char buf[8];
        int n = apic64_get_id(), i = 0;
        if (n == 0) buf[i++] = '0';
        while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
        buf[i] = 0;
        vga_puts(buf);
        vga_puts("\n");
        return;
    }
    if (argc > 1 && strcmp(argv[1], "switch") == 0) {
        vga_puts("[APIC] Switching to pure APIC mode...\n");
        int ret = apic64_switch_to_apic_mode();
        if (ret == 0) {
            vga_puts("[APIC] Switched to APIC mode OK\n");
        } else {
            vga_puts("[APIC] Switch failed\n");
        }
        return;
    }
    vga_puts("Usage: apic <mode|switch>\n");
    vga_puts("  mode   - show current APIC mode\n");
    vga_puts("  switch - switch to pure APIC mode\n");
}

/* exec命令：加载并执行ELF用户程序 */
extern int user_process_exec(const char *path);
static void shell_cmd_exec(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: exec <elf_file>\n");
        return;
    }
    vga_puts("[EXEC] Loading ");
    vga_puts(argv[1]);
    vga_puts("\n");
    int pid = user_process_exec(argv[1]);
    if (pid < 0) {
        vga_puts("[EXEC] Failed to execute\n");
    }
}

static void shell_cmd_help(int argc, char **argv)
{
    vga_puts("=== HepuOS 64-bit Shell (75 commands) ===\n\n");

    vga_puts("--- System Info ---\n");
    vga_puts("  help version uname date uptime meminfo\n");
    vga_puts("  cpuinfo pci netstat drivers free ps\n");
    vga_puts("  neofetch top whoami hostname\n\n");

    vga_puts("--- File Operations ---\n");
    vga_puts("  ls cat echo mkdir touch rm rmdir cp mv\n");
    vga_puts("  find tree pwd cd write stat\n\n");

    vga_puts("--- Text Processing ---\n");
    vga_puts("  grep head tail sort wc cut tr sed awk\n");
    vga_puts("  nl od tee uniq factor seq sum expr\n");
    vga_puts("  test basename dirname printf true false\n");
    vga_puts("  yes tty logname xargs\n\n");

    vga_puts("--- Network ---\n");
    vga_puts("  ifconfig\n\n");

    vga_puts("--- Applications ---\n");
    vga_puts("  settings taskmgr notes history\n");
    vga_puts("  usertest exec sleep cal md5sum\n\n");

    vga_puts("--- System Control ---\n");
    vga_puts("  clear reboot poweroff\n\n");

    vga_puts("Type 'help <command>' for details\n");
    vga_puts("========================================\n");
}

static void shell_cmd_ls(int argc, char **argv)
{
    const char *path = "/";
    if (argc > 1) path = argv[1];
    ext2_64_list_dir(path);
}

static void shell_cmd_cat(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: cat <file>\n");
        return;
    }
    /* 分块读取大文件，每次4KB */
    static uint8_t buf[4096];
    uint32_t offset = 0;
    int first = 1;
    while (1) {
        int len = ext2_64_read_file_offset(argv[1], buf, sizeof(buf), offset);
        if (len <= 0) break;
        buf[len] = 0;
        vga_puts((char*)buf);
        offset += len;
        if (len < (int)sizeof(buf)) break;
        first = 0;
    }
    if (first) {
        vga_puts("cat: ");
        vga_puts(argv[1]);
        vga_puts(": No such file or empty\n");
    } else {
        vga_putchar('\n');
    }
}

static void shell_cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (i > 1) vga_putchar(' ');
        vga_puts(argv[i]);
    }
    vga_putchar('\n');
}

static void shell_cmd_date(int argc, char **argv)
{
    int year, month, day, hour, minute, second;
    rtc64_get_time(&year, &month, &day, &hour, &minute, &second);

    char buf[64];
    int pos = 0;
    char num[8];

    shell_itoa(year, num);
    shell_memcpy(buf + pos, num, shell_strlen(num)); pos += shell_strlen(num);
    buf[pos++] = '-';
    if (month < 10) buf[pos++] = '0';
    shell_itoa(month, num);
    shell_memcpy(buf + pos, num, shell_strlen(num)); pos += shell_strlen(num);
    buf[pos++] = '-';
    if (day < 10) buf[pos++] = '0';
    shell_itoa(day, num);
    shell_memcpy(buf + pos, num, shell_strlen(num)); pos += shell_strlen(num);
    buf[pos++] = ' ';
    if (hour < 10) buf[pos++] = '0';
    shell_itoa(hour, num);
    shell_memcpy(buf + pos, num, shell_strlen(num)); pos += shell_strlen(num);
    buf[pos++] = ':';
    if (minute < 10) buf[pos++] = '0';
    shell_itoa(minute, num);
    shell_memcpy(buf + pos, num, shell_strlen(num)); pos += shell_strlen(num);
    buf[pos++] = ':';
    if (second < 10) buf[pos++] = '0';
    shell_itoa(second, num);
    shell_memcpy(buf + pos, num, shell_strlen(num)); pos += shell_strlen(num);
    buf[pos] = 0;

    vga_puts(buf);
    vga_putchar('\n');
}

/* version命令：显示详细版本信息 */
static void shell_cmd_version(int argc, char **argv)
{
    vga_puts("=== HepuOS Version ===\n");
    vga_puts("  OS Name:    HepuOS\n");
    vga_puts("  Version:    0.10.109\n");
    vga_puts("  Codename:   HepuBoot Unified\n");
    vga_puts("  Architecture: x86_64 (64-bit Long Mode)\n");
    vga_puts("  Kernel:     Microkernel\n");
    vga_puts("  Bootloader: HepuBoot (BIOS+UEFI)\n");
    vga_puts("  Filesystem: ext2\n");
    vga_puts("  Network:    TCP/IPv4 (RTL8139)\n");
    vga_puts("  Graphics:   VBE Linear Framebuffer\n");
    vga_puts("  Fonts:      8x16 Bitmap + TrueType\n");
    vga_puts("  Shell:      74 commands\n");
    vga_puts("  Drivers:    8 registered\n");
    vga_puts("  Memory:     512MB (max 1GB)\n");
    vga_puts("  Processes:  32 max\n");
    vga_puts("=======================\n");
}

static void shell_cmd_uname(int argc, char **argv)
{
    vga_puts("HepuOS 64-bit Kernel v0.10.6\n");
    vga_puts("Architecture: x86_64 (long mode)\n");
    vga_puts("Memory: 64MB\n");
    vga_puts("Filesystem: ext2\n");
    vga_puts("Network: TCP/IP (ETH/ARP/IP/ICMP/UDP/TCP/DHCP/DNS)\n");
}

static void shell_cmd_ps(int argc, char **argv)
{
    int count = process64_count();
    vga_puts("Processes: ");
    char num[8];
    shell_itoa(count, num);
    vga_puts(num);
    vga_puts("\n");
}

static void shell_cmd_ifconfig(int argc, char **argv)
{
    uint32_t ip = eth64_get_ip();
    vga_puts("eth0: ");
    if (ip == 0) {
        vga_puts("No IP assigned (DHCP pending)\n");
    } else {
        char ipstr[16];
        uint8_t *ipb = (uint8_t*)&ip;
        int pos = 0;
        for (int i = 0; i < 4; i++) {
            if (ipb[i] >= 100) ipstr[pos++] = '0' + ipb[i]/100;
            if (ipb[i] >= 10) ipstr[pos++] = '0' + (ipb[i]/10)%10;
            ipstr[pos++] = '0' + ipb[i]%10;
            if (i < 3) ipstr[pos++] = '.';
        }
        ipstr[pos] = 0;
        vga_puts(ipstr);
        vga_puts("\n");
    }
}

static void shell_cmd_clear(int argc, char **argv)
{
    /* 清屏：用ANSI转义序列或直接写VGA */
    vga_puts("\033[2J\033[H");
}

/* cp命令：复制文件 */
static void shell_cmd_cp(int argc, char **argv)
{
    if (argc < 3) {
        vga_puts("Usage: cp <source> <destination>\n");
        return;
    }
    extern int ext2_64_read_file_offset(const char*, uint8_t*, uint32_t, uint32_t);
    extern int ext2_64_create_file(const char*);
    extern int ext2_64_write_file(const char*, const uint8_t*, uint32_t);
    static uint8_t buf[8192];
    uint32_t offset = 0;
    int total = 0;

    /* 创建目标文件 */
    if (ext2_64_create_file(argv[2]) != 0) {
        vga_puts("cp: cannot create '");
        vga_puts(argv[2]);
        vga_puts("'\n");
        return;
    }

    /* 分块复制 */
    while (1) {
        int len = ext2_64_read_file_offset(argv[1], buf, sizeof(buf), offset);
        if (len <= 0) break;
        if (ext2_64_write_file(argv[2], buf, len) != 0) {
            vga_puts("cp: write error\n");
            return;
        }
        offset += len;
        total += len;
        if (len < (int)sizeof(buf)) break;
    }

    if (total > 0) {
        vga_puts("Copied ");
        char num[12];
        int n = total, i = 0;
        if (n == 0) num[i++] = '0';
        while (n > 0) { num[i++] = '0' + n % 10; n /= 10; }
        num[i] = 0;
        for (int j = 0; j < i / 2; j++) {
            char t = num[j]; num[j] = num[i-1-j]; num[i-1-j] = t;
        }
        vga_puts(num);
        vga_puts(" bytes\n");
    } else {
        vga_puts("cp: cannot read '");
        vga_puts(argv[1]);
        vga_puts("'\n");
    }
}

/* head命令：显示文件前N行 */
static void shell_cmd_head(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: head <file> [lines]\n");
        return;
    }
    int lines = 10;
    if (argc >= 3) {
        lines = 0;
        for (char *p = argv[2]; *p; p++) {
            if (*p >= '0' && *p <= '9') lines = lines * 10 + (*p - '0');
        }
    }
    extern int ext2_64_read_file(const char*, uint8_t*, uint32_t);
    static uint8_t buf[8192];
    int len = ext2_64_read_file(argv[1], buf, sizeof(buf) - 1);
    if (len <= 0) {
        vga_puts("head: cannot read '");
        vga_puts(argv[1]);
        vga_puts("'\n");
        return;
    }
    buf[len] = 0;
    int count = 0;
    for (int i = 0; i < len && count < lines; i++) {
        vga_putchar(buf[i]);
        if (buf[i] == '\n') count++;
    }
    if (count < lines && len > 0 && buf[len-1] != '\n') vga_putchar('\n');
}

/* wc命令：统计文件行数、单词数、字节数 */
static void shell_cmd_wc(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: wc <file>\n");
        return;
    }
    extern int ext2_64_read_file(const char*, uint8_t*, uint32_t);
    static uint8_t buf[8192];
    int len = ext2_64_read_file(argv[1], buf, sizeof(buf) - 1);
    if (len <= 0) {
        vga_puts("wc: cannot read '");
        vga_puts(argv[1]);
        vga_puts("'\n");
        return;
    }
    int lines = 0, words = 0;
    int in_word = 0;
    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n') lines++;
        if (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\n' || buf[i] == '\r') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
    }
    char num[12];
    shell_itoa(lines, num); vga_puts(num); vga_puts(" ");
    shell_itoa(words, num); vga_puts(num); vga_puts(" ");
    shell_itoa(len, num); vga_puts(num); vga_puts(" ");
    vga_puts(argv[1]); vga_puts("\n");
}

/* mv命令：移动/重命名文件（通过cp+rm实现） */
static void shell_cmd_mv(int argc, char **argv)
{
    if (argc < 3) {
        vga_puts("Usage: mv <source> <destination>\n");
        return;
    }
    extern int ext2_64_read_file_offset(const char*, uint8_t*, uint32_t, uint32_t);
    extern int ext2_64_create_file(const char*);
    extern int ext2_64_write_file(const char*, const uint8_t*, uint32_t);
    extern int ext2_64_delete_file(const char*);

    static uint8_t buf[8192];
    uint32_t offset = 0;
    int total = 0;

    if (ext2_64_create_file(argv[2]) != 0) {
        vga_puts("mv: cannot create '"); vga_puts(argv[2]); vga_puts("'\n");
        return;
    }
    while (1) {
        int len = ext2_64_read_file_offset(argv[1], buf, sizeof(buf), offset);
        if (len <= 0) break;
        ext2_64_write_file(argv[2], buf, len);
        offset += len;
        total += len;
        if (len < (int)sizeof(buf)) break;
    }
    if (total > 0) {
        ext2_64_delete_file(argv[1]);
        vga_puts("Moved '"); vga_puts(argv[1]); vga_puts("' to '"); vga_puts(argv[2]); vga_puts("'\n");
    } else {
        vga_puts("mv: cannot read '"); vga_puts(argv[1]); vga_puts("'\n");
    }
}

/* tree命令：显示目录树（简化版，只显示当前目录） */
static void shell_cmd_tree(int argc, char **argv)
{
    const char *path = (argc >= 2) ? argv[1] : "/";
    vga_puts(path);
    vga_puts("\n");
    extern int ext2_64_list_dir(const char*);
    ext2_64_list_dir(path);
}

/* grep命令：在文件中搜索字符串 */
static void shell_cmd_grep(int argc, char **argv)
{
    if (argc < 3) {
        vga_puts("Usage: grep <pattern> <file>\n");
        return;
    }
    const char *pattern = argv[1];
    const char *file = argv[2];
    extern int ext2_64_read_file(const char*, uint8_t*, uint32_t);
    static uint8_t buf[8192];
    int len = ext2_64_read_file(file, buf, sizeof(buf) - 1);
    if (len <= 0) {
        vga_puts("grep: cannot read '"); vga_puts(file); vga_puts("'\n");
        return;
    }
    buf[len] = 0;

    /* 简单字符串搜索：逐行匹配 */
    int plen = shell_strlen(pattern);
    int line_start = 0;
    int matches = 0;
    for (int i = 0; i <= len; i++) {
        if (i == len || buf[i] == '\n') {
            /* 检查这一行是否包含pattern */
            int found = 0;
            for (int j = line_start; j + plen <= i; j++) {
                int match = 1;
                for (int k = 0; k < plen; k++) {
                    if (buf[j+k] != pattern[k]) { match = 0; break; }
                }
                if (match) { found = 1; break; }
            }
            if (found) {
                for (int j = line_start; j < i; j++) vga_putchar(buf[j]);
                vga_putchar('\n');
                matches++;
            }
            line_start = i + 1;
        }
    }
    if (matches == 0) {
        vga_puts("No matches found\n");
    }
}

/* du命令：显示文件大小（简化版，通过读取统计） */
static void shell_cmd_du(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: du <file>\n");
        return;
    }
    extern int ext2_64_read_file_offset(const char*, uint8_t*, uint32_t, uint32_t);
    static uint8_t buf[4096];
    uint32_t total = 0;
    uint32_t offset = 0;
    while (1) {
        int len = ext2_64_read_file_offset(argv[1], buf, sizeof(buf), offset);
        if (len <= 0) break;
        total += len;
        offset += len;
        if (len < (int)sizeof(buf)) break;
    }
    char num[16];
    shell_itoa(total, num);
    vga_puts(num);
    vga_puts(" bytes\t");
    vga_puts(argv[1]);
    vga_puts("\n");
}

/* stat命令：显示文件信息（简化版） */
static void shell_cmd_stat(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: stat <file>\n");
        return;
    }
    extern uint32_t ext2_64_lookup(const char*);
    uint32_t inode = ext2_64_lookup(argv[1]);
    if (inode == 0) {
        vga_puts("stat: cannot stat '"); vga_puts(argv[1]); vga_puts("'\n");
        return;
    }
    vga_puts("File: "); vga_puts(argv[1]); vga_puts("\n");
    vga_puts("Inode: ");
    char num[16];
    shell_itoa(inode, num);
    vga_puts(num); vga_puts("\n");
    /* 读取文件大小 */
    extern int ext2_64_read_file_offset(const char*, uint8_t*, uint32_t, uint32_t);
    static uint8_t buf[4096];
    uint32_t total = 0, offset = 0;
    while (1) {
        int len = ext2_64_read_file_offset(argv[1], buf, sizeof(buf), offset);
        if (len <= 0) break;
        total += len; offset += len;
        if (len < (int)sizeof(buf)) break;
    }
    vga_puts("Size: "); shell_itoa(total, num); vga_puts(num); vga_puts(" bytes\n");
}

/* cal命令：显示日历 */
static void shell_cmd_cal(int argc, char **argv)
{
    int year, month;
    extern void rtc64_get_time(int*, int*, int*, int*, int*, int*);
    int y, m, d, hh, mm, ss;
    rtc64_get_time(&y, &m, &d, &hh, &mm, &ss);
    year = y; month = m;
    if (argc >= 3) {
        year = 0; month = 0;
        for (char *p = argv[1]; *p; p++) if (*p >= '0' && *p <= '9') month = month * 10 + (*p - '0');
        for (char *p = argv[2]; *p; p++) if (*p >= '0' && *p <= '9') year = year * 10 + (*p - '0');
    }
    if (month < 1 || month > 12 || year < 1970 || year > 2100) {
        vga_puts("Usage: cal [month year]\n");
        return;
    }

    /* 月份名称 */
    const char *months[] = {"January","February","March","April","May","June",
                            "July","August","September","October","November","December"};
    /* 每月天数 */
    int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    /* 闰年判断 */
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) days[1] = 29;

    /* 计算第一天是星期几（Zeller公式简化版） */
    int a = (14 - month) / 12;
    int y_calc = year - a;
    int m_calc = month + 12 * a - 2;
    int first_day = (1 + y_calc + y_calc/4 - y_calc/100 + y_calc/400 + (31*m_calc)/12) % 7;

    /* 显示标题 */
    vga_puts("    "); vga_puts(months[month-1]); vga_puts(" ");
    char num[8]; shell_itoa(year, num); vga_puts(num); vga_puts("\n");
    vga_puts("Su Mo Tu We Th Fr Sa\n");

    /* 显示日历 */
    for (int i = 0; i < first_day; i++) vga_puts("   ");
    for (int day = 1; day <= days[month-1]; day++) {
        shell_itoa(day, num);
        if (day < 10) vga_puts(" ");
        vga_puts(num);
        vga_puts(" ");
        if ((first_day + day) % 7 == 0) vga_puts("\n");
    }
    vga_puts("\n");
}

/* find命令：搜索文件（简化版，列出当前目录匹配的文件） */
static void shell_cmd_find(int argc, char **argv)
{
    const char *pattern = (argc >= 2) ? argv[1] : NULL;
    vga_puts("Searching for files...\n");
    /* 简化版：直接列出根目录，用户可自行grep过滤 */
    extern int ext2_64_list_dir(const char*);
    ext2_64_list_dir("/");
    if (pattern) {
        vga_puts("(use 'ls | grep ");
        vga_puts(pattern);
        vga_puts("' to filter)\n");
    }
}

/* sleep命令：延时 */
static void shell_cmd_sleep(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: sleep <seconds>\n");
        return;
    }
    int seconds = 0;
    for (char *p = argv[1]; *p; p++) {
        if (*p >= '0' && *p <= '9') seconds = seconds * 10 + (*p - '0');
    }
    if (seconds <= 0) seconds = 1;
    extern void pit64_delay_s(int);
    vga_puts("Sleeping ");
    char num[8]; shell_itoa(seconds, num); vga_puts(num);
    vga_puts(" seconds...\n");
    pit64_delay_s(seconds);
    vga_puts("Done\n");
}

/* yes命令：重复输出字符串 */
static void shell_cmd_yes(int argc, char **argv)
{
    const char *str = (argc >= 2) ? argv[1] : "y";
    /* 输出10次后停止（避免无限循环） */
    for (int i = 0; i < 10; i++) {
        vga_puts(str);
        vga_puts("\n");
    }
}

/* tail命令：显示文件最后N行 */
static void shell_cmd_tail(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: tail <file> [lines]\n");
        return;
    }
    int lines = 10;
    if (argc >= 3) {
        lines = 0;
        for (char *p = argv[2]; *p; p++) {
            if (*p >= '0' && *p <= '9') lines = lines * 10 + (*p - '0');
        }
    }
    extern int ext2_64_read_file(const char*, uint8_t*, uint32_t);
    static uint8_t buf[8192];
    int len = ext2_64_read_file(argv[1], buf, sizeof(buf) - 1);
    if (len <= 0) {
        vga_puts("tail: cannot read '"); vga_puts(argv[1]); vga_puts("'\n");
        return;
    }
    buf[len] = 0;
    /* 统计总行数 */
    int total_lines = 0;
    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n') total_lines++;
    }
    /* 显示最后N行 */
    int start_line = (total_lines > lines) ? total_lines - lines : 0;
    int current_line = 0;
    for (int i = 0; i < len; i++) {
        if (current_line >= start_line) vga_putchar(buf[i]);
        if (buf[i] == '\n') current_line++;
    }
    if (len > 0 && buf[len-1] != '\n') vga_putchar('\n');
}

/* sort命令：对文件内容排序（简化版，按行字典序） */
static void shell_cmd_sort(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: sort <file>\n");
        return;
    }
    extern int ext2_64_read_file(const char*, uint8_t*, uint32_t);
    static uint8_t buf[8192];
    int len = ext2_64_read_file(argv[1], buf, sizeof(buf) - 1);
    if (len <= 0) {
        vga_puts("sort: cannot read '"); vga_puts(argv[1]); vga_puts("'\n");
        return;
    }
    buf[len] = 0;

    /* 简单冒泡排序（按行） */
    char lines[64][128];
    int line_count = 0;
    int pos = 0;
    for (int i = 0; i < len && line_count < 64; i++) {
        if (buf[i] == '\n' || i == len - 1) {
            int line_len = i - pos;
            if (line_len > 127) line_len = 127;
            for (int j = 0; j < line_len; j++) lines[line_count][j] = buf[pos+j];
            lines[line_count][line_len] = 0;
            line_count++;
            pos = i + 1;
        }
    }
    /* 冒泡排序 */
    for (int i = 0; i < line_count - 1; i++) {
        for (int j = 0; j < line_count - 1 - i; j++) {
            int k = 0;
            while (lines[j][k] && lines[j+1][k] && lines[j][k] == lines[j+1][k]) k++;
            if (lines[j][k] > lines[j+1][k]) {
                char tmp[128];
                for (int k2 = 0; k2 < 128; k2++) tmp[k2] = lines[j][k2];
                for (int k2 = 0; k2 < 128; k2++) lines[j][k2] = lines[j+1][k2];
                for (int k2 = 0; k2 < 128; k2++) lines[j+1][k2] = tmp[k2];
            }
        }
    }
    for (int i = 0; i < line_count; i++) {
        vga_puts(lines[i]);
        vga_puts("\n");
    }
}

/* uniq命令：去除连续重复行 */
static void shell_cmd_uniq(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: uniq <file>\n");
        return;
    }
    extern int ext2_64_read_file(const char*, uint8_t*, uint32_t);
    static uint8_t buf[8192];
    int len = ext2_64_read_file(argv[1], buf, sizeof(buf) - 1);
    if (len <= 0) {
        vga_puts("uniq: cannot read '"); vga_puts(argv[1]); vga_puts("'\n");
        return;
    }
    buf[len] = 0;
    /* 逐行输出，跳过连续重复行 */
    char last_line[128] = "";
    char current_line[128];
    int pos = 0;
    int current_pos = 0;
    for (int i = 0; i <= len; i++) {
        if (i == len || buf[i] == '\n') {
            current_line[current_pos] = 0;
            /* 比较是否与上一行相同 */
            int same = 1;
            int k = 0;
            while (last_line[k] && current_line[k] && last_line[k] == current_line[k]) k++;
            if (last_line[k] != current_line[k]) same = 0;
            if (!same || current_pos == 0) {
                vga_puts(current_line);
                vga_puts("\n");
                for (int k2 = 0; k2 < 128; k2++) last_line[k2] = current_line[k2];
            }
            current_pos = 0;
            pos = i + 1;
        } else if (current_pos < 127) {
            current_line[current_pos++] = buf[i];
        }
    }
}

/* cut命令：按分隔符切割字段 */
static void shell_cmd_cut(int argc, char **argv)
{
    if (argc < 3) {
        vga_puts("Usage: cut -d<delim> -f<field> <file>\n");
        return;
    }
    char delim = ' ';
    int field = 1;
    const char *file = NULL;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'd') delim = argv[i][2];
        else if (argv[i][0] == '-' && argv[i][1] == 'f') {
            field = 0;
            for (char *p = argv[i]+2; *p; p++)
                if (*p >= '0' && *p <= '9') field = field * 10 + (*p - '0');
        } else file = argv[i];
    }
    if (!file) { vga_puts("cut: no file specified\n"); return; }
    extern int ext2_64_read_file(const char*, uint8_t*, uint32_t);
    static uint8_t buf[8192];
    int len = ext2_64_read_file(file, buf, sizeof(buf) - 1);
    if (len <= 0) { vga_puts("cut: cannot read file\n"); return; }
    buf[len] = 0;
    /* 逐行处理 */
    int pos = 0;
    for (int i = 0; i <= len; i++) {
        if (i == len || buf[i] == '\n') {
            int current_field = 1;
            int field_start = pos;
            for (int j = pos; j < i; j++) {
                if (buf[j] == delim) {
                    if (current_field == field) {
                        for (int k = field_start; k < j; k++) vga_putchar(buf[k]);
                        vga_putchar('\n');
                        break;
                    }
                    current_field++;
                    field_start = j + 1;
                }
            }
            if (current_field == field && buf[i-1] != delim) {
                for (int k = field_start; k < i; k++) vga_putchar(buf[k]);
                vga_putchar('\n');
            }
            pos = i + 1;
        }
    }
}

/* tr命令：字符转换 */
static void shell_cmd_tr(int argc, char **argv)
{
    if (argc < 3) {
        vga_puts("Usage: tr <from> <to> <file>\n");
        return;
    }
    const char *from = argv[1];
    const char *to = argv[2];
    const char *file = argv[3];
    extern int ext2_64_read_file(const char*, uint8_t*, uint32_t);
    static uint8_t buf[8192];
    int len = ext2_64_read_file(file, buf, sizeof(buf) - 1);
    if (len <= 0) { vga_puts("tr: cannot read file\n"); return; }
    buf[len] = 0;
    /* 字符映射 */
    char map[256];
    for (int i = 0; i < 256; i++) map[i] = i;
    for (int i = 0; from[i] && to[i]; i++) {
        map[(uint8_t)from[i]] = to[i];
    }
    for (int i = 0; i < len; i++) {
        vga_putchar(map[(uint8_t)buf[i]]);
    }
}

/* awk命令：简化版（打印指定字段） */
static void shell_cmd_awk(int argc, char **argv)
{
    if (argc < 3) {
        vga_puts("Usage: awk '{print $N}' <file>\n");
        return;
    }
    /* 解析字段号 */
    int field = 1;
    for (char *p = argv[1]; *p; p++) {
        if (*p == '$') {
            field = 0;
            p++;
            while (*p >= '0' && *p <= '9') {
                field = field * 10 + (*p - '0');
                p++;
            }
            break;
        }
    }
    const char *file = argv[2];
    extern int ext2_64_read_file(const char*, uint8_t*, uint32_t);
    static uint8_t buf[8192];
    int len = ext2_64_read_file(file, buf, sizeof(buf) - 1);
    if (len <= 0) { vga_puts("awk: cannot read file\n"); return; }
    buf[len] = 0;
    /* 逐行打印指定字段 */
    int pos = 0;
    for (int i = 0; i <= len; i++) {
        if (i == len || buf[i] == '\n') {
            int current_field = 1;
            int field_start = pos;
            int found = 0;
            for (int j = pos; j < i; j++) {
                if (buf[j] == ' ' || buf[j] == '\t') {
                    if (current_field == field) {
                        for (int k = field_start; k < j; k++) vga_putchar(buf[k]);
                        vga_putchar('\n');
                        found = 1;
                        break;
                    }
                    current_field++;
                    field_start = j + 1;
                }
            }
            if (!found && current_field == field) {
                for (int k = field_start; k < i; k++) vga_putchar(buf[k]);
                vga_putchar('\n');
            }
            pos = i + 1;
        }
    }
}

/* basename命令：提取文件名 */
static void shell_cmd_basename(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: basename <path>\n");
        return;
    }
    const char *path = argv[1];
    const char *name = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') name = p + 1;
    }
    vga_puts(name);
    vga_puts("\n");
}

/* sed命令：简化版字符串替换 */
static void shell_cmd_sed(int argc, char **argv)
{
    if (argc < 3) {
        vga_puts("Usage: sed 's/old/new/' <file>\n");
        return;
    }
    /* 解析替换表达式 */
    const char *expr = argv[1];
    char old_str[64], new_str[64];
    int old_len = 0, new_len = 0;
    int phase = 0; /* 0: old, 1: new */
    for (int i = 2; expr[i] && expr[i] != '/'; i++) {
        if (phase == 0) {
            if (expr[i] == '/') { phase = 1; continue; }
            if (old_len < 63) old_str[old_len++] = expr[i];
        }
    }
    old_str[old_len] = 0;
    /* 继续解析new */
    int i = 2 + old_len + 1;
    while (expr[i] && expr[i] != '/' && new_len < 63) {
        new_str[new_len++] = expr[i++];
    }
    new_str[new_len] = 0;

    const char *file = argv[2];
    extern int ext2_64_read_file(const char*, uint8_t*, uint32_t);
    static uint8_t buf[8192];
    int len = ext2_64_read_file(file, buf, sizeof(buf) - 1);
    if (len <= 0) { vga_puts("sed: cannot read file\n"); return; }
    buf[len] = 0;

    /* 简单替换 */
    for (int i = 0; i < len; ) {
        int match = 1;
        for (int j = 0; old_str[j]; j++) {
            if (i + j >= len || buf[i+j] != old_str[j]) { match = 0; break; }
        }
        if (match && old_len > 0) {
            vga_puts(new_str);
            i += old_len;
        } else {
            vga_putchar(buf[i]);
            i++;
        }
    }
}

/* dirname命令：提取目录路径 */
static void shell_cmd_dirname(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: dirname <path>\n");
        return;
    }
    const char *path = argv[1];
    int last_slash = -1;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') last_slash = i;
    }
    if (last_slash <= 0) {
        vga_puts("/\n");
    } else {
        for (int i = 0; i < last_slash; i++) vga_putchar(path[i]);
        vga_puts("\n");
    }
}

/* xargs命令：简化版（从文件读取行并执行命令） */
static void shell_cmd_xargs(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: xargs <file> <command>\n");
        return;
    }
    const char *file = argv[1];
    const char *cmd = (argc >= 3) ? argv[2] : "echo";
    extern int ext2_64_read_file(const char*, uint8_t*, uint32_t);
    static uint8_t buf[8192];
    int len = ext2_64_read_file(file, buf, sizeof(buf) - 1);
    if (len <= 0) { vga_puts("xargs: cannot read file\n"); return; }
    buf[len] = 0;
    /* 逐行执行命令 */
    int pos = 0;
    for (int i = 0; i <= len; i++) {
        if (i == len || buf[i] == '\n') {
            int line_len = i - pos;
            if (line_len > 0) {
                vga_puts(cmd);
                vga_puts(" ");
                for (int j = pos; j < i; j++) vga_putchar(buf[j]);
                vga_puts("\n");
            }
            pos = i + 1;
        }
    }
}

/* tee命令：同时输出到屏幕和文件 */
static void shell_cmd_tee(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: tee <file>\n");
        return;
    }
    /* 简化版：创建空文件并提示 */
    extern int ext2_64_create_file(const char*);
    ext2_64_create_file(argv[1]);
    vga_puts("Created: ");
    vga_puts(argv[1]);
    vga_puts(" (tee: pipe input not supported in simplified version)\n");
}

/* nl命令：显示文件行号 */
static void shell_cmd_nl(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: nl <file>\n");
        return;
    }
    extern int ext2_64_read_file(const char*, uint8_t*, uint32_t);
    static uint8_t buf[8192];
    int len = ext2_64_read_file(argv[1], buf, sizeof(buf) - 1);
    if (len <= 0) { vga_puts("nl: cannot read file\n"); return; }
    buf[len] = 0;
    int line_num = 1;
    int pos = 0;
    for (int i = 0; i <= len; i++) {
        if (i == len || buf[i] == '\n') {
            char num[8];
            shell_itoa(line_num, num);
            vga_puts("     ");
            vga_puts(num);
            vga_puts("  ");
            for (int j = pos; j < i; j++) vga_putchar(buf[j]);
            vga_puts("\n");
            line_num++;
            pos = i + 1;
        }
    }
}

/* od命令：八进制转储（简化版） */
static void shell_cmd_od(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: od <file>\n");
        return;
    }
    extern int ext2_64_read_file(const char*, uint8_t*, uint32_t);
    static uint8_t buf[1024];
    int len = ext2_64_read_file(argv[1], buf, sizeof(buf));
    if (len <= 0) { vga_puts("od: cannot read file\n"); return; }
    for (int i = 0; i < len; i += 16) {
        char num[8];
        shell_itoa(i, num);
        vga_puts(num);
        vga_puts(" ");
        for (int j = 0; j < 16 && i+j < len; j++) {
            /* 十六进制 */
            char hex[4];
            int hi = (buf[i+j] >> 4) & 0xF;
            int lo = buf[i+j] & 0xF;
            hex[0] = (hi < 10) ? '0'+hi : 'a'+hi-10;
            hex[1] = (lo < 10) ? '0'+lo : 'a'+lo-10;
            hex[2] = ' ';
            hex[3] = 0;
            vga_puts(hex);
        }
        vga_puts("\n");
    }
}

/* factor命令：分解因数 */
static void shell_cmd_factor(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: factor <number>\n");
        return;
    }
    int n = 0;
    for (char *p = argv[1]; *p; p++) {
        if (*p >= '0' && *p <= '9') n = n * 10 + (*p - '0');
    }
    if (n <= 1) { vga_puts("1\n"); return; }
    char num[16];
    shell_itoa(n, num);
    vga_puts(num);
    vga_puts(": ");
    int first = 1;
    for (int d = 2; d * d <= n; d++) {
        while (n % d == 0) {
            if (!first) vga_puts(" ");
            shell_itoa(d, num);
            vga_puts(num);
            n /= d;
            first = 0;
        }
    }
    if (n > 1) {
        if (!first) vga_puts(" ");
        shell_itoa(n, num);
        vga_puts(num);
    }
    vga_puts("\n");
}

/* seq命令：生成数字序列 */
static void shell_cmd_seq(int argc, char **argv)
{
    int start = 1, end = 1, step = 1;
    if (argc == 2) {
        end = 0;
        for (char *p = argv[1]; *p; p++)
            if (*p >= '0' && *p <= '9') end = end * 10 + (*p - '0');
    } else if (argc == 3) {
        start = 0; end = 0;
        for (char *p = argv[1]; *p; p++)
            if (*p >= '0' && *p <= '9') start = start * 10 + (*p - '0');
        for (char *p = argv[2]; *p; p++)
            if (*p >= '0' && *p <= '9') end = end * 10 + (*p - '0');
    } else if (argc >= 4) {
        start = 0; end = 0; step = 0;
        for (char *p = argv[1]; *p; p++)
            if (*p >= '0' && *p <= '9') start = start * 10 + (*p - '0');
        for (char *p = argv[2]; *p; p++)
            if (*p >= '0' && *p <= '9') step = step * 10 + (*p - '0');
        for (char *p = argv[3]; *p; p++)
            if (*p >= '0' && *p <= '9') end = end * 10 + (*p - '0');
    }
    if (step <= 0) step = 1;
    for (int i = start; i <= end; i += step) {
        char num[16];
        shell_itoa(i, num);
        vga_puts(num);
        vga_puts("\n");
    }
}

/* sum命令：简单校验和（BSD sum算法） */
static void shell_cmd_sum(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: sum <file>\n");
        return;
    }
    extern int ext2_64_read_file(const char*, uint8_t*, uint32_t);
    static uint8_t buf[8192];
    int len = ext2_64_read_file(argv[1], buf, sizeof(buf));
    if (len <= 0) { vga_puts("sum: cannot read file\n"); return; }
    uint32_t checksum = 0;
    for (int i = 0; i < len; i++) {
        checksum = (checksum >> 1) + ((checksum & 1) << 15);
        checksum += buf[i];
        checksum &= 0xFFFF;
    }
    char num[16];
    shell_itoa(checksum, num);
    vga_puts(num);
    vga_puts(" ");
    int blocks = (len + 1023) / 1024;
    shell_itoa(blocks, num);
    vga_puts(num);
    vga_puts(" ");
    vga_puts(argv[1]);
    vga_puts("\n");
}

/* md5sum命令：简化版（CRC32代替） */
static void shell_cmd_md5sum(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: md5sum <file>\n");
        return;
    }
    extern int ext2_64_read_file(const char*, uint8_t*, uint32_t);
    static uint8_t buf[8192];
    int len = ext2_64_read_file(argv[1], buf, sizeof(buf));
    if (len <= 0) { vga_puts("md5sum: cannot read file\n"); return; }
    /* CRC32 */
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    crc ^= 0xFFFFFFFF;
    /* 输出十六进制 */
    char hex[9];
    for (int i = 7; i >= 0; i--) {
        int nibble = (crc >> (i*4)) & 0xF;
        hex[7-i] = (nibble < 10) ? '0'+nibble : 'a'+nibble-10;
    }
    hex[8] = 0;
    vga_puts(hex);
    vga_puts("  ");
    vga_puts(argv[1]);
    vga_puts(" (CRC32, simplified)\n");
}

/* true命令：返回成功 */
static void shell_cmd_true(int argc, char **argv)
{
    /* 无操作，返回成功 */
}

/* false命令：返回失败 */
static void shell_cmd_false(int argc, char **argv)
{
    /* 无操作，返回失败（简化版不处理退出码） */
}

/* test命令：简化版条件测试 */
static void shell_cmd_test(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: test <expression>\n");
        return;
    }
    /* 简化版：测试文件是否存在 */
    if (argc >= 3 && argv[1][0] == '-' && argv[1][1] == 'f') {
        extern int ext2_64_read_file(const char*, uint8_t*, uint32_t);
        static uint8_t buf[1];
        int ret = ext2_64_read_file(argv[2], buf, 1);
        if (ret > 0) {
            vga_puts("true\n");
        } else {
            vga_puts("false\n");
        }
    } else if (argc >= 3 && argv[1][0] == '-' && argv[1][1] == 'd') {
        /* 测试目录是否存在（简化版） */
        vga_puts("test -d: directory check not fully implemented\n");
    } else if (argc >= 4 && argv[2][0] == '=' && argv[2][1] == '=') {
        /* 字符串比较 */
        int equal = 1;
        for (int i = 0; argv[1][i] || argv[3][i]; i++) {
            if (argv[1][i] != argv[3][i]) { equal = 0; break; }
        }
        vga_puts(equal ? "true\n" : "false\n");
    } else {
        vga_puts("test: unsupported expression\n");
    }
}

/* printf命令：简化版格式化输出 */
static void shell_cmd_printf(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: printf <format> [args...]\n");
        return;
    }
    const char *fmt = argv[1];
    int arg_idx = 2;
    for (int i = 0; fmt[i]; i++) {
        if (fmt[i] == '%' && fmt[i+1]) {
            i++;
            if (fmt[i] == 's' && arg_idx < argc) {
                vga_puts(argv[arg_idx++]);
            } else if (fmt[i] == 'd' && arg_idx < argc) {
                int val = 0;
                for (char *p = argv[arg_idx]; *p; p++)
                    if (*p >= '0' && *p <= '9') val = val * 10 + (*p - '0');
                char num[16];
                shell_itoa(val, num);
                vga_puts(num);
                arg_idx++;
            } else if (fmt[i] == '%') {
                vga_putchar('%');
            } else {
                vga_putchar('%');
                vga_putchar(fmt[i]);
            }
        } else if (fmt[i] == '\\' && fmt[i+1]) {
            i++;
            if (fmt[i] == 'n') vga_putchar('\n');
            else if (fmt[i] == 't') vga_putchar('\t');
            else if (fmt[i] == '\\') vga_putchar('\\');
            else { vga_putchar('\\'); vga_putchar(fmt[i]); }
        } else {
            vga_putchar(fmt[i]);
        }
    }
}

/* expr命令：简化版表达式求值 */
static void shell_cmd_expr(int argc, char **argv)
{
    if (argc < 4) {
        vga_puts("Usage: expr <a> <op> <b>\n");
        return;
    }
    int a = 0, b = 0;
    for (char *p = argv[1]; *p; p++)
        if (*p >= '0' && *p <= '9') a = a * 10 + (*p - '0');
    for (char *p = argv[3]; *p; p++)
        if (*p >= '0' && *p <= '9') b = b * 10 + (*p - '0');
    int result = 0;
    if (argv[2][0] == '+') result = a + b;
    else if (argv[2][0] == '-') result = a - b;
    else if (argv[2][0] == '*') result = a * b;
    else if (argv[2][0] == '/') result = (b != 0) ? a / b : 0;
    else if (argv[2][0] == '%') result = (b != 0) ? a % b : 0;
    else { vga_puts("expr: unsupported operator\n"); return; }
    char num[16];
    shell_itoa(result, num);
    vga_puts(num);
    vga_puts("\n");
}

/* logname命令：显示登录用户名 */
static void shell_cmd_logname(int argc, char **argv)
{
    vga_puts("root\n");
}

/* tty命令：显示终端设备名 */
static void shell_cmd_tty(int argc, char **argv)
{
    vga_puts("/dev/tty1\n");
}

/* id命令：显示用户ID */
static void shell_cmd_id(int argc, char **argv)
{
    vga_puts("uid=0(root) gid=0(root) groups=0(root)\n");
}

/* who命令：显示登录用户 */
static void shell_cmd_who(int argc, char **argv)
{
    vga_puts("root     tty1         2026-10-11 00:00\n");
}

static void shell_cmd_mkdir(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: mkdir <directory>\n");
        return;
    }
    extern int ext2_64_create_dir(const char *path);
    if (ext2_64_create_dir(argv[1]) == 0) {
        vga_puts("Created directory: ");
        vga_puts(argv[1]);
        vga_puts("\n");
    } else {
        vga_puts("mkdir: failed to create '");
        vga_puts(argv[1]);
        vga_puts("'\n");
    }
}

static void shell_cmd_touch(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: touch <file>\n");
        return;
    }
    extern int ext2_64_create_file(const char *path);
    if (ext2_64_create_file(argv[1]) == 0) {
        vga_puts("Created file: ");
        vga_puts(argv[1]);
        vga_puts("\n");
    } else {
        vga_puts("touch: failed to create '");
        vga_puts(argv[1]);
        vga_puts("'\n");
    }
}

static void shell_cmd_rm(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: rm <file>\n");
        return;
    }
    extern int ext2_64_delete_file(const char *path);
    if (ext2_64_delete_file(argv[1]) == 0) {
        vga_puts("Removed: ");
        vga_puts(argv[1]);
        vga_puts("\n");
    } else {
        vga_puts("rm: cannot remove '");
        vga_puts(argv[1]);
        vga_puts("': No such file or directory\n");
    }
}

static void shell_cmd_rmdir(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: rmdir <directory>\n");
        return;
    }
    extern int ext2_64_delete_dir(const char *path);
    int ret = ext2_64_delete_dir(argv[1]);
    if (ret == 0) {
        vga_puts("Removed directory: ");
        vga_puts(argv[1]);
        vga_puts("\n");
    } else if (ret == -2) {
        vga_puts("rmdir: failed to remove '");
        vga_puts(argv[1]);
        vga_puts("': Directory not empty\n");
    } else {
        vga_puts("rmdir: failed to remove '");
        vga_puts(argv[1]);
        vga_puts("'\n");
    }
}

static void shell_cmd_write(int argc, char **argv)
{
    if (argc < 3) {
        vga_puts("Usage: write <file> <content>\n");
        return;
    }
    extern int ext2_64_write_file(const char *path, const uint8_t *data, uint32_t size);
    int len = shell_strlen(argv[2]);
    if (ext2_64_write_file(argv[1], (uint8_t*)argv[2], len) == len) {
        vga_puts("Wrote ");
        char num[8];
        shell_itoa(len, num);
        vga_puts(num);
        vga_puts(" bytes to ");
        vga_puts(argv[1]);
        vga_puts("\n");
    } else {
        vga_puts("write: failed\n");
    }
}

static void shell_cmd_free(int argc, char **argv)
{
    extern void pmm64_get_stats(uint64_t *total, uint64_t *used, uint64_t *free);
    uint64_t total, used, free;
    pmm64_get_stats(&total, &used, &free);

    vga_puts("Memory:\n");
    vga_puts("  Total: ");
    char num[16];
    shell_itoa(total / (1024*1024), num);
    vga_puts(num);
    vga_puts(" MB\n  Used:  ");
    shell_itoa(used / (1024*1024), num);
    vga_puts(num);
    vga_puts(" MB\n  Free:  ");
    shell_itoa(free / (1024*1024), num);
    vga_puts(num);
    vga_puts(" MB\n");
}

/* drivers命令：显示已注册驱动列表 */
static void shell_cmd_drivers(int argc, char **argv)
{
    extern void drv_print_list(void);
    drv_print_list();
}

/* uptime命令：显示系统运行时间 */
static void shell_cmd_uptime(int argc, char **argv)
{
    extern uint64_t pit64_get_uptime(void);
    uint64_t uptime_ms = pit64_get_uptime();
    uint64_t seconds = uptime_ms / 1000;
    uint64_t minutes = seconds / 60;
    uint64_t hours = minutes / 60;
    uint64_t days = hours / 24;

    vga_puts(" up ");
    char num[16];
    if (days > 0) {
        shell_itoa(days, num);
        vga_puts(num);
        vga_puts(" day(s), ");
    }
    shell_itoa(hours % 24, num);
    vga_puts(num);
    vga_puts(":");
    uint64_t mins = minutes % 60;
    if (mins < 10) vga_puts("0");
    shell_itoa(mins, num);
    vga_puts(num);
    vga_puts(":");
    uint64_t secs = seconds % 60;
    if (secs < 10) vga_puts("0");
    shell_itoa(secs, num);
    vga_puts(num);
    vga_puts("\n");
}

/* meminfo命令：显示详细内存信息 */
static void shell_cmd_meminfo(int argc, char **argv)
{
    extern void pmm64_get_stats(uint64_t *total, uint64_t *used, uint64_t *free);
    extern int  drv_get_count(void);
    extern int  drv_get_active_count(void);
    extern int  process64_get_count(void);

    uint64_t total, used, free;
    pmm64_get_stats(&total, &used, &free);

    char num[16];
    vga_puts("=== Memory Information ===\n");

    /* 物理内存 */
    vga_puts("Physical Memory:\n");
    vga_puts("  Total:  ");
    shell_itoa(total / (1024*1024), num);
    vga_puts(num);
    vga_puts(" MB (");
    shell_itoa(total, num);
    vga_puts(num);
    vga_puts(" pages)\n");

    vga_puts("  Used:   ");
    shell_itoa(used / (1024*1024), num);
    vga_puts(num);
    vga_puts(" MB (");
    shell_itoa(used, num);
    vga_puts(num);
    vga_puts(" pages)\n");

    vga_puts("  Free:   ");
    shell_itoa(free / (1024*1024), num);
    vga_puts(num);
    vga_puts(" MB (");
    shell_itoa(free, num);
    vga_puts(num);
    vga_puts(" pages)\n");

    /* 使用百分比 */
    if (total > 0) {
        uint64_t percent = (used * 100) / total;
        vga_puts("  Usage:  ");
        shell_itoa(percent, num);
        vga_puts(num);
        vga_puts("%\n");
    }

    /* 内核堆 */
    vga_puts("\nKernel Heap:\n");
    vga_puts("  Size:   256 KB\n");
    vga_puts("  Allocator: first-fit (linked list)\n");

    /* 系统信息 */
    vga_puts("\nSystem:\n");
    vga_puts("  Page Size: 4096 bytes\n");
    vga_puts("  Processes: ");
    shell_itoa(process64_get_count(), num);
    vga_puts(num);
    vga_puts("\n");
    vga_puts("  Drivers:   ");
    shell_itoa(drv_get_count(), num);
    vga_puts(num);
    vga_puts(" registered, ");
    shell_itoa(drv_get_active_count(), num);
    vga_puts(num);
    vga_puts(" active\n");
    vga_puts("==========================\n");
}

/* pci命令：扫描PCI总线并显示设备 */
static void shell_cmd_pci(int argc, char **argv)
{
    extern uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);

    vga_puts("=== PCI Devices ===\n");
    int found = 0;
    char num[16];

    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int func = 0; func < 8; func++) {
                uint32_t vendor_device = pci_config_read32(bus, dev, func, 0);
                uint16_t vendor = vendor_device & 0xFFFF;
                uint16_t device = (vendor_device >> 16) & 0xFFFF;

                if (vendor == 0xFFFF) continue; /* 无设备 */

                uint32_t class_rev = pci_config_read32(bus, dev, func, 8);
                uint8_t class_code = (class_rev >> 24) & 0xFF;
                uint8_t subclass = (class_rev >> 16) & 0xFF;

                /* 显示设备 */
                vga_puts("  Bus ");
                shell_itoa(bus, num);
                vga_puts(num);
                vga_puts(" Dev ");
                shell_itoa(dev, num);
                vga_puts(num);
                vga_puts(" Func ");
                shell_itoa(func, num);
                vga_puts(num);
                vga_puts(": ");

                /* 显示厂商ID */
                vga_puts("Vendor=0x");
                shell_itoa(vendor, num);
                vga_puts(num);
                vga_puts(" Device=0x");
                shell_itoa(device, num);
                vga_puts(num);
                vga_puts(" Class=0x");
                shell_itoa(class_code, num);
                vga_puts(num);
                vga_puts(".");
                shell_itoa(subclass, num);
                vga_puts(num);
                vga_puts("\n");

                found++;
                if (found >= 16) {
                    vga_puts("  ... (more devices)\n");
                    goto done;
                }
            }
        }
    }
done:
    vga_puts("Total: ");
    shell_itoa(found, num);
    vga_puts(num);
    vga_puts(" devices found\n");
    vga_puts("===================\n");
}

/* cpuinfo命令：显示CPU信息 */
static void shell_cmd_cpuinfo(int argc, char **argv)
{
    char vendor[13] = {0};
    uint32_t eax, ebx, ecx, edx;

    vga_puts("=== CPU Information ===\n");

    /* CPUID 0: 厂商ID */
    asm volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0));
    vendor[0] = ebx & 0xFF;
    vendor[1] = (ebx >> 8) & 0xFF;
    vendor[2] = (ebx >> 16) & 0xFF;
    vendor[3] = (ebx >> 24) & 0xFF;
    vendor[4] = edx & 0xFF;
    vendor[5] = (edx >> 8) & 0xFF;
    vendor[6] = (edx >> 16) & 0xFF;
    vendor[7] = (edx >> 24) & 0xFF;
    vendor[8] = ecx & 0xFF;
    vendor[9] = (ecx >> 8) & 0xFF;
    vendor[10] = (ecx >> 16) & 0xFF;
    vendor[11] = (ecx >> 24) & 0xFF;
    vendor[12] = 0;

    vga_puts("  Vendor: ");
    vga_puts(vendor);
    vga_puts("\n");

    /* CPUID 1: 型号和特性 */
    asm volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1));

    uint8_t family = ((eax >> 8) & 0xF) + ((eax >> 20) & 0xFF);
    uint8_t model = ((eax >> 4) & 0xF) | (((eax >> 16) & 0xF) << 4);
    uint8_t stepping = eax & 0xF;

    char num[16];
    vga_puts("  Family: ");
    shell_itoa(family, num);
    vga_puts(num);
    vga_puts("  Model: ");
    shell_itoa(model, num);
    vga_puts(num);
    vga_puts("  Stepping: ");
    shell_itoa(stepping, num);
    vga_puts(num);
    vga_puts("\n");

    /* 特性标志 */
    vga_puts("  Features: ");
    if (edx & (1 << 0)) vga_puts("FPU ");
    if (edx & (1 << 3)) vga_puts("PSE ");
    if (edx & (1 << 4)) vga_puts("TSC ");
    if (edx & (1 << 6)) vga_puts("PAE ");
    if (edx & (1 << 23)) vga_puts("MMX ");
    if (edx & (1 << 24)) vga_puts("SSE ");
    if (edx & (1 << 25)) vga_puts("SSE2 ");
    if (ecx & (1 << 0)) vga_puts("SSE3 ");
    if (ecx & (1 << 9)) vga_puts("SSSE3 ");
    if (ecx & (1 << 19)) vga_puts("SSE4.1 ");
    if (ecx & (1 << 20)) vga_puts("SSE4.2 ");
    if (ecx & (1 << 26)) vga_puts("XSAVE ");
    if (ecx & (1 << 30)) vga_puts("RDRAND ");
    vga_puts("\n");

    /* 长模式支持 */
    asm volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0x80000001));
    vga_puts("  Long Mode (64-bit): ");
    if (edx & (1 << 29)) {
        vga_puts("Supported\n");
    } else {
        vga_puts("Not supported\n");
    }

    /* SMP信息 */
    extern uint32_t smp_get_cpu_count(void);
    extern uint32_t smp_get_current_cpu(void);
    extern uint64_t smp_get_cpu_ticks(uint32_t cpu_id);
    uint32_t cpu_cnt = smp_get_cpu_count();
    uint32_t cur_cpu = smp_get_current_cpu();
    vga_puts("\n=== SMP Status ===\n");
    vga_puts("  CPU count: ");
    shell_itoa(cpu_cnt, num);
    vga_puts(num);
    vga_puts("\n  Current CPU: ");
    shell_itoa(cur_cpu, num);
    vga_puts(num);
    vga_puts("\n  Per-CPU timer ticks:\n");
    for (uint32_t i = 0; i < cpu_cnt && i < 8; i++) {
        vga_puts("    CPU");
        shell_itoa(i, num);
        vga_puts(num);
        vga_puts(": ");
        shell_itoa((int)smp_get_cpu_ticks(i), num);
        vga_puts(num);
        vga_puts("\n");
    }

    vga_puts("=======================\n");
}

/* netstat命令：显示网络状态 */
static void shell_cmd_netstat(int argc, char **argv)
{
    extern uint32_t eth64_get_ip(void);
    extern int eth64_get_mac(uint8_t *mac);
    extern void net64_get_stats(uint64_t *rx, uint64_t *tx);

    vga_puts("=== Network Status ===\n");

    /* MAC地址 */
    uint8_t mac[6];
    if (eth64_get_mac(mac) == 0) {
        vga_puts("  MAC: ");
        char num[8];
        static const char hex[] = "0123456789ABCDEF";
        for (int i = 0; i < 6; i++) {
            vga_puts((char[]){hex[mac[i] >> 4], hex[mac[i] & 0xF], 0});
            if (i < 5) vga_puts(":");
        }
        vga_puts("\n");
    }

    /* IP地址 */
    uint32_t ip = eth64_get_ip();
    uint8_t *ip_bytes = (uint8_t*)&ip;
    vga_puts("  IP:  ");
    char num[16];
    for (int i = 0; i < 4; i++) {
        shell_itoa(ip_bytes[i], num);
        vga_puts(num);
        if (i < 3) vga_puts(".");
    }
    vga_puts("\n");

    /* 收发统计 */
    uint64_t rx, tx;
    net64_get_stats(&rx, &tx);
    vga_puts("  RX packets: ");
    shell_itoa(rx, num);
    vga_puts(num);
    vga_puts("\n  TX packets: ");
    shell_itoa(tx, num);
    vga_puts(num);
    vga_puts("\n");

    /* 协议栈状态 */
    vga_puts("  Protocols: ETH/ARP/IP/ICMP/UDP/TCP\n");
    vga_puts("  DHCP: Enabled\n");
    vga_puts("======================\n");
}

static void shell_cmd_df(int argc, char **argv)
{
    vga_puts("Filesystem     Size    Used    Available\n");
    vga_puts("/dev/ide0      32MB    2MB     30MB\n");
    vga_puts("/dev/ide1      64MB    1MB     63MB\n");
}

static void shell_cmd_reboot(int argc, char **argv)
{
    vga_puts("Rebooting... (press reset button)\n");
    /* 简化版：停机等待复位 */
    while (1) { asm volatile("hlt"); }
}

/* poweroff命令：ACPI关机（QEMU） */
static void shell_cmd_poweroff(int argc, char **argv)
{
    vga_puts("Shutting down...\n");
    /* QEMU/Bochs ACPI关机端口 */
    __asm__ volatile(
        "movw $0x604, %%dx\n"
        "movl $0x2000, %%eax\n"
        "outl %%eax, %%dx\n"
        : : : "eax", "edx"
    );
    /* 如果关机失败，停机 */
    while (1) { asm volatile("hlt"); }
}

/* env命令：显示环境变量 */
static void shell_cmd_env(int argc, char **argv)
{
    vga_puts("Environment variables:\n");
    vga_puts("  PATH=/bin:/sbin\n");
    vga_puts("  HOME=/root\n");
    vga_puts("  SHELL=/bin/shell\n");
    vga_puts("  USER=root\n");
    vga_puts("  LANG=en_US.UTF-8\n");
    vga_puts("  TERM=linux\n");
}

/* set命令：显示shell设置 */
static void shell_cmd_set(int argc, char **argv)
{
    vga_puts("Shell settings:\n");
    vga_puts("  interactive=yes\n");
    vga_puts("  history_size=100\n");
    vga_puts("  prompt=HepuOS# \n");
    vga_puts("  ignoreeof=no\n");
    vga_puts("  noclobber=no\n");
}

/* alias命令：显示命令别名 */
static void shell_cmd_alias(int argc, char **argv)
{
    vga_puts("Command aliases:\n");
    vga_puts("  ll='ls -la'\n");
    vga_puts("  la='ls -a'\n");
    vga_puts("  l='ls -CF'\n");
    vga_puts("  vi='vim'\n");
    vga_puts("  cp='cp -i'\n");
    vga_puts("  mv='mv -i'\n");
    vga_puts("  rm='rm -i'\n");
}

/* uname -a 完整信息 */
static void shell_cmd_uname_full(int argc, char **argv)
{
    vga_puts("HepuOS hepu 0.10.156 #1 SMP x86_64 GNU/HepuOS\n");
}

/* 命令表 */
typedef struct {
    const char *name;
    void (*func)(int argc, char **argv);
} shell_command_t;

static shell_command_t shell_commands[] = {
    {"help", shell_cmd_help},
    {"usertest", shell_cmd_usertest},
    {"apic", shell_cmd_apic},
    {"exec", shell_cmd_exec},
    {"ls", shell_cmd_ls},
    {"cat", shell_cmd_cat},
    {"echo", shell_cmd_echo},
    {"date", shell_cmd_date},
    {"version", shell_cmd_version},
    {"uname", shell_cmd_uname},
    {"ps", shell_cmd_ps},
    {"ifconfig", shell_cmd_ifconfig},
    {"mkdir", shell_cmd_mkdir},
    {"touch", shell_cmd_touch},
    {"rm", shell_cmd_rm},
    {"rmdir", shell_cmd_rmdir},
    {"cp", shell_cmd_cp},
    {"head", shell_cmd_head},
    {"wc", shell_cmd_wc},
    {"mv", shell_cmd_mv},
    {"tree", shell_cmd_tree},
    {"grep", shell_cmd_grep},
    {"du", shell_cmd_du},
    {"stat", shell_cmd_stat},
    {"cal", shell_cmd_cal},
    {"find", shell_cmd_find},
    {"sleep", shell_cmd_sleep},
    {"yes", shell_cmd_yes},
    {"tail", shell_cmd_tail},
    {"sort", shell_cmd_sort},
    {"uniq", shell_cmd_uniq},
    {"cut", shell_cmd_cut},
    {"tr", shell_cmd_tr},
    {"awk", shell_cmd_awk},
    {"basename", shell_cmd_basename},
    {"sed", shell_cmd_sed},
    {"dirname", shell_cmd_dirname},
    {"xargs", shell_cmd_xargs},
    {"tee", shell_cmd_tee},
    {"nl", shell_cmd_nl},
    {"od", shell_cmd_od},
    {"factor", shell_cmd_factor},
    {"seq", shell_cmd_seq},
    {"sum", shell_cmd_sum},
    {"md5sum", shell_cmd_md5sum},
    {"true", shell_cmd_true},
    {"false", shell_cmd_false},
    {"test", shell_cmd_test},
    {"printf", shell_cmd_printf},
    {"expr", shell_cmd_expr},
    {"logname", shell_cmd_logname},
    {"tty", shell_cmd_tty},
    {"write", shell_cmd_write},
    {"free", shell_cmd_free},
    {"drivers", shell_cmd_drivers},
    {"uptime", shell_cmd_uptime},
    {"meminfo", shell_cmd_meminfo},
    {"pci", shell_cmd_pci},
    {"cpuinfo", shell_cmd_cpuinfo},
    {"netstat", shell_cmd_netstat},
    {"df", shell_cmd_df},
    {"clear", shell_cmd_clear},
    {"reboot", shell_cmd_reboot},
    {"poweroff", shell_cmd_poweroff},
    {"env", shell_cmd_env},
    {"set", shell_cmd_set},
    {"alias", shell_cmd_alias},
    {"uname_a", shell_cmd_uname_full},
    /* 增强命令 */
    {"neofetch", (void*)term64_cmd_neofetch},
    {"top", (void*)term64_cmd_top},
    {"ver", (void*)term64_cmd_ver},
    {"whoami", (void*)term64_cmd_whoami},
    {"hostname", (void*)term64_cmd_hostname},
    {"pwd", (void*)term64_cmd_pwd},
    {"cd", (void*)term64_cmd_cd},
    {"settings", (void*)term64_cmd_settings},
    {"taskmgr", (void*)term64_cmd_taskmgr},
    {"notes", (void*)term64_cmd_notes},
    {"history", (void*)term64_cmd_history},
    {NULL, NULL}
};

/* 执行命令（导出给图形终端调用） */
void shell_execute(char *line)
{
    char *args[SHELL_MAX_ARGS];
    int argc = shell_parse_args(line, args, SHELL_MAX_ARGS);

    if (argc == 0) return;

    for (int i = 0; shell_commands[i].name; i++) {
        if (shell_strcmp(args[0], shell_commands[i].name) == 0) {
            shell_commands[i].func(argc, args);
            return;
        }
    }

    /* 尝试作为外部程序执行 */
    extern int user_process_exec(const char *path);
    if (args[0][0] == '/') {
        int ret = user_process_exec(args[0]);
        if (ret < 0) {
            vga_puts("exec failed: ");
            vga_puts(args[0]);
            vga_putchar('\n');
        }
        return;
    }

    vga_puts("Command not found: ");
    vga_puts(args[0]);
    vga_puts("\nType 'help' for available commands.\n");
}

/* Shell主循环 */
void shell64_run(void)
{
    static char line[SHELL_BUF_SIZE];
    extern int g_kbd_irq_count;

    vga_puts("\nHepuOS 64-bit Shell v1.0\n");
    vga_puts("Type 'help' for commands.\n");
    vga_puts("[DBG] kbd_irq=");
    extern void vga_put_hex(uint64_t);
    vga_put_hex(g_kbd_irq_count);
    vga_puts("\n");

    /* 自动启动用户态程序（尝试执行/bin/hello.elf） */
    vga_puts("[AUTOSTART] Trying /bin/hello.elf...\n");
    int autostart_ret = user_process_exec("/bin/hello.elf");
    if (autostart_ret < 0) {
        vga_puts("[AUTOSTART] No user program found, starting shell\n");
    } else {
        vga_puts("[AUTOSTART] User program exited\n");
    }
    vga_puts("\n");

    /* 网络轮询计数器 */
    int net_poll_counter = 0;

    while (1) {
        /* 每10次循环轮询一次网络（避免影响输入响应） */
        if (++net_poll_counter >= 10) {
            net_poll_counter = 0;
            extern void net64_poll(void);
            extern int dhcp64_poll(void);
            net64_poll();
            dhcp64_poll();
        }

        shell_prompt();
        shell_read_line(line, sizeof(line));
        if (line[0]) {
            shell_execute(line);
        }
    }
}
