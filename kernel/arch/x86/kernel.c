/* ============================================================================
 * 赫普OS (HEPU Hyper Evolve OS) - 内核主入口 V0.2
 * 新增：物理内存管理、分页、内核堆、进程管理、系统调用
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "hepu.h"
extern void syscall_dbg_check(const char *);
extern void serial_puts(unsigned short, const char *);
#include "vga.h"
#include "types.h"
#include "gdt.h"
#include "idt.h"
#include "fingerprint.h"
#include "mm.h"
#include "paging.h"
#include "kheap.h"
#include "sched.h"
#include "process.h"
#include "syscall.h"
#include "hal.h"
#include "ipc.h"
#include "string.h"
#include "pic.h"
#include "pit.h"
#include "irq.h"
#include "../x86_64/x86_64.h"
#include "keyboard.h"
#include "tss.h"
#include "initrd.h"
#include "vfs.h"
#include "devfs.h"
#include "net.h"
#include "touch.h"
#include "smp.h"
#include "dynld.h"
#include "../drivers/usb/usb_core.h"
#include "pm.h"
#include "security.h"
#include "dtb.h"
#include "audio.h"
#include "camera.h"
#include "bluetooth.h"
#include "wifi.h"
#include "arp.h"
#include "dhcp.h"
#include "dns.h"
#include "icmp.h"
#include "http.h"
#include "tls.h"
#include "tcp.h"
#include "websocket.h"
#include "x509.h"
#include "csprng.h"
#include "ecdhe.h"
#include "sha.h"
#include "hmac.h"
#include "hkdf.h"
#include "rsa.h"
#include "http2.h"
#include "tls_handshake.h"
#include "quic.h"
#include "http3.h"
#include "doq.h"
#include "quic_0rtt.h"
#include "quic_migration.h"
#include "quic_recovery.h"
#include "mp_quic.h"
#include "quic_datagram.h"
#include "qpack_dynamic.h"
#include "init_sequence.h"
#include "klog.h"
#include "kunit.h"
#include "kbench.h"
#include "kunit_log.h"
#include "services.h"
#include "gpu.h"
#include "sensors.h"
#include "gps.h"
#include "ota.h"
#include "codec.h"
#include "compositor.h"
#include "input.h"
#include "trace.h"
#include "media.h"
#include "camera_hal.h"
#include "ril.h"
#include "appframework.h"
#include "packagemanager.h"
#include "notification.h"
#include "windowsystem.h"
#include "share.h"
#include "account.h"
#include "systemui.h"
#include "webengine.h"
#include "ai.h"
#include "security2.h"
#include "media2.h"
#include "desktopmode.h"
#include "gameframework.h"
#include "enterprise.h"
#include "devtools.h"
#include "compat.h"
#include "fs2.h"
#include "net2.h"
#include "bt2.h"
#include "usb2.h"
#include "gfx2.h"
#include "camera2.h"
#include "audio2.h"
#include "telecom2.h"
#include "ai2.h"
#include "carlink.h"
#include "enterprise2.h"
#include "kernel_opt.h"
#include "drivers2.h"
#include "fs3.h"
#include "net3.h"
#include "appeco.h"
#include "kernel_core.h"
#include "drivers_real.h"
#include "userland.h"
#include "fs_real.h"
#include "bootchain.h"
#include "kdebug.h"
#include "kperf.h"
#include "distributed.h"
#include "hhal.h"
#include "hdpk.h"
#include "device_support.h"
#include "fp_enhanced.h"
#include "phone_service.h"
#include "sms_service.h"
#include "settings_app.h"
#include "file_manager.h"
#include "camera_app.h"
#include "gallery_app.h"
#include "browser_app.h"
#include "app_store.h"
#include "system_tools.h"
#include "music_player.h"
#include "video_player.h"
#include "photo_editor.h"
#include "voice_recorder.h"
#include "audio_effects.h"
#include "hug_sdk.h"
#include "hug_template.h"
#include "hug_debugger.h"
#include "hug_depman.h"
#include "apk_parser.h"
#include "microg_service.h"
#include "art_runtime.h"
#include "compat_layer.h"
#include "secure_boot.h"
#include "boot_optimizer.h"
#include "memory_optimizer.h"
#include "kernel_slimmer.h"
#include "perf_profiler.h"
#include "hepu_brand.h"
#include "app_container.h"
#include "distributed_os.h"
#include "car_connect.h"
#include "privacy_compute.h"
#include "realtime_kernel.h"
#include "power_optimizer.h"
#include "permission_manager.h"
#include "notification_center.h"
#include "i18n_system.h"
#include "multilingual_ime.h"
#include "backup_restore.h"
#include "storage_manager.h"
#include "multi_user.h"
#include "system_stability.h"
#include "app_freeze.h"
#include "id_randomizer.h"
#include "elastic_desktop.h"
#include "ai_notification.h"
#include "creator_mode.h"
#include "onboarding.h"
#include "lowmem_throttle.h"
#include "hdpk_template.h"
#include "android_api.h"
#include "driver_manager.h"
#include "bist.h"
#include "ai_assistant.h"
#include "socket.h"
#include "mount.h"
#include "quota.h"
#include "fs_registry.h"
#include "vfat.h"
#include "journal.h"
#include "ide.h"
#include "ext4.h"
#include "fscrypt.h"
#include "partition.h"
#include "unified_driver.h"
#include "ahci.h"
#include "nvme.h"
#include "exfat.h"
#include "ntfs.h"
#include "aes.h"
#include "eth.h"
#include "wifi.h"
#include "arp.h"
#include "dhcp.h"
#include "dns.h"
#include "icmp.h"
#include "http.h"
#include "tls.h"
#include "tcp.h"
#include "websocket.h"
#include "x509.h"
#include "csprng.h"
#include "ecdhe.h"
#include "sha.h"
#include "hmac.h"
#include "hkdf.h"
#include "rsa.h"
#include "http2.h"
#include "tls_handshake.h"
#include "quic.h"
#include "http3.h"
#include "doq.h"
#include "quic_0rtt.h"
#include "quic_migration.h"
#include "quic_recovery.h"
#include "mp_quic.h"
#include "quic_datagram.h"
#include "qpack_dynamic.h"
#include "init_sequence.h"
#include "kunit.h"
#include "kbench.h"
#include "kunit_log.h"

/* 用户态程序入口 (定义在 user/init/user_init.S) */
extern void user_init_start(void);
extern void user_init_end(void);

/* 第二个用户态进程 (定义在 user/apps/procb/procb.S) */
extern void procb_start(void);

/* initrd 镜像符号 (由 objcopy 生成) */
extern uint8_t _binary_build_v250_initrd_img_start[];
extern uint8_t _binary_build_v250_initrd_img_end[];

/* Multiboot 信息结构 */
typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint8_t  reserved[2];
} __attribute__((packed)) multiboot_info_t;

extern hepu_fingerprint_t hepu_fingerprint;

/* V4.10-E55: 版本体系修正——内核版本与系统版本分离:
 *  - hepu_kernel_version: 内核自身版本(独立演进)
 *  - hepu_system_version: 系统分发版本(与镜像命名 hepu-os-vX.Y.* 一致)
 * 0.9.0: 版本号格式统一为三段式(重要版本.默认版本.bug版本)
 *        重要版本=0(未到重要里程碑), 默认版本=9, bug版本=0
 * 0.10.0: 默认版本+1, 新增25个桌面应用, 完善图形子系统和应用生态 */
const char *hepu_kernel_version = "1.0.2";
const char *hepu_system_version = "0.10.5";
const char *hepu_kernel_name = "HEPU Hyper Evolve OS";

/* 简单的用户态测试函数（原型阶段在内核态模拟） */
static void __attribute__((unused)) user_test_function(void)
{
    /* 模拟用户态通过系统调用打印 */
    const char *msg = "  [user-mode sim] Hello! I am the first Hepu OS user process.\n";
    syscall_handler(SYS_PRINT, (uint32_t)msg, 0, 0, 0);

    uint32_t ver_ptr = syscall_handler(SYS_HEPU_INFO, 0, 0, 0, 0);
    syscall_handler(SYS_PRINT, ver_ptr, 0, 0, 0);
    syscall_handler(SYS_PRINT, (uint32_t)"\n", 0, 0, 0);
}

#ifdef HEPU_BOOT_DEBUG
/* V2.08: 内核启动蜂鸣诊断已移除——用户明确要求去蜂鸣, 改用文本日志.
   启动进度完全依赖屏幕KLOG文本(K0-K5, boot.S VGA直写) + 引导器行8-12文本标签. */
#endif


/* 初始化函数（定义在kernel_init.c） */
extern void hepu_sched_init(void);
extern void hepu_mem_init(uint32_t mem_kb);
extern void hepu_irq_init(void);
extern void hepu_fingerprint_check(void);

/* 64位支持检测（使用CPUID） */
int x86_64_detect(void)
{
    uint32_t eax, ebx, ecx, edx;
    /* 检测CPUID是否支持 */
    __asm__ volatile(
        "pushfl\n"
        "popl %%eax\n"
        "movl %%eax, %%ecx\n"
        "xorl $0x200000, %%eax\n"
        "pushl %%eax\n"
        "popfl\n"
        "pushfl\n"
        "popl %%eax\n"
        "xorl %%ecx, %%eax\n"
        : "=a"(eax) : : "ecx"
    );
    if (!(eax & 0x200000)) return 0;
    /* 检测扩展功能 */
    eax = 0x80000000;
    __asm__ volatile("cpuid" : "=a"(eax) : "a"(eax) : "ebx", "ecx", "edx");
    if (eax < 0x80000001) return 0;
    /* 检测长模式支持（EDX位29） */
    eax = 0x80000001;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(eax));
    return (edx & (1 << 29)) ? 1 : 0;
}

void hepu_kernel_main(uint32_t magic, multiboot_info_t *mb_info)
{
    (void)magic;
#ifdef HEPU_BOOT_DEBUG
    /* 诊断K4: 进入内核主函数(BSS清零+call都成功).
       屏幕日志主通道: 显示"K4 MAIN CALL"到行4(与boot.S的KLOG一致).
       V2.08: 已去除K4蜂鸣7次, 纯文本日志. */
    {
        extern char klog4[];
        __asm__ volatile(
            "movw $0x10, %%ax\n\t"
            "movw %%ax, %%es\n\t"
            "movl $0xB8000, %%edi\n\t"
            "movl $640, %%ecx\n\t"       /* 行4*160 */
            "addl %%ecx, %%edi\n\t"
            "movl %0, %%esi\n\t"
            "1: lodsb\n\t"
            "testb %%al, %%al\n\t"
            "jz 2f\n\t"
            "movb %%al, %%es:(%%edi)\n\t"
            "movb $0x0F, %%es:1(%%edi)\n\t"
            "addl $2, %%edi\n\t"
            "jmp 1b\n\t"
            "2:\n\t"
            : : "r"(klog4) : "ax", "cx", "di", "si", "memory");
    }
#endif

    /* ---- 基础初始化 ---- */
    vga_init();
    vga_clear();

    /* V3.81 调试: 提前初始化 COM1 串口(原在TSS后), 让 QEMU -serial file 记录全部日志 */
    {
        extern void serial_init_com1(void);
        serial_init_com1();
    /* EARLY 探针: 启动最早期读表区物理内容, 区分"引导器未加载.data"与"运行中写坏" */
    extern void serial_puthex(uint16_t, uint32_t);
    {
        uint32_t t0 = *(volatile uint32_t *)0x001498A0; /* V3.82段重排后新表区 */
        uint32_t v0 = *(volatile uint32_t *)0x00149800;
        serial_puts(0x3F8, "[EARLY] tbl0=");
        serial_puthex(0x3F8, t0);
        serial_puts(0x3F8, " ver=");
        serial_puthex(0x3F8, v0);
        serial_puts(0x3F8, "\n");
    }
    }
    /* V3.82: dump SeaBIOS 文本模式 VGA 寄存器 (对照 text_mode 恢复序列) */
    extern void vga_dump_regs(void);
    vga_dump_regs();
    gdt_init();
    idt_init();
    extern void early_console_init(void);
    early_console_init();
    extern void multiboot_init(uint32_t, void *);
    multiboot_init(magic, mb_info);

    /* V8.51: 64位支持检测（P2-1第一步：检测CPU能力，不切换长模式） */
    {
        int support_64bit = x86_64_detect();
        serial_puts(0x3F8, "[CPU] 64-bit support: ");
        serial_puts(0x3F8, support_64bit ? "YES" : "NO");
        serial_puts(0x3F8, "\n");
        if (support_64bit) {
            vga_puts("[CPU] x86_64 support detected (long mode available)\n");
        } else {
            vga_puts("[CPU] x86_64 not supported (32-bit only)\n");
        }
    }

    /* V6.70: 方案A启动画面——深色背景+滚动初始化日志
     * 如果HepuBoot提供了32bpp帧缓冲，则使用boot_gfx绘制启动画面
     * V0.10.5: 添加自动兼容检测，低兼容级别禁用图形启动画面 */
    int boot_splash_active = 0;
    extern int compat_get_level(void);
    int compat_level = compat_get_level();
    if (compat_level < 2) {
        serial_puts(0x3F8, "[BOOT] compat level low, disabling boot splash\n");
    }
    if (mb_info && (mb_info->flags & (1 << 12)) && mb_info->framebuffer_bpp == 32 && compat_level >= 2) {
        extern void boot_gfx_init(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
        extern void boot_log_init(int, int, int);
        extern void boot_log(const char *, ...);
        extern void boot_log_time(const char *);

        boot_gfx_init((uint32_t)mb_info->framebuffer_addr,
                      mb_info->framebuffer_width,
                      mb_info->framebuffer_height,
                      mb_info->framebuffer_pitch,
                      mb_info->framebuffer_bpp);

        /* 绘制标题 */
        extern void boot_draw_string(int, int, const char *, uint32_t);
        extern void boot_fill_rect(int, int, int, int, uint32_t);
        boot_draw_string(50, 30, "HepuOS Kernel v0.10.0", 0x00FFFFFF);
        boot_draw_string(50, 50, "Hepu Hyper Evolve OS", 0x00808080);
        boot_fill_rect(50, 72, 600, 1, 0x00404040);

        /* 初始化日志区域 */
        boot_log_init(50, 90, 30);  /* x=50, y=90, 最多30行 */
        boot_log("[INIT] Boot splash active (GOP %dx%d 32bpp)",
                 mb_info->framebuffer_width, mb_info->framebuffer_height);
        boot_log_time("kernel entry");
        boot_splash_active = 1;
    }

    /* V6.52: HepuBoot安全/恢复模式检测 */
    extern int multiboot_is_safe_mode(void);
    extern int multiboot_is_recovery_mode(void);
    int safe_mode = multiboot_is_safe_mode();
    int recovery_mode = multiboot_is_recovery_mode();
    if (safe_mode || recovery_mode) {
        vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
        if (recovery_mode) {
            vga_puts("\n*** RECOVERY MODE (hepu.recovery=1) ***\n");
            vga_puts("Minimal boot, drivers disabled, text mode only.\n\n");
            serial_puts(0x3F8, "[BOOT] RECOVERY MODE enabled\n");
        } else {
            vga_puts("\n*** SAFE MODE (hepu.safe=1) ***\n");
            vga_puts("Problematic drivers disabled, VGA fallback.\n\n");
            serial_puts(0x3F8, "[BOOT] SAFE MODE enabled\n");
        }
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }

    extern void cpu_init(void);
    cpu_init();
    syscall_dbg_check("after-cpu");

    /* ---- 启动横幅 ---- */
    vga_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    vga_puts("+==============================================+\n");
    vga_puts("| HEPU HYPER EVOLVE OS Hepu Hyper Evolve OS |\n");
    vga_puts("+==============================================+\n\n");

    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_printf("Kernel Version : %s\n", hepu_kernel_version);
    vga_printf("Architecture   : x86 (i686)\n");

    uint32_t mem_kb = 0;
    if (mb_info && (mb_info->flags & 0x1)) {
        mem_kb = mb_info->mem_lower + mb_info->mem_upper;
        vga_printf("Total Memory   : %d KB (%d MB)\n", mem_kb, mem_kb / 1024);
    }
    vga_putc('\n');

    hepu_fingerprint_check();
    if (boot_splash_active) { extern void boot_log(const char *, ...); boot_log("[INIT] fingerprint check OK"); }

    hepu_mem_init(mem_kb);
    if (boot_splash_active) { extern void boot_log(const char *, ...); boot_log("[INIT] PM: Memory manager initialized (%d KB)", mem_kb); }

    /* V7.87: 内存隔离初始化——解析启动参数 mem_isolation=0/1
     * 必须在第一个用户进程创建之前调用 */
    paging_init_memory_isolation();

    /* V3.87: 多格式兼容层 (Linux ELF32 / 后续 PE/Mach-O) */
    extern void binfmt_init(void);
    extern void binfmt_selftest(void);
    binfmt_init();
    binfmt_selftest();

    /* 测试内核堆 */
    void *test_ptr = kmalloc(256);
    if (test_ptr) {
        vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
        vga_printf("[KHEAP] kmalloc(256) = 0x%x test passed \n", (uint32_t)test_ptr);
        kfree(test_ptr);
    }
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_putc('\n');

    hepu_sched_init();
    hepu_irq_init();
    if (boot_splash_active) { extern void boot_log(const char *, ...); boot_log("[INIT] IDT: Interrupt table setup"); boot_log("[INIT] PROC: Scheduler ready"); }
    /* ---- initrd 文件系统 ---- */
    vga_puts("=== initrd fs ===\n");
    uint32_t initrd_addr = (uint32_t)_binary_build_v250_initrd_img_start;
    uint32_t initrd_sz = (uint32_t)(_binary_build_v250_initrd_img_end - _binary_build_v250_initrd_img_start);
    initrd_init(initrd_addr, initrd_sz);
    if (initrd_ready()) {
        initrd_list();
    }
    if (boot_splash_active) { extern void boot_log(const char *, ...); boot_log("[INIT] FS: initrd mounted"); }
    serial_puts(0x3F8, "[LIVE] initrd done\n");
    vga_putc('\n');

    /* ---- V2.23: RTC 实时时钟 + 串口 UART ----
     * 提前到 init_sequence 之前初始化, 使 FS/IDE 等阶段可用串口调试输出 */
    extern void rtc_init(void);
    extern void serial_init_com1(void);
    rtc_init();
    serial_puts(0x3F8, "[LIVE] rtc done\n");
    serial_init_com1();

    /* V2.35: 图形化开机 logo (串口就绪, 显示约2.5秒后继续)
     * V6.52: 恢复模式跳过splash, 直接文本启动 */
    if (!recovery_mode) {
        extern void splash_show(void);
        splash_show();
    } else {
        vga_puts("[BOOT] recovery mode: splash skipped\n");
    }
    serial_puts(0x3F8, "[LIVE] splash done\n");
    serial_puts(0x3F8, "[DBG] before VFS init\n");

    /* ---- VFS 虚拟文件系统 ---- */
    vga_puts("=== VFS VFS ===\n");
    serial_puts(0x3F8, "[DBG] after vga_puts VFS\n");
    /* V2.30: 捕获引导器 VGA 诊断 → 内核日志(dmesg 可回溯)
     * V0.10.5: 恢复klog_capture_boot，只是读取0xB8000显存，不会卡住 */
    serial_puts(0x3F8, "[DBG] before klog_capture_boot\n");
    klog_capture_boot();
    klog_printf("[KLOG] kernel log started\n");
    serial_puts(0x3F8, "[DBG] after klog_capture_boot\n");

    serial_puts(0x3F8, "[DBG] before compat_init\n");
    {
        extern void autocompat_init(void);
        autocompat_init();
    }
    /* V0.10.5: 启动降级保护初始化 */
    extern void boot_degrade_init(void);
    boot_degrade_init();

    serial_puts(0x3F8, "[DBG] before hepu_init_sequence\n");
    hepu_init_sequence();
    serial_puts(0x3F8, "[DBG] after hepu_init_sequence\n");

    /* V6.80: IDE已初始化，重试自动挂载存储显存分区 */
    extern void vvram_try_auto_mount(void);
    vvram_try_auto_mount();

    /* V6.52: 引导失败计数检查（IDE已在hepu_init_sequence中初始化，读取LBA 10）
     * 如果连续失败>=3次，自动进入恢复模式 */
    {
        extern int ide_read_sectors(uint32_t, uint8_t *, uint32_t);
        extern void multiboot_force_recovery_mode(void);
        uint8_t boot_state_buf[512];
        if (ide_read_sectors(10, boot_state_buf, 1) == 1) {
            uint32_t magic = *(uint32_t *)boot_state_buf;
            uint32_t fail_count = *(uint32_t *)(boot_state_buf + 8);
            if (magic == 0x48455055 && fail_count >= 3) {
                vga_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
                vga_printf("[BOOT] fail_count=%d, auto entering RECOVERY MODE\n", fail_count);
                serial_puts(0x3F8, "[BOOT] auto recovery mode triggered\n");
                vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                multiboot_force_recovery_mode();
                recovery_mode = 1;
            }
        }
    }

    /* V6.52: HepuBoot配置文件读取（LBA 11，固定位置，无需文件系统）
     * 配置结构: magic("HEPC") + version + default_mode + timeout + extra_cmdline[256] */
    {
        extern int ide_read_sectors(uint32_t, uint8_t *, uint32_t);
        uint8_t cfg_buf[512];
        if (ide_read_sectors(11, cfg_buf, 1) == 1) {
            uint32_t cfg_magic = *(uint32_t *)cfg_buf;
            if (cfg_magic == 0x48455043) {  /* "HEPC" */
                uint32_t cfg_default_mode = *(uint32_t *)(cfg_buf + 8);
                vga_printf("[BOOT] hepboot.cfg: default_mode=%d\n", cfg_default_mode);
                serial_puts(0x3F8, "[BOOT] hepboot.cfg loaded\n");
                /* 如果配置指定安全/恢复模式，且未被引导失败计数覆盖，则应用 */
                if (!recovery_mode) {
                    if (cfg_default_mode == 1) {
                        safe_mode = 1;
                        vga_puts("[BOOT] config: SAFE MODE\n");
                    } else if (cfg_default_mode == 2) {
                        recovery_mode = 1;
                        vga_puts("[BOOT] config: RECOVERY MODE\n");
                    }
                }
            }
        }
    }

    /* V3.87 阶段B-2: initrd 文件挂载到 VFS /app/ —— .exe/.elf/.hug 应用
     * 以"已安装应用"形式暴露给文件系统 (APK 式: 应用区只读, 系统区不可见)
     * V6.75: 同时挂载到根目录 /，兼容用户态使用 /filename 路径 */
    {
        extern int initrd_vfs_mount(const char *);
        initrd_vfs_mount("/app");
        initrd_vfs_mount("/");
    }
        kunit_init();
    kbench_init();
    extern int kbench_run_kernel(void);
    kbench_run_kernel();
    kunit_log_init();
    extern void kunit_register_all_suites(void);
    kunit_register_all_suites();

    /* V0.10.1: 自动运行全部kunit测试，结果输出到串口便于自动化验证 */
    extern int kunit_run_all(void);
    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[KTEST] === Auto-run all kunit suites ===\n");
    int ktest_fail = kunit_run_all();
    serial_puts(0x3F8, ktest_fail ? "[KTEST] RESULT: FAIL\n" : "[KTEST] RESULT: ALL PASS\n");
    
    /* ---- V3.78: 诊断日志落盘 (ext2 已就绪, 写入 /hepu_boot.log) ---- */
    extern void klog_save_to_disk(void);
        klog_save_to_disk();

    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_putc('\n');

    /* ---- V2.23: PCI 总线探测——自动枚举硬件, 供驱动匹配绑定 ---- */
    extern int pci_scan_all(void);
    extern void pci_dump_devices(void);
    int pci_count = pci_scan_all();
    vga_printf("[PCI] bus scan done: %d device(s) detected\n", pci_count);
    pci_dump_devices();
    vga_putc('\n');
    
    /* ---- V2.23: PCI 设备注册进设备树 (驱动自动匹配基础) ---- */
    extern int pci_bind_to_devtree(void);
    pci_bind_to_devtree();
    
    /* ---- V2.62: USB 驱动栈 (PCI 已扫描, 检测控制器 + 枚举 + HID)
     * V6.52: 安全/恢复模式禁用USB, 避免驱动问题导致启动失败 */
    if (!safe_mode && !recovery_mode) {
        extern void usb_init(void);
        usb_init();
    } else {
        vga_puts("[BOOT] safe/recovery mode: USB disabled\n");
    }

    /* ---- V8.30: AC97声卡驱动 */
    if (!safe_mode && !recovery_mode) {
        extern int ac97_detect(void);
        extern int ac97_init(void);
        if (ac97_detect() == 0) {
            ac97_init();
        }
    }
    vga_putc('\n');

    /* ---- V2.23: PS/2 鼠标
     * V6.52: 恢复模式禁用PS/2鼠标, 只保留键盘 */
    if (!recovery_mode) {
        extern int ps2mouse_init(void);
        ps2mouse_init();
    } else {
        vga_puts("[BOOT] recovery mode: PS/2 mouse disabled\n");
    }
    vga_putc('\n');

    /* ---- V4.35-E80: 输入子系统 + 触屏驱动 (必须先于用户态进程:
     * proc1 SYS_EXIT 会经 sys_exit 直达 shell, 跳过下方代码,
     * 旧位置在进程创建后导致 input 子系统从未初始化, 触屏 dev_id=-1) ---- */
    extern void input_init(void);
    input_init();
    /* V6.52: 安全/恢复模式禁用触屏驱动 */
    if (!safe_mode && !recovery_mode) {
        extern int touch_hw_init(void);
        touch_hw_init();
    } else {
        vga_puts("[BOOT] safe/recovery mode: touchscreen disabled\n");
    }

    /* ---- 创建用户态进程并进入 ---- */
    vga_puts("=== user processes ===\n");
    process_t *proc1 = process_create("init",
                                       (uint32_t)user_init_start,
                                       PRIV_USER, 1 /* E1: NORM */);
    process_t *proc2 = process_create("procb",
                                       (uint32_t)procb_start,
                                       PRIV_USER, 1 /* E1: NORM */);
    if (proc1 && proc2) {
        vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
        vga_printf("[PROC] init (PID=%d), procb (PID=%d)\n", proc1->pid, proc2->pid);
        vga_puts("[PROC] preemptive sched ready , timer will switch processes \n");
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        vga_putc('\n');
        /* V2.22: 恢复用户态进程 (TSS/用户段/GDT已实现, PAGE_USER已加),
         * init 有限演示后 SYS_EXIT -> sys_exit 启动内核 Shell.
         * 若用户态异常崩溃, process_enter_user_mode 返回时由下方 Shell 兜底. */
            process_enter_user_mode(proc1);
        }

    /* V6.70: 方案A启动画面——输出启动耗时并切换回文本模式 */
    if (boot_splash_active) {
        extern void boot_log(const char *, ...);
        extern void boot_log_time(const char *);
        extern uint32_t boot_get_time_ms(void);
        boot_log("[INIT] VBE: Framebuffer ready");
        boot_log_time("init complete");
        boot_log("System ready. Starting shell...");
        /* 短暂显示启动画面 */
        for (volatile int i = 0; i < 20000000; i++) { __asm__ volatile("nop"); }
        /* 切换回VGA文本模式 */
        extern void vga_gfx_text_mode(void);
        vga_gfx_text_mode();
        vga_init();
        vga_clear();
    }

    vga_puts("(start Shell...)\n");

    /* V6.52: 启动成功，清除引导失败计数（写入LBA 10） */
    {
        extern int ide_read_sectors(uint32_t, uint8_t *, uint32_t);
        extern int ide_write_sectors(uint32_t, const uint8_t *, uint32_t);
        uint8_t bs_buf[512];
        if (ide_read_sectors(10, bs_buf, 1) == 1) {
            uint32_t magic = *(uint32_t *)bs_buf;
            if (magic == 0x48455055) {
                *(uint32_t *)(bs_buf + 8) = 0;  /* fail_count = 0 */
                ide_write_sectors(10, bs_buf, 1);
            }
        }
    }

    /* 标记Shell已启动，避免首次sys_exit误判Shell未启动而重复启动 */
    extern void shell_mark_launched(void);
    shell_mark_launched();

    /* 启动内核 Shell */
    extern void shell_main(void);
    shell_main();
}
