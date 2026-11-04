/* ============================================================================
 * 赫普OS - Shell：命令执行器
 * 从 shell.c 拆分（V6.46 代码重构，大文件拆分目标<500行）
 * 包含：cmd_run_auto/cmd_run/shell_execute（命令分发器）
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "vga.h"
#include "process.h"
#include "shell.h"
#include "serial.h"
#include "pit.h"
#include "hepu.h"
#include "i18n.h"
#include "vfs.h"
#include "ext2.h"
#include "ide_ext2.h"
#include "wm.h"
#include "ide.h"
#include "power.h"
#include "usb.h"
#include "appreg.h"
#include "touch.h"
#include "touch_hw.h"
int cmd_run_auto(const char *args)
{
    extern int binfmt_exec(const char *, const uint8_t *, uint32_t);
    extern void binfmt_set_cmdline(const char *);
    if (!args || *args == '\0') {
        vga_puts(" Usage: run < elf file >\n");
        return -1;
    }
    if (!initrd_ready()) {
        vga_puts(" error: initrd not load \n");
        return -1;
    }
    /* V3.87 B-3: 真实命令行传参——args 完整命令串 "prog arg1 arg2...",
     * 第一个 token 为文件名, 剩余参数原样传给 binfmt 加载器 (PE argv 注入) */
    char fname[64];
    const char *sp = args;
    int fn = 0;
    while (*sp && *sp != ' ' && *sp != '\t' && fn < 62) fname[fn++] = *sp++;
    fname[fn] = 0;
    binfmt_set_cmdline(args);   /* 完整命令行 "prog arg1 arg2" 交给加载器分词 */

    /* V3.87 阶段B-2: 支持 /app/xxx 路径 (VFS 已挂载) → 剥前缀取文件名,
     * 与裸文件名同走 initrd 读取; 保持 APK 式应用区语义 */
    const char *app_name = fname;
    if (fname[0] == '/') {
        const char *last = fname;
        for (const char *p = fname; *p; p++) {
            if (*p == '/') last = p + 1;
        }
        if (last != fname) app_name = last;
    }
    /* 无后缀时: 先试原名, 再补 .elf */
    char full[64];
    const char *dot = 0;
    for (const char *p = app_name; *p; p++) if (*p == '.') dot = p;
    if (!dot) {
        int len = 0;
        while (app_name[len] && len < 48) { full[len] = app_name[len]; len++; }
        full[len] = 0;
        int fd0 = initrd_open(full);
        if (fd0 < 0) {
            /* 补 .elf 再试 */
            int l2 = len;
            if (l2 < 56) {
                full[l2++] = '.'; full[l2++] = 'e';
                full[l2++] = 'l'; full[l2++] = 'f';
            }
            full[l2] = 0;
        } else {
            initrd_close(fd0);
        }
    } else {
        int len = 0;
        while (app_name[len] && len < 63) { full[len] = app_name[len]; len++; }
        full[len] = 0;
    }
    int fd = initrd_open(full);
    if (fd < 0) {
        vga_printf(" error: file not found %s\n", full);
        return -1;
    }
    int size = initrd_size(fd);
    int index = initrd_find(full);
    initrd_close(fd);
    if (index < 0 || size <= 0) {
        vga_puts(" error: initrd entry fail\n");
        return -1;
    }
    uint8_t *data = (uint8_t *)initrd_get_data(index);
    int rc = binfmt_exec(full, data, (uint32_t)size);
    vga_printf(" run %s -> rc=%d\n", full, rc);
    /* V6.60: 串口输出返回值，用于调试Rust用户态程序 */
    {
        extern void serial_puts(uint16_t, const char *);
        extern void serial_puthex(uint16_t, uint32_t);
        serial_puts(0x3F8, "[RUN] ");
        serial_puts(0x3F8, full);
        serial_puts(0x3F8, " -> rc=");
        serial_puthex(0x3F8, (uint32_t)rc);
        serial_puts(0x3F8, "\n");
    }
    return rc;
}

void cmd_run(char *args)
{
    cmd_run_auto(args);
}

/* V2.25: 解析并显示 MBR 分区表 (LBA0 446 起 4 个 16 字节分区项) */
/* 解析并执行命令 */
void shell_execute(char *cmd)
{
    /* 去除前导空格 */
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0') return;

    /* 提取命令和参数 */
    char *args = cmd;
    while (*args && *args != ' ') args++;
    if (*args == ' ') {
        *args = '\0';
        args++;
        while (*args == ' ') args++;
    }

    /* 命令别名: h=help, q=reboot, l=ls, m=man */
    if (strcmp(cmd, "h") == 0) cmd = "help";
    else if (strcmp(cmd, "q") == 0) cmd = "reboot";
    else if (strcmp(cmd, "l") == 0) cmd = "ls";
    else if (strcmp(cmd, "m") == 0) cmd = "man";

    if (strcmp(cmd, "help") == 0) {
        cmd_help(args);
    } else if (strcmp(cmd, "version") == 0) {
        cmd_version();
    } else if (strcmp(cmd, "man") == 0) {
        cmd_man(args);
    } else if (strcmp(cmd, "about") == 0) {
        cmd_about();
    } else if (strcmp(cmd, "clear") == 0) {
        cmd_clear();
    } else if (strcmp(cmd, "ps") == 0) {
        cmd_ps();
    } else if (strcmp(cmd, "ktest") == 0) {   /* V3.97-E43 */
        cmd_ktest();
    } else if (strcmp(cmd, "affinity") == 0) {   /* V3.96-E42 */
        cmd_affinity(args);
    } else if (strcmp(cmd, "prio") == 0) {   /* V3.90-E36 */
        cmd_prio(args);
    } else if (strcmp(cmd, "perf") == 0) {
        cmd_perf();
    } else if (strcmp(cmd, "cacheinfo") == 0) {   /* V4.00-E45 */
        cmd_cacheinfo();
    } else if (strcmp(cmd, "dmatest") == 0) {   /* V4.01-E46 */
        cmd_dmatest();
    } else if (strcmp(cmd, "iostat") == 0) {    /* V4.02-E47 */
        extern void iopolicy_dump(void);
        iopolicy_dump();
    } else if (strcmp(cmd, "locktest") == 0) {
        cmd_locktest();
    } else if (strcmp(cmd, "time") == 0) {
        cmd_time();
    } else if (strcmp(cmd, "about") == 0) {
        cmd_about();
    } else if (strcmp(cmd, "audio") == 0) {
        cmd_audio(args);
    } else if (strcmp(cmd, "echo") == 0) {
        cmd_echo(args);
    } else if (strcmp(cmd, "canary") == 0) {
        cmd_canary(args);
    } else if (strcmp(cmd, "wd") == 0) {
        cmd_wd(args);
    } else if (strcmp(cmd, "limits") == 0) {
        cmd_limits(args);
    } else if (strcmp(cmd, "rlimit") == 0) {   /* V3.91-E37 */
        cmd_rlimit(args);
    } else if (strcmp(cmd, "meminfo") == 0) {
        cmd_meminfo();
    } else if (strcmp(cmd, "ifconfig") == 0) {   /* V7.98: 网络状态查询 */
        extern void cmd_ifconfig(void);
        cmd_ifconfig();
    } else if (strcmp(cmd, "ping") == 0) {   /* V7.98: Ping网络测试 */
        extern void cmd_ping(char *args);
        cmd_ping(args);
    } else if (strcmp(cmd, "dhcp") == 0) {   /* V7.99: DHCP自动获取IP */
        extern void cmd_dhcp(void);
        cmd_dhcp();
    } else if (strcmp(cmd, "dns") == 0) {   /* V8.08: DNS域名解析 */
        extern void cmd_dns(char *args);
        cmd_dns(args);
    } else if (strcmp(cmd, "top") == 0) {   /* V8.12: 系统监控 */
        extern void cmd_top(char *args);
        cmd_top(args);
    } else if (strcmp(cmd, "taskmgr") == 0) {   /* V7.28: 任务管理器 */
        extern void cmd_taskmgr(void);
        cmd_taskmgr();
    } else if (strcmp(cmd, "sysinfo") == 0) {
        cmd_sysinfo();
    } else if (strcmp(cmd, "rtc") == 0) {
        cmd_rtc(args);
    } else if (strcmp(cmd, "memtest") == 0) {
        cmd_memtest();
    } else if (strcmp(cmd, "ipcstress") == 0) {
        cmd_ipcstress();
    } else if (strcmp(cmd, "setappcpu") == 0) {
        cmd_setappcpu(args);
    } else if (strcmp(cmd, "hdpk") == 0) {
        cmd_hdpk();
    } else if (strcmp(cmd, "schtest") == 0) {
        extern void schtest_run(void);
        schtest_run();
    } else if (strcmp(cmd, "schstat") == 0) {
        extern void schstat_show(void);
        schstat_show();
    } else if (strcmp(cmd, "softirqtest") == 0) {   /* V3.92-E38 */
        cmd_softirqtest(args);
    } else if (strcmp(cmd, "softstat") == 0) {   /* V3.92-E38 */
        cmd_softstat();
    } else if (strcmp(cmd, "softtick") == 0) {   /* V3.92-E38 诊断 */
        cmd_softtick();
    } else if (strcmp(cmd, "quota") == 0) {   /* V4.17-E62 */
        cmd_quota(args);
    } else if (strcmp(cmd, "mac") == 0) {   /* V4.18-E63 */
        cmd_mac(args);
    } else if (strcmp(cmd, "fwrite") == 0) {   /* V4.18-E63 */
        cmd_fwrite(args);
    } else if (strcmp(cmd, "lang") == 0) {   /* V4.19-E64 */
        cmd_lang(args);
    } else if (strcmp(cmd, "dtb") == 0) {   /* V4.20-E65 */
        cmd_dtb(args);
    } else if (strcmp(cmd, "cjk") == 0) {   /* V4.22-E67 */
        cmd_cjk(args);
    } else if (strcmp(cmd, "banner") == 0) {   /* V4.24-E69 */
        cmd_banner(args);
    } else if (strcmp(cmd, "ime") == 0) {   /* V4.25-E70 */
        cmd_ime(args);
    } else if (strcmp(cmd, "lockstat") == 0) {   /* V3.94-E40 */
        cmd_lockstat();
    } else if (strcmp(cmd, "lockhold") == 0) {   /* V3.94-E40 */
        cmd_lockhold(args);
    } else if (strcmp(cmd, "softirqtest") == 0) {   /* V3.92-E38 */
        cmd_softirqtest(args);
    } else if (strcmp(cmd, "softstat") == 0) {   /* V3.92-E38 */
        cmd_softstat();
    } else if (strcmp(cmd, "leakcheck") == 0) {
        extern void kmem_trace_dump(void);
        kmem_trace_dump();
    } else if (strcmp(cmd, "mpool") == 0) {
        extern void mempool_stats(void);
        mempool_stats();
    } else if (strcmp(cmd, "crashlog") == 0) {
        extern int crashlog_dump(void);
        crashlog_dump();
    } else if (strcmp(cmd, "ls") == 0) {
        cmd_ls();
    } else if (strcmp(cmd, "hug") == 0) {
        cmd_hug(args);
    } else if (strcmp(cmd, "run") == 0) {
        cmd_run(args);
    } else if (strcmp(cmd, "gfx") == 0) {
        cmd_gfx();
    } else if (strcmp(cmd, "disk") == 0) {
        cmd_disk();
    } else if (strcmp(cmd, "gfxtest") == 0) {
        cmd_gfxtest();
    } else if (strcmp(cmd, "edesktop") == 0) {
        extern void cmd_edesktop(void);
        cmd_edesktop();
    } else if (strcmp(cmd, "appstore") == 0) {
        extern void cmd_appstore(void);
        cmd_appstore();
    } else if (strcmp(cmd, "pae_compat_test") == 0) {
        extern void cmd_pae_compat_test(void);
        cmd_pae_compat_test();
    } else if (strcmp(cmd, "pae_switch") == 0) {
        extern void cmd_pae_switch(void);
        cmd_pae_switch();
    } else if (strcmp(cmd, "pae_proc_test") == 0) {
        extern void cmd_pae_proc_test(void);
        cmd_pae_proc_test();
    } else if (strcmp(cmd, "pae_test") == 0) {
        extern void cmd_pae_test(void);
        cmd_pae_test();
    } else if (strcmp(cmd, "gfxinfo") == 0) {
        extern void cmd_gfxinfo(void);
        cmd_gfxinfo();
    } else if (strcmp(cmd, "gfxtest_subsys") == 0) {
        extern void cmd_gfxtest_subsys(void);
        cmd_gfxtest_subsys();
    } else if (strcmp(cmd, "monitors") == 0) {   /* V7.29: 多显示器信息 */
        extern void cmd_monitors(void);
        cmd_monitors();
    } else if (strcmp(cmd, "fonttest") == 0) {
        extern void cmd_fonttest(void);
        cmd_fonttest();
    } else if (strcmp(cmd, "fontsize") == 0) {
        extern void cmd_fontsize(char*);
        cmd_fontsize(args);
    } else if (strcmp(cmd, "fonttype") == 0) {
        extern void cmd_fonttype(char*);
        cmd_fonttype(args);
    } else if (strcmp(cmd, "hinting") == 0) {   /* V7.30: TrueType Hinting控制 */
        extern void cmd_hinting(char*);
        cmd_hinting(args);
    } else if (strcmp(cmd, "fontload") == 0) {
        extern void cmd_fontload(char*);
        cmd_fontload(args);
    } else if (strcmp(cmd, "cjkfonttest") == 0) {
        extern void cmd_cjkfonttest(char*);
        cmd_cjkfonttest(args);
    } else if (strcmp(cmd, "fontlist") == 0) {
        extern void cmd_fontlist(char*);
        cmd_fontlist(args);
    } else if (strcmp(cmd, "gterm") == 0) {
        extern void cmd_gterm(char*);
        cmd_gterm(args);
    } else if (strcmp(cmd, "fs") == 0) {
        cmd_fs(args);
    } else if (strcmp(cmd, "dmesg") == 0) {
        cmd_dmesg(args);
    } else if (strcmp(cmd, "loglevel") == 0) {
        cmd_loglevel(args);
    } else if (strcmp(cmd, "phone") == 0) {
        extern void vga_gfx_init(void);
        extern void mobile_run(void);
        vga_gfx_init();   /* 进入手机模式前先初始化图形模式 */
        mobile_run();
        /* V2.53: 手机模式按 G 请求切到桌面模式 → 自动进入桌面 */
        extern int hepu_mode_switch_request;
        extern int hepu_ui_mode;
        if (hepu_mode_switch_request == 1) {
            hepu_mode_switch_request = 0;
            hepu_ui_mode = 1;
            vga_gfx_init();
            extern void wm_init(void);
            extern void desktop_init(void);
            extern void wm_main_loop(void);
            wm_init();
            desktop_init();
            wm_main_loop();
            /* V3.82: 桌面退出(关机)后字体平面已被图形模式污染, 自动重启重载字体 */
            sys_reboot_after_gfx();
            return;
        }
        /* V3.82: 退出后字体平面已被图形模式污染, 自动重启重载字体 */
        sys_reboot_after_gfx();
    } else if (strcmp(cmd, "gesture") == 0) {
        cmd_gesture(args);
    } else if (strcmp(cmd, "touchtest") == 0) {
        cmd_touchtest(args);
    } else if (strcmp(cmd, "apps") == 0) {
        /* V2.51: 列出统一应用注册表 (双模式) */
        vga_puts(" Unified App Registry (V2.51)\n");
        int n = appreg_count();
        int m = appreg_mobile_count();
        int d = appreg_desktop_count();
        vga_printf(" total=%d mobile=%d desktop=%d\n", n, m, d);
        for (int i = 0; i < n; i++) {
            const hepu_app_t *a = appreg_get(i);
            if (!a) continue;
            const char *mod = (a->mode == HEPU_APP_BOTH) ? "BOTH" :
                              (a->mode == HEPU_APP_MOBILE) ? "MOBILE" : "DESKTOP";
            vga_printf("  %2d [%s] %s (%s) mobSlot=%d\n",
                       (int)a->id, mod, a->name_zh, a->name_en,
                       (int)a->mobile_slot);
        }
    } else if (strcmp(cmd, "mode") == 0) {
        /* V2.53: UI 模式切换/持久化 (CMOS RAM 0x37) */
        extern int hepu_ui_mode;
        if (args && (args[0] == 's' || args[0] == 'S')) {
            /* 模式存 CMOS 持久化 */
            extern void cmos_write_byte(uint8_t, uint8_t);
            cmos_write_byte(0x37, (uint8_t)hepu_ui_mode);
            vga_printf(" UI mode saved: %d (CMOS 0x37)\n", hepu_ui_mode);
        } else if (args && (args[0] == 'r' || args[0] == 'R')) {
            extern uint8_t cmos_read_byte(uint8_t);
            uint8_t m = cmos_read_byte(0x37);
            vga_printf(" saved UI mode: %d (0=none 1=desktop 2=phone)\n", (int)m);
        } else {
            vga_printf(" current UI mode: %d (0=shell 1=desktop 2=phone)\n",
                       hepu_ui_mode);
            vga_puts(" usage: mode save | mode restore | mode\n");
        }
    } else if (strcmp(cmd, "sec") == 0) {
        /* V2.63: 安全模块——MAC 状态/规则/自测 */
        extern void sec_set_enforcing(int);
        extern int sec_get_enforcing(void);
        extern int sec_get_domain_count(void);
        extern int sec_get_type_count(void);
        extern int sec_get_rule_count(void);
        extern void sec_selftest(void);
        extern const char *sec_get_domain_name(int);
        extern const char *sec_get_type_name(int);
        extern int sec_get_rule(int, void *);
        typedef struct { int used; uint8_t source_domain, target_type, object_class, permissions; } secrule;
        if (args && args[0] == 'm') {
            if (args[1] == 'e') sec_set_enforcing(1);
            else if (args[1] == 'p') sec_set_enforcing(0);
            else if (args[1] == 'd') sec_set_enforcing(-1);
        } else if (args && args[0] == 'r') {
            int n = sec_get_rule_count();
            vga_printf(" MAC rules (%d):\n", n);
            for (int i = 0; i < n; i++) {
                secrule r;
                if (sec_get_rule(i, &r) == 0) {
                    vga_printf("  %s -> %s class=%d perm=0x%02x\n",
                               sec_get_domain_name(r.source_domain),
                               sec_get_type_name(r.target_type),
                               r.object_class, r.permissions);
                }
            }
        } else if (args && args[0] == 't') {
            sec_selftest();
        } else {
            vga_printf(" Security (MAC): mode=%s, domains=%d, types=%d, rules=%d\n",
                       sec_get_enforcing() > 0 ? "Enforcing" :
                       (sec_get_enforcing() == 0 ? "Permissive" : "Disabled"),
                       sec_get_domain_count(), sec_get_type_count(), sec_get_rule_count());
            vga_puts("  sec mode enforcing|permissive|disabled | sec rules | sec test\n");
        }
    } else if (strcmp(cmd, "usb") == 0) {
        /* V2.62: USB 栈状态 / V4.48-E93: 端口扫描 / HID 演示 */
        extern int usb_device_count(void);
        extern int usb_hc_detect_pci(void);
        extern int usb_scan_ports(void);
        extern void usb_hid_kbd_demo(void);
        extern void serial_puts(unsigned short, const char *);
        if (args && args[0] == 'd') {
            usb_hid_kbd_demo();
        } else if (args && args[0] == 's') {
            /* usb scan: 重新扫描 UHCI 端口 */
            vga_puts(" USB port rescan:\n");
            usb_scan_ports();
        } else {
            vga_puts(" USB subsystem:\n");
            int n = usb_device_count();
            vga_printf("  enumerated devices: %d\n", n);
            for (int i = 0; i < n; i++) {
                usb_device_t *ud = usb_get_device(i);
                if (ud) {
                    vga_printf("   [%d] port=%d speed=%d addr=%d conf=%d\n",
                               i, ud->port, ud->speed, ud->address, ud->configured);
                }
            }
            serial_puts(0x3F8, "[SHELL] usb cmd: devices=");
            serial_puts(0x3F8, n ? "1" : "0");
            serial_puts(0x3F8, "\n");
            vga_puts("  try: usb scan | usb demo\n");
        }
    } else if (strcmp(cmd, "pm") == 0) {
        cmd_pm(args);
    } else if (strcmp(cmd, "sensors") == 0) {
        /* V3.79: 传感器框架查看 (P1-② 闭环入口) */
        extern int sensors_read(int, void *);
        extern int sensors_get_info(int, void *);
        const char *snames[] = {"Accel", "Gyro", "Mag", "Light",
                                "Prox", "Pressure", "Temp", "Heart", "Step"};
        extern int sensors_sim_write(int, float, float, float, float, int);
        extern int sensors_sim_demo(void);
        if (args && args[0] == 's' && args[1] == 'i' && args[2] == 'm') {
            /* sensors sim <id> <x> <y> <z> <v> —— 手写整数解析(内核无 sscanf) */
            extern int atoi(const char *);
            const char *t = args + 4;
            int nums[5] = {0, 0, 0, 0, 0};
            int ni = 0;
            while (*t && ni < 5) {
                while (*t == ' ') t++;
                if (*t == 0) break;
                nums[ni++] = atoi(t);
                while (*t && *t != ' ') t++;
            }
            int id = nums[0], x = nums[1], y = nums[2], z = nums[3], v = nums[4];
            if (sensors_sim_write(id, x, y, z, v, 3) == 0)
                vga_printf(" sim wrote sensor[%d] (x=%d y=%d z=%d v=%d)\n", id, x, y, z, v);
            else vga_puts(" sim FAIL (id out of range or not registered)\n");
            return;
        }
        if (args && args[0] == 'd') {
            sensors_sim_demo();
            vga_puts(" sim demo injected (accel/gyro/mag/light/prox/temp/heart/steps)\n");
        }
        vga_puts("--- sensors ---\n");
        for (int i = 0; i < 9; i++) {
            typedef struct { float x, y, z, value; int accuracy; } sdt;
            sdt d;
            if (sensors_read(i, &d) == 0) {
                /* 内核 vga_printf 不支持 %f: 显示为整数 */
                vga_printf(" %s: x=%d y=%d z=%d v=%d acc=%d\n",
                           snames[i], (int)d.x, (int)d.y, (int)d.z,
                           (int)d.value, d.accuracy);
            } else {
                vga_printf(" %s: n/a\n", snames[i]);
            }
        }
    } else if (strcmp(cmd, "tcp") == 0) {
        cmd_tcp(args);
    } else if (strcmp(cmd, "theme") == 0) {
        /* V3.82: 主题切换 (文本模式直接切, 图形界面立即生效) */
        extern const char *theme_apply(int);
        extern int theme_current(void);
        const char *tnames[4] = {"Hepu Blue", "Dark", "OLED Black", "TRON"};
        if (args && args[0] == 'l') {
            vga_puts(" themes: 0=Hepu Blue 1=Dark 2=OLED Black 3=TRON\n");
        } else if (args && args[0] >= '0' && args[0] <= '3') {
            int tid = args[0] - '0';
            const char *n = theme_apply(tid);
            vga_printf(" theme switched to %s\n", n);
        } else {
            int cur = theme_current();
            if (cur >= 0 && cur < 4)
                vga_printf(" current theme: %s\n", tnames[cur]);
            else
                vga_puts(" current theme: ?\n");
            vga_puts(" usage: theme [0-3] | theme list\n");
        }
    } else if (strcmp(cmd, "reboot") == 0) {
        machine_reboot();
    } else if (strcmp(cmd, "shutdown") == 0 || strcmp(cmd, "poweroff") == 0 || strcmp(cmd, "power") == 0) {
        machine_poweroff();
    } else {
        vga_printf(" %s: %s (help)\n", i18n_get(I18N_CMD_NOT_FOUND), cmd);
    }
}

/* Shell 主循环 */
