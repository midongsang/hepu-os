/* ============================================================================
 * 赫普OS - 统一初始化序列
 * 管理100+模块的分阶段初始化
 * ========================================================================== */

#include <stdint.h>
#include "types.h"
#include "init_sequence.h"
#include "compat_detect.h"
#include "paging.h"   /* D-7: LFB 映射需要 PAGE_PRESENT / PAGE_WRITE */

/* V0.10.5: 安全初始化调用 - 防止单个函数卡住导致整个启动失败 */
static int init_call_count = 0;
static int init_fail_count = 0;
#define MAX_INIT_FAILURES 20

static void safe_init_call(const char *name, void (*func)(void)) {
    init_call_count++;

    /* 如果失败次数过多，进入安全模式跳过后续非关键初始化 */
    if (init_fail_count >= MAX_INIT_FAILURES) {
        extern void serial_puts(uint16_t, const char *);
        serial_puts(0x3F8, "[INIT-SAFE] skipping: ");
        serial_puts(0x3F8, name);
        serial_puts(0x3F8, "\n");
        return;
    }

    /* 调用函数 - 注意：由于没有异常处理，这里只能依赖函数自身的健壮性 */
    func();
}

/* V0.10.5: 统一使用串口输出，避免VGA操作在BIOS环境下卡住 */
static void init_serial_puts(const char *s) {
    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, s);
}
static void init_serial_puthex(uint32_t v) {
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puthex(0x3F8, v);
}
#include "vga.h"
#include "security.h"
#include "permission_manager.h"

/* 外部初始化函数声明（按阶段分组） */

/* === 阶段1：核心子系统 === */
extern void vfs_init(void);
extern void devfs_init(void);
extern void ipc_init(void);
extern void net_init(void);
extern void touch_init(void);
extern void smp_init(void);
extern void dynld_init(void);

/* === 阶段2：硬件驱动 === */
extern void usb_init(void);
extern void pm_init(void);
extern void audio_init(void);
extern void camera_init(void);
extern void bt_init(void);
extern void wifi_init(void);
extern void gpu_init(void);
extern void sensors_init(void);
extern void gps_init(void);
extern void codec_init(void);
extern void compositor_init(void);
extern void input_init(void);

/* === 阶段3：网络协议 === */
extern void arp_init(void);
extern void dhcp_init(void);
extern void hepu_dns_init(void);
extern void icmp_init(void);
extern void hepu_http_init(void);
extern void hepu_tls_init(void);
extern void tcp_init(void);
extern void ws_init(void);
extern void http2_init(void);
extern void tls_handshake_init(void);
extern void quic_init(void);
extern void http3_init(void);
extern void doq_init(void);
extern void quic_0rtt_init(void);
extern void quic_migration_init(void);
extern void mp_quic_init(void);
extern void eth_init(void);
extern void socket_init(void);

/* === 阶段4：安全加密 === */
extern void sec_init(void);
extern void x509_init(void);
extern void csprng_init(void);
extern void ecdhe_init(void);
extern void sha_init(void);
extern void hmac_init(void);
extern void hkdf_init(void);
extern void rsa_init(void);
extern void aes_init(void);
extern void secure_boot_init(void);
extern void permission_manager_init(void);
extern void privacy_compute_init(void);
extern void fscrypt_init(void);

/* === 阶段5：系统服务 === */
extern void services_init(void);
extern void services_start_boot(void);
extern void trace_init(void);
extern void perf_init(void);
extern void log_init(void);
extern void media_init(void);
extern void camhal_init(void);
extern void ril_init(void);
extern void ota_init(void);
extern void phone_service_init(void);
extern void sms_service_init(void);
extern void settings_init(void);
extern void notification_center_init(void);
extern void backup_restore_init(void);
extern void storage_manager_init(void);
extern void multi_user_init(void);
extern void system_stability_init(void);

/* === 阶段6：应用框架 === */
extern void appframework_init(void);
extern void packagemanager_init(void);
extern void notification_init(void);
extern void windowsystem_init(void);
extern void share_init(void);
extern void account_init(void);
extern void systemui_init(void);
extern void webengine_init(void);
extern void ai_init(void);
extern void security2_init(void);
extern void media2_init(void);
extern void desktopmode_init(void);
extern void gameframework_init(void);
extern void enterprise_init(void);
extern void devtools_init(void);
extern void compat_init(void);

/* === 阶段7：扩展模块 === */
extern void fs2_init(void);
extern void net2_init(void);
extern void bt2_init(void);
extern void usb2_init(void);
extern void gfx2_init(void);
extern void camera2_init(void);
extern void audio2_init(void);
extern void telecom2_init(void);
extern void ai2_init(void);
extern void carlink_init(void);
extern void enterprise2_init(void);
extern void kernel_opt_init(void);
extern void drivers2_init(void);
extern void fs3_init(void);
extern void net3_init(void);
extern void appeco_init(void);
extern void kernel_core_init(void);
extern void drivers_real_init(void);
extern void userland_init(void);
extern void fs_real_init(void);
extern void bootchain_init(void);
extern void kdebug_init(void);
extern void kperf_init(void);
extern void distributed_init(void);
extern void hdpk_init(void);
extern void device_support_init(void);
extern void fp_enhanced_init(void);
extern void file_manager_init(void);
extern void camera_app_init(void);
extern void gallery_init(void);
extern void browser_init(void);
extern void app_store_init(void);
extern void system_tools_init(void);
extern void music_player_init(void);
extern void video_player_init(void);
extern void photo_editor_init(void);
extern void voice_recorder_init(void);
extern void audio_effects_init(void);
extern void hug_sdk_init(void);
extern void hug_template_init(void);
extern void hug_debugger_init(void);
extern void hug_depman_init(void);
extern void apk_parser_init(void);
extern void microg_service_init(void);
extern void art_runtime_init(void);
extern void compat_layer_init(void);
extern void boot_optimizer_init(void);
extern void memory_optimizer_init(void);
extern void kernel_slimmer_init(void);
extern void perf_profiler_init(void);
extern void app_container_init(void);
extern void distributed_os_init(void);
extern void car_connect_init(void);
extern void rtk_kernel_init(void);
extern void power_optimizer_init(void);
extern void i18ns_system_init(void);
extern void multilingual_ime_init(void);
extern void app_freeze_init(void);
extern void id_randomizer_init(void);
extern void elastic_desktop_init(void);
extern void ai_notification_init(void);
extern void creator_mode_init(void);
extern void onboarding_init(void);
extern void lowmem_throttle_init(void);
extern void hdpk_template_init(void);
extern void android_api_init(void);
extern void bist_init(void);
extern void ai_assistant_init(void);
extern void mount_init(void);
extern void quota_init(void);
extern void fs_registry_init(void);
extern void vfat_init(void);
extern void vfat_register(void);
extern void journal_init(void);
extern void ide_init(void);
extern void ext4_init(void);
extern void ext4_register(void);
extern void partition_init(void);
extern void ahci_init(void);
extern void nvme_init(void);
extern void hepu_exfat_init(void);
extern void hepu_ntfs_init(void);

static init_stats_t init_stats;

/* D-7 取证：统计 0xA0000 前 64000 字节里的非零像素数。
 * flip 写的是全黑，正常应接近 0；读到大量非零说明 flip 之后还有人往显存写。 */
/* 注意: 这里原来写死 0xA0000（VGA 显存），但 VBE 真彩色模式下帧缓冲在
 * 0xFD000000，探针读的是一块与桌面完全无关的遗留内存 —— 它报的 nonzero
 * 涨落和"桌面画了多少"没有关系，曾据此误判过 P3-13。
 * 改为读当前真实绘制基址，VGA 模式下自然还是 0xA0000，两种模式都对。 */
static void gfx_fb_stats(const char *tag)
{
    extern uint8_t *gfx_fb_addr;
    volatile uint8_t *fb = (volatile uint8_t *)gfx_fb_addr;
    int nz = 0;
    for (int i = 0; i < 64000; i++) if (fb[i]) nz++;
    /* P3-13 定位: 分块统计整帧, 看"没画"的是哪一段。
     * 1920x1080x32 = 8,294,400 B, 每块 512KB 抽前 64KB, 共 16 块。
     * 输出形如 [0]=... [1]=... 便于一眼看出内容分布。 */
    int blk[16];
    for (int b = 0; b < 16; b++) {
        int bz = 0;
        for (int i = 0; i < 65536; i++) if (fb[b * 524288 + i]) bz++;
        blk[b] = bz;
    }
    init_serial_puts("[GFX] fb ");
    init_serial_puts(tag);
    init_serial_puts(" nonzero=");
    init_serial_puthex((uint32_t)nz);
    init_serial_puts(" blk=");
    for (int b = 0; b < 16; b++) {
        init_serial_puthex((uint32_t)blk[b]);
        init_serial_puts(b == 15 ? "\n" : "/");
    }
}

/* 阶段名称 */
const char *hepu_init_phase_name(init_phase_t phase)
{
    switch (phase) {
        case INIT_PHASE_CORE: return " cores subsystem ";
        case INIT_PHASE_HARDWARE: return " HW driver ";
        case INIT_PHASE_FS: return " fs ";
        case INIT_PHASE_NETWORK: return " net proto ";
        case INIT_PHASE_SECURITY: return " security encryption ";
        case INIT_PHASE_SERVICES: return " system service ";
        case INIT_PHASE_APPS: return " app framework ";
        default: return " unknown ";
    }
}

/* 执行单个阶段 */
int hepu_init_phase(init_phase_t phase)
{
    u32 count = 0;
    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[INIT] phase: ");
    serial_puts(0x3F8, hepu_init_phase_name(phase));
    serial_puts(0x3F8, "\n");

    init_serial_puts("=== "); init_serial_puts(hepu_init_phase_name(phase)); init_serial_puts(" ===\n");

    switch (phase) {
        case INIT_PHASE_CORE:
            vfs_init(); devfs_init(); ipc_init(); net_init();
            touch_init(); smp_init(); dynld_init();
            count = 7;
            break;

        case INIT_PHASE_HARDWARE:
            /* V2.62: usb_init 改由 kernel.c 在 PCI 扫描后调用 (需 pci_devices 表) */
            pm_init(); audio_init(); camera_init();
            extern void iopolicy_init(void);
            iopolicy_init();
            bt_init(); wifi_init(); gpu_init(); sensors_init();
            gps_init(); codec_init(); compositor_init(); input_init();
            count = 12;
            break;

        case INIT_PHASE_FS:
            mount_init(); quota_init(); fs_registry_init();
            extern void mac_init(void);
            mac_init();
            extern void i18n_init(void);
            i18n_init();
            extern int dtb_init(void *);
            dtb_init(NULL);
            extern void dtb_self_check(void);
            dtb_self_check();
            extern void cjk_init(void);
            cjk_init();
            vfat_init(); vfat_register(); journal_init();
            ide_init();
            /* V4.01-E46: IDE 总线主控 DMA 探测(失败自动回退 PIO) */
            extern void ide_dma_init(void);
            ide_dma_init();
            /* V6.78: 自动检测并挂载磁盘swap分区 */
            extern int swap_auto_detect(void);
            swap_auto_detect();
#ifdef HEPU_BOOT_DEBUG
            /* V2.24: 自动执行 IDE 磁盘IO自测(读LBA0+写读回LBA1) */
            extern void ide_selftest(void);
            ide_selftest();
            /* V2.26: IDE磁盘 ext2 文件系统挂载+读写自测 */
            extern void ide_ext2_selftest(void);
            ide_ext2_selftest();
#endif
            /* D-2: 32 位已接入 shared/security/user_access.h 契约，跑一次自检 */
            extern void user_access32_selftest(void);
            user_access32_selftest();

            ext4_init(); ext4_register();
            fscrypt_init(); partition_init(); ahci_init();
            nvme_init(); hepu_exfat_init(); hepu_ntfs_init();
            fs2_init(); fs3_init(); fs_real_init();
            count = 18;
            break;

        case INIT_PHASE_NETWORK:
            eth_init(); wifi_init(); arp_init(); dhcp_init();
            hepu_dns_init(); icmp_init(); hepu_http_init();
            hepu_tls_init(); tcp_init(); ws_init();
            http2_init(); tls_handshake_init();
            quic_init(); http3_init(); doq_init();
            quic_0rtt_init(); quic_migration_init(); mp_quic_init();
            socket_init(); net2_init(); net3_init();
            /* 网络自测试 */
            extern void network_selftest(void);
            network_selftest();
            count = 21;
            break;

        case INIT_PHASE_SECURITY:
            sec_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[SEC] sec_init done\n"); }
            sec_selftest();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[SEC] sec_selftest done\n"); }
            x509_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[SEC] x509_init done\n"); }
            csprng_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[SEC] csprng_init done\n"); }
            ecdhe_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[SEC] ecdhe_init done\n"); }
            sha_init(); hmac_init(); hkdf_init(); rsa_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[SEC] crypto libs done\n"); }
            aes_init(); secure_boot_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[SEC] aes+secure_boot done\n"); }
            permission_manager_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[SEC] permission_manager_init done\n"); }
            {
                extern void serial_puts(uint16_t, const char *);
                serial_puts(0x3F8, "[INIT] calling prm_selftest...\n");
            }
            prm_selftest();
            {
                extern void serial_puts(uint16_t, const char *);
                serial_puts(0x3F8, "[INIT] prm_selftest done\n");
            }
            privacy_compute_init();
            {
                extern void serial_puts(uint16_t, const char *);
                serial_puts(0x3F8, "[INIT] privacy_compute_init done, calling security2_init...\n");
            }
            security2_init();
            {
                extern void serial_puts(uint16_t, const char *);
                serial_puts(0x3F8, "[INIT] security2_init done\n");
            }
            count = 13;
            break;

        case INIT_PHASE_SERVICES:
            services_init(); services_start_boot();
            trace_init(); perf_init(); log_init();
            media_init(); camhal_init(); ril_init(); ota_init();
            phone_service_init(); sms_service_init(); settings_init();
            /* V7.15: 应用字体大小设置 */
            extern int settings_apply_font_size(void);
            settings_apply_font_size();
            /* V7.25: 加载默认字体配置（文件系统已初始化） */
            extern void font_load_default_config(void);
            font_load_default_config();
            notification_center_init(); backup_restore_init();
            storage_manager_init(); multi_user_init();
            system_stability_init(); power_optimizer_init();
            memory_optimizer_init(); boot_optimizer_init();
            kernel_slimmer_init(); perf_profiler_init();
            count = 22;
            break;

        case INIT_PHASE_APPS:
        {
            extern void serial_puts(uint16_t, const char *);
            extern int multiboot_is_nographics(void);
            int protect_gfx = multiboot_is_nographics();
            serial_puts(0x3F8, "[APPS] starting app framework (72 modules)...\n");
            if (protect_gfx) serial_puts(0x3F8, "[APPS] protect mode: graphics/desktop init skipped\n");

            appframework_init(); packagemanager_init();
            {
                extern void serial_puts(uint16_t, const char *);
                serial_puts(0x3F8, "[APPS] 1/7: core framework done\n");
                serial_puts(0x3F8, "[APPS-DEBUG] about to call notification_init\n");
            }
            notification_init();
            {
                extern void serial_puts(uint16_t, const char *);
                serial_puts(0x3F8, "[APPS] notification_init done\n");
            }
            windowsystem_init();
            {
                extern void serial_puts(uint16_t, const char *);
                serial_puts(0x3F8, "[APPS] windowsystem_init done\n");
            }
            share_init(); account_init(); systemui_init();
            {
                extern void serial_puts(uint16_t, const char *);
                serial_puts(0x3F8, "[APPS] systemui_init done\n");
            }
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[APPS] 2/7: webengine/ai/media...\n"); }
            webengine_init(); ai_init(); media2_init();
            desktopmode_init(); gameframework_init();
            enterprise_init(); devtools_init(); compat_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[APPS] 3/7: bt/usb/gfx/camera...\n"); }
            bt2_init(); usb2_init(); if (!protect_gfx) gfx2_init();

            /* D-7: pluggable 图形层选完后，如果这台机器并没有真实帧缓冲
             * （BIOS + multiboot 加载时 multiboot 信息里没有 framebuffer 字段，
             * 日志就是 "[MB-VBE] no framebuffer info, fallback VGA"），
             * 那么探测出来的只有文本后端。而桌面（desktop_render /
             * elastic_desktop）走的是老的 gfx_draw_* 接口，gfx_flip() 会把
             * back_buffer 刷到 gfx_fb_addr = 0xA0000 —— 文本模式下那里是
             * **字符显存**，于是整屏字符位图被像素数据覆盖，画面变彩色花屏。
             *
             * 所以这里补一次真正的 VGA mode 0x13 设置：没有线性帧缓冲时，
             * 0x13 是这台机器唯一能出图的方式。 */
            {
                extern int gfx_has_vbe_fb(void);
                extern void vga_gfx_init(void);
                extern void serial_puts(uint16_t, const char *);
                if (!protect_gfx && !gfx_has_vbe_fb()) {
                    serial_puts(0x3F8, "[GFX] no VBE/GOP framebuffer -> entering VGA mode 0x13\n");
                    vga_gfx_init();
                    /* 切完模式必须立刻提交一次：0xA0000 里还留着 SeaBIOS 文本模式
                     * 的字符位图，在 256 色模式下会被当成像素解释 —— 那就是花屏。
                     * vga_gfx_init 只清了 back_buffer，没有 flip。 */
                    {
                        extern void gfx_flip(void);
                        gfx_flip();
                    }
                    serial_puts(0x3F8, "[GFX] VGA mode 0x13 entered (first flip done)\n");
                    /* D-7 取证: 读回 CRTC/GC/seq 实值，确认寄存器写入是否真的生效；
                     * 顺便读 bochs VBE DISPI 的 enable 位 —— QEMU -vga std 若 VBE 处于
                     * enabled 状态，传统 VGA 寄存器会被 VBE 路径接管。 */
                    {
                        extern void vga_dump_regs(void);
                        vga_dump_regs();
                        uint16_t dispi;
                        __asm__ volatile ("outw %0, %%dx" :: "a"((uint16_t)4), "d"((uint16_t)0x1CE));
                        __asm__ volatile ("inw %%dx, %0" : "=a"(dispi) : "d"((uint16_t)0x1CF));
                        serial_puts(0x3F8, "[GFX] bochs-dispi index4=");
                        {
                            extern void serial_puthex(uint16_t, uint32_t);
                            serial_puthex(0x3F8, dispi);
                        }
                        serial_puts(0x3F8, "\n");
                    }
                }
            }
            camera2_init(); audio2_init(); telecom2_init();
            ai2_init(); carlink_init(); enterprise2_init();
            /* CJK 字模是按"文本平面 2"写 0xA0000 的（slot 从 0x80 起 = offset 0x1000+），
             * 在图形模式下那就是帧缓冲 —— 字体一加载屏幕上就多出点阵。
             * 这里立刻 flip 一次，用 back_buffer 把它覆盖掉。 */
            {
                extern void gfx_flip(void);
                extern int gfx_bpp;
                if (gfx_bpp == 8) gfx_flip();   /* 8bpp 下字模会落到 0xA0000，需刷掉 */
            }
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[APPS] 4/7: kernel/drivers/userland...\n"); }
            kernel_opt_init(); drivers2_init(); appeco_init();
            kernel_core_init(); drivers_real_init();
            userland_init(); bootchain_init(); kdebug_init();
            kperf_init(); distributed_init(); hdpk_init();
            device_support_init();
            fp_enhanced_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[APPS] 5/7: apps (file/camera/browser...)\n"); }
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[APPS] file_manager_init...\n"); }
            file_manager_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[APPS] camera_app_init...\n"); }
            camera_app_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[APPS] gallery_init...\n"); }
            gallery_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[APPS] browser_init...\n"); }
            browser_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[APPS] app_store_init...\n"); }
            app_store_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[APPS] system_tools_init...\n"); }
            system_tools_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[APPS] music_player_init...\n"); }
            music_player_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[APPS] video_player_init...\n"); }
            video_player_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[APPS] photo_editor_init...\n"); }
            photo_editor_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[APPS] voice_recorder_init...\n"); }
            voice_recorder_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[APPS] audio_effects_init...\n"); }
            audio_effects_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[APPS] hug_sdk_init...\n"); }
            hug_sdk_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[APPS] hug_template_init...\n"); }
            hug_template_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[APPS] hug_debugger_init...\n"); }
            hug_debugger_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[APPS] hug_depman_init...\n"); }
            hug_depman_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[APPS] apk_parser_init...\n"); }
            apk_parser_init();
            { extern void serial_puts(uint16_t, const char *); serial_puts(0x3F8, "[APPS] 5.5/7: compat/android...\n"); }
            microg_service_init(); art_runtime_init();
            compat_layer_init(); app_container_init();
            distributed_os_init(); car_connect_init();
            rtk_kernel_init(); i18ns_system_init();
            multilingual_ime_init(); app_freeze_init();
            id_randomizer_init();
            /* 2026-11-19: 崩溃摘要跨重启横幅 */
            {
                extern void crash_nvram_print_banner(void);
                crash_nvram_print_banner();
            }
            /* 2026-11-19: 崩溃摘要验证入口 —— 根分区有 /bsod_selftest 就崩一次 */
            {
                extern void serial_puts(uint16_t, const char *);
                extern int ide_ext2_read_file(const char *, const char *, char *, int);
                extern void bsod_show(const char *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
                char tag2[8];
                if (ide_ext2_read_file("/", "bsod_selftest", tag2, 4) > 0) {
                    serial_puts(0x3F8, "[SELFTEST] /bsod_selftest found -> bsod\n");
                    bsod_show("crash-nvram selftest", 14, 0x2, 0xDEADBEEF, 0x08, 0x1000, 0xDEADB000);
                }
            }
            /* 2026-11-19: 32 位自检入口 —— 根分区有 /diag_selftest 就跑一遍 */
            {
                extern void serial_puts(uint16_t, const char *);
                extern int ide_ext2_read_file(const char *, const char *, char *, int);
                extern void diag_selftest32(void);
                char tag[8];
                if (ide_ext2_read_file("/", "diag_selftest", tag, 4) > 0) {
                    serial_puts(0x3F8, "[DIAG32] /diag_selftest found -> running self-test\n");
                    diag_selftest32();
                }
            }
            /* 2026-11-20: shell FS→VFS 回归自测（P1/P2/P3 落盘验证）
             * 标记文件 /vfs_selftest 首字节 'w' 跑写阶段、'r' 跑读阶段。
             * 两次独立启动 + 同一张盘 = 真跨重启证据，不依赖 system_reset。 */
            {
                extern void serial_puts(uint16_t, const char *);
                extern int ide_ext2_read_file(const char *, const char *, char *, int);
                extern int fs_vfs_selftest(char);
                char tag[8];
                if (ide_ext2_read_file("/", "vfs_selftest", tag, 4) > 0 && (tag[0] == 'w' || tag[0] == 'r')) {
                    serial_puts(0x3F8, "[VFS-TEST] /vfs_selftest found\n");
                    fs_vfs_selftest(tag[0]);
                }
            }
            if (!protect_gfx) {
                extern void serial_puts(uint16_t, const char *);
                serial_puts(0x3F8, "[APPS] 6/7: pre-desktop done, calling elastic_desktop_init...\n");
                elastic_desktop_init();
            }
            if (!protect_gfx)
            {
                extern void serial_puts(uint16_t, const char *);
                serial_puts(0x3F8, "[APPS] 7/7: elastic_desktop_init done!\n");
                gfx_fb_stats("after-desktop-init");
            /* D-7 收口: 32 位桌面从内核里下线，改由用户态应用负责。
             *
             * 原因: 32 位是微内核 —— 应用在用户态。把整套桌面(4522 行 GUI)
             * 塞进 ring0 与架构定位相悖，也让 32/64 两套桌面永远无法共用。
             * 内核这边只保留两件事: framebuffer 描述 (SYS_FB_DESC) 与映射
             * (SYS_MMAP)，桌面逻辑搬到 user/apps/desktop.c。
             *
             * 这里不再进 elastic_desktop_main_loop()，只留一句交接日志。 */
            /* D-8 / P1-4: 32 位第一次跑 64 位那套桌面。
             *
             * hpdesk.c(8206 行) 已编进 32 位 —— 它不碰硬件, 只调 gfx64_* 图元
             * (ARCHITECTURE_LAYERS.md 判据), 32 位用 gfx64_compat*.c 提供同名实现。
             * 这里先只画一帧验证链路, 主循环放到下一步。 */
            {
                extern void serial_puts(uint16_t, const char *);
                void init_serial_puthex(uint32_t);
                extern int gfx_width, gfx_height;
                /* 桌面经注册表启动, 不再直接调 hpdesk_init ——
                 * 这样"可切换桌面"这条接口才真的被用起来: 上层只认
                 * desk_start()/desk_tick(), 具体是哪个桌面由注册表决定。
                 * scale 参数对 hpdesk 只是个提示, 它自己按 config 算。 */
                extern int  desk_start(uint32_t);
                extern int  desk_tick(void);

                /* B2: 先实测一次"物理地址 -> 用户虚拟地址"是否真建 PTE。
                 * 桌面搬用户态要靠它, 所以先在这个位置验一遍, 免得桌面那头
                 * 拿到地址却写不动才发现。跑在 desk_start 之前, 不碰桌面状态。 */
                { extern void sys_mmap_selftest(void); sys_mmap_selftest(); }

                serial_puts(0x3F8, "[EDESKTOP] desk_start on 32-bit\n");
                int rc = desk_start(100);
                serial_puts(0x3F8, "[EDESKTOP] res=");
                init_serial_puthex((uint32_t)gfx_width);
                serial_puts(0x3F8, "x");
                init_serial_puthex((uint32_t)gfx_height);
                serial_puts(0x3F8, " desk_start rc=");
                init_serial_puthex((uint32_t)rc);
                serial_puts(0x3F8, "\n");
                if (rc == 0) {
                    uint32_t t0l, t0h, t1l, t1h;
                    __asm__ volatile("rdtsc" : "=a"(t0l), "=d"(t0h));
                    desk_tick();
                    __asm__ volatile("rdtsc" : "=a"(t1l), "=d"(t1h));
                    uint64_t dt = ((uint64_t)t1h << 32 | t1l) - ((uint64_t)t0h << 32 | t0l);
                    serial_puts(0x3F8, "[EDESKTOP] first frame via registry, cycles=");
                    init_serial_puthex((uint32_t)(dt >> 32));
                    init_serial_puthex((uint32_t)(dt & 0xFFFFFFFFu));
                    serial_puts(0x3F8, "\n");
                }
            }
                { extern void vga_dump_regs(void); vga_dump_regs(); }
            }
        }
            ai_notification_init(); creator_mode_init();
            onboarding_init(); lowmem_throttle_init();
            hdpk_template_init(); android_api_init();
            bist_init(); ai_assistant_init();
            count = 72;
            break;

        default:
            break;
    }

    init_stats.modules_initialized += count;
    {
        extern void serial_puts(uint16_t, const char *);
        serial_puts(0x3F8, "[INIT] phase done: ");
        serial_puts(0x3F8, hepu_init_phase_name(phase));
        serial_puts(0x3F8, "\n");
    }
    init_serial_puts(" init "); init_serial_puthex(count); init_serial_puts(" module\n");
    return count;
}

/* 完整初始化序列 */
void hepu_init_sequence(void)
{
    init_stats.modules_initialized = 0;
    init_stats.modules_failed = 0;

    init_serial_puts("\n=== Hepu OS unified init sequence ===\n\n");

    for (int phase = 0; phase < INIT_PHASE_COUNT; phase++) {
        hepu_init_phase((init_phase_t)phase);
        init_serial_puts("\n");
    }

    init_serial_puts("[INIT] QUIC DATAGRAM + QPACK dynamic table \n");
    init_serial_puts("\n=== init done: total ");
    init_serial_puthex(init_stats.modules_initialized);
    init_serial_puts(" module ===\n\n");
}

/* 获取统计 */
void hepu_init_get_stats(init_stats_t *stats)
{
    if (stats) *stats = init_stats;
}
