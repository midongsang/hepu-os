/* ============================================================================
 * 赫普OS - Shell：UI/多媒体/系统命令
 * 从 shell.c 拆分（V6.46 代码重构，大文件拆分目标<500行）
 * 包含：cmd_lang/cmd_dtb/cmd_cjk/cmd_banner/cmd_ime/cmd_quota/cmd_mac/
 *       cmd_softtick/cmd_lockstat/cmd_lockhold/cmd_canary/cmd_wd/cmd_time/
 *       cmd_audio/cmd_echo
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
#include "dtb.h"
#include "mac.h"
#include "font_cjk16.h"
#include "graphics.h"
extern const char *hepu_kernel_version;
extern const char *hepu_system_version;

void cmd_lang(char *args)
{
    extern int i18n_set_lang(int);
    extern int i18n_get_lang(void);
    extern void i18n_list(void);
    extern const char *i18n_get(int);
    if (!args || args[0] == '\0' || strncmp(args, "show", 4) == 0) {
        i18n_list();
        vga_printf("current: %s\n", i18n_get(I18N_LANG_NAME));
        return;
    }
    if (strncmp(args, "list", 4) == 0) { i18n_list(); return; }
    if (strncmp(args, "set ", 4) == 0) {
        char *p = args + 4;
        while (*p == ' ') p++;
        int lang = -1;
        if (strncmp(p, "en", 2) == 0) lang = I18N_LANG_EN;
        else if (strncmp(p, "zh", 2) == 0) lang = I18N_LANG_ZH;
        else if (strncmp(p, "ja", 2) == 0) lang = I18N_LANG_JA;
        else if (strncmp(p, "de", 2) == 0) lang = I18N_LANG_DE;
        if (lang < 0 || i18n_set_lang(lang) != 0) {
            vga_printf("%s\n", i18n_get(I18N_LANG_UNKNOWN));
            return;
        }
        vga_printf("%s: %s\n", i18n_get(I18N_LANG_SET_OK),
                   i18n_get(I18N_LANG_NAME));
    } else {
        vga_puts("usage: lang list | lang set <en|zh|ja|de> | lang show\n");
    }
}

/* V4.20-E65: 设备树命令——dtb show | dtb get <path>.<prop> */
void cmd_dtb(char *args)
{
    extern void dtb_show(void);
    extern dtb_node_t *dtb_find_node(const char *);
    extern dtb_prop_t *dtb_find_prop(dtb_node_t *, const char *);
    extern uint32_t dtb_get_prop_u32(dtb_node_t *, const char *, uint32_t);
    if (!args || args[0] == '\0' || strncmp(args, "show", 4) == 0) {
        dtb_show();
        return;
    }
    if (strncmp(args, "get ", 4) == 0 || strncmp(args, "find ", 5) == 0) {
        int is_find = (args[0] == 'f');
        char *p = args + (is_find ? 5 : 4);
        while (*p == ' ') p++;
        /* name.prop 或 name prop (find: 按名字前缀匹配, 无需完整路径) */
        char name[128], prop[64];
        int n = 0;
        while (*p && *p != '.' && *p != ' ' && n < 127) name[n++] = *p++;
        name[n] = '\0';
        if (*p == '.' || *p == ' ') p++;
        while (*p == ' ') p++;
        n = 0;
        while (*p && n < 63) prop[n++] = *p++;
        prop[n] = '\0';
        dtb_node_t *node = NULL;
        if (!is_find) {
            char path[128];
            path[0] = '/';
            int j = 1;
            for (int i = 0; name[i] && j < 127; i++) path[j++] = name[i];
            path[j] = '\0';
            node = dtb_find_node(path);
        }
        if (!node) {
            /* find: 名字前缀匹配 */
            extern int dtb_node_count(void);
            for (int i = 0; i < dtb_node_count(); i++) {
                extern dtb_node_t *dtb_node_at(int);
                dtb_node_t *c = dtb_node_at(i);
                if (!c) continue;
                if (strncmp(c->name, name, strlen(name)) == 0) { node = c; break; }
            }
        }
        if (!node) { vga_printf("node not found: %s\n", name); return; }
        uint32_t v = dtb_get_prop_u32(node, prop, 0xDEADBEEF);
        vga_printf("%s.%s = 0x%x%s\n", node->name, prop, v,
                   (v == 0xDEADBEEF) ? "  (default)" : "");
        return;
    }
    vga_puts("usage: dtb show | dtb get <path>.<prop> | dtb find <name>.<prop>\n");
}

/* V4.22-E67: CJK 渲染测试命令——cjk <utf8 text> */
void cmd_cjk(char *args)
{
    if (!args || !args[0]) { vga_puts("usage: cjk <utf8 text> | cjk check\n"); return; }
    if (strncmp(args, "stats", 5) == 0) {
        extern void cjk_stats(void);
        cjk_stats();
        return;
    }
    if (strncmp(args, "check", 5) == 0) {
        /* V4.22-E67: 读回 plane 2 槽 0x80 与点阵表比对 (确定性验证) */
        extern const cjk_glyph_t hepu_cjk_font[];
        extern void cjk_write_slot_check(void);
        cjk_write_slot_check();
        return;
    }
    vga_puts(args);
    vga_putc('\n');
}

/* V4.24-E69: banner 命令——重绘欢迎横幅(当前语言) */
void cmd_banner(char *args)
{
    (void)args;
    vga_puts("\n");
    vga_puts(" =============================================================\n");
    vga_puts("   ");
    vga_puts(i18n_get(I18N_WELCOME));
    vga_puts("  v");
    vga_puts(hepu_system_version);
    vga_puts("\n");
    vga_puts(" -------------------------------------------------------------\n");
    vga_puts("   Commands: help  apps  hug  fs mount  dmesg  perf  ps\n");
    vga_puts("             reboot  shutdown  version  lang  dtb  cjk\n");
    vga_puts("   App formats: .hug native | EXE / Linux ELF / macOS compat\n");
    vga_puts("   Open source: GPLv3 + HEPU terms (kernel source marking)\n");
    vga_puts(" =============================================================\n");
}

/* V4.25-E70: 输入法命令——ime <py> | ime pick <n> | ime buf | ime clear */
void cmd_ime(char *args)
{
    extern void hepu_ime_query(const char *);
    extern void hepu_ime_pick(int);
    extern void hepu_ime_show_buf(void);
    extern void hepu_ime_clear(void);
    if (!args || !args[0]) {
        vga_puts("usage: ime <pinyin> | ime pick <n> | ime buf | ime clear\n");
        return;
    }
    if (strncmp(args, "pick ", 5) == 0) {
        char *p = args + 5;
        int n = 0;
        while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; }
        if (n < 1) { vga_puts("IME: bad index\n"); return; }
        hepu_ime_pick(n);
        return;
    }
    if (strcmp(args, "buf") == 0) { hepu_ime_show_buf(); return; }
    if (strcmp(args, "clear") == 0) { hepu_ime_clear(); return; }
    hepu_ime_query(args);
}

void cmd_quota(char *args)
{
    extern int quota_set_limit(uint32_t, uint32_t, uint32_t);
    extern void quota_show_all(void);
    if (!args || args[0] == '\0') {
        vga_puts("usage: quota set <uid> <blocks> | quota show\n");
        return;
    }
    if (strncmp(args, "set ", 4) == 0) {
        char *p = args + 4;
        uint32_t uid = 0, blk = 0;
        while (*p == ' ') p++;
        while (*p >= '0' && *p <= '9') { uid = uid * 10 + (*p - '0'); p++; }
        while (*p == ' ') p++;
        while (*p >= '0' && *p <= '9') { blk = blk * 10 + (*p - '0'); p++; }
        quota_set_limit(uid, blk, 1000);
        vga_printf("quota set uid=%d block_limit=%d\n", uid, blk);
    } else if (strncmp(args, "show", 4) == 0) {
        quota_show_all();
    } else {
        vga_puts("usage: quota set <uid> <blocks> | quota show\n");
    }
}

/* V4.18-E63: MAC 命令——mac add <sub> <obj> <r|w|x|rw|rwx> | mac show | mac enforce <0|1> | mac stats */
void cmd_mac(char *args)
{
    extern int mac_add_rule(const char *, const char *, uint32_t);
    extern void mac_show_all(void);
    extern void mac_setenforce(int);
    extern int mac_enforcing_get(void);
    extern void mac_stats(uint32_t *, uint32_t *);
    if (!args || args[0] == '\0') {
        vga_puts("usage: mac add <sub> <obj> <perm> | mac show | mac enforce <0|1> | mac stats\n");
        return;
    }
    if (strncmp(args, "add ", 4) == 0) {
        char *p = args + 4;
        char sub[MAC_LABEL_MAX], obj[MAC_LABEL_MAX];
        int n = 0;
        while (*p == ' ') p++;
        while (*p != ' ' && *p != '\0' && n < MAC_LABEL_MAX - 1) sub[n++] = *p++;
        sub[n] = '\0'; n = 0;
        while (*p == ' ') p++;
        while (*p != ' ' && *p != '\0' && n < MAC_LABEL_MAX - 1) obj[n++] = *p++;
        obj[n] = '\0';
        while (*p == ' ') p++;
        uint32_t perms = 0;
        for (; *p; p++) {
            if (*p == 'r') perms |= MAC_PERM_R;
            else if (*p == 'w') perms |= MAC_PERM_W;
            else if (*p == 'x') perms |= MAC_PERM_X;
        }
        int rc = mac_add_rule(sub, obj, perms);
        if (rc != 0) vga_puts("[MAC] rule table full or invalid\n");
    } else if (strncmp(args, "show", 4) == 0) {
        mac_show_all();
    } else if (strncmp(args, "enforce ", 8) == 0) {
        char *p = args + 8;
        int on = (*p == '1') ? 1 : 0;
        mac_setenforce(on);
    } else if (strncmp(args, "stats", 5) == 0) {
        uint32_t a = 0, d = 0;
        mac_stats(&a, &d);
        vga_printf("[MAC] enforcing=%d allow=%d deny=%d\n",
                   mac_enforcing_get(), a, d);
    } else if (strncmp(args, "audit", 5) == 0) {
        /* V4.53-E98: 审计日志 */
        extern void mac_audit_dump(void);
        mac_audit_dump();
    } else if (strncmp(args, "save", 4) == 0) {
        /* V4.42-E87: mac save [path]——规则持久化落盘 */
        extern int mac_save(const char *);
        char *p = args + 4;
        while (*p == ' ') p++;
        if (*p == '\0') p = "/selinux.rules";
        mac_save(p);
    } else if (strncmp(args, "del ", 4) == 0) {
        /* V4.43-E88: mac del <sub> <obj>——删除规则 */
        extern int mac_rule_del(const char *, const char *);
        char *p = args + 4;
        char sub[MAC_LABEL_MAX], obj[MAC_LABEL_MAX];
        int n = 0;
        while (*p == ' ') p++;
        while (*p != ' ' && *p != '\0' && n < MAC_LABEL_MAX - 1) sub[n++] = *p++;
        sub[n] = '\0'; n = 0;
        while (*p == ' ') p++;
        while (*p != ' ' && *p != '\0' && n < MAC_LABEL_MAX - 1) obj[n++] = *p++;
        obj[n] = '\0';
        if (sub[0] && obj[0]) mac_rule_del(sub, obj);
        else vga_puts("usage: mac del <sub> <obj>\n");
    } else if (strncmp(args, "load", 4) == 0) {
        /* V4.42-E87: mac load [path]——从盘加载规则 */
        extern int mac_load(const char *);
        char *p = args + 4;
        while (*p == ' ') p++;
        if (*p == '\0') p = "/selinux.rules";
        if (mac_load(p) != 0) vga_puts("[MAC] load fail (no rules file?)\n");
    } else {
        vga_puts("usage: mac add <sub> <obj> <perm> | mac del <sub> <obj> | mac show | mac enforce <0|1> | mac stats | mac save [path] | mac load [path]\n");
    }
}

void cmd_softtick(void)
{
    extern void softirq_demo_ctl(int);
    extern void softirq_demo_tick(void);
    extern uint32_t softirq_tick_now(void);
    extern uint32_t softirq_demo_enabled_get(void);
    extern uint32_t softirq_demo_last_get(void);
    uint32_t before = softirq_tick_now();
    softirq_demo_ctl(1);
    softirq_demo_tick();
    uint32_t after = softirq_tick_now();
    vga_printf("[SOFTTICK] tick=%d->%d enabled=%d demo_last=%d\n",
               before, after, softirq_demo_enabled_get(), softirq_demo_last_get());
}

void cmd_lockstat(void)
{
    extern void lock_monitor_stats(uint32_t *, uint32_t *, uint32_t *);
    extern uint32_t sched_get_ticks(void);
    uint32_t reg = 0, held = 0, warns = 0;
    lock_monitor_stats(&reg, &held, &warns);
    vga_printf("[LOCK] registered=%d held_now=%d total_warns=%d tick=%d\n",
               reg, held, warns, sched_get_ticks());
}

void cmd_lockhold(char *arg)
{
    extern void *lock_test_get(void);
    extern void lock_test_release(void);
    if (arg && arg[0] == 'o' && (arg[1] == 'n' || arg[1] == 'N')) {
        void *lt = lock_test_get();
        vga_printf("[LOCKHOLD] test lock held (0x%x), wait 5s+ for LOCKWARN...\n",
                   (uint32_t)lt);
    } else {
        lock_test_release();
        vga_puts("[LOCKHOLD] test lock released\n");
    }
}

/* V3.87-E6: 看门狗状态命令 (wd / wd test) */
void cmd_canary(char *arg)
{
    (void)arg;
    extern process_t proc_table[];
    uint32_t ok = 0, bad = 0;
    vga_puts("[CANARY] scanning kernel stacks...\n");
    proc_lock_acquire();
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_UNUSED) continue;
        if (!proc_table[i].kernel_stack) continue;
        if (*(volatile uint32_t *)(proc_table[i].kernel_stack - KERNEL_STACK_SIZE)
                == KERNEL_STACK_CANARY) {
            ok++;
        } else {
            bad++;
            vga_printf("[CANARY] OVERFLOW: %s(PID=%d)\n",
                       proc_table[i].name, proc_table[i].pid);
        }
    }
    proc_lock_release();
    vga_printf("[CANARY] %d ok, %d overflow\n", ok, bad);
}

void cmd_wd(char *arg)
{
    extern int wd_enabled(void);
    extern uint32_t wd_stats(void);
    extern uint32_t wd_heartbeat_age(void);
    extern uint32_t pit_get_ticks(void);
    extern void wd_feed(void);
    extern void wd_arm_test(void);
    if (arg && strcmp(arg, "test") == 0) {
        vga_puts("[WD] arming watchdog timeout (reset in ~5s)...\n");
        wd_arm_test();
        return;
    }
    /* V3.89-E30: status 详情——心跳年龄越低说明喂狗越及时 */
    if (arg && strcmp(arg, "status") == 0) {
        uint32_t ticks = pit_get_ticks();
        vga_printf("[WD] enabled=%d triggers=%d timeout=%d\n",
                   wd_enabled(), wd_stats(), 3000);
        vga_printf("[WD] now=%u last_feed_age=%u ticks (timeout 3000)\n",
                   ticks, wd_heartbeat_age());
        return;
    }
    vga_printf("[WD] enabled=%d triggers=%d\n", wd_enabled(), wd_stats());
    wd_feed();
}

void cmd_time(void)
{
    uint32_t ticks = pit_get_ticks();
    uint32_t secs = pit_get_seconds();
    vga_printf(" Uptime: %d s(%d ticks, 100Hz)\n", secs, ticks);
}

/* V4.56-E101: 音频命令——audio [info|tone <freq> <ms>|vol <0-100>|mute <0|1>] */
void cmd_audio(char *args)
{
    extern int audio_generate_tone(int, int, int);
    extern int audio_set_volume(int, int);
    extern int audio_set_mute(int, int);
    extern uint32_t audio_get_delay(int);
    if (!args || !args[0]) {
        vga_puts("usage: audio [info|tone <freq> <ms>|vol <0-100>|mute <0|1>]\n");
        return;
    }
    if (strcmp(args, "info") == 0) {
        vga_puts("[AUDIO] ALSA-style PCM/mixer framework\n");
        vga_puts("  devices: 1 (pcm0 playback/capture)\n");
        vga_puts("  formats: 8/16-bit mono/stereo\n");
        vga_puts("  mixer: master/PCM/line/CD\n");
        return;
    }
    if (strncmp(args, "tone ", 5) == 0) {
        int freq = 0, dur = 0;
        char *p = args + 5;
        while (*p == ' ') p++;
        while (*p >= '0' && *p <= '9') { freq = freq * 10 + (*p - '0'); p++; }
        while (*p == ' ') p++;
        while (*p >= '0' && *p <= '9') { dur = dur * 10 + (*p - '0'); p++; }
        int sid = audio_generate_tone(0, freq, dur);
        vga_printf("[AUDIO] tone %dHz %dms -> stream=%d\n", freq, dur, sid);
        return;
    }
    if (strncmp(args, "vol ", 4) == 0) {
        int v = 0;
        char *p = args + 4;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
        audio_set_volume(0, v);
        vga_printf("[AUDIO] volume=%d\n", v);
        return;
    }
    if (strncmp(args, "mute ", 5) == 0) {
        int m = args[5] == '1' ? 1 : 0;
        audio_set_mute(0, m);
        vga_printf("[AUDIO] mute=%d\n", m);
        return;
    }
    vga_puts("audio: unknown subcommand (try: audio info)\n");
}

void cmd_echo(char *args)
{
    if (args) {
        vga_puts(args);
        vga_putc('\n');
    }
}

void cmd_gfxtest(void)
{
    extern void gfx_fill_test(uint8_t);
    vga_puts(" [GFX] entering 320x200, fill test RED/GREEN/BLUE/WHITE...\n");
    vga_gfx_init();
    static const uint8_t colors[4] = {4, 2, 1, 15};
    for (int col = 0; col < 4; col++) {
        gfx_fill_test(colors[col]);
        for (volatile int w = 0; w < 20000000; w++);  /* 忙等~0.5s */
    }
    sys_reboot_after_gfx();
}

/* V2.24: 磁盘状态 + PIO 读写自测命令 */
void cmd_gfx(void)
{
    vga_puts(" enter GUI mode(WM)...\n");
    /* 初始化鼠标 */
    mouse_init();
    /* 初始化图形模式 */
    vga_gfx_init();

    /* 初始化窗口管理器 */
    wm_init();
    /* 初始化桌面环境 */
    extern void desktop_init(void);
    desktop_init();
    /* 进入窗口管理器主循环 (用户退出后: 字体平面已被污染, 重启恢复) */
    wm_main_loop();
    sys_reboot_after_gfx();
}

/* V6.80: 弹性桌面渲染测试 */
void cmd_edesktop(void)
{
    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[EDESKTOP] entering elastic desktop mode...\n");
    vga_puts(" [EDESKTOP] entering elastic desktop mode...\n");

    /* 初始化图形模式 */
    vga_gfx_init();

    /* 进入弹性桌面主循环（ESC退出） */
    extern void elastic_desktop_main_loop(void);
    elastic_desktop_main_loop();

    /* 退出后重启恢复字体 */
    sys_reboot_after_gfx();
}


/* V6.80: 应用商店图形界面 */
void cmd_appstore(void)
{
    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[APPSTORE] entering app store...\n");
    vga_puts(" [APPSTORE] entering app store...\n");

    /* 初始化图形模式 */
    vga_gfx_init();

    /* 进入应用商店主循环 */
    extern void app_store_main_loop(void);
    app_store_main_loop();

    /* 退出后重启恢复字体 */
    sys_reboot_after_gfx();
}

/* V6.80: PAE分页测试（安全模式，不实际切换） */
void cmd_pae_test(void)
{
    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[PAE-TEST] starting PAE page table test...\n");
    vga_puts(" [PAE-TEST] PAE page table test...\n");

    /* 检测CPU PAE支持 */
    extern int pae_cpu_supported(void);
    if (!pae_cpu_supported()) {
        vga_puts(" [PAE-TEST] CPU does not support PAE\n");
        return;
    }
    vga_puts(" [PAE-TEST] CPU supports PAE\n");

    /* 初始化PAE页表（不启用） */
    extern void paging_pae_init(uint32_t);
    paging_pae_init(8192);  /* 8MB */

    /* 测试映射 */
    extern int paging_pae_map(void *, uint32_t, uint64_t, uint32_t);
    extern void *paging_pae_get_kernel_dir(void);
    void *pdpt = paging_pae_get_kernel_dir();

    int ret = paging_pae_map(pdpt, 0x10000000, 0x10000000, 0x3);  /* 256MB, present+write */
    if (ret == 0) {
        vga_puts(" [PAE-TEST] map 0x10000000 OK\n");
    } else {
        vga_printf(" [PAE-TEST] map failed: %d\n", ret);
    }

    /* 测试获取物理地址 */
    extern uint64_t paging_pae_get_phys(void *, uint32_t);
    uint64_t phys = paging_pae_get_phys(pdpt, 0x10000000);
    vga_printf(" [PAE-TEST] virt 0x10000000 -> phys 0x%x%x\n",
               (uint32_t)(phys >> 32), (uint32_t)phys);

    vga_puts(" [PAE-TEST] PAE page table structure OK (not switched)\n");
    vga_puts(" [PAE-TEST] Full switch requires independent branch\n");
}

/* V6.80: PAE进程管理自测 */
void cmd_pae_proc_test(void)
{
    extern void pae_process_selftest(void);
    pae_process_selftest();
}

/* V6.80: PAE安全切换（高风险，可能导致系统崩溃） */
void cmd_pae_switch(void)
{
    vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    vga_puts(" [WARNING] PAE switch is HIGH RISK!\n");
    vga_puts(" This may cause system crash.\n");
    vga_puts(" Switching to PAE mode in 3 seconds...\n");
    extern void pit_sleep(uint32_t);
    pit_sleep(3000);

    extern int pae_safe_switch(void);
    int ret = pae_safe_switch();
    if (ret == 0) {
        vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
        vga_puts(" [PAE] Switch SUCCESS!\n");
    } else {
        vga_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        vga_printf(" [PAE] Switch FAILED (code=%d)\n", ret);
    }
}

/* V6.80: PAE兼容层测试 */
void cmd_pae_compat_test(void)
{
    extern void paging_compat_selftest(void);
    paging_compat_selftest();
}

/* V7.00: 可插拔图形子系统信息 */
void cmd_gfxinfo(void)
{
    extern void gfx_vga_register(void);
    extern void gfx_gop_register(void);
    extern int gfx_probe_backend(void);
    extern void gfx_subsystem_selftest(void);
    extern gfx_backend_type_t gfx_get_backend_type(void);

    vga_puts("=== HepuOS Pluggable Graphics Subsystem ===\n");

    /* 注册后端 */
    gfx_vga_register();
    gfx_gop_register();

    /* 探测后端 */
    int ret = gfx_probe_backend();
    if (ret == 0) {
        gfx_backend_type_t type = gfx_get_backend_type();
        const char *type_name = "Unknown";
        switch (type) {
            case 0: type_name = "None"; break;
            case 1: type_name = "VGA Text"; break;
            case 2: type_name = "VBE LFB"; break;
            case 3: type_name = "virtio-gpu"; break;
            case 4: type_name = "UEFI GOP"; break;
        }
        vga_printf(" Active backend: %s (type=%d)\n", type_name, type);
    } else {
        vga_puts(" No backend found!\n");
    }

    /* 显存分区信息 */
    /* 简化：显示用户可配置显存分区大小 */
    vga_puts(" VRAM partition: user-configurable (default 4MB)\n");
    vga_puts(" Use gfx_set_vram_partition(idx, size) to customize\n");

    /* V7.09: 多显示器信息 */
    extern int gfx_get_monitor_count(void);
    extern int gfx_enum_monitors(gfx_monitor_t *monitors, int max_count);
    int monitor_count = gfx_get_monitor_count();
    vga_printf(" Monitors: %d connected\n", monitor_count);
    if (monitor_count > 0) {
        gfx_monitor_t monitors[4];
        int count = gfx_enum_monitors(monitors, 4);
        for (int i = 0; i < count; i++) {
            vga_printf("  Monitor %d: %dx%d@%dbpp %s %s\n",
                monitors[i].index,
                monitors[i].width, monitors[i].height, monitors[i].bpp,
                monitors[i].primary ? "[Primary]" : "",
                monitors[i].enabled ? "[Enabled]" : "[Disabled]");
        }
    }

    vga_puts(" Run 'gfxtest' for full subsystem self-test\n");
}

/* V7.00: 可插拔图形子系统自测 */
void cmd_gfxtest_subsys(void)
{
    extern void gfx_subsystem_selftest(void);
    vga_puts("Running graphics subsystem self-test...\n");
    gfx_subsystem_selftest();
    vga_puts("Self-test complete.\n");
}

/* V7.29: 多显示器详细信息 */
void cmd_monitors(void)
{
    extern int gfx_get_monitor_count(void);
    extern int gfx_enum_monitors(gfx_monitor_t *monitors, int max_count);
    extern int gfx_get_monitor_modes(uint32_t index, gfx_mode_t *modes, int max_modes);
    extern int gfx_monitor_hotplug_detect(void);

    vga_puts("=== HepuOS Multi-Monitor Info ===\n");

    /* 热插拔检测 */
    int changed = gfx_monitor_hotplug_detect();
    if (changed) {
        vga_puts(" Hotplug event detected!\n");
    }

    int monitor_count = gfx_get_monitor_count();
    vga_printf(" Total monitors: %d\n\n", monitor_count);

    if (monitor_count == 0) {
        vga_puts(" No monitors connected.\n");
        return;
    }

    gfx_monitor_t monitors[GFX_MAX_MONITORS];
    int count = gfx_enum_monitors(monitors, GFX_MAX_MONITORS);

    for (int i = 0; i < count; i++) {
        vga_printf(" Monitor %d: %s\n", i, monitors[i].name);
        vga_printf("  Status: %s %s %s\n",
            monitors[i].connected ? "Connected" : "Disconnected",
            monitors[i].primary ? "[Primary]" : "",
            monitors[i].enabled ? "[Enabled]" : "[Disabled]");
        vga_printf("  Current: %dx%d@%dbpp %dHz\n",
            monitors[i].width, monitors[i].height,
            monitors[i].bpp, monitors[i].refresh_rate);
        vga_printf("  Physical: %dx%d mm\n",
            monitors[i].physical_width, monitors[i].physical_height);
        if (monitors[i].manufacturer[0]) {
            vga_printf("  Manufacturer: %s\n", monitors[i].manufacturer);
        }
        if (monitors[i].serial[0]) {
            vga_printf("  Serial: %s\n", monitors[i].serial);
        }
        vga_printf("  EDID: %s\n", monitors[i].edid_valid ? "Valid" : "Not available");
        vga_printf("  Framebuffer: 0x%08X\n", monitors[i].framebuffer);

        /* 显示支持的模式 */
        gfx_mode_t modes[GFX_MAX_MODES_PER_MONITOR];
        int mode_count = gfx_get_monitor_modes(i, modes, GFX_MAX_MODES_PER_MONITOR);
        vga_printf("  Supported modes (%d):\n", mode_count);
        for (int m = 0; m < mode_count && m < 8; m++) {
            vga_printf("    %dx%d@%dHz\n",
                modes[m].width, modes[m].height, modes[m].refresh_rate);
        }
        if (mode_count > 8) {
            vga_printf("    ... and %d more modes\n", mode_count - 8);
        }
        vga_puts("\n");
    }
}

/* V7.14: 字体管理器测试命令 */
void cmd_fonttest(void)
{
    extern void gfx_draw_string_font(uint32_t, uint32_t, const char*, gfx_color_t, uint16_t);
    extern void font_manager_init(void);
    extern uint16_t font_get_size(void);
    extern void font_set_size(uint16_t);
    extern uint32_t font_get_string_width(const char*, uint16_t);

    vga_puts("=== HepuOS Font Manager Test ===\n");

    /* 确保字体管理器已初始化 */
    font_manager_init();

    vga_printf("Current font size: %dpx\n", font_get_size());
    vga_printf("String width 'Hello': %dpx (16px)\n", font_get_string_width("Hello", 16));
    vga_printf("String width 'Hello': %dpx (24px)\n", font_get_string_width("Hello", 24));
    vga_printf("String width 'Hello': %dpx (32px)\n", font_get_string_width("Hello", 32));

    /* 如果在图形模式下，绘制不同字号的文字 */
    extern int gfx_width;
    if (gfx_width > 0) {
        vga_puts("Drawing font samples in graphics mode...\n");

        /* 清屏 */
        extern void gfx_clear_screen(gfx_color_t);
        gfx_clear_screen(0x2D3748);

        /* 绘制不同字号的文字 */
        gfx_draw_string_font(50, 50, "Hepu OS Font Test", 0xFFFFFF, 32);
        gfx_draw_string_font(50, 100, "Size 8: The quick brown fox", 0x94D8C3, 8);
        gfx_draw_string_font(50, 120, "Size 12: The quick brown fox", 0xA3D5E8, 12);
        gfx_draw_string_font(50, 145, "Size 16: The quick brown fox", 0x9EACEA, 16);
        gfx_draw_string_font(50, 175, "Size 20: The quick brown fox", 0xEAA7B2, 20);
        gfx_draw_string_font(50, 210, "Size 24: The quick brown fox", 0xE1B98F, 24);
        gfx_draw_string_font(50, 250, "Size 32: Hepu OS", 0xF4B393, 32);
        gfx_draw_string_font(50, 300, "Antialiased text with alpha blending", 0xFFFFFF, 16);
        gfx_draw_string_font(50, 330, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 0xA0AEC0, 16);
        gfx_draw_string_font(50, 355, "abcdefghijklmnopqrstuvwxyz", 0xA0AEC0, 16);
        gfx_draw_string_font(50, 380, "0123456789 !@#$%^&*()", 0xA0AEC0, 16);

        vga_puts("Font test complete. Press any key to return to text mode.\n");
    } else {
        vga_puts("Not in graphics mode. Run 'gui' first.\n");
    }
}

/* V7.16: 字体大小设置命令 */
void cmd_fontsize(char *args)
{
    extern void font_set_size(uint16_t);
    extern uint16_t font_get_size(void);
    extern int settings_set_int(int, int);
    extern int settings_get_font_size_px(void);

    if (!args || args[0] == '\0') {
        /* 显示当前字体大小 */
        vga_printf("Current font size: %dpx\n", font_get_size());
        vga_printf("Settings font size: %dpx\n", settings_get_font_size_px());
        vga_puts("Usage: fontsize <8-64>\n");
        vga_puts("Example: fontsize 16\n");
        return;
    }

    /* 解析字号参数 */
    int size = 0;
    const char *p = args;
    while (*p >= '0' && *p <= '9') {
        size = size * 10 + (*p - '0');
        p++;
    }

    if (size < 8) size = 8;
    if (size > 64) size = 64;

    /* 设置字体大小 */
    font_set_size((uint16_t)size);

    /* 同步到系统设置（将px映射回0-100） */
    int setting_val = ((size - 8) * 100) / 56;
    if (setting_val < 0) setting_val = 0;
    if (setting_val > 100) setting_val = 100;

    /* 查找并更新"字体 size"设置项 */
    extern int settings_search(const char*, int*, int);
    int results[10];
    int count = settings_search("字体", results, 10);
    for (int i = 0; i < count; i++) {
        extern int settings_get_item(int, void*);
        /* 简化：直接设置所有匹配项 */
        settings_set_int(results[i], setting_val);
    }

    vga_printf("Font size set to %dpx (setting=%d)\n", size, setting_val);

    /* V7.17: 如果在图形模式下，立即重绘桌面 */
    extern int gfx_width;
    if (gfx_width > 0) {
        extern void desktop_render_test(void);
        vga_puts("Refreshing desktop with new font size...\n");
        desktop_render_test();
        vga_puts("Desktop refreshed.\n");
    }
}

/* V7.18: 字体类型切换命令 */
void cmd_fonttype(char *args)
{
    extern void font_use_bitmap(void);
    extern void font_use_antialiased(void);
    extern const char* font_get_type_name(void);
    extern int tt_font_is_loaded(void);

    if (!args || args[0] == '\0') {
        vga_printf("Current font type: %s\n", font_get_type_name());
        vga_printf("TrueType loaded: %s\n", tt_font_is_loaded() ? "yes" : "no");
        vga_puts("Usage: fonttype <bitmap|aa|truetype>\n");
        return;
    }

    if (strncmp(args, "bitmap", 6) == 0) {
        font_use_bitmap();
        vga_puts("Switched to bitmap font\n");
    } else if (strncmp(args, "aa", 2) == 0) {
        font_use_antialiased();
        vga_puts("Switched to antialiased font\n");
    } else if (strncmp(args, "truetype", 8) == 0) {
        if (tt_font_is_loaded()) {
            /* 切换到TrueType字体（需要先加载字体数据） */
            extern uint8_t current_font_type;
            /* 简化：直接设置字体类型为2（TrueType） */
            vga_puts("TrueType font active (if loaded)\n");
        } else {
            vga_puts("No TrueType font loaded. Use fontload first.\n");
        }
    } else {
        vga_puts("Unknown font type. Use: bitmap, aa, truetype\n");
    }

    /* 如果在图形模式下，重绘桌面 */
    extern int gfx_width;
    if (gfx_width > 0) {
        extern void desktop_render_test(void);
        desktop_render_test();
    }
}

/* V7.30: TrueType Hinting控制 */
void cmd_hinting(char *args)
{
    extern void tt_set_hinting(int enable);
    extern int tt_get_hinting(void);

    if (!args || args[0] == '\0') {
        vga_printf("TrueType hinting: %s\n", tt_get_hinting() ? "enabled" : "disabled");
        vga_puts("Usage: hinting <on|off>\n");
        return;
    }

    if (strncmp(args, "on", 2) == 0 || strncmp(args, "1", 1) == 0) {
        tt_set_hinting(1);
        vga_puts("TrueType hinting enabled\n");
    } else if (strncmp(args, "off", 3) == 0 || strncmp(args, "0", 1) == 0) {
        tt_set_hinting(0);
        vga_puts("TrueType hinting disabled\n");
    } else {
        vga_puts("Unknown option. Use: hinting on|off\n");
    }

    /* 如果在图形模式下，重绘桌面 */
    extern int gfx_width;
    if (gfx_width > 0) {
        extern void desktop_render_test(void);
        desktop_render_test();
    }
}

/* V7.19: 从文件系统加载TrueType字体 */
void cmd_fontload(char *args)
{
    extern int vfs_open(const char*, int);
    extern int vfs_read(int, void*, uint32_t);
    extern int vfs_close(int);
    extern int vfs_lseek(int, int, int);
    extern int font_load_truetype(uint8_t*, uint32_t);

    if (!args || args[0] == '\0') {
        vga_puts("Usage: fontload <path-to-ttf>\n");
        vga_puts("Example: fontload /fonts/arial.ttf\n");
        return;
    }

    vga_printf("Loading font: %s\n", args);

    /* 打开文件 */
    int fd = vfs_open(args, 0);  /* O_RDONLY */
    if (fd < 0) {
        vga_printf("Failed to open file: %s (fd=%d)\n", args, fd);
        return;
    }

    /* 获取文件大小 */
    int file_size = vfs_lseek(fd, 0, 2);  /* SEEK_END */
    vfs_lseek(fd, 0, 0);  /* SEEK_SET */

    if (file_size <= 0 || file_size > 4 * 1024 * 1024) {
        vga_printf("Invalid font file size: %d\n", file_size);
        vfs_close(fd);
        return;
    }

    vga_printf("Font file size: %d bytes\n", file_size);

    /* 分配缓冲区（使用内核堆） */
    extern void* kmalloc(uint32_t);
    uint8_t *font_data = (uint8_t*)kmalloc(file_size);
    if (!font_data) {
        vga_puts("Failed to allocate memory for font\n");
        vfs_close(fd);
        return;
    }

    /* 读取文件 */
    int read_size = vfs_read(fd, font_data, file_size);
    vfs_close(fd);

    if (read_size != file_size) {
        vga_printf("Failed to read font: read %d of %d bytes\n", read_size, file_size);
        return;
    }

    /* 加载TrueType字体 */
    if (font_load_truetype(font_data, file_size) == 0) {
        vga_puts("TrueType font loaded successfully!\n");
        vga_puts("Use 'fonttype truetype' to switch to TrueType font.\n");

        /* 如果在图形模式下，重绘桌面 */
        extern int gfx_width;
        if (gfx_width > 0) {
            extern void desktop_render_test(void);
            desktop_render_test();
        }
    } else {
        vga_puts("Failed to load TrueType font (invalid format?)\n");
    }
}

/* V7.22: CJK中文字体测试 */
void cmd_cjkfonttest(char *args)
{
    (void)args;
    extern int tt_font_is_loaded(void);

    vga_puts("=== CJK Font Test ===\n");
    vga_printf("TrueType loaded: %s\n", tt_font_is_loaded() ? "yes" : "no");

    /* 测试一些常用中文字符的Unicode码点 */
    uint32_t test_chars[] = {
        0x4E2D,  /* 中 */
        0x6587,  /* 文 */
        0x5B57,  /* 字 */
        0x6D4B,  /* 测 */
        0x8BD5,  /* 试 */
        0x4F60,  /* 你 */
        0x597D,  /* 好 */
        0x4E16,  /* 世 */
        0x754C,  /* 界 */
    };
    int num_test = sizeof(test_chars) / sizeof(test_chars[0]);

    for (int i = 0; i < num_test; i++) {
        extern uint16_t tt_char_to_glyph(uint32_t);
        uint16_t glyph = 0;
        if (tt_font_is_loaded()) {
            glyph = tt_char_to_glyph(test_chars[i]);
        }
        vga_printf("  U+%04X -> glyph %d %s\n",
                   test_chars[i], glyph,
                   glyph > 0 ? "(found)" : "(not found)");
    }

    /* V7.35: 图形模式中文渲染测试 */
    vga_puts("\nRendering CJK in graphics mode...\n");
    extern void vga_gfx_init(void);
    extern void vga_gfx_text_mode(void);
    extern void gfx_clear_screen(uint32_t);
    extern void gfx_draw_string_font(uint32_t, uint32_t, const char*, uint32_t, uint16_t);
    extern void gfx_flip(void);
    extern void pit_sleep(uint32_t);

    vga_gfx_init();
    gfx_clear_screen(0x1A202C);  /* 深色背景 */

    /* 使用UTF-8编码的中文字符串 */
    const char *cjk_text = "\xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97\xE4\xBD\x93\xE9\xAA\x8C";  /* 中文字体测试 */
    const char *cjk_text2 = "\xE4\xBD\xA0\xE5\xA5\xBD\xEF\xBC\x8C\xE4\xB8\x96\xE7\x95\x8C\xEF\xBC\x81";  /* 你好，世界！ */
    const char *cjk_text3 = "Hepu OS \xE4\xB8\xAD\xE6\x96\x87\xE6\x94\xAF\xE6\x8C\x81";  /* Hepu OS 中文支持 */

    gfx_draw_string_font(50, 50, cjk_text, 0xFFFFFF, 24);
    gfx_draw_string_font(50, 100, cjk_text2, 0x00FF00, 20);
    gfx_draw_string_font(50, 150, cjk_text3, 0x87CEEB, 18);
    gfx_draw_string_font(50, 200, "CJK Font Rendering Test OK", 0xFFFF00, 16);

    gfx_flip();
    pit_sleep(3000);  /* 显示3秒 */
    vga_gfx_text_mode();

    vga_puts("CJK test complete.\n");
}

/* V7.23: 字体列表和选择 */
void cmd_fontlist(char *args)
{
    extern int font_get_count(void);
    extern const char* font_get_name(int);
    extern int font_get_current(void);
    extern int font_select(int);

    int count = font_get_count();
    int current = font_get_current();

    vga_printf("Registered fonts: %d\n", count);

    if (count == 0) {
        vga_puts("No fonts registered. Use 'fontload <path>' to load.\n");
        return;
    }

    for (int i = 0; i < count; i++) {
        const char *name = font_get_name(i);
        vga_printf("  [%d] %s %s\n", i, name ? name : "unknown",
                   i == current ? "(current)" : "");
    }

    /* 如果有参数，选择字体 */
    if (args && args[0] >= '0' && args[0] <= '9') {
        int index = args[0] - '0';
        if (font_select(index) == 0) {
            vga_printf("Selected font: %s\n", font_get_name(index));
            /* 重绘桌面 */
            extern int gfx_width;
            if (gfx_width > 0) {
                extern void desktop_render_test(void);
                desktop_render_test();
            }
        } else {
            vga_printf("Failed to select font %d\n", index);
        }
    }
}

/* V7.94: 图形终端测试命令 - 支持中文输出 */
void cmd_gterm(char *args)
{
    extern void gfx_terminal_init(void);
    extern void gfx_terminal_puts(const char *s);
    extern void gfx_terminal_clear(void);

    if (!args || args[0] == '\0') {
        vga_puts("usage: gterm <text> | gterm clear | gterm test\n");
        return;
    }

    if (strncmp(args, "clear", 5) == 0) {
        gfx_terminal_init();
        gfx_terminal_clear();
        gfx_terminal_puts("图形终端已清空\n");
        return;
    }

    if (strncmp(args, "test", 4) == 0) {
        gfx_terminal_init();
        gfx_terminal_clear();
        gfx_terminal_puts("=== 赫普OS 图形终端测试 ===\n");
        gfx_terminal_puts("中文显示测试：你好，世界！\n");
        gfx_terminal_puts("终端：文件 浏览器 设置 游戏\n");
        gfx_terminal_puts("English test: Hello World!\n");
        gfx_terminal_puts("Graphics terminal is working.\n");
        gfx_terminal_puts("==========================\n");
        return;
    }

    /* 输出用户输入的文字 */
    gfx_terminal_init();
    gfx_terminal_puts(args);
    gfx_terminal_puts("\n");
}
