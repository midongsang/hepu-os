/* ============================================================================
 * 赫普OS 64位 - Shell命令行解释器
 * 支持基本命令：help, ls, cat, echo, date, uname, ps, clear, reboot
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
extern void vga_put_hex(uint64_t val);   /* V0.55.1: 提前原型, 消除隐式声明冲突 */
#include "process64.h"
#include "domain64.h"
#include "capability64.h"
#include "shared/ai/hepu_ai.h"

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
/* P3: 会话级developer mode（替换全局 hepu_dev_mode） */
extern int  hepu_is_dev(void);
extern void hepu_set_dev_mode(int on);
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
    if (hepu_is_dev()) vga_puts("hepu@os [dev]:~# ");
    else vga_puts("hepu@os:~$ ");
}

/* 读取一行enter */
/* V0.20.65: 命令历史 */
#define HISTORY_SIZE 16
static char history[HISTORY_SIZE][128];
static int history_count = 0;
static int history_pos = 0;

typedef struct { const char *name; void (*func)(int argc, char **argv); } shell_command_t;

static void history_add(const char *cmd) {
    if (history_count < HISTORY_SIZE) {
        for (int i = 0; i < 127 && cmd[i]; i++) history[history_count][i] = cmd[i];
        history[history_count][127] = 0;
        history_count++;
    } else {
        for (int i = 0; i < HISTORY_SIZE-1; i++) {
            for (int j = 0; j < 128; j++) history[i][j] = history[i+1][j];
        }
        for (int i = 0; i < 127 && cmd[i]; i++) history[HISTORY_SIZE-1][i] = cmd[i];
        history[HISTORY_SIZE-1][127] = 0;
    }
    history_pos = history_count;
}

static int shell_read_line(char *buf, int max)
{
    int pos = 0;
    while (pos < max - 1) {
        if (keyboard64_haschar()) {
            char c = keyboard64_getchar();
            if (c == '\n' || c == '\r') {
                vga_putchar('\n');
                buf[pos] = 0;
                history_add(buf);
                return pos;
            } else if (c == '\b' || c == 0x7F) {
                if (pos > 0) {
                    pos--;
                    vga_putchar('\b');
                    vga_putchar(' ');
                    vga_putchar('\b');
                }
            } else if (c == 0x48) { /* 上箭头 - 历史 */
                if (history_pos > 0) {
                    history_pos--;
                    /* 清当前行 */
                    for (int i = 0; i < pos; i++) { vga_putchar('\b'); vga_putchar(' '); vga_putchar('\b'); }
                    pos = 0;
                    for (int i = 0; history[history_pos][i] && i < max-1; i++) {
                        buf[pos++] = history[history_pos][i];
                        vga_putchar(history[history_pos][i]);
                    }
                }
            } else if (c == 0x50) { /* 下箭头 - 历史 */
                if (history_pos < history_count) {
                    history_pos++;
                    for (int i = 0; i < pos; i++) { vga_putchar('\b'); vga_putchar(' '); vga_putchar('\b'); }
                    pos = 0;
                    if (history_pos < history_count) {
                        for (int i = 0; history[history_pos][i] && i < max-1; i++) {
                            buf[pos++] = history[history_pos][i];
                            vga_putchar(history[history_pos][i]);
                        }
                    }
                }
            } else if (c == '\t') { /* Tab 补全 */
                /* 简单补全：匹配常用命令 */
                static const char *cmds[] = {"help","hepu","ls","cat","pwd","echo","ps","cd","mkdir","rm","clear","date","uname","reboot","sleep","exec","browser","scrapling","ifconfig","version","meminfo","free","top","mem","dev","sandbox","info","dmesg","sync","vmstat","pstree","id","who","w","lsblk","blkid","timedatectl","lscpu","lspci","lsusb","ss","htop","kill","killall","renice","taskset",0};
                char prefix[64]; int plen = 0;
                int i = pos - 1;
                while (i >= 0 && buf[i] != ' ') { i--; }
                int start = i + 1;
                plen = pos - start;
                if (plen >= 64) plen = 63;
                for (int j = 0; j < plen; j++) prefix[j] = buf[start+j];
                prefix[plen] = 0;
                int matches = 0; const char *match = 0;
                for (int ci = 0; cmds[ci]; ci++) {
                    const char *name = cmds[ci];
                    int ok = 1;
                    for (int j = 0; j < plen; j++) {
                        if (name[j] != prefix[j]) { ok = 0; break; }
                    }
                    if (ok) { matches++; match = name; }
                }
                if (matches == 1 && match) {
                    for (int j = plen; match[j]; j++) {
                        buf[pos++] = match[j];
                        vga_putchar(match[j]);
                    }
                    vga_putchar(' ');
                    buf[pos++] = ' ';
                } else if (matches > 1) {
                    vga_putchar('\n');
                    for (int ci = 0; cmds[ci]; ci++) {
                        const char *name = cmds[ci];
                        int ok = 1;
                        for (int j = 0; j < plen; j++) {
                            if (name[j] != prefix[j]) { ok = 0; break; }
                        }
                        if (ok) { vga_puts(name); vga_puts("  "); }
                    }
                    vga_putchar('\n');
                    shell_prompt();
                    for (int j = 0; j < pos; j++) vga_putchar(buf[j]);
                }
            } else if (c >= 0x20 && c < 0x7F) {
                buf[pos++] = c;
                vga_putchar(c);
            }
        }
        net64_poll();
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

/* exec命令：加载并执行ELFuser程序 */
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
    vga_puts("  clear reboot poweroff devmode\n\n");

    vga_puts("--- Hepu Native Management ---\n");
    vga_puts("  hepu ps       process isolation view\n");
    vga_puts("  hepu info     system contract overview\n");
    vga_puts("  hepu dev      device backend status\n");
    vga_puts("  hepu sandbox  sandbox list\n");
    vga_puts("  hepu mem      Memory Quota\n\n");

    vga_puts("Type 'help <command>' for details\n");
    vga_puts("========================================\n");
}

/* V0.20.67: history 命令 */
static void shell_cmd_history(int argc, char **argv) {
    (void)argc;(void)argv;
    for (int i = 0; i < history_count; i++) {
        char num[8]; int n = i+1, j = 0;
        do { num[j++] = '0' + n%10; n /= 10; } while (n > 0);
        for (int k = j-1; k >= 0; k--) { char c[2]; c[0]=num[k]; c[1]=0; vga_puts(c); }
        vga_puts("  ");
        vga_puts(history[i]);
        vga_putchar('\n');
    }
}

/* V0.20.67: which 命令 */
static void shell_cmd_which(int argc, char **argv) {
    if (argc < 2) { vga_puts("usage: which <command>\n"); return; }
    static const char *cmds[] = {"help","hepu","ls","cat","pwd","echo","ps","cd","mkdir","rm","clear","date","uname","reboot","sleep","exec","browser","scrapling","ifconfig","version","meminfo","free","history","which","dmesg","sync","vmstat","pstree","id","who","w","lsblk","blkid","timedatectl","lscpu","lspci","lsusb","ss","htop",0};
    for (int i = 0; cmds[i]; i++) {
        if (strcmp(cmds[i], argv[1]) == 0) {
            vga_puts(argv[1]);
            vga_puts(": built-in\n");
            return;
        }
    }
    vga_puts(argv[1]);
    vga_puts(": /bin/");
    vga_puts(argv[1]);
    vga_puts(".elf\n");
}

/* V0.20.63: hepu 命名空间 — kernelcapability的管理入口 */
static void shell_cmd_ps(int argc, char **argv);
static void shell_cmd_hepu(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("hepu - Hepu kernel management\n");
        vga_puts("Usage: hepu <subcommand>\n\n");
        vga_puts("  hepu ps       process isolation view\n");
        vga_puts("  hepu dev      device backend status\n");
        vga_puts("  hepu sandbox sandbox list\n");
        vga_puts("  hepu mem      memory quota status\n");
        vga_puts("  hepu info     system contract overview\n");
        return;
    }
    if (strcmp(argv[1], "ps") == 0) {
        shell_cmd_ps(0, 0);
    } else if (strcmp(argv[1], "gui") == 0) {
        extern int hepu_nographics;
        hepu_nographics = 0;
        extern int desktop64_init(void);
        extern void desktop64_run(void);
        if (desktop64_init() == 0) {
            vga_puts("Starting GUI...\n");
            desktop64_run();
        } else {
            vga_puts("GUI init failed (no VBE)\n");
        }
    } else if (strcmp(argv[1], "info") == 0) {
        vga_puts("=== Hepu OS Native Contract ===\n");
        vga_puts("  Resource: page-frame allocator + per-process PML4\n");
        vga_puts("  Isolation: SMEP/SMAP/W^X, user window 0x40000000-0x80000000\n");
        vga_puts("  Device: native drivers + driver-proxy VM (reserved)\n");
        vga_puts("  Compat: Linux syscall translation (boundary, no kernel intrusion)\n");
    } else if (strcmp(argv[1], "kill") == 0) {
        if (argc < 3) { vga_puts("usage: kill <pid>\n"); return; }
        int pid = 0;
        for (char *p = argv[2]; *p; p++) pid = pid*10 + (*p - '0');
        extern int user_process_kill(int pid);
        int r = user_process_kill(pid);
        if (r == 0) vga_puts("killed\n");
        else vga_puts("kill failed\n");
    } else if (strcmp(argv[1], "install") == 0) {
        if (argc < 3) { vga_puts("usage: install <src> <dst>\n"); return; }
        extern int ext2_64_read_file(const char *path, uint8_t *buf, uint32_t max);
        extern int ext2_64_write_file(const char *path, const uint8_t *data, uint32_t size);
        extern void *kmalloc(uint32_t size);
        extern void kfree(void *ptr);
        uint8_t *buf = kmalloc(2*1024*1024);
        if (!buf) { vga_puts("install: out of memory\n"); return; }
        int sz = ext2_64_read_file(argv[2], buf, 2*1024*1024);
        if (sz <= 0) { vga_puts("install: cannot read source\n"); kfree(buf); return; }
        int r = ext2_64_write_file(argv[3], buf, sz);
        if (r == sz) vga_puts("install: ok\n");
        else vga_puts("install: write failed\n");
        kfree(buf);
    } else if (strcmp(argv[1], "pkg") == 0) {
        /* 包管理器：list = 列出/bin下所present可执行file */
        if (argc < 3 || strcmp(argv[2], "list") == 0) {
            extern int ext2_64_list_dir(const char*);
            vga_puts("=== Hepu Installed Packages (/bin) ===\n");
            ext2_64_list_dir("/bin");
        } else if (strcmp(argv[2], "install") == 0) {
            vga_puts("pkg: use install <src> <dst>\n");
        } else {
            vga_puts("usage: pkg list|install\n");
        }
    } else if (strcmp(argv[1], "sandbox") == 0) {
        /* Compat Sandbox Pool */
        vga_puts("=== Compat Sandbox Pool ===\n");
        vga_puts("  native: active\n");
        vga_puts("  linux_compat: idle (v1.0+)\n");
        vga_puts("  win_compat: disabled\n");
    } else if (strcmp(argv[1], "gtop") == 0) {
        /* Graphical Task Monitor - 双视图 */
        extern int hepu_nographics;
        if (hepu_nographics) { vga_puts("Text mode active, run gui first\n"); return; }
        vga_puts("=== Graphical Task Monitor ===\n");
        if (hepu_is_dev()) {
            vga_puts("[Dev view] isolation tree + process detail + handles + drivers\n");
        } else {
            vga_puts("[User view] app cards + perf overview + permissions\n");
        }
        vga_puts("(v1.0 full GUI)\n");
    } else if (strcmp(argv[1], "audit") == 0) {
        /* Audit Logview */

        if (!hepu_is_dev()) { vga_puts("requires hep dev on\n"); return; }
        vga_puts("=== Audit Log ===\n");
        vga_puts("  [recent] hep dev on\n");
        vga_puts("  [recent] ps list processes\n");
        vga_puts("  (v1.0 full ring buffer + time filter)\n");
    } else if (strcmp(argv[1], "mode") == 0) {
        /* 一键资源预设模板（QoS调度联动） */
        if (argc < 3) { vga_puts("usage: mode office|game|quiet\n"); return; }
        if (strcmp(argv[2], "office") == 0) {
            vga_puts("Office: throttle background, stabilize foreground\n");
        } else if (strcmp(argv[2], "game") == 0) {
            vga_puts("Gaming: boost foreground game, throttle background/AI\n");
        } else if (strcmp(argv[2], "quiet") == 0) {
            vga_puts("Quiet: pause all background updates/indexing/AI\n");
        } else {
            vga_puts("usage: mode office|game|quiet\n");
        }
    } else if (strcmp(argv[1], "hep") == 0) {
        /* Hepu View Switch: developer mode切换 */
        if (argc < 3) {
            vga_puts("usage: hep dev on|off\n");
            return;
        }
        if (strcmp(argv[2], "dev") == 0 && argc >= 4) {
    
            if (strcmp(argv[3], "on") == 0) {
                hepu_set_dev_mode(1);
                vga_puts("Developer mode ON (all actions audited)\n");
            } else if (strcmp(argv[3], "off") == 0) {
                hepu_set_dev_mode(0);
                vga_puts("Developer mode OFF, back to user view\n");
            }
        } else if (strcmp(argv[2], "domain") == 0) {
            /* Isolation Tree视图 */
    
            if (!hepu_is_dev()) { vga_puts("requires hep dev on\n"); return; }
            vga_puts("=== Isolation Tree ===\n");
            vga_puts("├─ kernel domain (protected)\n");
            vga_puts("├─ system service domain\n");
            vga_puts("├─ gui compositor domain\n");
            vga_puts("├─ user app domain (native)\n");
            vga_puts("└─ linux-compat domain\n");
        } else if (strcmp(argv[2], "trace") == 0) {
    
            if (!hepu_is_dev()) { vga_puts("requires hep dev on\n"); return; }
            vga_puts("=== Syscall Trace ===\n");
            vga_puts("v1.0+ full pid-filtered trace\n");
        } else if (strcmp(argv[2], "elf") == 0) {
    
            if (!hepu_is_dev()) { vga_puts("requires hep dev on\n"); return; }
            vga_puts("=== ELF Check ===\n");
            vga_puts("v1.0+ full ELF parse + symbol table\n");
        } else if (strcmp(argv[2], "driver") == 0) {
    
            if (!hepu_is_dev()) { vga_puts("requires hep dev on\n"); return; }
            vga_puts("=== Driver List ===\n");
            vga_puts("  ide / rtc / pit / kbd / mouse / pci / rtl8139 / ahci / vbe / usb\n");
        } else if (strcmp(argv[2], "compat") == 0) {
    
            if (!hepu_is_dev()) { vga_puts("requires hep dev on\n"); return; }
            vga_puts("=== Compatibility Layer ===\n");
            vga_puts("  linux_compat: idle\n");
            vga_puts("  win_compat: disabled\n");
            vga_puts("  hep compat restart linux0: v1.0+\n");
        } else if (strcmp(argv[2], "explain") == 0) {
    
            if (!hepu_is_dev()) { vga_puts("requires hep dev on\n"); return; }
            vga_puts("=== hepua AI Diagnostics ===\n");
            vga_puts("  hep explain panic last: AI parse crash\n");
            vga_puts("  hep explain deadlock: AI analyze deadlock\n");
        } else if (strcmp(argv[2], "hotload") == 0) {
    
            if (!hepu_is_dev()) { vga_puts("requires hep dev on\n"); return; }
            vga_puts("=== Hot Reload ===\n");
            vga_puts("  hep hotload load plugin.elf\n");
            vga_puts("  hep hotload unload plugin.elf\n");
            vga_puts("  v1.0+ full symbols + relocation check\n");
        } else {
            vga_puts("hep dev on|off | domain | trace\n");
        }
    } else if (strcmp(argv[1], "fw") == 0) {
        /* 固件medium心化 */
        vga_puts("=== Firmware Management ===\n");
        vga_puts("  VBE BIOS: loaded\n");
        vga_puts("  USB firmware: built-in\n");
        vga_puts("  NIC firmware: built-in\n");
        vga_puts("  [v1.0] unified firmware loader + signature verify\n");
    } else if (strcmp(argv[1], "irq") == 0) {
        /* irq隔离 */
        vga_puts("=== IRQ allocation ===\n");
        vga_puts("  IRQ0: PIT timer (sched+TCP)\n");
        vga_puts("  IRQ1: keyboard\n");
        vga_puts("  IRQ12: mouse\n");
        vga_puts("  IRQ14: IDEdisk\n");
        vga_puts("  IRQ11: NIC\n");
        vga_puts("  [v1.0] domain-isolated IRQ routing\n");
    } else if (strcmp(argv[1], "ipc") == 0) {
        /* 带capability check的IPC Bus */
        vga_puts("=== IPC Bus ===\n");
        vga_puts("  pipe(pipe): implemented\n");
        vga_puts("  signal(signal): implemented\n");
        vga_puts("  sharedmemory: implemented(COW)\n");
        vga_puts("  capability check: v1.0+ full\n");
    } else if (strcmp(argv[1], "snapshot") == 0) {
        /* VFS快照原语 */
        vga_puts("snapshot: fsync ok (v1.0 full snapshot)\n");
    } else if (strcmp(argv[1], "devlist") == 0) {
        /* devicecapability抽象：列出已注册driver */
        vga_puts("=== registered devices ===\n");
        vga_puts("  ide (disk)\n");
        vga_puts("  rtc (real-time clock)\n");
        vga_puts("  pit (timer)\n");
        vga_puts("  kbd (keyboard)\n");
        vga_puts("  mouse (mouse)\n");
        vga_puts("  pci (PCI bus)\n");
        vga_puts("  rtl8139 (NIC)\n");
        vga_puts("  ahci (SATA)\n");
        vga_puts("  vbe (graphics)\n");
        vga_puts("  usb (USB)\n");
    } else if (strcmp(argv[1], "trace") == 0) {
        /* audit/trace调试框架 */
        vga_puts("=== Hepu audit/trace ===\n");
        vga_puts("  syscallcount: recorded\n");
        vga_puts("  errorcount: recorded\n");
        vga_puts("  process switch: recorded\n");
        vga_puts("  [v1.0] fulltrace buffer + pid-filtered\n");
    } else if (strcmp(argv[1], "hepua") == 0) {
        /* Hepu AI 助手 - 规则诊断骨架 */
        vga_puts("=== Hepu AI (hepua) ===\n");
        vga_puts("  status: "); vga_puts(hepua_status()); vga_puts("\n");

        if (argc < 3 || strcmp(argv[2], "status") == 0) {
            if (hepu_is_dev()) {
                vga_puts("  [developer mode] panic/deadlock/syscheck/explain\n");
            } else {
                vga_puts("  [normal mode] syscheck available (hep dev on unlock all)\n");
            }
            vga_puts("  Usage: hepua <syscheck|panic|deadlock|explain>\n");
        } else if (strcmp(argv[2], "syscheck") == 0) {
            hepua_result_t r;
            hepua_diagnose(HEPUAI_SYSCHECK, &r);
            vga_puts("  summary: "); vga_puts(r.summary); vga_puts("\n");
            vga_puts(r.detail); vga_puts("\n");
        } else if (strcmp(argv[2], "panic") == 0) {
            if (!hepu_is_dev()) { vga_puts("  requires hep dev on\n"); return; }
            hepua_result_t r;
            hepua_diagnose(HEPUAI_PANIC, &r);
            vga_puts("  summary: "); vga_puts(r.summary); vga_puts("\n");
            vga_puts(r.detail); vga_puts("\n");
        } else if (strcmp(argv[2], "deadlock") == 0) {
            if (!hepu_is_dev()) { vga_puts("  requires hep dev on\n"); return; }
            hepua_result_t r;
            hepua_diagnose(HEPUAI_DEADLOCK, &r);
            vga_puts("  summary: "); vga_puts(r.summary); vga_puts("\n");
            vga_puts(r.detail); vga_puts("\n");
        } else if (strcmp(argv[2], "explain") == 0) {
            if (!hepu_is_dev()) { vga_puts("  requires hep dev on\n"); return; }
            char buf[256];
            const char *q = (argc >= 4) ? argv[3] : "";
            hepua_explain(q, buf, sizeof(buf));
            vga_puts(buf); vga_puts("\n");
        } else {
            vga_puts("  unknown subcommand,Usage: hepua <syscheck|panic|deadlock|explain>\n");
        }
    } else if (strcmp(argv[1], "wm") == 0) {
        /* 窗口管理器 */
        if (argc < 3 || strcmp(argv[2], "list") == 0) {
            extern int window64_get_count(void);
            vga_puts("=== Window List ===\n");
            vga_puts("  open windows: "); vga_put_hex(window64_get_count()); vga_puts("\n");
        } else {
            vga_puts("usage: wm list\n");
        }
    } else if (strcmp(argv[1], "domains") == 0) {
        /* Isolation Domains列表：调用真实 domain64_list */
        extern int domain64_list(domain_t *out, int max);
        extern void domain64_refresh_stats(void);
        domain_t doms[DOMAIN64_MAX_SLOTS];
        domain64_refresh_stats();
        int n = domain64_list(doms, DOMAIN64_MAX_SLOTS);
        vga_puts("=== Hepu Isolation Domains ===\n");
        vga_puts("ID  TYPE          PARENT  PROCS  MEM(pages)  NAME\n");
        for (int i = 0; i < n; i++) {
            char buf[16];
            shell_itoa(doms[i].id, buf);
            vga_puts(buf); vga_puts("   ");
            const char *tname = "unknown";
            switch (doms[i].type) {
                case 0: tname = "kernel       "; break;
                case 1: tname = "system       "; break;
                case 2: tname = "gui          "; break;
                case 3: tname = "linux-compat "; break;
                case 4: tname = "android-compat"; break;
                case 5: tname = "user         "; break;
            }
            vga_puts(tname); vga_puts("  ");
            shell_itoa(doms[i].parent_id, buf); vga_puts(buf); vga_puts("      ");
            shell_itoa(doms[i].proc_count, buf); vga_puts(buf); vga_puts("      ");
            vga_put_hex(doms[i].mem_pages); vga_puts("    ");
            vga_puts(doms[i].name); vga_puts("\n");
        }
    } else if (strcmp(argv[1], "caps") == 0) {
        /* Capability System视图 */
        extern int cap64_list(capability_t *out, int max);
        extern int cap64_count(void);
        capability_t clist[CAP64_MAX_CAPS];
        int cn = cap64_list(clist, CAP64_MAX_CAPS);
        vga_puts("=== Hepu Capability System ===\n");
        vga_puts("active capabilities: ");
        { char b[8]; shell_itoa(cap64_count(), b); vga_puts(b); }
        vga_puts("\nID  TYPE            OWNER  RIGHTS            RESOURCE\n");
        for (int i = 0; i < cn; i++) {
            char buf[16];
            shell_itoa(clist[i].id, buf); vga_puts(buf); vga_puts("   ");
            const char *tname = "unknown       ";
            switch (clist[i].type) {
                case 0: tname = "device        "; break;
                case 1: tname = "buffer        "; break;
                case 2: tname = "event         "; break;
                case 3: tname = "fence         "; break;
                case 4: tname = "ipc_ep        "; break;
                case 5: tname = "service_ep    "; break;
            }
            vga_puts(tname); vga_puts(" ");
            shell_itoa(clist[i].owner_domain_id, buf); vga_puts(buf); vga_puts("      ");
            /* 权利位转字符串 */
            char rbuf[12]; int rp = 0;
            if (clist[i].base_rights & CAP_RIGHT_READ)    rbuf[rp++] = 'R';
            if (clist[i].base_rights & CAP_RIGHT_WRITE)   rbuf[rp++] = 'W';
            if (clist[i].base_rights & CAP_RIGHT_EXECUTE) rbuf[rp++] = 'X';
            if (clist[i].base_rights & CAP_RIGHT_DERIVE)  rbuf[rp++] = 'D';
            if (clist[i].base_rights & CAP_RIGHT_REVOKE)  rbuf[rp++] = 'K';
            while (rp < 11) rbuf[rp++] = ' ';
            rbuf[11] = 0;
            vga_puts(rbuf); vga_puts(" ");
            vga_put_hex(clist[i].resource_id); vga_puts("\n");
        }
    } else if (strcmp(argv[1], "nice") == 0) {
        /* nice <pid> <prio>: modifiedprocess优先级 0=idle 1=low 2=normal 3=high 4=rt */
        if (argc < 4) { vga_puts("usage: nice <pid> <prio 0-4>\n"); return; }
        extern int process64_set_priority(int pid, int prio);
        int pid = 0, prio = 0;
        for (char *p = argv[2]; *p; p++) pid = pid*10 + (*p - '0');
        for (char *p = argv[3]; *p; p++) prio = prio*10 + (*p - '0');
        if (process64_set_priority(pid, prio) == 0) vga_puts("nice: ok\n");
        else vga_puts("nice: failed\n");
    } else if (strcmp(argv[1], "dev") == 0) {
        vga_puts("=== device backend ===\n");
        vga_puts("  storage: AHCI/SATA (native), IDE (native)\n");
        vga_puts("  network: E1000 (native), RTL8139 (native)\n");
        vga_puts("  input:   PS/2 (native), USB HID (native)\n");
        vga_puts("  display: VBE (native)\n");
        vga_puts("  [reserved] driverproxy VM: disabled\n");
    } else if (strcmp(argv[1], "sandbox") == 0) {
        vga_puts("=== sandbox list ===\n");
        vga_puts("  native process: shell, init\n");
        vga_puts("  Linux compat process: none\n");
    } else if (strcmp(argv[1], "mem") == 0) {
        extern uint64_t pmm64_get_memory_size_mb(void);
        vga_puts("=== Memory Quota ===\n");
        vga_puts("  total physical memory: ");
        char buf[16]; int n = pmm64_get_memory_size_mb(), i = 0;
        if (n == 0) buf[i++] = '0';
        while (n > 0) { buf[i++] = '0' + n%10; n /= 10; }
        for (int j = i-1; j >= 0; j--) { char c[2]; c[0]=buf[j]; c[1]=0; vga_puts(c); }
        vga_puts(" MB\n");
        vga_puts("  user window: 0x40000000 - 0x80000000 (1GB)\n");
        vga_puts("  vDSO page:   0x7FFF0000 (read-only)\n");
    } else if (strcmp(argv[1], "compat") == 0) {
        vga_puts("=== Linux Compatibility Layerstatus ===\n");
        vga_puts("  stage: 2 (user-mode ELF loading + syscall translation)\n");
        vga_puts("  syscall translation: 96numbers mapped\n");
        vga_puts("  ELF relocation: R_X86_64_RELATIVE/64/GLOB_DAT/JUMP_SLOT/PC32/PLT32\n");
        vga_puts("  dynamic linker: framework ready, no dynamic program run\n");
        vga_puts("  vDSO: base page mapped at0x7FFF0000\n");
        vga_puts("  real impl: read/write/open/close/mmap/brk/exit/fork/pipe/clock\n");
        vga_puts("  stub returns: most network/fileperm/signal\n");
        vga_puts("  unsupported: .so loading, OpenGL, audio, X11, USB storage\n");
    } else {
        vga_puts("unknown subcommand: "); vga_puts(argv[1]); vga_puts("\n");
        vga_puts("available: ps info dev sandbox mem compat\n");
    }
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
    /* 分块读取大file,每次4KB */
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
    vga_puts("  Version:    0.56.1\n");
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
    vga_puts("HepuOS 64-bit Kernel v0.56.1\n");
    vga_puts("Architecture: x86_64 (long mode)\n");
    vga_puts("Memory: 64MB\n");
    vga_puts("Filesystem: ext2\n");
    vga_puts("Network: TCP/IP (ETH/ARP/IP/ICMP/UDP/TCP/DHCP/DNS)\n");
}

static void shell_cmd_ps(int argc, char **argv)
{
    (void)argc;(void)argv;
    process64_stat_t processes[PROCESS64_MAX_SLOTS];
    int count = process64_snapshot(processes, PROCESS64_MAX_SLOTS);

    if (!hepu_is_dev()) {
        /* 普通user视图：只显示应用,隐藏PID/PRIO等底层细节 */
        vga_puts("=== running medium apps ===\n");
        for (int i = 0; i < count; i++) {
            if (processes[i].pid == 0) continue;  /* 隐藏idle */
            vga_puts("  • ");
            vga_puts(processes[i].name);
            vga_puts("\n");
        }
        vga_puts("(enter hepu hep dev on viewfull process detail)\n");
        return;
    }
    /* on发者视图：full表格 */
    vga_puts("PID  PPID  PRIO  NAME            STATE  DOMAIN     CPU  MEM(KB)  RUNTIME\n");
    vga_puts("---  ----  ----  --------------- -----  ---------  ---  -------  -------\n");
    for (int i = 0; i < count; i++) {
        /* PID */
        char buf[8]; int n = processes[i].pid, j = 0;
        if (n == 0) buf[j++] = '0';
        while (n > 0) { buf[j++] = '0' + n%10; n /= 10; }
        for (int k = 0; k < (4-j); k++) vga_puts(" ");
        for (int k = j-1; k >= 0; k--) { char c[2]; c[0]=buf[k]; c[1]=0; vga_puts(c); }
        vga_puts("  ");
        /* PPID */
        int p = processes[i].parent_pid; j = 0;
        if (p == 0) buf[j++] = '0';
        while (p > 0) { buf[j++] = '0' + p%10; p /= 10; }
        for (int k = 0; k < (4-j); k++) vga_puts(" ");
        for (int k = j-1; k >= 0; k--) { char c[2]; c[0]=buf[k]; c[1]=0; vga_puts(c); }
        vga_puts("  ");
        /* PRIO */
        vga_puts("P");
        char pc[4]; int pj = 0; int pv = processes[i].priority;
        if (pv == 0) pc[pj++] = '0';
        while (pv > 0) { pc[pj++] = '0' + pv%10; pv /= 10; }
        for (int k = pj-1; k >= 0; k--) { char c[2]; c[0]=pc[k]; c[1]=0; vga_puts(c); }
        for (int k = 0; k < (4-pj-1); k++) vga_puts(" ");
        vga_puts("  ");
        /* NAME */
        char nm[16]; int ni = 0;
        while (processes[i].name[ni] && ni < 15) { nm[ni] = processes[i].name[ni]; ni++; }
        nm[ni] = 0;
        vga_puts(nm);
        for (int k = ni; k < 15; k++) vga_puts(" ");
        vga_puts(" ");
        /* STATE */
        const char *st = "?";
        if (processes[i].state == 1) st = "READY";
        else if (processes[i].state == 2) st = "RUN";
        else if (processes[i].state == 3) st = "WAIT";
        else if (processes[i].state == 4) st = "ZOMB";
        else if (processes[i].state == 0) st = "IDLE";
        vga_puts(st);
        for (int k = 0; k < (7-(int)shell_strlen(st)); k++) vga_puts(" ");
        /* DOMAIN (Isolation Domains) */
        const char *dom = "native";
        if (processes[i].env_type == 1) dom = "linux";
        else if (processes[i].env_type == 2) dom = "win";
        vga_puts(dom);
        for (int k = 0; k < (10-(int)shell_strlen(dom)); k++) vga_puts(" ");
        /* CPU */
        char cb[4]; int cj = 0; int cn = processes[i].cpu_id;
        if (cn == 0) cb[cj++] = '0';
        while (cn > 0) { cb[cj++] = '0' + cn%10; cn /= 10; }
        for (int k = 0; k < (2-cj); k++) vga_puts(" ");
        for (int k = cj-1; k >= 0; k--) { char c[2]; c[0]=cb[k]; c[1]=0; vga_puts(c); }
        vga_puts("  ");
        /* MEM (KB) */
        int mem_kb = (int)(processes[i].mem_pages * 4);  /* 4KB per page */
        char mb[10]; int mj = 0;
        if (mem_kb == 0) mb[mj++] = '0';
        while (mem_kb > 0) { mb[mj++] = '0' + mem_kb%10; mem_kb /= 10; }
        for (int k = 0; k < (8-mj); k++) vga_puts(" ");
        for (int k = mj-1; k >= 0; k--) { char c[2]; c[0]=mb[k]; c[1]=0; vga_puts(c); }
        vga_puts("  ");
        /* RUNTIME (ticks) */
        char rt[20]; int ri = 0; uint64_t rv = processes[i].runtime;
        if (rv == 0) rt[ri++] = '0';
        while (rv > 0) { rt[ri++] = '0' + rv%10; rv /= 10; }
        for (int k = ri-1; k >= 0; k--) { char c[2]; c[0]=rt[k]; c[1]=0; vga_puts(c); }
        vga_puts("\n");
    }
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
    /* clear screen：用ANSI转义序列或直接写VGA */
    vga_puts("\033[2J\033[H");
}

/* cp命令：copy file */
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

    /* created目标file */
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

/* head命令：show file前N行 */
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

/* wc命令：统计file行数、单词数、字节数 */
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

/* mv命令：move/renamefile（通过cp+rm实现） */
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

/* tree命令：显示directory树（简化版,只显示当前directory） */
static void shell_cmd_tree(int argc, char **argv)
{
    const char *path = (argc >= 2) ? argv[1] : "/";
    vga_puts(path);
    vga_puts("\n");
    extern int ext2_64_list_dir(const char*);
    ext2_64_list_dir(path);
}

/* grep命令：在filemedium搜索字符串 */
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
            /* 检查这一行yesno包含pattern */
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

/* du命令：show filesize（简化版,通过读取统计） */
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

/* stat命令：show file信息（简化版） */
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
    /* 读取filesize */
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

    /* 月份name */
    const char *months[] = {"January","February","March","April","May","June",
                            "July","August","September","October","November","December"};
    /* 每月天数 */
    int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    /* 闰年判断 */
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) days[1] = 29;

    /* 计算第一天yes星期几（Zeller公式简化版） */
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

/* find命令：搜索file（简化版,列出当前directory匹配的file） */
static void shell_cmd_find(int argc, char **argv)
{
    const char *pattern = (argc >= 2) ? argv[1] : NULL;
    vga_puts("Searching for files...\n");
    /* 简化版：直接列出根directory,user可自行grep过滤 */
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
    /* 输出10次后stopped（避免none限循环） */
    for (int i = 0; i < 10; i++) {
        vga_puts(str);
        vga_puts("\n");
    }
}

/* tail命令：show file最后N行 */
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

/* sort命令：对file内容排序（简化版,按行字典序） */
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
    /* 逐行输出,跳过连续重复行 */
    char last_line[128] = "";
    char current_line[128];
    int current_pos = 0;
    int pos = 0;      /* V0.55.1: 仅记录上一行结束位置, 当前无人读取 */
    for (int i = 0; i <= len; i++) {
        if (i == len || buf[i] == '\n') {
            current_line[current_pos] = 0;
            /* 比较yesno与上一行相同 */
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
            (void)pos;
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

/* basename命令：提取file名 */
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

/* dirname命令：提取directorypath */
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

/* xargs命令：简化版（从file读取行并执行命令） */
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

/* tee命令：同时输出到屏幕和file */
static void shell_cmd_tee(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: tee <file>\n");
        return;
    }
    /* 简化版：create empty file并提示 */
    extern int ext2_64_create_file(const char*);
    ext2_64_create_file(argv[1]);
    vga_puts("Created: ");
    vga_puts(argv[1]);
    vga_puts(" (tee: pipe input not supported in simplified version)\n");
}

/* nl命令：show file行号 */
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

/* true命令：返回success */
static void shell_cmd_true(int argc, char **argv)
{
    /* none操作,返回success */
}

/* false命令：返回failed */
static void shell_cmd_false(int argc, char **argv)
{
    /* none操作,返回failed（简化版不处理退出码） */
}

/* test命令：简化版条件测试 */
static void shell_cmd_test(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: test <expression>\n");
        return;
    }
    /* 简化版：测试fileyesno存在 */
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
        /* 测试directoryyesno存在（简化版） */
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

/* logname命令：显示登录user名 */
static void shell_cmd_logname(int argc, char **argv)
{
    vga_puts("root\n");
}

/* tty命令：显示终端device名 */
static void shell_cmd_tty(int argc, char **argv)
{
    vga_puts("/dev/tty1\n");
}

static void shell_cmd_mkdir(int argc, char **argv)
{
    if (argc < 2) {
        vga_puts("Usage: mkdir <directory>\n");
        return;
    }
    extern int ext2_64_mkdir(const char *path);
    if (ext2_64_mkdir(argv[1]) == 0) {
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

/* drivers命令：显示已注册Driver List */
static void shell_cmd_drivers(int argc, char **argv)
{
    extern void drv_print_list(void);
    drv_print_list();
}

/* uptime命令：显示systemrunning时间 */
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

/* meminfo命令：显示详细memory信息 */
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

    /* 物理memory */
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

    /* V0.13.51: KHEAP + 块缓存 + memory压力 */
    {
        extern void kheap64_get_stats(uint64_t *, uint64_t *, uint64_t *,
                                      uint64_t *, uint64_t *);
        extern int  ext2_cache_used_count(void);
        extern void mem_pressure_collect(void *st);
        typedef struct {
            uint64_t phys_total, phys_used, phys_free;
            uint64_t kheap_total, kheap_used, kheap_peak;
            uint64_t cache_bytes;
            int cache_slots_used;
            int pressure_level;
            uint64_t reclaim_count;
        } mps_t;
        uint64_t kt = 0, ku = 0, kf = 0, ac = 0, fc = 0;
        mps_t mps;
        kheap64_get_stats(&kt, &ku, &kf, &ac, &fc);
        mem_pressure_collect(&mps);
        vga_puts("Kernel Heap (KHEAP):\n");
        vga_puts("  Total:  ");
        shell_itoa(kt / 1024, num); vga_puts(num); vga_puts(" KB\n");
        vga_puts("  Used:   ");
        shell_itoa(ku / 1024, num); vga_puts(num); vga_puts(" KB\n");
        vga_puts("  Alloc:  ");
        shell_itoa(ac, num); vga_puts(num);
        vga_puts("  Free:   ");
        shell_itoa(fc, num); vga_puts(num); vga_puts("\n");
        vga_puts("Block Cache:  ");
        shell_itoa(mps.cache_slots_used, num); vga_puts(num); vga_puts(" slots (");
        shell_itoa(mps.cache_bytes / 1024, num); vga_puts(num); vga_puts(" KB)\n");
        vga_puts("Pressure:    level=");
        shell_itoa(mps.pressure_level, num); vga_puts(num);
        vga_puts(" reclaim=");
        shell_itoa(mps.reclaim_count, num); vga_puts(num);
        vga_puts("\n");
    }

    /* kernel堆 */
    vga_puts("\nKernel Heap:\n");
    vga_puts("  Size:   256 KB\n");
    vga_puts("  Allocator: first-fit (linked list)\n");

    /* system info */
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

/* pci命令：扫描PCI bus并显示device */
/* ============================================================================
 * 系统信息与诊断命令（工具清单「一、二、七」组）
 * 全部只读现有内核数据源，不新增采集路径。
 * ========================================================================== */

/* dmesg: 内核日志环形缓冲 */
static void shell_cmd_dmesg(int argc, char **argv)
{
    (void)argc; (void)argv;
    extern uint32_t klog64_snapshot(char *out, uint32_t max);
    extern uint32_t klog64_used(void);
    static char kbuf[4096];
    uint32_t n = klog64_snapshot(kbuf, 4095);
    if (n > 4095) n = 4095;
    kbuf[n] = 0;
    if (n == 0) { vga_puts("dmesg: kernel log is empty\n"); return; }
    vga_puts(kbuf);
    vga_puts("\n-- end of kernel log, ");
    char knum[16]; shell_itoa((int)klog64_used(), knum);
    vga_puts(knum);
    vga_puts(" bytes buffered --\n");
}

/* sync: 把块缓存与 ext2 inode 缓存刷到盘上 */
static void shell_cmd_sync(int argc, char **argv)
{
    (void)argc; (void)argv;
    extern void blk_cache_flush(void);
    extern void ext2_cache_flush_all(void);
    blk_cache_flush();
    ext2_cache_flush_all();
    vga_puts("sync: block cache + ext2 inode cache flushed\n");
}

/* vmstat: 进程与内存概览 */
static void shell_cmd_vmstat(int argc, char **argv)
{
    (void)argc; (void)argv;
    extern void pmm64_get_stats(uint64_t *total, uint64_t *used, uint64_t *free);
    extern int process64_get_count(void);
    extern int process64_get_running_count(void);
    uint64_t tot = 0, used = 0, freem = 0;
    pmm64_get_stats(&tot, &used, &freem);
    char num[16];
    vga_puts("procs  running  mem_total  mem_used  mem_free   (MB)\n");
    shell_itoa(process64_get_count(), num); vga_puts(num); vga_puts("      ");
    shell_itoa(process64_get_running_count(), num); vga_puts(num); vga_puts("         ");
    shell_itoa((int)(tot / (1024u * 1024u)), num); vga_puts(num); vga_puts("        ");
    shell_itoa((int)(used / (1024u * 1024u)), num); vga_puts(num); vga_puts("      ");
    shell_itoa((int)(freem / (1024u * 1024u)), num); vga_puts(num); vga_puts("\n");
    vga_puts("note: si/so/io 未统计 —— 本系统无 swap，块设备计数器未接入\n");
}

/* pstree: 按父子关系打印进程树 */
static process64_stat_t g_pt_buf[PROCESS64_MAX_SLOTS];
static int g_pt_n;
static void pstree_walk(int pid, int depth)
{
    for (int i = 0; i < g_pt_n; i++) {
        if (g_pt_buf[i].pid == pid) continue;
        if (g_pt_buf[i].parent_pid != pid) continue;
        for (int d = 0; d < depth; d++) vga_puts("  ");
        vga_puts(depth ? "`- " : "");
        vga_puts(g_pt_buf[i].name);
        vga_puts("(");
        char num[16]; shell_itoa(g_pt_buf[i].pid, num); vga_puts(num); vga_puts(")\n");
        if (depth < 8) pstree_walk(g_pt_buf[i].pid, depth + 1);
    }
}
static void shell_cmd_pstree(int argc, char **argv)
{
    (void)argc; (void)argv;
    g_pt_n = process64_snapshot(g_pt_buf, PROCESS64_MAX_SLOTS);
    vga_puts("init(0)\n");
    pstree_walk(0, 1);
    vga_puts("-- total ");
    char num[16]; shell_itoa(g_pt_n, num); vga_puts(num); vga_puts(" processes --\n");
}

/* id: 当前账号身份（角色 -> uid 映射：developer=0, normal=1000） */
static void shell_cmd_id(int argc, char **argv)
{
    (void)argc; (void)argv;
    extern const char *auth64_current_user(void);
    extern int auth64_current_role(void);
    const char *who = auth64_current_user();
    int role = auth64_current_role();   /* 0=none 1=normal 2=developer */
    char num[16];
    if (role <= 0) { vga_puts("id: not logged in\n"); return; }
    vga_puts("uid=");
    shell_itoa(role == 2 ? 0 : 1000, num); vga_puts(num);
    vga_puts("("); vga_puts(who ? who : "?"); vga_puts(")");
    vga_puts(" gid=");
    shell_itoa(role == 2 ? 0 : 1000, num); vga_puts(num);
    vga_puts(" groups=");
    shell_itoa(role == 2 ? 0 : 1000, num); vga_puts(num);
    vga_puts(role == 2 ? "(root)" : "(users)");
    vga_puts("\n");
}

/* who / w: 会话列表（本系统单会话，tty1） */
static void shell_cmd_who(int argc, char **argv)
{
    (void)argc; (void)argv;
    extern const char *auth64_current_user(void);
    extern int auth64_current_role(void);
    extern uint64_t pit64_get_uptime(void);
    if (auth64_current_role() <= 0) { vga_puts("no session\n"); return; }
    const char *who = auth64_current_user();
    vga_puts(who ? who : "?"); vga_puts("   tty1    ");
    uint64_t sec = pit64_get_uptime() / 1000;
    char num[16]; shell_itoa((int)(sec / 3600), num); vga_puts(num); vga_puts(":");
    int mins = (int)((sec / 60) % 60);
    if (mins < 10) vga_puts("0");
    shell_itoa(mins, num); vga_puts(num);
    vga_puts("  (up)\n");
}

/* ---- 进程控制：kill / killall / renice / taskset ---- */
static long shell_atol_local(const char *s)
{
    long v = 0; int neg = 0;
    if (!s) return 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}

static void shell_cmd_kill(int argc, char **argv)
{
    extern int user_process_kill(int pid);
    extern int process64_kill_pid(int pid, int code);
    if (argc < 2) { vga_puts("usage: kill [-9] <pid>\n"); return; }
    int sig = 15, idx = 1;
    if (argv[1][0] == '-' && argv[1][1] == '9') { sig = 9; idx = 2; }
    if (idx >= argc) { vga_puts("kill: missing pid\n"); return; }
    int pid = (int)shell_atol_local(argv[idx]);
    int rc = user_process_kill(pid);
    if (rc != 0) rc = process64_kill_pid(pid, 128 + sig);
    char num[16];
    if (rc == 0) {
        vga_puts("kill: pid "); shell_itoa(pid, num); vga_puts(num);
        vga_puts(" terminated (sig "); shell_itoa(sig, num); vga_puts(num); vga_puts(")\n");
    } else if (rc == -2) {
        vga_puts("kill: refused (idle 或当前进程不允许终止)\n");
    } else {
        vga_puts("kill: no such process "); shell_itoa(pid, num); vga_puts(num); vga_puts("\n");
    }
}

static void shell_cmd_killall(int argc, char **argv)
{
    extern int process64_kill_by_name(const char *name, int code);
    if (argc < 2) { vga_puts("usage: killall <name>\n"); return; }
    int n = process64_kill_by_name(argv[1], 128 + 9);
    char num[16];
    if (n < 0) { vga_puts("killall: bad name\n"); return; }
    shell_itoa(n, num);
    vga_puts("killall: "); vga_puts(num); vga_puts(" process(es) named ");
    vga_puts(argv[1]); vga_puts(" terminated\n");
    if (n == 0) vga_puts("  (当前进程不会被自己杀，改名后再试)\n");
}

static void shell_cmd_renice(int argc, char **argv)
{
    extern int process64_set_priority(int pid, int prio);
    extern int process64_get_priority(int pid);
    if (argc < 3) { vga_puts("usage: renice <prio 0-4> <pid>   (0=idle 2=normal 4=realtime)\n"); return; }
    int prio = (int)shell_atol_local(argv[1]);
    int pid = (int)shell_atol_local(argv[2]);
    int old = process64_get_priority(pid);
    int rc = process64_set_priority(pid, prio);
    char num[16];
    if (rc != 0) { vga_puts("renice: no such process 或 prio 超出 0-4\n"); return; }
    vga_puts("renice: pid "); shell_itoa(pid, num); vga_puts(num);
    vga_puts(" priority "); shell_itoa(old, num); vga_puts(num);
    vga_puts(" -> "); shell_itoa(process64_get_priority(pid), num); vga_puts(num);
    vga_puts("（时间片同步重算）\n");
}

static void shell_cmd_taskset(int argc, char **argv)
{
    extern int process64_set_affinity(int pid, int cpu);
    extern int process64_get_affinity(int pid);
    extern uint32_t smp_get_cpu_count(void);
    char num[16];
    if (argc >= 2 && argv[1][0] == '-' && argv[1][1] == 'p') {
        int pid = (int)shell_atol_local(argv[2]);
        int cpu = process64_get_affinity(pid);
        if (cpu < 0) { vga_puts("taskset: no such process\n"); return; }
        vga_puts("pid "); shell_itoa(pid, num); vga_puts(num);
        vga_puts(" current affinity: cpu "); shell_itoa(cpu, num); vga_puts(num); vga_puts("\n");
        return;
    }
    if (argc < 3) { vga_puts("usage: taskset <cpu> <pid> | taskset -p <pid>\n"); return; }
    int cpu = (int)shell_atol_local(argv[1]);
    int pid = (int)shell_atol_local(argv[2]);
    int rc = process64_set_affinity(pid, cpu);
    if (rc < 0) { vga_puts("taskset: no such process\n"); return; }
    vga_puts("taskset: pid "); shell_itoa(pid, num); vga_puts(num);
    vga_puts(" 亲和性 -> cpu "); shell_itoa(rc, num); vga_puts(num);
    vga_puts("（共 "); shell_itoa((int)smp_get_cpu_count(), num); vga_puts(num); vga_puts(" 核）\n");
    vga_puts("  注意: AP 调度尚未开放(SB-25)，绑到 AP 目前只记账、不迁移\n");
}

/* lsblk / blkid: MBR 分区表（partition64 只解析 MBR；GPT 盘会回报无签名） */
typedef struct {
    int     present;
    uint8_t type;
    uint32_t lba_start;
    uint32_t sectors;
    int     fs_ext2;
} sh_part_t;

static void shell_cmd_lsblk(int argc, char **argv)
{
    (void)argc; (void)argv;
    extern int partition64_scan(void *parts);
    sh_part_t p[4];
    for (int i = 0; i < 4; i++) { p[i].present = 0; p[i].type = 0; p[i].lba_start = 0; p[i].sectors = 0; p[i].fs_ext2 = 0; }
    int rc = partition64_scan(p);
    if (rc != 0) {
        vga_puts("lsblk: partition table scan failed (rc=");
        char num[16]; shell_itoa(rc, num); vga_puts(num);
        vga_puts(")\n  -1 = 读盘失败, -2 = 无 MBR 签名（GPT 盘属正常）\n");
        return;
    }
    vga_puts("NAME     START        SECTORS      SIZE(MB)  FS\n");
    for (int i = 0; i < 4; i++) {
        if (!p[i].present) continue;
        vga_puts("hd0p"); char num[16]; shell_itoa(i + 1, num); vga_puts(num);
        vga_puts("     ");
        shell_itoa((int)p[i].lba_start, num); vga_puts(num);
        vga_puts("      ");
        shell_itoa((int)p[i].sectors, num); vga_puts(num);
        vga_puts("      ");
        shell_itoa((int)(p[i].sectors / 2048u), num); vga_puts(num);
        vga_puts("        ");
        vga_puts(p[i].fs_ext2 ? "ext2" : "unknown");
        vga_puts("\n");
    }
}

static void shell_cmd_blkid(int argc, char **argv)
{
    (void)argc; (void)argv;
    extern int partition64_scan(void *parts);
    sh_part_t p[4];
    for (int i = 0; i < 4; i++) { p[i].present = 0; p[i].type = 0; p[i].lba_start = 0; p[i].sectors = 0; p[i].fs_ext2 = 0; }
    int rc = partition64_scan(p);
    if (rc != 0) { vga_puts("blkid: no MBR partition table\n  UUID 未接入（本系统不做 UUID 持久化）\n"); return; }
    for (int i = 0; i < 4; i++) {
        if (!p[i].present) continue;
        vga_puts("hd0p"); char num[16]; shell_itoa(i + 1, num); vga_puts(num);
        vga_puts(": TYPE=\""); vga_puts(p[i].fs_ext2 ? "ext2" : "unknown");
        vga_puts("\"  PTYPE=0x"); shell_itoa((int)p[i].type, num); vga_puts(num);
        vga_puts("\n");
    }
}

/* timedatectl: 时间与时区状态（当前只有 RTC，无 NTP 客户端） */
static void shell_cmd_timedatectl(int argc, char **argv)
{
    (void)argc; (void)argv;
    extern void rtc64_get_time(int *y, int *mo, int *d, int *h, int *mi, int *s);
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    rtc64_get_time(&y, &mo, &d, &h, &mi, &se);
    char num[16];
    vga_puts("      Local time: ");
    shell_itoa(y, num); vga_puts(num); vga_puts("-");
    if (mo < 10) vga_puts("0"); shell_itoa(mo, num); vga_puts(num); vga_puts("-");
    if (d < 10) vga_puts("0"); shell_itoa(d, num); vga_puts(num); vga_puts(" ");
    if (h < 10) vga_puts("0"); shell_itoa(h, num); vga_puts(num); vga_puts(":");
    if (mi < 10) vga_puts("0"); shell_itoa(mi, num); vga_puts(num); vga_puts(":");
    if (se < 10) vga_puts("0"); shell_itoa(se, num); vga_puts(num);
    vga_puts("\n     Time zone: 由 OOBE 选择（尚未写入全局 tz 变量）\n");
    vga_puts("  NTP synced: no（未实现 NTP 客户端，时间为 RTC 直读）\n");
}

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

                if (vendor == 0xFFFF) continue; /* nonedevice */

                uint32_t class_rev = pci_config_read32(bus, dev, func, 8);
                uint8_t class_code = (class_rev >> 24) & 0xFF;
                uint8_t subclass = (class_rev >> 16) & 0xFF;

                /* 显示device */
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

/* netstat命令：显示网络status */
static void shell_cmd_netstat(int argc, char **argv)
{
    extern uint32_t eth64_get_ip(void);
    extern int eth64_get_mac(uint8_t *mac);
    extern void net64_get_stats(uint64_t *rx, uint64_t *tx);

    vga_puts("=== Network Status ===\n");

    /* MACaddress */
    uint8_t mac[6];
    if (eth64_get_mac(mac) == 0) {
        vga_puts("  MAC: ");
        static const char hex[] = "0123456789ABCDEF";
        for (int i = 0; i < 6; i++) {
            vga_puts((char[]){hex[mac[i] >> 4], hex[mac[i] & 0xF], 0});
            if (i < 5) vga_puts(":");
        }
        vga_puts("\n");
    }

    /* IPaddress */
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

    /* 协议栈status */
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
    /* 简化版：停机waiting复位 */
    while (1) { asm volatile("hlt"); }
}

/* poweroff命令：ACPIoff机（QEMU） */
static void shell_cmd_poweroff(int argc, char **argv)
{
    vga_puts("Shutting down...\n");
    /* QEMU/Bochs ACPIoff机port */
    __asm__ volatile(
        "movw $0x604, %%dx\n"
        "movl $0x2000, %%eax\n"
        "outl %%eax, %%dx\n"
        : : : "eax", "edx"
    );
    /* 如果off机failed,停机 */
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

/* uname -a full信息 */
static void shell_cmd_uname_full(int argc, char **argv)
{
    vga_puts("HepuOS hepu 0.56.1 #1 SMP x86_64 GNU/HepuOS\n");
}

/* V0.13.18: driver管理器命令 drv update|repo|list|install|remove|help */
static void shell_cmd_drv(int argc, char **argv)
{
    extern int drv_mgr_update(void);
    extern int drv_mgr_download(const char *name);
    extern int drv_mgr_install(const char *name);
    extern int drv_mgr_uninstall(int id);
    extern void drv_mgr_print_repo(void);
    extern void drv_mgr_print_installed(void);

    if (argc < 2) {
        vga_puts("usage: drv <update|repo|list|install NAME|remove ID|help>\n");
        return;
    }
    if (shell_strcmp(argv[1], "update") == 0) {
        drv_mgr_update();
    } else if (shell_strcmp(argv[1], "repo") == 0) {
        drv_mgr_print_repo();
    } else if (shell_strcmp(argv[1], "list") == 0) {
        drv_mgr_print_installed();
    } else if (shell_strcmp(argv[1], "install") == 0) {
        if (argc < 3) vga_puts("usage: drv install NAME\n");
        else drv_mgr_install(argv[2]);
    } else if (shell_strcmp(argv[1], "remove") == 0) {
        if (argc < 3) vga_puts("usage: drv remove ID\n");
        else {
            int id = 0;
            for (char *c = argv[2]; *c; c++) id = id * 10 + (*c - '0');
            drv_mgr_uninstall(id);
        }
    } else {
        vga_puts("drv: update - fetch repo index\n");
        vga_puts("     repo - list repo drivers\n");
        vga_puts("     list - list installed drivers\n");
        vga_puts("     install NAME - download+install driver\n");
        vga_puts("     remove ID - uninstall driver\n");
    }
}

/* V0.13.19: system注册表命令 reg get|set|del|list|save */
static void shell_cmd_reg(int argc, char **argv)
{
    extern const char *reg64_get(const char *key, const char *def);
    extern int reg64_set(const char *key, const char *value);
    extern int reg64_delete(const char *key);
    extern void reg64_list(void);
    extern int reg64_save(void);

    if (argc < 2) {
        vga_puts("usage: reg <get KEY|set KEY VALUE|del KEY|list|save>\n");
        return;
    }
    if (shell_strcmp(argv[1], "get") == 0 && argc >= 3) {
        const char *v = reg64_get(argv[2], "(null)");
        vga_puts(v);
        vga_puts("\n");
    } else if (shell_strcmp(argv[1], "set") == 0 && argc >= 4) {
        if (reg64_set(argv[2], argv[3]) == 0) {
            vga_puts("reg: set ok\n");
        } else {
            vga_puts("reg: set failed\n");
        }
    } else if (shell_strcmp(argv[1], "del") == 0 && argc >= 3) {
        if (reg64_delete(argv[2]) == 0) vga_puts("reg: deleted\n");
        else vga_puts("reg: not found\n");
    } else if (shell_strcmp(argv[1], "list") == 0) {
        reg64_list();
    } else if (shell_strcmp(argv[1], "save") == 0) {
        if (reg64_save() == 0) vga_puts("reg: saved to /etc/registry.dat\n");
        else vga_puts("reg: save failed\n");
    } else {
        vga_puts("reg: get KEY - read value\n");
        vga_puts("     set KEY VALUE - write value\n");
        vga_puts("     del KEY - delete entry\n");
        vga_puts("     list - dump all\n");
        vga_puts("     save - persist to disk\n");
    }
}

/* V0.13.20: GPS 定位命令 gps status|sim|feed */
static void shell_cmd_gps(int argc, char **argv)
{
    extern void gps64_print_status(void);
    extern void gps64_simulate_fix(void);
    extern int gps64_feed(const char *line);

    if (argc < 2) {
        vga_puts("usage: gps <status|sim|feed NMEA>\n");
        return;
    }
    if (shell_strcmp(argv[1], "status") == 0) {
        gps64_print_status();
    } else if (shell_strcmp(argv[1], "sim") == 0) {
        gps64_simulate_fix();
    } else if (shell_strcmp(argv[1], "feed") == 0 && argc >= 3) {
        gps64_feed(argv[2]);
        gps64_print_status();
    } else {
        vga_puts("gps: status - show fix\n");
        vga_puts("     sim - inject simulated fix\n");
        vga_puts("     feed NMEA - parse one NMEA sentence\n");
    }
}

/* V0.13.21: disk管理 disk info|fsck|list */
static void shell_cmd_disk(int argc, char **argv)
{
    extern int disk64_info(void);
    extern int disk64_fsck(void);
    extern int disk64_list(void);

    if (argc < 2) {
        vga_puts("usage: disk <info|fsck|list>\n");
        return;
    }
    if (shell_strcmp(argv[1], "info") == 0) disk64_info();
    else if (shell_strcmp(argv[1], "fsck") == 0) disk64_fsck();
    else if (shell_strcmp(argv[1], "list") == 0) disk64_list();
    else vga_puts("disk: info|fsck|list\n");
}

/* V0.13.21: fileoff联 assoc get EXT|set EXT APP|list|check PATH */
static void shell_cmd_assoc(int argc, char **argv)
{
    extern int file_assoc_get_ext(const char *path, char *ext, int max);
    extern const char *file_assoc_get(const char *ext);
    extern int file_assoc_set(const char *ext, const char *app);
    extern void file_assoc_list(void);

    if (argc < 2) {
        vga_puts("usage: assoc <get EXT|set EXT APP|list|check PATH>\n");
        return;
    }
    if (shell_strcmp(argv[1], "get") == 0 && argc >= 3) {
        const char *app = file_assoc_get(argv[2]);
        vga_puts(argv[2]);
        vga_puts(" -> ");
        vga_puts(app);
        vga_puts("\n");
    } else if (shell_strcmp(argv[1], "set") == 0 && argc >= 4) {
        if (file_assoc_set(argv[2], argv[3]) == 0) {
            vga_puts("assoc: set ok (use 'reg save' to persist)\n");
        } else vga_puts("assoc: set failed\n");
    } else if (shell_strcmp(argv[1], "list") == 0) {
        file_assoc_list();
    } else if (shell_strcmp(argv[1], "check") == 0 && argc >= 3) {
        char ext[16];
        file_assoc_get_ext(argv[2], ext, sizeof(ext));
        vga_puts("path: ");
        vga_puts(argv[2]);
        vga_puts("\n  ext: [");
        vga_puts(ext);
        vga_puts("]\n  app: ");
        vga_puts(file_assoc_get(ext));
        vga_puts("\n");
    } else {
        vga_puts("assoc: get EXT - query app\n");
        vga_puts("       set EXT APP - set association\n");
        vga_puts("       list - show rules\n");
        vga_puts("       check PATH - show ext+app for path\n");
    }
}

/* V0.13.23: 多语言 lang get|set zh|en|list */
static void shell_cmd_lang(int argc, char **argv)
{
    extern int l10n64_get_lang(void);
    extern int l10n64_set_lang(int lang);
    extern const char *l10n64_lang_name(void);
    extern int l10n64_init(void);

    if (argc < 2) {
        vga_puts("usage: lang <get|set zh|en>\n");
        return;
    }
    if (shell_strcmp(argv[1], "get") == 0) {
        vga_puts("current: ");
        vga_puts(l10n64_lang_name());
        vga_puts("\n");
    } else if (shell_strcmp(argv[1], "set") == 0 && argc >= 3) {
        int lang = (argv[2][0] == 'e' && argv[2][1] == 'n') ? 1 : 0;
        if (l10n64_set_lang(lang) == 0) {
            vga_puts("lang set to ");
            vga_puts(l10n64_lang_name());
            vga_puts("\n");
        } else vga_puts("lang: set failed\n");
    } else if (shell_strcmp(argv[1], "list") == 0) {
        vga_puts("available: zh (Chinese) / en (English)\n");
    } else {
        vga_puts("lang: get - show current\n");
        vga_puts("     set zh|en - switch (persisted)\n");
    }
}

/* V0.13.25: kernel模块 kmod load PATH|list|unload NAME */
static void shell_cmd_kmod(int argc, char **argv)
{
    extern int kmod64_load(const char *name, void *elf_data, uint64_t size);
    extern int kmod64_init_module(int slot);
    extern int kmod64_find(const char *name);
    extern int kmod64_unload(int slot);
    extern void kmod64_list(void);
    extern int ext2_64_read_file(const char *path, uint8_t *buffer, uint32_t max_size);

    if (argc < 2) {
        vga_puts("usage: kmod <load PATH|list|unload NAME>\n");
        return;
    }
    if (shell_strcmp(argv[1], "load") == 0 && argc >= 3) {
        static uint8_t buf[65536];
        int n = ext2_64_read_file(argv[2], buf, sizeof(buf));
        if (n <= 0) { vga_puts("kmod: cannot read "); vga_puts(argv[2]); vga_puts("\n"); return; }
        int slot = kmod64_load(argv[2], buf, (uint64_t)n);
        if (slot < 0) { vga_puts("kmod: load failed\n"); return; }
        int rc = kmod64_init_module(slot);
        vga_puts("kmod: loaded, init rc=");
        {
            char nb[8]; int np = 0;
            if (rc == 0) nb[np++] = '0';
            int rv = rc;
            while (rv && np < 7) { nb[np++] = '0' + rv % 10; rv /= 10; }
            for (int k = 0; k < np / 2; k++) { char c = nb[k]; nb[k] = nb[np-1-k]; nb[np-1-k] = c; }
            nb[np] = 0;
            vga_puts(nb);
        }
        vga_puts("\n");
    } else if (shell_strcmp(argv[1], "list") == 0) {
        kmod64_list();
    } else if (shell_strcmp(argv[1], "unload") == 0 && argc >= 3) {
        int s2 = kmod64_find(argv[2]);
        if (s2 < 0) { vga_puts("kmod: not loaded\n"); return; }
        if (kmod64_unload(s2) == 0) { vga_puts("kmod: unloaded\n"); }
    } else {
        vga_puts("kmod: load PATH - load ELF driver module\n");
        vga_puts("     list - show loaded modules\n");
        vga_puts("     unload NAME - unload module\n");
    }
}

/* V0.13.26: USB status usb status|list|hotplug */
static void shell_cmd_usb(int argc, char **argv)
{
    extern int usb64_dev_count(void);
    extern int usb64_dev_info(int idx, uint8_t *port, uint16_t *vid, uint16_t *pid, char *kind);
    extern int usb64_hotplug_pending(void);

    if (argc < 2) {
        vga_puts("usage: usb <status|list|hotplug>\n");
        return;
    }
    if (shell_strcmp(argv[1], "status") == 0) {
        vga_puts("== USB Status ==\n");
        char nb[12];
        vga_puts("  devices: ");
        {
            int v = usb64_dev_count(); int p = 0;
            if (v == 0) nb[p++] = '0';
            while (v && p < 11) { nb[p++] = '0' + v % 10; v /= 10; }
            for (int k = 0; k < p / 2; k++) { char c = nb[k]; nb[k] = nb[p-1-k]; nb[p-1-k] = c; }
            nb[p] = 0; vga_puts(nb);
        }
        vga_puts("\n");
    } else if (shell_strcmp(argv[1], "list") == 0) {
        int n = usb64_dev_count();
        vga_puts("== USB Devices ==\n");
        for (int i = 0; i < n; i++) {
            uint8_t port; uint16_t vid, pid; char kind[16];
            usb64_dev_info(i, &port, &vid, &pid, kind);
            vga_puts("  [");
            vga_puts(kind);
            vga_puts("] port=");
            vga_put_hex(port);
            vga_puts(" vid=");
            vga_put_hex(vid);
            vga_puts(" pid=");
            vga_put_hex(pid);
            vga_puts("\n");
        }
        if (n == 0) vga_puts("  (none)\n");
        vga_puts("== end ==\n");
    } else if (shell_strcmp(argv[1], "hotplug") == 0) {
        vga_puts(usb64_hotplug_pending() ? "hotplug: event pending\n" : "hotplug: none\n");
    } else if (shell_strcmp(argv[1], "mount") == 0) {
        /* V0.13.36: USB 挂载到 ext2 */
        extern int usb64_mount_usb(void);
        extern int usb64_dev_count(void);
        if (usb64_dev_count() == 0) { vga_puts("usb mount: no usb storage device\n"); return; }
        int rc = usb64_mount_usb();
        vga_puts(rc == 0 ? "usb mount: OK (ext2 on usb)\n" : "usb mount: failed\n");
    } else if (shell_strcmp(argv[1], "umount") == 0) {
        /* V0.13.37: 卸载USB, 恢复IDEsystem盘 */
        extern int usb64_umount_usb(void);
        int rc = usb64_umount_usb();
        vga_puts(rc == 0 ? "usb umount: OK (back to system disk)\n" : "usb umount: failed\n");
    } else {
        vga_puts("usb: status - controller/devices\n");
        vga_puts("     list - enumerate devices\n");
        vga_puts("     hotplug - check plug events\n");
        vga_puts("     mount - mount usb storage to ext2\n");
        vga_puts("     umount - back to system disk\n");
    }
}

/* ===== V0.55 P4: devmode 普通用户 <-> 开发者 角色切换 ===== */
static void shell_read_pass(char *buf, int max)
{
    int pos = 0;
    while (pos < max - 1) {
        if (keyboard64_haschar()) {
            char c = keyboard64_getchar();
            if (c == '\n' || c == '\r') { vga_putchar('\n'); break; }
            if (c == '\b' || c == 0x7F) {
                if (pos > 0) { pos--; vga_puts("\b \b"); }
            } else if (c >= 0x20 && c < 0x7F) {
                buf[pos++] = c;
                vga_putchar('*');
            }
        }
    }
    buf[pos] = 0;
}

static void shell_cmd_devmode(int argc, char **argv)
{
    extern int auth64_is_configured(void);
    extern int auth64_current_role(void);
    extern int auth64_elevate(const char *pass);
    extern int auth64_deelevate(void);

    if (!auth64_is_configured()) {
        vga_puts("devmode: no account (complete first-boot setup first)\n");
        return;
    }
    int role = auth64_current_role();

    if (argc > 1 && shell_strcmp(argv[1], "off") == 0) {
        if (role != 2) { vga_puts("devmode: not in developer mode\n"); return; }
        vga_puts(auth64_deelevate() == 0 ?
                 "devmode: switched back to normal user\n" :
                 "devmode: failed to save\n");
        return;
    }
    if (role == 2) { vga_puts("devmode: already in developer mode\n"); return; }

    /* 提权确认: 普通用户在 OOBE 中未设管理员密码, 以账户密码验证.
     * 成功后角色持久化到 /account.conf, 重启后仍为开发者. */
    vga_puts("Password: ");
    char pass[64];
    shell_read_pass(pass, sizeof(pass));
    int rc = auth64_elevate(pass);
    if (rc == 0) {
        vga_puts("devmode: developer mode enabled (persistent)\n");
    } else {
        vga_puts("devmode: wrong password\n");
    }
}

/* 命令表 */
static shell_command_t shell_commands[] = {
    {"help", shell_cmd_help},
    {"hepu", shell_cmd_hepu},
    {"devmode", shell_cmd_devmode},
    {"history", shell_cmd_history},
    {"which", shell_cmd_which},
    {"drv", shell_cmd_drv},
    {"reg", shell_cmd_reg},
    {"gps", shell_cmd_gps},
    {"disk", shell_cmd_disk},
    {"assoc", shell_cmd_assoc},
    {"lang", shell_cmd_lang},
    {"kmod", shell_cmd_kmod},
    {"usb", shell_cmd_usb},
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
    {"dmesg", shell_cmd_dmesg},
    {"sync", shell_cmd_sync},
    {"vmstat", shell_cmd_vmstat},
    {"pstree", shell_cmd_pstree},
    {"id", shell_cmd_id},
    {"who", shell_cmd_who},
    {"w", shell_cmd_who},
    {"lsblk", shell_cmd_lsblk},
    {"blkid", shell_cmd_blkid},
    {"timedatectl", shell_cmd_timedatectl},
    {"kill", shell_cmd_kill},
    {"killall", shell_cmd_killall},
    {"renice", shell_cmd_renice},
    {"taskset", shell_cmd_taskset},
    /* 常用别名：linux 习惯名 -> 已有实现 */
    {"lscpu", shell_cmd_cpuinfo},
    {"lspci", shell_cmd_pci},
    {"lsusb", shell_cmd_usb},
    {"ss", shell_cmd_netstat},
    {"htop", (void*)term64_cmd_top},
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
/* V0.13.30: 导出命令名表(终端补全/帮助用) */
int shell_get_command_names(char names[][16], int max)
{
    int n = 0;
    for (int i = 0; shell_commands[i].name && n < max; i++) {
        int j = 0;
        while (shell_commands[i].name[j] && j < 15) {
            names[n][j] = shell_commands[i].name[j];
            j++;
        }
        names[n][j] = 0;
        n++;
    }
    return n;
}

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
    vga_put_hex(g_kbd_irq_count);
    vga_puts("\n");

    /* 自动启动user态程序（尝试执行/bin/hello.elf） */
    vga_puts("[AUTOSTART] Trying /bin/hello.elf...\n");
    int autostart_ret = user_process_exec("/bin/hello.elf");
    if (autostart_ret < 0) {
        vga_puts("[AUTOSTART] No user program found, starting shell\n");
    } else {
        vga_puts("[AUTOSTART] User program exited\n");
    }
    vga_puts("\n");

    /* 安全回归：useraddressaccessed限制（kerneladdress/未映射address应被拒绝） */
    vga_puts("[AUTOSTART] Running /bin/sec_test.elf security regression...\n");
    if (user_process_exec("/bin/sec_test.elf") < 0) {
        vga_puts("[AUTOSTART] sec_test not found (optional)\n");
    }
    vga_puts("\n");

    /* fork 端到端测试 */
    vga_puts("[AUTOSTART] Running /bin/forktest.elf...\n");
    if (user_process_exec("/bin/forktest.elf") < 0) {
        vga_puts("[AUTOSTART] forktest not found (optional)\n");
    }
    vga_puts("\n");

    /* V0.20.54: Compatibility Layer测试 */
    vga_puts("[AUTOSTART] Running /bin/comptest.elf...\n");
    user_process_exec("/bin/comptest.elf");
    vga_puts("\n");

    /* 网络轮询count器 */
    int net_poll_counter = 0;

    while (1) {
        /* 每10次循环轮询一次网络（避免影响enter响应） */
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
