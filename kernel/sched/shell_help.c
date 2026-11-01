/* ============================================================================
 * 赫普OS - Shell：帮助系统
 * 从 shell.c 拆分（V6.46 代码重构，大文件拆分目标<500行）
 * 包含：help_table/man_table/cmd_help/cmd_man/cmd_version/cmd_about/help_group_title
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "vga.h"
#include "i18n.h"
#include "shell.h"
#include "keyboard.h"
#include "pit.h"
#include "hepu.h"
extern const char *hepu_kernel_version;
extern const char *hepu_system_version;
#include "process.h"
#include "hepu.h"
extern const char *hepu_kernel_version;
extern const char *hepu_system_version;

const help_entry_t help_table[] = {
    /* ---- 系统/电源 SYS ---- */
    {"reboot",    "SYS",  "reboot",                       "reboot the system (warm reset)", "reboot"},
    {"shutdown",  "SYS",  "shutdown | poweroff | power",  "power off the system",           "shutdown"},
    {"poweroff",  "SYS",  "poweroff",                     "alias of shutdown",              "poweroff"},
    {"power",     "SYS",  "power",                        "alias of shutdown",              "power"},
    {"pm",        "SYS",  "pm [sleep|wake|idle|tmo|batt]","power management: sleep/wake/idle/timeout/battery", "pm sleep"},
    {"mode",      "SYS",  "mode [save|restore]",          "persist/restore UI mode via CMOS","mode save"},
    {"theme",     "SYS",  "theme [0-3|list]",             "switch UI theme (Blue/Dark/OLED/TRON)", "theme 3"},
    /* ---- 信息 INFO ---- */
    {"help",      "INFO", "help [cmd|all]",               "command manual; help <cmd> for detail", "help fs"},
    {"version",   "INFO", "version",                      "show system version",            "version"},
    {"man",       "INFO", "man [cmd|all]",                "detailed command manual",        "man pm"},
    {"about",     "INFO", "about",                        "full system info (V4.08-E53)",   "about"},
    {"clear",     "INFO", "clear",                        "clear screen",                   "clear"},
    {"echo",      "INFO", "echo <msg>",                   "echo a message",                 "echo hello"},
    {"time",      "INFO", "time",                         "show uptime",                    "time"},
    {"dmesg",     "INFO", "dmesg [-l <0..3>]",            "show kernel log; -l filters by level", "dmesg -l 1"},
    {"loglevel",  "INFO", "loglevel [<0..3>]",            "get/set global log threshold",   "loglevel 1"},
    /* ---- 进程/资源 PROC ---- */
    {"ps",        "PROC", "ps",                           "list processes (PID/prio/limits)",  "ps"},
    {"ktest",     "TEST", "ktest",                             "run all KUnit suites + report (stress)",   "ktest"},
    {"affinity",  "PROC", "affinity <pid> <mask>",          "set CPU affinity (bitmask of CPUs)",       "affinity 1 1"},
    {"prio",      "PROC", "prio <pid> <0|1|2>",            "set process priority (0=RT/1=NORM/2=BKG)","prio 1 1"},
    {"meminfo",   "PROC", "meminfo",                      "memory report: kheap/kmalloc/procs", "meminfo"},
    {"sysinfo",   "SYS",  "sysinfo",                     "V4.05-E50: system perf counters", "sysinfo"},
    {"rtc",       "SYS",  "rtc [alarm <sec>|alarm off]", "V4.06-E51: CMOS clock + alarm wake", "rtc alarm 5"},
    {"limits",    "PROC", "limits [<pid> <mem_kb>]",      "set per-process memory limit (0=unlim)", "limits 3 2048"},
    {"rlimit",    "PROC", "rlimit [<pid> <mem_kb> <cpu_ticks>]", "view/set per-process RLIMIT (mem+cpu)", "rlimit 3 2048 100"},
    {"setappcpu", "PROC", "setappcpu <ticks>",            "set next ELF program cpu limit (0=unlim)", "setappcpu 30"},
    {"perf",      "PROC", "perf",                         "perf stats: sched/kheap/slab/spin/irq", "perf"},
    {"cacheinfo", "PROC", "cacheinfo",                    "cache layout: process_t hot-cluster + asm-deps", "cacheinfo"},
    {"dmatest",   "IO",   "dmatest",                      "IDE DMA batch r/w test: DMA vs PIO + verify", "dmatest"},
    {"iostat",    "IO",   "iostat",                       "IO policy: device strategy + poll/irq counters", "iostat"},
    {"locktest",  "PROC", "locktest",                     "spinlock stress: kmalloc/kfree x1000", "locktest"},
    {"memtest",   "PROC", "memtest",                      "verify memory charge/refund",    "memtest"},
    {"ipcstress", "PROC", "ipcstress",                    "IPC sleep-lock timeout/acquire paths", "ipcstress"},
    {"leakcheck", "PROC", "leakcheck",                    "memory leak trace: live allocs by caller", "leakcheck"},
    {"schtest",   "PROC", "schtest",                      "sched dynamic-slice demo: CPU(A) vs IO(B)", "schtest"},
    {"schstat",   "PROC", "schstat",                      "show dynamic time-slice adjustment stats", "schstat"},
    {"softirqtest","PROC","softirqtest [on|off]",        "bottom-half demo: raise every 25 ticks",  "softirqtest on"},
    {"softstat",  "PROC", "softstat",                     "softirq stats: raised/handled/dropped/defer", "softstat"},
    {"quota",     "FS",   "quota set <uid> <blocks>|show", "disk quota: set limit / show table", "quota show"},
    {"mac",       "SEC",  "mac add <sub> <obj> <perm>|show|save", "mandatory access control rules (persist)", "mac save"},
    {"fwrite",    "FS",   "fwrite <path> <text>",             "write text to ext2 file (VFS+MAC)", "fwrite /t.txt hi"},
    {"lang",      "SYS",  "lang list|set <en|zh|ja|de>|show", "multi-language switch (i18n)", "lang set zh"},
    {"dtb",       "HW",   "dtb show|find <name> <prop>",       "device tree: show platform / query prop", "dtb find timer clock-frequency"},
    {"cjk",       "SYS",  "cjk <text>|check|stats",            "CJK render test / plane2 check / stats", "cjk stats"},
    {"banner",    "SYS",  "banner",                            "redraw welcome banner (i18n)", "banner"},
    {"ime",       "SYS",  "ime <py>|pick <n>|buf|clear",       "pinyin IME: query/pick/buffer", "ime zhong"},
    {"softtick",  "PROC", "softtick",                     "manual: ctl(1)+tick() once, show raised",   "softtick"},
    {"lockstat",  "PROC", "lockstat",                     "deadlock monitor: registered/held/warns",   "lockstat"},
    {"lockhold",  "PROC", "lockhold [on|off]",           "hold test lock 5s+ to trigger LOCKWARN",   "lockhold on"},
    {"crashlog",  "LOG",  "crashlog",                     "show data-disk /crash.log (user-app crash history)", "crashlog"},
    {"wd",        "PROC", "wd [status|test]",            "watchdog status/age; test=arm reset", "wd status"},
    {"canary",    "PROC", "canary",                       "scan kernel-stack bottom canaries", "canary"},
    /* ---- 文件/应用 FS ---- */
    {"ls",        "FS",   "ls",                           "list initrd files",              "ls"},
    {"fs",        "FS",   "fs mount|ls|cat <n>|write <n> <t>|rm <n>|cp <a> <b>|mv <a> <b>|sync|fsck [-f]", "data-disk ext2 operations", "fs mount"},
    {"disk",      "FS",   "disk",                         "IDE disk info & IO test",        "disk"},
    {"hug",       "FS",   "hug <app.hug>",                "run a .hug native app",          "hug demo.hug"},
    {"apps",      "FS",   "apps",                         "list unified app registry (dual-mode)", "apps"},
    {"run",       "FS",   "run <elf|exe|macho>",          "run compat binary (Linux/Windows/macOS)", "run linuxhello"},
    /* ---- 设备 DEV ---- */
    {"gfx",       "DEV",  "gfx",                          "enter GUI (VGA graphics mode)",  "gfx"},
    {"gfxtest",   "DEV",  "gfxtest",                      "gfx solid color fill test",      "gfxtest"},
    {"phone",     "DEV",  "phone",                        "enter phone mode (mobile UI)",   "phone"},
    {"touchtest", "DEV",  "touchtest",                    "touchscreen driver selftest",    "touchtest"},
    {"gesture",   "DEV",  "gesture on|off|test|th|q|ui",   "gesture events + cfg + queue",  "gesture ui right"},
    {"usb",       "DEV",  "usb",                          "USB subsystem status",           "usb"},
    {"sensors",   "DEV",  "sensors",                      "sensor subsystem status",        "sensors"},
    /* ---- 安全/机型 SEC ---- */
    {"sec",       "MACH",  "sec",                          "security module status",         "sec"},
    {"hdpk",      "MACH",  "hdpk",                         "H-DPK community templates/devices", "hdpk"},
};

#define HELP_ENTRIES (sizeof(help_table) / sizeof(help_table[0]))

const char *help_group_title(char g)
{
    switch (g) {
        case 'S': return i18n_get(I18N_GROUP_SYS);
        case 'I': return i18n_get(I18N_GROUP_INFO);
        case 'P': return i18n_get(I18N_GROUP_PROC);
        case 'F': return i18n_get(I18N_GROUP_FILES);
        case 'D': return i18n_get(I18N_GROUP_DEV);
        case 'M': return i18n_get(I18N_GROUP_SEC);
        default:  return "[Other]";
    }
}

void cmd_help(char *args)
{
    if (args && args[0] != 0 && strcmp(args, "all") != 0) {
        /* help <cmd>: 单命令详情 */
        for (uint32_t i = 0; i < HELP_ENTRIES; i++) {
            if (strcmp(help_table[i].name, args) == 0) {
                vga_printf(" %s -- %s\n", help_table[i].name,
                           help_table[i].desc);
                vga_printf("   group : %s\n",
                           help_group_title(help_table[i].group[0]));
                vga_printf("   syntax: %s\n", help_table[i].syntax);
                vga_printf("   usage : %s\n", help_table[i].ex);
                return;
            }
        }
        vga_printf(" no manual for '%s' (try: help)\n", args);
        return;
    }
    if (args && strcmp(args, "all") == 0) {
        /* help all: 全量手册(按分组) */
        char last = 0;
        for (uint32_t i = 0; i < HELP_ENTRIES; i++) {
            if (help_table[i].group[0] != last) {
                last = help_table[i].group[0];
                vga_printf("\n %s\n", help_group_title(last));
            }
            vga_printf("  %-12s %s\n", help_table[i].name,
                       help_table[i].desc);
        }
        vga_puts("\n Tip: help <cmd> shows syntax + example.\n");
        return;
    }
    /* help: 分组总览 */
    vga_puts(" HEPU OS "); vga_puts(i18n_get(I18N_HELP_TITLE));
    vga_puts(" (v1.02) -- help <cmd> for details\n");
    vga_puts("   help all - full manual\n\n");
    char last = 0;
    for (uint32_t i = 0; i < HELP_ENTRIES; i++) {
        if (help_table[i].group[0] != last) {
            last = help_table[i].group[0];
            vga_printf(" %s\n", help_group_title(last));
        }
        vga_puts("  ");
        vga_puts(help_table[i].name);
        vga_puts("  ");
    }
    vga_puts("\n");
}

void cmd_version(void)
{
    vga_printf(" Hepu OS(HEPU Hyper Evolve OS) v%s\n", hepu_system_version);
    vga_printf(" Kernel v%s | arch: x86(i686) microkernel\n", hepu_kernel_version);
    vga_puts(" License: GPLv3 + HEPU Additional Terms\n");
}

void cmd_about(void)
{
    /* V4.08-E53: 完整系统信息——版本/架构/构建/特性清单 */
    extern const char *hepu_kernel_version;
extern const char *hepu_system_version;
    extern uint32_t sched_get_ticks(void);
    extern void kheap_stats(uint32_t *, uint32_t *, uint32_t *);
    extern void sched_stats(uint32_t *, uint32_t *, uint32_t *);
    uint32_t hu = 0, hf = 0, ht = 0;
    kheap_stats(&hu, &hf, &ht);
    uint32_t tp = 0, rn = 0, rd = 0;
    sched_stats(&tp, &rn, &rd);
    vga_puts("===== HEPU OS about =====\n");
    /* V4.24-E69: 版本标签 i18n */
    vga_puts(i18n_get(I18N_SYSTEM_VER));
    vga_printf(" : Hepu OS v%s (HEPU Hyper Evolve OS)\n", hepu_system_version);
    vga_puts(i18n_get(I18N_KERNEL_VER));
    vga_printf(" : v%s x86(i686) 32-bit microkernel, ring0/ring3\n", hepu_kernel_version);
    vga_puts(" Boot   : Multiboot (ISO/IMG) | ROM hepu-os-v1.02\n");
    vga_printf(" "); vga_puts(i18n_get(I18N_MEM_USED));
    vga_printf(": kheap %uKB (used %uKB free %uKB)\n", ht / 1024, hu / 1024, hf / 1024);
    vga_printf(" Procs  : total=%u running=%u ready=%u | uptime %u.%02us\n",
               tp, rn, rd, sched_get_ticks() / 100, sched_get_ticks() % 100);
    vga_puts(" FILESYS: ext2 read/write (data disk), VFS\n");
    vga_puts(" POWER  : S3 deep sleep, battery sim, low-batt auto sleep, RTC alarm wake\n");
    vga_puts(" RTC    : CMOS clock + alarm (IRQ8)\n");
    vga_puts(" MULTI  : preemptive sched, priority, dynamic timeslice, SMP framework\n");
    vga_puts(" LOADER : ELF + .hug app format, user-mode syscall\n");
    vga_puts(" DEV    : PS/2 kbd/mouse, IDE PIO, serial, VGA text+gfx\n");
    vga_puts(" MISC   : softirq, watchdog, sysinfo counters, i18n EN\n");
}


const man_entry_t man_table[] = {
    {"help", "help [cmd|all]\n  List all commands, or show syntax/example of one.\n  help all prints the full manual table.\n"},
    {"man",  "man [cmd|all]\n  Detailed manual for a command; man all prints every page.\n"},
    {"version", "version\n  System v1.02 + Kernel v1.0.2 + license.\n"},
    {"about", "about\n  Full system info: version, arch, kheap, procs, feature list.\n"},
    {"ps",   "ps\n  Process table: PID, state, mem, cpu, violations.\n  Expect: total procs:1, current PID:0, pid=1 init NORM.\n"},
    {"ktest","ktest\n  Kernel self-test: heap, paging, locks, IPC, signals.\n  ~16s. Used for regression after every change.\n"},
    {"schtest", "schtest\n  Scheduler stress test: spawns procs, checks preemption.\n  ~35s. Must end with 0 ISR faults.\n"},
    {"pm",   "pm [sleep|wake|idle|tmo|batt|save|level]\n  sleep : deep sleep (S3), wake by key/RTC alarm.\n  batt <pct>: simulate battery; <=5%% auto sleep.\n  tmo <sec>: idle timeout. save on|off: low power.\n  level <0|1|2>: 0=performance(10min) 1=balanced(5min) 2=saver(1min).\n"},
    {"rtc",  "rtc [alarm ...]\n  rtc              : show CMOS time.\n  rtc alarm <sec>  : one-shot alarm (with pm sleep).\n  rtc alarm daily <hh:mm>   : daily alarm.\n  rtc alarm weekly <dow> <hh:mm> : weekly repeat.\n  rtc alarm off    : disable. dow: 0-6 or sun..sat.\n"},
    {"fs",   "fs [ls|stat|mkdir|rm|cat|write]\n  ext2 filesystem on data disk. ls lists files.\n"},
    {"ls",   "ls [path]\n  List ext2 directory entries.\n"},
    {"disk", "disk [probe|info]\n  IDE/PIO disk probe and info.\n"},
    {"run",  "run <app.hug>\n  Load and run a .hug application in user mode.\n"},
    {"hug",  "hug [list|info]\n  HEPU Unified binary format (.hug) helpers.\n"},
    {"apps", "apps\n  Show installed applications.\n"},
    {"sysinfo", "sysinfo\n  Uptime, ctx switches, IRQ counters, kheap used/free.\n"},
    {"meminfo", "meminfo\n  Physical memory map, free pages, kheap stats.\n"},
    {"memtest", "memtest\n  Memory stress test (kmalloc/free loops).\n"},
    {"leakcheck", "leakcheck\n  Kernel heap leak detector: allocated blocks vs freed.\n"},
    {"mpool", "mpool\n  Memory pool stats: 32B/64B/128B pools free/total/allocs.\n"},
    {"wd",   "wd [feed|stats|timeout <s>]\n  Watchdog: feed it or let it reset the kernel.\n"},
    {"reboot", "reboot\n  Restart the system (triple fault reset).\n"},
    {"shutdown", "shutdown\n  Power off (QEMU exits with -no-reboot).\n"},
    {"poweroff", "poweroff\n  Same as shutdown.\n"},
    {"theme","theme [0-3|list]\n  UI theme: Blue/Dark/OLED/TRON.\n"},
    {"mode", "mode [save|restore]\n  Persist/restore UI mode via CMOS.\n"},
    {"gfx",  "gfx\n  Enter graphics mode demo (double-buffered VGA).\n"},
    {"gfxtest", "gfxtest\n  Graphics stress: fill/rect/line patterns.\n"},
    {"softstat", "softstat\n  Softirq stats: raised/handled/dropped/max_defer.\n"},
    {"quota", "quota set <uid> <blocks>\n  quota show\n  Disk quota: set block limit per uid, show table.\n"},
    {"mac", "mac add <sub> <obj> <perm> | mac del <sub> <obj>\n  mac show | mac enforce <0|1> | mac stats | mac audit\n  mac save [path] | mac load [path]   (persist rules to disk, default /selinux.rules)\n  SELinux-style MAC: subject x object x perm (R/W/X), default deny.\n  audit: dump recent 16 MAC decisions (allow/deny/permissive).\n"},
    {"lang", "lang list\n  lang set <en|zh|ja|de>\n  lang show\n  Multi-language i18n switch. Non-ASCII shown as '?' until font.\n"},
    {"dtb", "dtb show\n  dtb find <name> <prop>\n  Device tree: show builtin platform, query property by name prefix.\n"},
    {"cjk", "cjk <utf8 text>\n  cjk check | cjk stats\n  CJK glyph render / plane2 verify / stats.\n"},
    {"banner", "banner\n  Redraw the welcome banner in current language.\n"},
    {"ime", "ime <pinyin>\n  ime pick <n> | ime buf | ime clear\n  Pinyin IME: query candidates, pick into buffer.\n"},
    {"softirqtest", "softirqtest [on|off]\n  Bottom-half demo: raise softirq every 25 ticks.\n"},
    {"sec",  "sec\n  Security module info (sandbox, permissions).\n"},
    {"loglevel", "loglevel [0-3]\n  Kernel log verbosity.\n"},
    {"dmesg", "dmesg\n  Dump kernel ring log buffer.\n"},
    {"crashlog", "crashlog\n  Show last recorded crash (register dump).\n"},
    {"iostat", "iostat\n  I/O policy and request stats.\n"},
    {"dmatest", "dmatest\n  DMA transfer self-test.\n"},
    {"cacheinfo", "cacheinfo\n  CPUID cache geometry (L1/L2, line size).\n"},
    {"perf", "perf\n  Perf counters (cycles, instructions).\n"},
    {"prio", "prio <pid> <0-255>\n  Set process priority (dynamic timeslice).\n"},
    {"affinity", "affinity <pid> <cpu>\n  Pin process to CPU (SMP framework).\n"},
    {"limits", "limits\n  Show per-process resource limits.\n"},
    {"rlimit", "rlimit <pid> <res> <val>\n  Set resource limit for a process.\n"},
    {"locktest", "locktest\n  Lock contention stress test.\n"},
    {"lockstat", "lockstat\n  Lock acquire/release stats.\n"},
    {"lockhold", "lockhold\n  Simulate holding a lock (deadlock demo).\n"},
    {"ipcstress", "ipcstress\n  IPC message throughput test.\n"},
    {"touchtest", "touchtest\n  Touchscreen driver test (if present).\n"},
    {"gesture", "gesture on|off|test|threshold [lp] [dt] [sw]|q [drain|clear]|ui <type>\n  on/off: register/unregister gesture event callback (UI hook).\n  test: inject tap/swipe/pinch to drive the callback.\n  threshold: show or set long-press ticks / double-tap window / swipe min.\n  q: gesture event queue (pending/dropped); q drain/clear.\n  ui: feed a gesture to wm_handle_gesture (text-mode UI logic test).\n"},
    {"usb", "usb [list|probe]\n  USB device list (framework stub).\n"},
    {"sensors", "sensors\n  Read simulated sensors (battery, temp).\n"},
    {"phone", "phone [dial|sms]\n  Telephony demo commands.\n"},
    {"echo", "echo <text>\n  Print text back.\n"},
    {"clear", "clear\n  Clear the screen.\n"},
    {"time", "time\n  Uptime in seconds/ticks.\n"},
    {"canary", "canary <on|off|check>\n  Stack canary protection control.\n"},
    {"softtick", "softtick\n  Softirq tick statistics.\n"},
    {"hdpk", "hdpk\n  HEPU package format helper.\n"},
    {"tcp", "tcp connect <a> <b> <c> <d> <port> | tcp listen <port>\n  tcp accept <lfd> | tcp send <fd> <text> | tcp recv <fd>\n  tcp close <fd> | tcp tick | tcp (list all)\n  TCP state machine: SYN_SENT->ESTABLISHED->FIN_WAIT_1->TIME_WAIT->CLOSED.\n  tcp tick manually advances the simulated remote (SYN-ACK/ACK/FIN+ACK).\n"},
    {"audio", "audio [info|tone <freq> <ms>|vol <0-100>|mute <0|1>]\n  ALSA-style PCM/mixer framework.\n  tone: sine wave test (e.g. audio tone 440 200).\n  vol: master volume 0-100. mute: 0=off 1=on.\n"},
};

void cmd_man(char *args)
{
    if (args && args[0] != 0 && strcmp(args, "all") != 0) {
        for (uint32_t i = 0; i < sizeof(man_table) / sizeof(man_table[0]); i++) {
            if (strcmp(man_table[i].name, args) == 0) {
                vga_printf("== man %s ==\n", man_table[i].name);
                vga_puts(man_table[i].body);
                vga_puts("\n");
                return;
            }
        }
        vga_printf(" no manual page for '%s' (try: help %s)\n", args, args);
        return;
    }
    if (args && strcmp(args, "all") == 0) {
        for (uint32_t i = 0; i < sizeof(man_table) / sizeof(man_table[0]); i++) {
            vga_printf("== man %s ==\n", man_table[i].name);
            vga_puts(man_table[i].body);
            vga_puts("\n");
        }
        return;
    }
    vga_puts(" HEPU OS manual -- man <cmd> for a page, man all for everything\n");
    extern int i18n_get_lang(void);
    extern const char *i18n_get(int);
    vga_printf(" (language: %s)\n", i18n_get(2));   /* LANG_NAME */
    uint32_t n = sizeof(man_table) / sizeof(man_table[0]);
    vga_printf(" %u manual pages available:\n", n);
    for (uint32_t i = 0; i < n; i++) {
        vga_puts("  ");
        vga_puts(man_table[i].name);
        vga_puts("  ");
    }
    vga_puts("\n Tip: help <cmd> shows syntax; man <cmd> explains it.\n");
}

