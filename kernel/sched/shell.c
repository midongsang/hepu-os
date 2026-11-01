/* ============================================================================
 * 赫普OS - 内核 Shell
 * 简单的命令行解释器，支持基础命令
 * ========================================================================== */

#include <stdint.h>
#include "shell.h"
#include "mac.h"
#include "i18n.h"
#include "dtb.h"
#include "font_cjk16.h"
#include "vfs.h"
#include "keyboard.h"
#include "ext2.h"
#include "ide_ext2.h"  /* V4.44-E89: fm_entry_t/list_entries */
#include <stddef.h>
#include "vga.h"
#include "keyboard.h"
#include "pit.h"
#include "hepu.h"
#include "string.h"
#include "process.h"
#include "serial.h"
#include "hdpk_template.h"  /* V3.87-E15 */
#include "sched.h"
#include "initrd.h"
#include "hug_loader.h"
#include "vga_gfx.h"
#include "mouse.h"
#include "wm.h"
#include "touch_hw.h"
#include "appreg.h"
#include "power.h"
#include "ide.h"
#include "kheap.h"
#include "usb.h"  /* V4.48-E93: usb_device_t/usb_get_device */

#define SHELL_BUFFER_SIZE 128

extern const char *hepu_kernel_version;
extern const char *hepu_system_version;

void cmd_clear(void)
{
    vga_clear();
}

void cmd_ls(void)
{
    extern void ide_ext2_list_dir(void);
    ide_ext2_list_dir();
}


void cmd_fwrite(char *args)
{
    extern int vfs_open(const char *, int);
    extern int vfs_write(int, const void *, uint32_t);
    extern int vfs_close(int);
    extern int ext2_path_create_v2(const char *, uint16_t);
    if (!args || args[0] == '\0') { vga_puts("usage: fwrite <path> <text>\n"); return; }
    char *sp = args;
    while (*sp == ' ') sp++;
    char *ep = sp;
    while (*ep && *ep != ' ') ep++;
    char path[64];
    int n = ep - sp; if (n > 63) n = 63;
    memcpy(path, sp, n); path[n] = '\0';
    /* 自动补 / 前缀 (ext2 只支持绝对路径; 测试时避免特殊字符键) */
    if (path[0] != '/') {
        for (int i = n; i >= 0; i--) path[i + 1] = path[i];
        path[0] = '/';
    }
    while (*ep == ' ') ep++;
    char *text = ep;
    if (text[0] == '\0') text = "hello";   /* 单参数: 写固定内容 */
    /* V4.18-E63: 直调 ext2 VFS 写(构造节点)以触发 MAC 挂钩 */
    extern int ext2_vfs_write(vfs_node_t *, uint32_t, const void *, uint32_t);
    extern int ext2_vfs_read(vfs_node_t *, uint32_t, void *, uint32_t);
    int cr = ext2_path_create_v2(path, 0x1A4);
    if (cr <= 0) {
        /* 已存在: resolve + lookup 取现有 inode */
        extern int ext2_resolve_path(const char *, uint32_t *, char *, int);
        extern int ext2_read_inode(uint32_t, ext2_inode_t *);
        extern int ext2_lookup(ext2_inode_t *, const char *, uint32_t *);
        uint32_t dir_inode = 0;
        char fname[256];
        if (ext2_resolve_path(path, &dir_inode, fname, 256) == 0) {
            ext2_inode_t dir;
            if (ext2_read_inode(dir_inode, &dir) == 0) {
                uint32_t existing = 0;
                if (ext2_lookup(&dir, fname, &existing) == 0) cr = (int)existing;
            }
        }
    }
    vga_printf("fwrite dbg: create rc=%d\n", cr);
    if (cr <= 0) { vga_puts("fwrite: create fail\n"); return; }
    static vfs_node_t tmp_node;
    memset(&tmp_node, 0, sizeof(tmp_node));
    strncpy(tmp_node.name, path + 1, VFS_MAX_NAME - 1);   /* 去掉 / 前缀 */
    tmp_node.inode_num = (uint32_t)cr;
    int w = ext2_vfs_write(&tmp_node, 0, text, strlen(text));
    vga_printf("fwrite: wrote %d bytes (mac hook active)\n", w);
}


void cmd_hug(char *args)
{
    if (!args || *args == '\0') {
        vga_puts(" Usage: hug <app> | install | uninstall | list | info <app> | upgrade <app>\n");
        return;
    }
    /* V3.87 阶段D: 真机安装器分派 */
    if (strncmp(args, "install ", 8) == 0) {
        extern int hug_install(const char *);
        hug_install(args + 8);
        return;
    }
    if (strncmp(args, "uninstall ", 10) == 0) {
        extern int hug_uninstall(const char *);
        hug_uninstall(args + 10);
        return;
    }
    if (strcmp(args, "list") == 0) {
        extern int hug_list_apps(void);
        hug_list_apps();
        return;
    }
    /* V3.87-D4: hug info / hug upgrade */
    if (strncmp(args, "info ", 5) == 0) {
        extern int hug_info_cmd(const char *);
        hug_info_cmd(args + 5);
        return;
    }
    if (strncmp(args, "upgrade ", 8) == 0) {
        extern int hug_upgrade(const char *);
        hug_upgrade(args + 8);
        return;
    }
    /* V3.87-D2: "hug run <app>" 与 "hug <app>" 等价 (跳过 run 前缀) */
    if (strncmp(args, "run ", 4) == 0) {
        if (!initrd_ready()) {
            vga_puts(" error: initrd not load \n");
            return;
        }
        hug_run(args + 4);
        return;
    }
    if (!initrd_ready()) {
        vga_puts(" error: initrd not load \n");
        return;
    }
    hug_run(args);
}

/* V3.87: 运行外部格式程序 (Linux ELF32 等)——
 * 从 initrd 读取文件 → binfmt 识别 → 对应加载器.
 * 沙箱: 兼容进程独立页目录 + syscall ABI 翻译 + 文件路径白名单,
 * 内核基础零改动 (APK 式隔离). */
/* V3.87: 运行外部格式程序 (Linux ELF32 等)——
 * 从 initrd 读取文件 → binfmt 识别 → 对应加载器.
 * 沙箱: 兼容进程独立页目录 + syscall ABI 翻译 + 文件路径白名单,
 * 内核基础零改动 (APK 式隔离). */
void shell_main(void)
{
    extern int keyboard_readline(char *buf, int max_len);
    char buffer[SHELL_BUFFER_SIZE];
    extern void wd_init(void);
    wd_init();  /* V3.87-E6: 启动软看门狗(5s 超时自动复位) */

    /* V4.42-E87: 启动自动加载持久化 MAC 规则 (sys_exit 直调 shell_main,
     * 必经路径——放这里确保任何进入路径都执行; 内部已做磁盘 mount guard) */
    extern void mac_autoload(void);
    mac_autoload();

    /* V2.22根因修复: Shell 由 sys_exit 在 int 0x80 上下文调用(isr128 已 cli),
     * 此处必须 sti 恢复中断. 否则键盘 IRQ1/PIT 中断全不触发:
     *   - keyboard_getchar 的 hlt 永不唤醒 → 真机无法输入(用户实测"输入没办法输入")
     *   - pit_sleep 的 hlt 卡死(诊断版 waiting 3s 卡住) */
    __asm__ volatile ("sti");

    extern void serial_puts(unsigned short, const char *);
    serial_puts(0x3F8, "[SHELL] main started\n");
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);

#ifdef HEPU_BOOT_DEBUG
    /* V2.23: Shell 自检——等待3秒让真机键盘/控制器就绪(诊断版调试用途).
     * 只做自检等待, 不自动注入任何命令, 输入完全交给用户. */
    extern void pit_sleep(uint32_t);
    vga_puts(" [SHELL] self-test: waiting 3s for keyboard ready...\n");
    pit_sleep(3000);
    vga_puts(" [SHELL] self-test done, ready for input\n");

    /* V3.87: 兼容层自动验证——启动即运行 Linux ELF 测试程序,
     * 结果直接进启动日志 (真机键盘时序不确定时的确定性验收通道). */
    {
        extern void serial_puts(uint16_t, const char *);
        /* V6.65 DEBUG: fork-test放在最开头测试 */
        serial_puts(0x3F8, "[DEBUG] before fork-test at start\n");
        vga_puts(" [SHELL] auto-test: run fork-test (fork+waitpid)...\n");
        serial_puts(0x3F8, "[AUTO] fork-test start\n");
        extern int cmd_run_auto(const char *);
        cmd_run_auto("fork-test");
        serial_puts(0x3F8, "[AUTO] fork-test done\n");
        serial_puts(0x3F8, "[DEBUG] after fork-test at start\n");
        vga_puts(" [SHELL] auto-test: run linuxhello...\n");
        serial_puts(0x3F8, "[AUTO] linuxhello start\n");
        cmd_run_auto("linuxhello");
        serial_puts(0x3F8, "[AUTO] linuxhello done\n");
        vga_puts(" [SHELL] auto-test: run linuxc (sandbox+getpid)...\n");
        serial_puts(0x3F8, "[AUTO] linuxc start\n");
        cmd_run_auto("linuxc");
        serial_puts(0x3F8, "[AUTO] linuxc done\n");
        vga_puts(" [SHELL] auto-test: run linuxbrk (heap)...\n");
        serial_puts(0x3F8, "[AUTO] linuxbrk start\n");
        cmd_run_auto("linuxbrk");
        serial_puts(0x3F8, "[AUTO] linuxbrk done\n");
        vga_puts(" [SHELL] auto-test: run linuxcrt (envp/auxv+runtime)...\n");
        serial_puts(0x3F8, "[AUTO] linuxcrt start\n");
        cmd_run_auto("linuxcrt");
        serial_puts(0x3F8, "[AUTO] linuxcrt done\n");
        vga_puts(" [SHELL] auto-test: run winhello (PE32 console)...\n");
        serial_puts(0x3F8, "[AUTO] winhello.exe start\n");
        cmd_run_auto("winhello.exe");
        serial_puts(0x3F8, "[AUTO] winhello.exe done\n");
        vga_puts(" [SHELL] auto-test: run winargs (PE32 API: handle/cmdline/argv/tick/sleep)...\n");
        serial_puts(0x3F8, "[AUTO] winargs.exe start\n");
        cmd_run_auto("/app/winargs.exe --mode demo hello");
        serial_puts(0x3F8, "[AUTO] winargs.exe done\n");
        vga_puts(" [SHELL] auto-test: run macho_demo (Mach-O FEEDFACE)...\n");
        serial_puts(0x3F8, "[AUTO] macho_demo start\n");
        cmd_run_auto("macho_demo alpha beta");
        serial_puts(0x3F8, "[AUTO] macho_demo done\n");
        /* V6.60: Rust用户态服务测试——C核心+汇编最底层+Rust用户态技术路线验证 */
        vga_puts(" [SHELL] auto-test: run hello-rust (Rust no_std + brk alloc)...\n");
        serial_puts(0x3F8, "[AUTO] hello-rust start\n");
        cmd_run_auto("hello-rust");
        serial_puts(0x3F8, "[AUTO] hello-rust done\n");
        /* V6.60: Rust cat工具测试——文件读写/lseek/access */
        vga_puts(" [SHELL] auto-test: run rust-cat (file I/O + lseek)...\n");
        serial_puts(0x3F8, "[AUTO] rust-cat start\n");
        cmd_run_auto("cat");
        serial_puts(0x3F8, "[AUTO] rust-cat done\n");
        /* V6.60: Rust echo工具测试——write/heap alloc/getpid/time */
        vga_puts(" [SHELL] auto-test: run rust-echo (write + heap + getpid)...\n");
        serial_puts(0x3F8, "[AUTO] rust-echo start\n");
        cmd_run_auto("echo");
        serial_puts(0x3F8, "[AUTO] rust-echo done\n");
        /* V6.60: Rust sysinfo工具测试——系统信息+字符串函数+格式化输出 */
        vga_puts(" [SHELL] auto-test: run rust-sysinfo (system info + string funcs)...\n");
        serial_puts(0x3F8, "[AUTO] rust-sysinfo start\n");
        cmd_run_auto("sysinfo");
        serial_puts(0x3F8, "[AUTO] rust-sysinfo done\n");
        /* V6.63: Rust fs-client测试——文件服务客户端库验证 */
        vga_puts(" [SHELL] auto-test: run rust-fs-client (file service client)...\n");
        serial_puts(0x3F8, "[AUTO] rust-fs-client start\n");
        cmd_run_auto("fs-client");
        serial_puts(0x3F8, "[AUTO] rust-fs-client done\n");
        /* V6.65: 端到端IPC集成测试——fork+服务发现+完整IPC通信 */
        vga_puts(" [SHELL] auto-test: run ipc-e2e-test (fork+service+IPC)...\n");
        serial_puts(0x3F8, "[AUTO] ipc-e2e-test start\n");
        cmd_run_auto("ipc-e2e-test");
        serial_puts(0x3F8, "[AUTO] ipc-e2e-test done\n");
        /* V6.67: 文件服务跨进程端到端测试——暂时跳过(IPC阻塞问题排查中) */
        vga_puts(" [SHELL] auto-test: skip file-server-e2e-test (IPC debug)...\n");
        serial_puts(0x3F8, "[AUTO] file-server-e2e-test skipped\n");
        /* V6.69: BSD Socket API测试 */
        vga_puts(" [SHELL] auto-test: run socket-test (BSD Socket API loopback)...\n");
        serial_puts(0x3F8, "[AUTO] socket-test start\n");
        cmd_run_auto("socket-test");
        serial_puts(0x3F8, "[AUTO] socket-test done\n");
        /* V6.50: 图形模式测试 (gfxtest) — 放在最后, 切换图形模式后自动切回文本 */
        vga_puts(" [SHELL] auto-test: run gfxtest (VGA 320x200 graphics)...\n");
        serial_puts(0x3F8, "[AUTO] gfxtest start\n");
        cmd_run_auto("gfxtest");
        serial_puts(0x3F8, "[AUTO] gfxtest done\n");
        serial_puts(0x3F8, "[AUTO] ALL DONE\n");
        /* V7.31: PAE完整切换验证（独立分支测试） */
        serial_puts(0x3F8, "[AUTO] PAE full validation start...\n");
        vga_puts(" [SHELL] auto-test: PAE validation...\n");
        cmd_run_auto("pae_test");
        serial_puts(0x3F8, "[AUTO] pae_test done\n");
        cmd_run_auto("pae_proc_test");
        serial_puts(0x3F8, "[AUTO] pae_proc_test done\n");
        cmd_run_auto("pae_compat_test");
        serial_puts(0x3F8, "[AUTO] pae_compat_test done\n");
        serial_puts(0x3F8, "[AUTO] PAE validation complete\n");
        /* V6.90: 自动渲染桌面用于视觉验证 (5秒后自动继续) */
        serial_puts(0x3F8, "[AUTO] rendering desktop preview...\n");
        extern void vga_gfx_init(void);
        vga_gfx_init();
        extern void desktop_render_test(void);
        desktop_render_test();
        /* 保持桌面显示5秒 */
        extern void pit_sleep(uint32_t);
        pit_sleep(5000);
        /* 切回文本模式 */
        extern void vga_gfx_text_mode(void);
        vga_gfx_text_mode();
        serial_puts(0x3F8, "[AUTO] desktop preview done\n");
    }
    firstboot_welcome();  /* V3.87-E15: 首次启动引导页(auto-test 之后, 避免被挤出屏外) */
#endif

    extern void serial_puts(uint16_t, const char *);

    while (1) {
        extern void wd_feed(void);
        wd_feed();  /* V3.87-E6: 软看门狗喂狗(主循环) */
        extern void softirq_run(void);
        softirq_run();  /* V3.87-E8: 主循环执行底半部 */
        /* V7.98: 网络轮询，接收数据包 */
        extern void net_poll(void);
        net_poll();

        vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
        vga_puts("hepu@os:~$ ");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);        int len = keyboard_readline(buffer, SHELL_BUFFER_SIZE);
        if (len > 0) {
            shell_execute(buffer);
        }
    }
}

/* V7.98: 网络状态查询命令 */
void cmd_ifconfig(void)
{
    vga_printf("=== Network Interface Status ===\n");

    /* eth0状态 */
    extern int eth_get_mac(const char *name, uint8_t *mac);
    extern int eth_get_link_status(const char *name, void *status);
    uint8_t mac[6];
    if (eth_get_mac("eth0", mac) == 0) {
        vga_printf("eth0: MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        vga_printf("eth0: not found\n");
    }

    /* RTL8139统计 */
    extern void rtl8139_get_stats(uint32_t *tx_pkts, uint32_t *rx_pkts,
                                   uint32_t *tx_bytes, uint32_t *rx_bytes, int *link);
    uint32_t tx_pkts, rx_pkts, tx_bytes, rx_bytes;
    int link;
    rtl8139_get_stats(&tx_pkts, &rx_pkts, &tx_bytes, &rx_bytes, &link);
    vga_printf("      Link: %s\n", link ? "UP" : "DOWN");
    vga_printf("      TX: %u packets, %u bytes\n", tx_pkts, tx_bytes);
    vga_printf("      RX: %u packets, %u bytes\n", rx_pkts, rx_bytes);

    /* DHCP状态 */
    extern int dhcp_get_config(const char *interface, void *config);
    vga_printf("      DHCP: (use dhcp command to configure)\n");
    vga_printf("================================\n");
}

/* V7.98: Ping命令 */
void cmd_ping(char *args)
{
    if (!args || *args == '\0') {
        vga_puts("Usage: ping <ip-address>\n");
        return;
    }

    /* 简单的点分十进制IP解析 */
    uint32_t ip = 0;
    int a = 0, b = 0, c = 0, d = 0;
    char *p = args;
    while (*p >= '0' && *p <= '9') { a = a * 10 + (*p - '0'); p++; }
    if (*p == '.') p++;
    while (*p >= '0' && *p <= '9') { b = b * 10 + (*p - '0'); p++; }
    if (*p == '.') p++;
    while (*p >= '0' && *p <= '9') { c = c * 10 + (*p - '0'); p++; }
    if (*p == '.') p++;
    while (*p >= '0' && *p <= '9') { d = d * 10 + (*p - '0'); p++; }

    if (a == 0 && b == 0 && c == 0 && d == 0) {
        vga_printf("ping: invalid IP address %s\n", args);
        return;
    }
    ip = (a << 24) | (b << 16) | (c << 8) | d;

    vga_printf("PING %d.%d.%d.%d: 32 data bytes\n", a, b, c, d);

    /* 执行4次ping */
    extern int ping_once(uint32_t dest_ip, void *result);
    for (int i = 0; i < 4; i++) {
        uint8_t result[32];
        ping_once(ip, result);
        extern void pit_sleep(uint32_t);
        pit_sleep(500);  /* 500ms间隔 */
    }
}

/* V7.99: DHCP命令 */
void cmd_dhcp(void)
{
    vga_puts("Starting DHCP client...\n");
    extern int dhcp_start(const char *interface);
    int ret = dhcp_start("eth0");
    if (ret == 0) {
        vga_puts("DHCP configuration complete\n");
    } else {
        vga_puts("DHCP failed\n");
    }
}

/* V8.08: DNS域名解析命令 */
void cmd_dns(char *args)
{
    if (!args || args[0] == '\0') {
        vga_puts("Usage: dns <hostname>\n");
        vga_puts("Example: dns www.google.com\n");
        return;
    }

    /* 跳过空格 */
    while (*args == ' ') args++;

    vga_printf("Resolving %s...\n", args);

    extern int hepu_dns_resolve(const char *hostname, uint32_t *ip_addr);
    uint32_t ip_addr = 0;
    int ret = hepu_dns_resolve(args, &ip_addr);

    if (ret == 0) {
        vga_printf("%s -> %d.%d.%d.%d\n",
                   args,
                   (ip_addr >> 24) & 0xFF,
                   (ip_addr >> 16) & 0xFF,
                   (ip_addr >> 8) & 0xFF,
                   ip_addr & 0xFF);
    } else {
        vga_puts("DNS resolution failed\n");
    }
}

/* V8.12: 系统监控命令（top） */
void cmd_top(char *args)
{
    (void)args;

    vga_puts("=== HepuOS System Monitor ===\n");

    /* 系统信息 */
    extern uint32_t pit_get_ticks(void);
    uint32_t uptime = pit_get_ticks() / 1000;
    vga_printf("Uptime: %d:%02d:%02d\n",
               uptime / 3600, (uptime % 3600) / 60, uptime % 60);

    /* 内存信息 */
    extern int kcore_get_memory_stats(uint64_t *total, uint64_t *used, uint64_t *free,
                                       uint64_t *kernel, uint64_t *user);
    uint64_t total_mem = 0, used_mem = 0, free_mem = 0, kernel_mem = 0, user_mem = 0;
    kcore_get_memory_stats(&total_mem, &used_mem, &free_mem, &kernel_mem, &user_mem);
    vga_printf("Memory: %dMB total, %dMB used, %dMB free (%.1f%%)\n",
               (int)(total_mem / 1024 / 1024),
               (int)(used_mem / 1024 / 1024),
               (int)(free_mem / 1024 / 1024),
               total_mem > 0 ? (float)used_mem * 100.0 / total_mem : 0.0);

    /* 进程信息 */
    extern process_t proc_table[];
    int total_procs = 0;
    int running_procs = 0;
    for (int i = 0; i < 256; i++) {
        if (proc_table[i].state != 0) {  /* PROC_UNUSED = 0 */
            total_procs++;
            if (proc_table[i].state == 1) {  /* PROC_READY = 1 */
                running_procs++;
            }
        }
    }
    vga_printf("Processes: %d total, %d running\n", total_procs, running_procs);

    /* 网络信息 */
    vga_puts("\n--- Network ---\n");
    extern void rtl8139_get_stats(uint32_t *tx, uint32_t *rx, uint32_t *tx_bytes, uint32_t *rx_bytes, int *link);
    uint32_t tx_pkts = 0, rx_pkts = 0, tx_bytes = 0, rx_bytes = 0;
    int link = 0;
    rtl8139_get_stats(&tx_pkts, &rx_pkts, &tx_bytes, &rx_bytes, &link);
    vga_printf("eth0 (RTL8139): link %s, TX %d pkts (%d bytes), RX %d pkts (%d bytes)\n",
               link ? "up" : "down",
               tx_pkts, tx_bytes, rx_pkts, rx_bytes);

    /* 进程列表 */
    vga_puts("\n--- Process List ---\n");
    vga_printf("%-4s %-20s %-8s\n", "PID", "Name", "State");
    vga_puts("---- -------------------- --------\n");

    /* 显示前5个进程 */
    int shown = 0;
    for (int i = 0; i < 256 && shown < 5; i++) {
        if (proc_table[i].state != 0) {
            const char *state_name = "unknown";
            if (proc_table[i].state == 0) state_name = "unused";
            else if (proc_table[i].state == 1) state_name = "ready";
            else if (proc_table[i].state == 2) state_name = "running";
            else if (proc_table[i].state == 3) state_name = "blocked";
            vga_printf("%-4d %-20s %-8s\n",
                       proc_table[i].pid,
                       proc_table[i].name[0] ? proc_table[i].name : "unknown",
                       state_name);
            shown++;
        }
    }

    vga_puts("\n(System monitor - simplified view)\n");
}
