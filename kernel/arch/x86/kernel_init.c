/* ============================================================================
 * 赫普OS - 内核初始化函数
 * 从 kernel.c 拆分（大文件拆分目标<500行）
 * 包含：hepu_sched_init/hepu_mem_init/hepu_irq_init
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
extern hepu_fingerprint_t hepu_fingerprint;
#include "mm.h"
#include "paging.h"
#include "kheap.h"
#include "iosched.h"
#include "sched.h"
#include "process.h"
#include "syscall.h"
#include "hal.h"
#include "ipc.h"
#include "string.h"
#include "pic.h"
#include "pit.h"
#include "irq.h"
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

/* 进程与调度初始化——从 hepu_kernel_main 提取 */
void hepu_sched_init(void)
{
    vga_puts("=== process subsystem ===\n");
    sched_init();
    syscall_dbg_check("after-sched");
    process_init();
    syscall_dbg_check("after-proc");
    extern void domain_init(void);
    domain_init();
    extern void qos_init(void);
    qos_init();
    ipc_init();
    extern void ipc_sem_init(void);
    ipc_sem_init();
    extern void ipc_mutex_init(void);
    ipc_mutex_init();
    extern void ipc_cond_init(void);
    ipc_cond_init();
    extern void ipc_fifo_init(void);
    ipc_fifo_init();
    extern void ipc_perm_init(void);
    ipc_perm_init();
    /* V6.64: 服务注册表初始化（微内核服务发现） */
    extern void service_init(void);
    service_init();
    syscall_dbg_check("after-ipc");
    unified_driver_init();
    /* V6.88: AHCI/SATA驱动初始化（真机磁盘访问必需） */
    extern void ahci_init(void);
    ahci_init();
    /* V6.50: 显存分配器初始化 */
    extern void vram_init(void);
    vram_init();
    extern void vram_selftest(void);
    vram_selftest();
    /* V6.50: IO调度器初始化（在IDE驱动初始化之后） */
    extern void iosched_init(int);
    iosched_init(IOSCHED_NOOP);
    extern void iosched_selftest(void);
    iosched_selftest();
    /* V6.50: 文件系统日志初始化和自测 */
    extern void journal_init(void);
    journal_init();
    extern void journal_selftest(void);
    journal_selftest();
    /* V6.50: 存储加密自测 */
    extern void crypto_selftest(void);
    crypto_selftest();
    /* V6.50: 快照初始化和自测 */
    extern int snapshot_init(void);
    snapshot_init();
    extern void snapshot_selftest(void);
    snapshot_selftest();
    /* V6.50: 统一内存初始化和自测 */
    extern int umem_init(void);
    umem_init();
    extern void umem_selftest(void);
    umem_selftest();
    /* V6.50: 透明压缩文件系统初始化和自测 */
    extern int transcomp_init(void);
    transcomp_init();
    extern void transcomp_selftest(void);
    transcomp_selftest();
    /* V6.50: Hepu虚拟显存初始化和自测
     * V6.76: 使用VVRAM_DEFAULT_SIZE(64MB), 内部会动态降级到可用大小 */
    extern int vvram_init(uint32_t);
    vvram_init(0);  /* 0=使用默认大小 */
    extern void vvram_selftest(void);
    vvram_selftest();
    /* V6.80: 图形系统自测
     * V0.10.5: 暂时禁用旧版gfx_selftest(BIOS环境下vga_gfx_init设置VGA模式13h可能卡住),
     * 新版可插拔图形子系统(gfx_subsystem_selftest)会处理VGA文本模式/VBE降级 */
    /* extern void gfx_selftest(void); gfx_selftest(); */
    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[GFX-TEST] legacy gfx_selftest disabled (using pluggable gfx subsystem)\n");
    /* V7.00: 可插拔图形子系统初始化和自测 */
    /* D-7 第二步: 32 位真彩色。
     * 桌面是按 24 位真彩色画的（0x2D3748 这类 0xRRGGBB），VGA 0x13 只有 8bpp
     * 索引色 —— 0x2D3748 会被当成调色板索引 0x48 去查，查不到就是黑屏。
     * 这里用 bochs dispi 把显示切成 32bpp 并回填 gfx_* 全局量,
     * 必须在下面 gfx_*_register / gfx_probe_backend 之前 —— 后端探测看的就是它们。
     *
     * 此刻分页还没开(第 295 行才 paging_enable), LFB 物理地址可直接访问,
     * 所以只设模式不映射; 映射放到 paging_enable 之后补。 */
    {
        extern int bochs_dispi_init(uint32_t, uint32_t, uint32_t);
        extern uint32_t bochs_dispi_fb_phys(void);
        extern uint32_t bochs_dispi_width(void);
        extern uint32_t bochs_dispi_height(void);
        extern uint32_t bochs_dispi_bpp(void);
        extern int gfx_width, gfx_height, gfx_bpp;
        extern uint8_t *gfx_fb_addr;
        /* P1-2: 与 64 位桌面同分辨率。64 位 hpdesk 按 1920x1080 / 150% 设计
         * (VISUAL_SPEC.md), 32 位若停在 640x480, 侧栏 500 逻辑像素直接装不下,
         * 共用桌面前提就是两边同屏。 */
        if (bochs_dispi_init(1920, 1080, 32) == 0) {
            gfx_width  = (int)bochs_dispi_width();
            gfx_height = (int)bochs_dispi_height();
            gfx_bpp    = (int)bochs_dispi_bpp();
            gfx_fb_addr = (uint8_t *)bochs_dispi_fb_phys();
            /* 行跨距必须一起改：gfx_pitch 默认是 GFX_WIDTH(320)，那是 8bpp 的老口径。
             * 真彩色下一行 640*4=2560 字节，凡是按 gfx_pitch 算行偏移的直写路径
             * （字符、图标符号等不走 backend 的老函数）都会错位 —— 表现就是整屏
             * 横向条纹。 */
            /* 单缓冲直写: framebuffer 必须指向 LFB。
             * 32 位图形有两条并存的绘制路径 —— graphics.h 的 gfx_* 走 backend
             * 直写 LFB，而 gfx_write_pixel_rgb / gfx_draw_string 这类老 API 写的是
             * 全局 framebuffer。它默认 = back_buffer（按 8bpp 320x200 分配的数组），
             * 在 32bpp 640x480 下写就是越界+错位，屏幕表现为整片横向条纹。
             * 把 framebuffer 指到 LFB 之后两条路一致；gfx_flip() 里已有
             * "framebuffer == gfx_fb_addr 就 return" 的单缓冲判定。 */
            {
                extern uint8_t *framebuffer;
                framebuffer = (uint8_t *)bochs_dispi_fb_phys();
                serial_puts(0x3F8, "[GFX] framebuffer -> LFB (single-buffer direct)\n");
            }
            {
                extern int gfx_pitch;
                gfx_pitch = (int)(bochs_dispi_width() * (bochs_dispi_bpp() / 8));
                serial_puts(0x3F8, "[GFX] gfx_pitch=");
                extern void serial_puthex(uint16_t, uint32_t);
                serial_puthex(0x3F8, (uint32_t)gfx_pitch);
                serial_puts(0x3F8, "\n");
            }
            /* VBE 后端内部状态同样要跟上, 否则它仍按 320x200@8bpp 算行跨距 */
            {
                extern void gfx_vbe_sync_mode(void);
                gfx_vbe_sync_mode();
            }
            serial_puts(0x3F8, "[GFX] true-color mode applied: ");
            extern void serial_puthex(uint16_t, uint32_t);
            serial_puthex(0x3F8, (uint32_t)gfx_width);
            serial_puts(0x3F8, "x");
            serial_puthex(0x3F8, (uint32_t)gfx_height);
            serial_puts(0x3F8, "@32bpp fb=0x");
            serial_puthex(0x3F8, (uint32_t)(uintptr_t)gfx_fb_addr);
            serial_puts(0x3F8, "\n");
        } else {
            serial_puts(0x3F8, "[GFX] dispi unavailable, keep default 320x200@8bpp\n");
        }
    }
    extern void gfx_vga_register(void);
    extern void gfx_gop_register(void);
    extern void gfx_virtio_register(void);
    extern void gfx_vbe_register(void);
    extern int gfx_probe_backend(void);
    extern void gfx_subsystem_selftest(void);
    gfx_vga_register();
    gfx_gop_register();
    gfx_virtio_register();
    gfx_vbe_register();
    gfx_probe_backend();
    gfx_subsystem_selftest();
    /* V7.12: 字体管理器初始化 */
    extern void font_manager_init(void);
    font_manager_init();
    /* V7.17: TrueType字体解析器初始化 */
    extern void tt_font_init(void);
    tt_font_init();
    /* V7.15: 应用字体大小设置 */
    extern int settings_apply_font_size(void);
    settings_apply_font_size();
    /* V6.50: 多队列块层初始化和自测 */
    extern int blk_mq_init(uint32_t, uint32_t);
    blk_mq_init(4, 2);  /* 4个SW队列，2个HW队列 */
    extern void blk_mq_selftest(void);
    blk_mq_selftest();
    /* V6.50: 显存虚拟化初始化和自测 */
    extern int vram_virt_init(void);
    vram_virt_init();
    extern void vram_virt_selftest(void);
    vram_virt_selftest();
    /* V6.50: 任务管理器自测 */
    extern void task_manager_selftest(void);
    task_manager_selftest();
    extern void hal_device_init(void);
    hal_device_init();
    extern void hotplug_init(void);
    hotplug_init();
    extern void hal_irq_init(void);
    hal_irq_init();
    extern void hal_clock_init(uint32_t);
    hal_clock_init(1000);
    extern void hal_gpio_init(void);
    hal_gpio_init();
    extern void dma_init(void);
    dma_init();
    /* V6.50: DMA自测 */
    extern void dma_selftest(void);
    dma_selftest();
    extern void power_init(void);
    power_init();
    extern void pm_init(void);
    pm_init();
    syscall_dbg_check("after-drv");
    syscall_init();
    syscall_dbg_check("after-syscall-init");
    vga_putc('\n');
    serial_puts(0x3F8, "[M1] after newline\n");
}

/* 内存管理初始化——从 hepu_kernel_main 提取 */
void hepu_mem_init(uint32_t mem_kb)
{
    vga_puts("=== memory subsystem ===\n");
    if (mem_kb > 0) {
        mm_init(mem_kb, 0x00200000);
    }
    syscall_dbg_check("after-mm-init");
    extern char __bss_end[];
    {
        extern uint8_t __kernel_end[];
        uint32_t kern_reserve_end = ((uint32_t)__kernel_end + 0xFFF) & ~0xFFF;
        mm_add_region(0x00000000, kern_reserve_end, MEM_REGION_KERNEL);
        vga_printf("[MM] reserved kernel+bss+stack region: 0x0 - 0x%x (%d MB)\n",
                   kern_reserve_end, kern_reserve_end / (1024 * 1024));
    }
    extern uint8_t __kernel_end[];
    uint32_t heap_base = ((uint32_t)__kernel_end + 0xFFF) & ~0xFFF;
    /* V6.60: 动态内核堆大小, 避免小内存机器上堆覆盖所有可用内存
     * V6.76: 扩展分级支持更大内存: <128M=8M, <256M=16M, <512M=32M, <1G=64M, >=1G=128M */
    uint32_t heap_size = 128 * 1024 * 1024;
    /* P1-4: 256MB 档从 16MB 提到 32MB。hpdesk 的离屏层(顶栏 691KB、侧栏等)
     * 加上阴影缓存(单个最大 3MB, 3 个面板)在 16MB 下会 OOM, 表现是
     * [LY] no surface —— 顶栏没有半透明、面板也没有阴影。 */
    if (mem_kb < 128 * 1024) heap_size = 8 * 1024 * 1024;
    else if (mem_kb < 256 * 1024) heap_size = 32 * 1024 * 1024;
    else if (mem_kb < 512 * 1024) heap_size = 32 * 1024 * 1024;
    else if (mem_kb < 1024 * 1024) heap_size = 64 * 1024 * 1024;
    mm_add_region(heap_base, heap_size, MEM_REGION_RESERVED);
    /* V6.60: 调试输出内存参数 */
    {
        extern void serial_puts(uint16_t, const char *);
        extern void serial_puthex(uint16_t, uint32_t);
        serial_puts(0x3F8, "[MEM-DBG] mem_kb=");
        serial_puthex(0x3F8, mem_kb);
        serial_puts(0x3F8, " kern_end=0x");
        serial_puthex(0x3F8, (uint32_t)__kernel_end);
        serial_puts(0x3F8, " heap_base=0x");
        serial_puthex(0x3F8, heap_base);
        serial_puts(0x3F8, " heap_size=");
        serial_puthex(0x3F8, heap_size);
        serial_puts(0x3F8, " heap_end=0x");
        serial_puthex(0x3F8, heap_base + heap_size);
        serial_puts(0x3F8, "\n");
    }
    /* V6.50: 初始化伙伴算法（在所有内存保留区域添加之后） */
    {
        uint32_t total_pages = (mem_kb * 1024) / 4096;
        extern void buddy_init(uint32_t);
        buddy_init(total_pages);
        vga_puts("[MM] buddy allocator initialized\n");
        extern void buddy_selftest(void);
        buddy_selftest();
    }
    paging_init(mem_kb);
    syscall_dbg_check("after-paging");
    paging_enable();
    /* D-7: bochs dispi 的真彩色 LFB 在 0xFD000000, 远超 256MB 直映区。
     * 分页一开, 之前能直接写的物理地址就变成#PF —— 必须补映射。 */
    {
        extern uint32_t bochs_dispi_fb_phys(void);
        extern uint32_t bochs_dispi_width(void);
        extern uint32_t bochs_dispi_height(void);
        extern page_directory_t *paging_get_kernel_dir(void);
        extern int paging_map(page_directory_t *, uint32_t, uint32_t, uint32_t);
        uint32_t fb = bochs_dispi_fb_phys();
        if (fb && fb >= 0x10000000u) {
            uint32_t bytes = bochs_dispi_width() * bochs_dispi_height() * 4u;
            int ok = 1;
            for (uint32_t off = 0; off < bytes; off += 4096u) {
                if (paging_map(paging_get_kernel_dir(), fb + off, fb + off,
                               PAGE_PRESENT | PAGE_WRITE) != 0) { ok = 0; break; }
            }
            serial_puts(0x3F8, ok ? "[GFX] LFB mapped after paging_enable\n"
                                 : "[GFX] LFB map failed after paging_enable\n");
        }
    }
    /* V6.80: 映射GOP帧缓冲区（可能在256MB以上，如0x80000000） */
    {
        #include "multiboot.h"
        mb_framebuffer_t fb;
        if (multiboot_get_framebuffer(&fb) == 0 && fb.addr >= 0x10000000) {
            page_directory_t *kdir = paging_get_kernel_dir();
            uint32_t fb_size = fb.pitch * fb.height;
            uint32_t fb_end = fb.addr + fb_size;
            for (uint32_t p = fb.addr; p < fb_end; p += 4096) {
                paging_map(kdir, p, p, 0x7); /* PRESENT|WRITE|USER */
            }
            vga_printf("[INIT] GOP framebuffer mapped: 0x%x size=%dKB\n",
                       fb.addr, fb_size / 1024);
        }
    }
    {
        kheap_init(heap_base, heap_size);
    }
    /* V6.81: PAE切换测试（独立分支，默认不自动切换）
     * 已验证：pae_safe_switch可成功切换到PAE模式
     * PAE模式下页错误处理已修复（详细诊断+全地址按需分配）
     * 可通过Shell命令"pae_switch"手动触发
     */
    //
    /*
    {
        extern void serial_puts(uint16_t, const char *);
        serial_puts(0x3F8, "[PAE-INIT] starting PAE switch test...\n");
        vga_puts("[PAE-INIT] PAE switch test...\n");

        extern int pae_safe_switch(void);
        int ret = pae_safe_switch();
        if (ret == 0) {
            serial_puts(0x3F8, "[PAE-INIT] PAE switch SUCCESS!\n");
            vga_puts("[PAE-INIT] PAE mode enabled!\n");
            // 切换后验证内存访问
            volatile uint32_t *test = (volatile uint32_t *)0x3000;
            *test = 0xCAFEBABE;
            if (*test == 0xCAFEBABE) {
                serial_puts(0x3F8, "[PAE-INIT] memory access OK\n");
            }
        } else {
            serial_puts(0x3F8, "[PAE-INIT] PAE switch FAILED, using legacy paging\n");
            vga_printf("[PAE-INIT] PAE switch failed (code=%d), legacy paging\n", ret);
        }
    }
    */
    /* V6.50: VMA自测（需要kheap_init之后） */
    extern void vma_selftest(void);
    vma_selftest();
    /* V6.50: zram内存压缩初始化和自测（需要kheap_init之后）
     * V6.76: 从64页扩展到256页(1MB压缩存储, 可解压约2-4MB数据) */
    extern void zram_init(uint32_t);
    zram_init(256);
    extern void zram_selftest(void);
    zram_selftest();
    /* V7.98: PCI总线扫描 + 网卡驱动初始化（需要kheap_init之后） */
    extern int pci_scan_all(void);
    pci_scan_all();
    extern int rtl8139_init(void);
    rtl8139_init();
    /* V8.03: e1000网卡驱动初始化 */
    extern int e1000_init(void);
    e1000_init();
    /* V7.98: IP协议层初始化 */
    extern void ip_init(void);
    ip_init();
    /* V7.99: UDP协议层初始化 */
    extern void udp_init(void);
    udp_init();
    syscall_dbg_check("after-mm");
    extern void slab_init(void);
    slab_init();
    extern void reclaim_init(void);
    reclaim_init();
    extern void swap_init(void);
    swap_init();
    extern void hugetlb_init(void);
    hugetlb_init();
    /* V6.50: 大页自测 */
    extern void hugetlb_selftest(void);
    hugetlb_selftest();
    /* V6.77: PAE物理地址扩展自测 */
    extern void pae_init(void);
    pae_init();  /* V6.80: 先初始化PAE框架 */
    extern void pae_selftest(void);
    pae_selftest();
    extern void mempool_init(void);
    mempool_init();
    extern void acpi_init(void);
    acpi_init();
}

/* 系统调用测试+硬件中断初始化——从 hepu_kernel_main 提取 */
void hepu_irq_init(void)
{
    vga_puts("=== syscall test ===\n");
    serial_puts(0x3F8, "[M2] after banner\n");
    vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    serial_puts(0x3F8, "[M3] after setcolor\n");
    serial_puts(0x3F8, "[M4] before handler\n");
    uint32_t ver = syscall_handler(SYS_HEPU_INFO, 0, 0, 0, 0);
    vga_printf("[SYSCALL] SYS_HEPU_INFO -> %s\n", (char *)ver);
    uint32_t fp_ok = syscall_handler(SYS_HEPU_FP_VERIFY, 0, 0, 0, 0);
    vga_printf("[SYSCALL] SYS_HEPU_FP_VERIFY -> %s\n",
               fp_ok == 0 ? " fingerprint has 效 " : " fingerprint invalid ");
    uint32_t alloc = syscall_handler(SYS_MALLOC, 128, 0, 0, 0);
    vga_printf("[SYSCALL] SYS_MALLOC(128) -> 0x%x\n", alloc);
    syscall_handler(SYS_FREE, alloc, 0, 0, 0);
    vga_puts("[SYSCALL] SYS_FREE -> ok");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_putc('\n');
    vga_putc('\n');
    vga_puts("=== HW IRQ subsystem ===\n");
    pic_init();
    /* V6.89: APIC初始化（真机中断控制器，失败则回退PIC） */
    extern int apic_init(void);
    apic_init();
    irq_init();
    pit_init();
    keyboard_init();
    tss_init();
}

/* 版权指纹校验——从 hepu_kernel_main 提取 */
void hepu_fingerprint_check(void)
{
    vga_set_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    vga_puts("[HEPU] kernel fingerprint verify ...\n");
    fingerprint_result_t fp_result = fingerprint_verify(&hepu_fingerprint);
    if (fp_result.valid) {
        vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
        vga_printf("[HEPU] fingerprint ok: %s\n", hepu_fingerprint.name);
        vga_printf("[HEPU] License: %s\n", hepu_fingerprint.license);
    } else {
        vga_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
        vga_puts("[HEPU] warn: kernel fingerprint tampered !\n");
    }
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_putc('\n');
}

