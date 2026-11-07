/* ============================================================================
 * 赫普OS - 64位内核主入口
 * V0.10.6: 初始64位内核实现（简化版，仅核心功能）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* V0.54.8: COM1 serial (headless CI boot test) */
extern int uart64_init(int com);
extern int uart64_puts(int com, const char *str);

/* V0.55: 内核环形日志 (日志查看器数据源) */
extern void klog64_log(const char *s);

/* 本地基础库函数 */
extern void vga_clear(void);
extern void vga_puts(const char *str);
extern void vga_put_hex(uint64_t val);
extern void *memset(void *dst, int val, size_t n);

/* x86_64功能函数 */
extern int x86_64_detect(void);
extern int x86_64_is_long_mode(void);
extern void x86_64_enable_sce(void);
extern void x86_64_enable_nxe(void);
extern uint64_t x86_64_read_msr(uint32_t msr);

/* 中断处理 */
extern void irq64_init(void);

/* GDT/TSS初始化 */
extern void gdt64_init(void);
extern void tss64_init(void);

/* 系统调用 */
extern void syscall64_init(void);
extern void syscall64_test(void);

/* 进程管理 */
extern void process64_init(void);
extern int process64_create(const char *name, void (*entry)(void), int priority);
extern void process64_list(void);

/* 虚拟内存管理 */
extern void vmm64_init(uint64_t pml4_addr);
extern void vmm64_load_cr3(uint64_t pml4_addr);

/* IDE驱动 */
extern void ide64_init(void);
extern void ide64_test(void);
extern uint64_t ide64_get_capacity(int drive);
extern int ide64_read_sector(int drive, uint8_t *buf);

/* ext2文件系统 */
extern int ext2_64_init(void);
extern void ext2_64_set_partition_offset(uint32_t lba);
extern int ext2_64_read_file(const char *path, uint8_t *buffer, uint32_t max_size);

/* TrueType字体 */
extern int ttf64_init(const uint8_t *data, uint32_t size);
extern uint64_t pmm64_alloc_pages(int count);

/* RTL8139网卡 */
extern int rtl8139_init(void);

/* 图形子系统 */
extern int gfx64_init_from_bootloader(void);

/* 网络协议栈 */
extern void net64_init(void);
extern void net64_poll(void);

/* 完整系统调用 */
extern void syscall64_full_init(void);

/* DHCP和DNS */
extern void dhcp64_init(void);
extern int dhcp64_poll(void);
extern void dns64_init(void);

/* HTTP客户端 */
extern void http64_init(void);

/* 进程管理 */
extern void process64_init(void);

/* Shell */
extern void shell64_run(void);

/* 桌面环境 */
extern int desktop64_init(void);
extern void desktop64_run(void);

/* 用户态进程 */
extern void user_process_init(void);
extern void user_process_test(void);
extern int user_process_create(const char *name, void (*entry)(void));
extern int user_process_switch(int pid);
extern void user_program_hello(void);

/* PS/2鼠标 */
extern int ps2mouse_init(void);
extern int mouse64_init(int width, int height);
extern void mouse64_update(int width, int height);

/* AHCI/SATA */
extern void ahci64_init(void);

/* 字体管理器 */
extern void font64_init(void);

/* 窗口管理器 */
extern void window64_init(void);

/* 文件管理器 */
extern void fm64_init(void);

/* 系统设置 */
extern void syssettings64_init(void);

/* 记事本 */
extern void notes64_init(void);

/* 终端增强 */
extern void term64_init(void);

/* 应用商店 */
extern void appstore64_init(void);

/* 键盘驱动 */
extern void keyboard64_init(void);
extern void keyboard64_test(void);

/* RTC驱动 */
extern void rtc64_init(void);

/* V0.55 P2/P3: 账户权限 + 首启向导 */
extern void auth64_init(void);
extern int  auth64_is_configured(void);
extern void oobe64_run(void);

/* 物理内存管理 */
extern void pmm64_init(uint64_t memory_size, uint64_t bitmap_addr);
extern void pmm64_get_stats(uint64_t *total, uint64_t *used, uint64_t *free);
extern uint64_t pmm64_alloc_page(void);

/* 内核堆分配器 */
extern void kheap64_init(void *start, uint64_t size);
extern void* kmalloc(size_t size);
extern void kfree(void *ptr);

/* 用户空间访问安全 */
extern void user_access64_init(void);

/* PIT定时器 */
extern void pit64_init(void);

/* APIC中断控制器 */
extern void apic64_init(void);

/* 内核栈（16KB） */
#define KERNEL_STACK_SIZE 16384
uint8_t kernel_stack[KERNEL_STACK_SIZE] __attribute__((aligned(16)));
uint8_t *kernel_stack_top = kernel_stack + KERNEL_STACK_SIZE;

/* Multiboot信息指针（从引导器传递）
 * 必须放在 .data section 而不是 BSS：BSS zeroing 会覆盖它们！ */
uint64_t g_mb_info_addr __attribute__((section(".data"))) = 0;
uint32_t g_mb_magic __attribute__((section(".data"))) = 0;

/* V0.13.54: 根分区信息 (MBR解析保存, ext2挂载后自动扩展用) */
uint32_t g_root_lba = 0;
uint32_t g_root_size = 0;

/* 64位内核主函数 */
void x86_64_kernel_main(void)
{
    /* 最早期启用FPU/SSE，防止编译器生成的SSE指令在启用前触发#UD */
    extern void x86_64_enable_fpu_early(void);
    x86_64_enable_fpu_early();

    /* V0.54.8: 尽早初始化COM1串口 —— headless CI QEMU smoke test 只有 serial.log 非空才算过
     * 16550 UART (uart64_init 在 kernel/arch/x86_64/uart64.c)，纯 IO port 访问, 不依赖 paging/FPU. */
    uart64_init(0);
    uart64_puts(0, "=== HepuOS 64-bit Kernel booting ===\n");

    /* 显示启动信息 */
    vga_puts("========================================\n");
    vga_puts("  HepuOS 64-bit Kernel v0.54.6\n");
    vga_puts("  x86_64 Long Mode\n");
    vga_puts("========================================\n\n");

    /* 检查长模式 */
    if (x86_64_is_long_mode()) {
        vga_puts("[OK] Long mode active\n");
    } else {
        vga_puts("[ERROR] Not in long mode!\n");
        while (1) { __asm__ volatile("hlt"); }
    }

    /* 显示CPU信息 */
    vga_puts("[INFO] CPU supports x86_64: ");
    if (x86_64_detect()) {
        vga_puts("Yes\n");
    } else {
        vga_puts("No\n");
    }

    /* 显示Multiboot信息 */
    vga_puts("[INFO] MB magic: ");
    vga_put_hex(g_mb_magic);
    vga_puts("\n");
    vga_puts("[INFO] MB info addr: ");
    vga_put_hex(g_mb_info_addr);
    vga_puts("\n");

    /* 启用高级特性 */
    vga_puts("[INIT] Enabling SCE (System Call Extensions)...\n");
    x86_64_enable_sce();

    vga_puts("[INIT] Enabling NXE (No-Execute)...\n");
    x86_64_enable_nxe();

    vga_puts("[INIT] Enabling FPU/SSE...\n");
    extern void x86_64_enable_fpu(void);
    x86_64_enable_fpu();

    /* 初始化物理内存管理（2GB内存，位图使用静态数组） */
    vga_puts("[INIT] Initializing physical memory manager (2GB max)...\n");
    /* V0.13.63: 真机内存探测 — stage2(E820)/UEFI(EFI内存图) 写入 0x90100(64位字节)
     * 替代硬编码 2GB: 真机内存 <2GB 时硬编码会分配不存在的物理页 -> 启动即崩 */
    {
        uint64_t real_mem = *(volatile uint64_t *)0x90100;
        if (real_mem < (256ULL * 1024 * 1024)) real_mem = 256ULL * 1024 * 1024; /* 兜底 256MB */
        if (real_mem > (2ULL * 1024 * 1024 * 1024)) real_mem = 2ULL * 1024 * 1024 * 1024; /* 上限 2GB (VMM 上限) */
        pmm64_init(real_mem, 0);
        vga_puts("[INFO] PMM physical memory: ");
        vga_put_hex(real_mem / (1024 * 1024));
        vga_puts(" MB\n");
    }
    /* V0.33: 显式保留内核栈（.bss 里的静态数组）。
     * 链接符号 _end 只覆盖到 .bss 末尾，但内核栈那段必须保证
     * 不会被分配器当成空闲页发给用户栈 —— 一旦重叠，用户栈一写
     * 就把内核栈内容冲掉（auxv 里会莫名其妙出现内核地址）。 */
    {
        extern uint8_t hepu_kstack_bottom[], hepu_kstack_top[];
        extern void pmm64_reserve_range(uint64_t base, uint64_t length);
        uint64_t lo = (uint64_t)hepu_kstack_bottom;
        uint64_t hi = (uint64_t)hepu_kstack_top;
        if (hi > lo) {
            pmm64_reserve_range(lo, hi - lo);
            vga_puts("[PMM] reserved kernel stack: ");
            vga_put_hex(lo);
            vga_puts(" - ");
            vga_put_hex(hi);
            vga_puts("\n");
        }
    }
    vga_puts("[OK] Physical memory manager initialized\n");
    vga_puts("[BOOT] step1 done\n");

    /* 初始化统一驱动框架 */
    extern void drv_init(void);
    drv_init();
    klog64_log("[BOOT] driver framework initialized");
    vga_puts("[BOOT] step2 done (drv_init)\n");

    uint64_t mem_total, mem_used, mem_free;
    pmm64_get_stats(&mem_total, &mem_used, &mem_free);
    vga_puts("[INFO] Memory: total=");
    vga_put_hex(mem_total / (1024 * 1024));
    vga_puts("MB used=");
    vga_put_hex(mem_used / (1024 * 1024));
    vga_puts("MB free=");
    vga_put_hex(mem_free / (1024 * 1024));
    vga_puts("MB\n");

    /* V0.54.6 优化: kheap 从 4MB 扩容到 32MB
     * 64 位内核内存更大, 原始 4MB 是从 32 位继承的历史限制 */
    vga_puts("[INIT] Initializing kernel heap (32MB)...\n");
    kheap64_init((void*)0x300000, 32 * 1024 * 1024);
    vga_puts("[OK] Kernel heap initialized\n");

    /* Crypto 核心快速初始化（AES/SHA/HMAC/HKDF/CSPRNG — 启动必需）
     * RSA/ECDHE/X509/TLS 延迟到桌面启动后再做，节省 ~1500-2000ms */
    vga_puts("[INIT] Initializing crypto core...\n");
    extern void crypto_init_core(void);
    crypto_init_core();
    vga_puts("[OK] Crypto core initialized\n");

    /* 初始化用户空间访问安全 */
    vga_puts("[INIT] Initializing user space access protection...\n");
    user_access64_init();

    /* 初始化PIT定时器（1ms滴答） */
    vga_puts("[INIT] Initializing PIT timer...\n");
    pit64_init();
    vga_puts("[OK] PIT timer initialized\n");

    /* 初始化虚拟内存管理（4级页表） */
    vga_puts("[INIT] Initializing 64-bit virtual memory (4-level paging)...\n");
    uint64_t pml4_addr = pmm64_alloc_page();
    vga_puts("[INFO] PML4 allocated at: ");
    vga_put_hex(pml4_addr);
    vga_puts("\n");
    vmm64_init(pml4_addr);
    vmm64_load_cr3(pml4_addr);
    vga_puts("[OK] Switched to kernel page tables\n");
    vga_puts("[BOOT] step3 done (paging)\n");

    /* V0.20.59: 提前装 IDT，防止切页表后到 SMP/APIC 之间任何异常导致 triple fault 回 BIOS */
    vga_puts("[INIT] Early IDT (crash-safe)...\n");
    extern void irq64_init(void);
    irq64_init();
    vga_puts("[OK] Early IDT installed\n");

    /* 阶段2 ③：开 SMEP/SMAP。
     * 此时内核映射(低身份/高半区)均已 U=0，内核只在内核页执行，ring3 用户页不可被内核取指。
     *
     * V0.33 修正：SMEP/SMAP 在 x86-64 规范里是**可选**特性。
     *   - CPU 不支持时，CR4 对应位置不上，copy_*_user 里的 STAC 就是非法指令(#UD)；
     *     旧实现无条件执行 STAC，在默认 QEMU(qemu64) 和部分真机上内核一进用户程序就死机。
     *   - 现在：能力探测统一收口到 x86_64_detect_cpu_features()，
     *     「支不支持」与「置没置」分开；关掉 SMAP 时 copy 路径自动退化为普通访问（见 hepu_smap.h）。
     */
    {
        extern uint32_t x86_64_detect_cpu_features(void);
        x86_64_detect_cpu_features();
        extern int x86_64_enable_smep_if_supported(void);
        extern int x86_64_enable_smap_if_supported(void);
        extern int x86_64_enable_sse_if_supported(void);
        x86_64_enable_smep_if_supported();
        x86_64_enable_smap_if_supported();
        /* V0.33: 用户态 SSE 是 ABI 硬要求（glibc/gcc 默认生成 SSE 指令），
         * CR4.OSFXSR 不开的话用户程序在 movaps 处直接 #GP。 */
        x86_64_enable_sse_if_supported();
        /* 打印最终生效的硬件状态（单一事实源 = CR4） */
        extern int x86_64_smap_active(void);
        extern int x86_64_smep_active(void);
        vga_puts("[x86_64] hardware state: SMEP=");
        vga_puts(x86_64_smep_active() ? "on" : "off");
        vga_puts(" SMAP=");
        vga_puts(x86_64_smap_active() ? "on" : "off");
        vga_puts(" SSE=");
        {
            extern int x86_64_sse_active(void);
            vga_puts(x86_64_sse_active() ? "on" : "off");
        }
        vga_puts("\n");
    }

    /* V0.13.60: UEFI GOP 下启动日志双路输出(0xB8000 不可见, 画到帧缓冲)
     * 必须在 load_cr3 之后: fb 映射在新页表, 旧页表(stage2)未映射 0xFD000000,
     * 提前激活会在 vga_puts 时访问未映射 fb -> 页错误挂起 */
    extern void early_fb_text_init_from_bootloader(void);
    early_fb_text_init_from_bootloader();

    /* 初始化APIC中断控制器（VMM之后，可映射高地址） */
    vga_puts("[INIT] Initializing APIC (step 1: Local APIC only)...\n");
    apic64_init();
    vga_puts("[OK] APIC initialized\n");
    vga_puts("[BOOT] step4 done (APIC)\n");

    /* 切换到纯APIC模式（必须在启动AP之前）：
       否则 apic_mode 全局标志保持0，AP核启动后 apic64_get_id() 与
       irq_handler_64 的FPU保存区选择都走PIC分支，导致所有AP核误判自己
       为CPU0、FXSAVE写入BSP的保存区，破坏多核渲染稳定性。 */
    extern int apic64_switch_to_apic_mode(void);
    apic64_switch_to_apic_mode();

    /* 初始化多处理器支持（SMP）。
       AP核启动后会在ap_c_entry中自旋等待BSP完成IDT初始化（g_apic_idt_ready），
       因此即使irq64_init在smp_init之后，AP核也不会在IDT无效时收到中断。 */
    vga_puts("[INIT] SMP init...\n");
    extern int smp_init(void);
    int smp_r = smp_init();
    if (smp_r != 0) vga_puts("[WARN] SMP init failed, continuing UP\n");
    else vga_puts("[OK] SMP done\n");
    vga_puts("[BOOT] step5 done (SMP)\n");

    /* GDT/TSS已在汇编入口处加载（entry_64.S） */
    vga_puts("[INIT] Initializing 64-bit GDT/TSS...\n");
    vga_puts("[OK] GDT loaded at entry (kernel GDT active)\n");
    tss64_init();

    /* 初始化中断处理（IDT已在前面提前装好） */
    vga_puts("[INIT] 64-bit IDT ready (early install)\n");

    /* 初始化系统调用
     *
     * V0.33 修复（初始化顺序）：原来顺序是 init → test → full_init，
     * 而 syscall64_test() 里那次 getpid 调用走的是 syscall64_dispatch，
     * 它检查 `syscall_initialized` —— 此刻 full_init 还没跑，标志还是 0，
     * 于是 dispatch 直接返回 -1，测试打印出 "PID: FFFFFFF"（-1 的十六进制）。
     * test 的目的是验证系统调用**能用**，前提是分发器已经就绪，
     * 所以 full_init 必须排在 test 之前。 */
    vga_puts("[INIT] Initializing 64-bit syscall...\n");
    syscall64_init();
    syscall64_full_init();
    syscall64_test();

    /* P0-4: seccomp 沙箱状态初始化（全槽位 disabled） */
    extern void seccomp64_init(void);
    seccomp64_init();

    /* 初始化ELF64加载器 */
    extern void elf64_init(void);
    elf64_init();

    /* 初始化进程管理（移到后面统一初始化，避免重复） */
    /* process64_init();
    process64_create("init", 0x200000, 0x300000);
    process64_list(); */

    /* 初始化IDE驱动 */
    vga_puts("[INIT] Initializing 64-bit IDE driver...\n");
    ide64_init();
    /* ide64_test();  跳过测试，加快启动 */

    /* 检测磁盘容量 */
    vga_puts("[INIT] Detecting IDE drive capacity...\n");
    ide64_get_capacity(0);  /* master */
    ide64_get_capacity(1);  /* slave */

    /* 解析MBR分区表，找到ext2根分区 */
    vga_puts("[INIT] Parsing MBR partition table...\n");
    /* 出厂镜像根分区固定在 LBA13000; 若 MBR 中有可用的 0x83 ext2 分区,
     * 下面的扫描会用实际分区覆盖。这样 SATA/USB 启动(读不到 MBR)也能找到根。 */
    ext2_64_set_partition_offset(13000);
    g_root_lba = 13000;
    g_root_size = 0;
    int mbr_ext2_found = 0;
    {
        uint8_t mbr[512];
        if (ide64_read_sector(0, mbr) == 0) {
            /* 检查MBR签名 */
            if (mbr[510] == 0x55 && mbr[511] == 0xAA) {
                /* 收集所有Linux分区 */
                uint32_t part_lba[4] = {0, 0, 0, 0};
                uint32_t part_size[4] = {0, 0, 0, 0};
                int part_count = 0;

                for (int p = 0; p < 4; p++) {
                    uint8_t *entry = &mbr[446 + p * 16];
                    uint8_t type = entry[4];
                    uint32_t lba_start = entry[8] | (entry[9] << 8) |
                                        (entry[10] << 16) | (entry[11] << 24);
                    uint32_t sectors = entry[12] | (entry[13] << 8) |
                                      (entry[14] << 16) | (entry[15] << 24);

                    if (type == 0x83 && lba_start > 0) {
                        part_lba[part_count] = lba_start;
                        part_size[part_count] = sectors;
                        part_count++;

                        vga_puts("[INIT] Found Linux partition ");
                        char buf[16];
                        int n = part_count, i = 0;
                        if (n == 0) buf[i++] = '0';
                        while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
                        buf[i] = 0;
                        for (int j = 0; j < i / 2; j++) {
                            char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
                        }
                        vga_puts(buf);
                        vga_puts(": LBA=");
                        n = lba_start; i = 0;
                        if (n == 0) buf[i++] = '0';
                        while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
                        buf[i] = 0;
                        for (int j = 0; j < i / 2; j++) {
                            char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
                        }
                        vga_puts(buf);
                        vga_puts(" size=");
                        n = sectors / 2048; i = 0;
                        if (n == 0) buf[i++] = '0';
                        while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
                        buf[i] = 0;
                        for (int j = 0; j < i / 2; j++) {
                            char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
                        }
                        vga_puts(buf);
                        vga_puts("MB\n");
                    }
                }

                /* V0.13.51 修复: 三区制下根分区选择
                 * 旧逻辑选最后一个分区(P3自由分区无文件系统) -> 超级块全0 -> 除零崩溃
                 * 新逻辑: 遍历0x83分区, 校验ext2魔数(0xEF53), 选第一个有效的作根 */
                if (part_count > 0) {
                    int root_part = -1;
                    for (int pi = 0; pi < part_count; pi++) {
                        uint8_t sb_buf[2048];
                        if (ide64_read_sector(part_lba[pi] + 2, sb_buf) == 0) {
                            uint16_t magic = (uint16_t)sb_buf[56] |
                                             ((uint16_t)sb_buf[57] << 8);
                            if (magic == 0xEF53) { root_part = pi; break; }
                        }
                    }
                    if (root_part < 0) {
                        /* 没有可用的 ext2 分区(如 GPT 混合镜像的保护性 MBR 0xEE),
                         * 回退出厂固定布局 LBA13000, 而不是取最后一个分区 */
                        ext2_64_set_partition_offset(13000);
                        g_root_lba = 13000;
                        g_root_size = 0;
                        vga_puts("[INIT] No ext2 partition in MBR, fallback root LBA=13000\n");
                    } else {
                        mbr_ext2_found = 1;
                        ext2_64_set_partition_offset(part_lba[root_part]);
                        /* V0.13.54: 保存根分区信息供 ext2 自动扩展 */
                        g_root_lba = part_lba[root_part];
                        g_root_size = part_size[root_part];
                        vga_puts("[INIT] Using partition ");
                        char buf[16];
                        int n = root_part + 1, i = 0;
                        if (n == 0) buf[i++] = '0';
                        while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
                        buf[i] = 0;
                        for (int j = 0; j < i / 2; j++) {
                            char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
                        }
                        vga_puts(buf);
                        vga_puts(" as root (");
                        vga_puts(root_part == 0 ? "initrd recovery" : "ext2 root");
                        vga_puts(")\n");
                    }
                } else {
                    /* MBR 中没有 0x83 分区 (GPT 混合镜像的保护性 0xEE):
                     * 使用出厂固定布局, 否则会误读 GPT 头当作 ext2 超级块 */
                    ext2_64_set_partition_offset(13000);
                    g_root_lba = 13000;
                    g_root_size = 0;
                    vga_puts("[INIT] No 0x83 partition in MBR, fallback root LBA=13000\n");
                }
            } else {
                vga_puts("[INIT] No MBR signature, using raw disk\n");
            }
        }
    }

    /* 提前初始化存储HAL（ext2需要通过它读取磁盘） */
    extern int storage_hal_init(void);
    storage_hal_init();
    klog64_log("[BOOT] storage HAL initialized");

    /* V0.20.10: AHCI 提前到 ext2 之前初始化，真机 SATA 盘直接走 AHCI */
    vga_puts("[INIT] Early AHCI/SATA init...\n");
    ahci64_init();
    {
        extern int ahci64_disk_present(void);
        extern int ahci64_read_disk(uint32_t, uint8_t, uint8_t *);
        extern void ext2_64_set_block_device(int (*rd)(uint32_t, uint8_t, uint8_t *));
        extern int usb64_init(void);
        extern int usb64_storage_present(void);
        extern int usb_storage_read_sectors(uint32_t, uint8_t, uint8_t *);
        int have_ahci = ahci64_disk_present();
        int ahci_has_root = 0;
        uint8_t probe[512];
        if (have_ahci && ahci64_read_disk(13002, 1, probe) == 0 &&
            probe[56] == 0x53 && probe[57] == 0xEF) {
            ahci_has_root = 1;
        }

        /* 从 U 盘启动且机器内还有 SATA 盘时, 不能误把内部盘当根设备。
         * 仅当没有 MBR 0x83 根分区、且当前 SATA 盘不含出厂根分区时探测 U 盘。 */
        int usb_has_root = 0;
        if (!mbr_ext2_found && (!have_ahci || !ahci_has_root)) {
            vga_puts("[INIT] Probing USB boot...\n");
            usb64_init();
            if (!usb64_storage_present()) {
                /* UHCI可能没找到U盘, 试EHCI */
                vga_puts("[INIT] UHCI no storage, trying EHCI...\n");
                extern int ehci64_init(void);
                if (ehci64_init() == 0) {
                    extern void usb64_enumerate(void);
                    usb64_enumerate();
                }
            }
            if (usb64_storage_present() &&
                usb_storage_read_sectors(13002, 1, probe) == 0 &&
                probe[56] == 0x53 && probe[57] == 0xEF) {
                usb_has_root = 1;
            }
        }

        if (usb_has_root) {
            vga_puts("[INIT] USB mass storage found, using as root device\n");
            ext2_64_set_block_device(usb_storage_read_sectors);
            extern void storage_hal_set_device(int);
            storage_hal_set_device(1);
            ext2_64_set_partition_offset(13000);
            g_root_lba = 13000;
            g_root_size = 0;
        } else if (mbr_ext2_found) {
            /* IDE MBR 中已找到根分区, 保持 IDE 块设备 */
        } else if (have_ahci) {
            ext2_64_set_block_device(ahci64_read_disk);
        } else {
            vga_puts("[INIT] No SATA/USB disk, falling back to IDE\n");
        }
    }

    /* 初始化ext2文件系统 */
    vga_puts("[INIT] Initializing 64-bit ext2 filesystem...\n");
    ext2_64_init();
    klog64_log("[BOOT] ext2 root mounted");
    vga_puts("[BOOT] step6 done (ext2)\n");

    /* V0.13.54: 根分区 ext2 自动扩展 (分区表 > 文件系统时扩展到全分区) */
    if (g_root_size > 0) {
        extern int ext2_64_expand_to_partition(uint32_t lba, uint32_t sectors);
        int xr = ext2_64_expand_to_partition(g_root_lba, g_root_size);
        vga_puts("[EXTRESIZE] root=");
        vga_put_hex((uint64_t)g_root_lba);
        vga_puts(" size=");
        vga_put_hex((uint64_t)g_root_size);
        vga_puts(" rc=");
        vga_put_hex((uint64_t)xr);
        vga_puts("\n");
    }

    /* V0.13.51: 分区表扫描 + 挂载状态 (P1: /cfg /data 挂载表地基) */
    {
        extern int partition64_scan(void *parts);
        typedef struct {
            int present; uint8_t type; uint32_t lba_start;
            uint32_t sectors; int fs_ext2;
        } part_t;
        part_t parts[4];
        extern void vga_put_hex(uint64_t v);
        int pr = partition64_scan(parts);
        vga_puts("[PARTS] MBR scan rc=");
        vga_put_hex((uint64_t)pr);
        vga_puts("\n");
        for (int i = 0; i < 4; i++) {
            if (parts[i].present) {
                vga_puts("[PARTS] P");
                vga_put_hex((uint64_t)(i + 1));
                vga_puts(" type=");
                vga_put_hex((uint64_t)parts[i].type);
                vga_puts(" lba=");
                vga_put_hex((uint64_t)parts[i].lba_start);
                vga_puts(" sz=");
                vga_put_hex((uint64_t)parts[i].sectors);
                vga_puts(" ext2=");
                vga_put_hex((uint64_t)(parts[i].fs_ext2 ? 1 : 0));
                vga_puts("\n");
            }
        }

        /* V0.13.56: 未格式化分区自动 mkfs (/cfg /data 就绪)
         * 出厂 P2(配置) P3(自由) 无文件系统 -> 开机自动创建 ext2 使其可用
         * 已格式化分区跳过(真机首次mkfs后不再重复) */
        extern int ext2_64_mkfs(uint32_t lba, uint32_t sectors, const char *label);
        for (int i = 1; i < 4; i++) {   /* P2/P3/P4 */
            if (parts[i].present && parts[i].type == 0x83 && !parts[i].fs_ext2) {
                const char *lbl = (i == 1) ? "HEPU_CFG" : (i == 2) ? "HEPU_DATA" : "HEPU_PART";
                int mr = ext2_64_mkfs(parts[i].lba_start, parts[i].sectors, lbl);
                vga_puts("[MKFS] P");
                vga_put_hex((uint64_t)(i + 1));
                vga_puts(" (");
                vga_puts(lbl);
                vga_puts(") rc=");
                vga_put_hex((uint64_t)mr);
                vga_puts(mr == 0 ? " -> /cfg,/data ready\n" : "\n");
                parts[i].fs_ext2 = (mr == 0) ? 1 : 0;
            }
        }
        /* 挂载状态汇总 */
        for (int i = 1; i < 4; i++) {
            if (parts[i].present && parts[i].type == 0x83) {
                vga_puts("[MOUNT] P");
                vga_put_hex((uint64_t)(i + 1));
                vga_puts(" = ");
                vga_puts(i == 1 ? "/cfg (用户配置分区)\n"
                                : i == 2 ? "/data (自由分区)\n" : "/part\n");
            }
        }

        /* V0.13.57: 多实例挂载 — P2(/cfg) P3(/data) 文件级访问 */
        extern int ext2_64_mount_slot(int slot, uint32_t off);
        for (int i = 1; i < 4; i++) {
            if (parts[i].present && parts[i].type == 0x83 && parts[i].fs_ext2) {
                int ms = ext2_64_mount_slot(i, parts[i].lba_start);
                vga_puts("[MOUNT-OK] P");
                vga_put_hex((uint64_t)(i + 1));
                vga_puts(" rc=");
                vga_put_hex((uint64_t)ms);
                vga_puts(ms == 0 ? " mounted\n" : "\n");
            }
        }



    }



    /* V0.13.51: 内存压力管理初始化 + 启动内存状态 */
    {
        extern void mem_pressure_collect(void *st);
        typedef struct {
            uint64_t phys_total, phys_used, phys_free;
            uint64_t kheap_total, kheap_used, kheap_peak;
            uint64_t cache_bytes;
            int cache_slots_used;
            int pressure_level;
            uint64_t reclaim_count;
        } mps_t;
        extern void vga_put_hex(uint64_t v);
        mps_t mps;
        mem_pressure_collect(&mps);
        vga_puts("[MEMPRESS] Phys free=");
        vga_put_hex(mps.phys_free);
        vga_puts(" KHEAP used=");
        vga_put_hex(mps.kheap_used);
        vga_puts("/");
        vga_put_hex(mps.kheap_total);
        vga_puts(" cache=");
        vga_put_hex(mps.cache_slots_used);
        vga_puts(" slots pressure=");
        vga_put_hex((uint64_t)mps.pressure_level);
        vga_puts("\n");
    }

    /* 注：字体加载由 gfx64_init_from_bootloader() 统一处理
     * (内嵌 DejaVuSans 子集 + NotoSansCJK 子集)，此处不再重复 */

    /* 初始化RTL8139网卡 */
    vga_puts("[INIT] Initializing 64-bit RTL8139 network...\n");
    rtl8139_init();

    /* 初始化图形子系统（VBE） */
    vga_puts("[INIT] Initializing 64-bit graphics (VBE)...\n");
    gfx64_init_from_bootloader();
    /* 启用TrueType字体（如果已加载） */
    extern void gfx64_enable_ttf(int enable);
    extern void gfx64_set_font_size(int size);
    gfx64_set_font_size(16);
    gfx64_enable_ttf(1);
    vga_puts("[INIT] TrueType font enabled for graphics\n");

    /* 初始化网络协议栈 */
    vga_puts("[INIT] Initializing 64-bit network stack...\n");
    net64_init();

    /* 初始化DHCP和DNS */
    vga_puts("[INIT] Initializing 64-bit DHCP client...\n");
    dhcp64_init();
    /* V0.13.19: 驱动管理器初始化(仓库延迟加载, 由 drv 命令触发) */
    vga_puts("[INIT] Initializing 64-bit DNS resolver...\n");
    dns64_init();

    /* 初始化HTTP客户端 */
    vga_puts("[INIT] Initializing 64-bit HTTP client...\n");
    http64_init();

    /* 初始化进程管理 */
    vga_puts("[INIT] Initializing 64-bit process manager...\n");
    process64_init();

    /* 初始化用户态进程 */
    vga_puts("[INIT] Initializing 64-bit user process support...\n");
    user_process_init();
    /* 内置 hello 自测以内核函数为入口、需旧模式映射栈到 0x6ff00000；在隔离模式下会把
     * PDPT[1] 泄漏进内核 PDPT，导致后续 ELF 进程的隔离判定误判为"与 RAM 恒等映射冲突"。
     * 故隔离模式下不创建内置 hello（它本就不适用隔离模型）。 */
#if defined(HEPU_USER_ISOLATION) && HEPU_USER_ISOLATION
    vga_puts("[USER64] isolation mode: skip built-in legacy hello self-test\n");
#else
    /* 只创建进程，不切换（切换在所有初始化完成后） */
    user_process_create("hello", user_program_hello);
#endif

    /* ------------------------------------------------------------------
     * V0.33: 启动自检电池（HEPU_BOOT_SELFTEST 开关，默认关）
     *
     * 契约依据：OPEN_BUGS SEC-08「sec_test.elf 自动运行已改为 HEPU_BOOT_SELFTEST
     * 开关（默认关，make BOOT_SELFTEST=1 打开）」——这条约定 v0.13.68 就写下了，
     * 但代码里这十几个用户程序一直是无条件跑，约定没落地。
     *
     * 为什么要收回开关后面：
     *   1. 发布构建不该在每次开机时连跑十几个用户程序（拖慢启动、污染启动日志）；
     *   2. 自检是**验证手段**，不是系统功能 —— 它属于调试/回归通道；
     *   3. 现在用户态异常只终止该进程（SEC-07 已落地），自检失败不再拖垮整机，
     *      所以开关打开时也能安全地跑完整套。
     *
     * 打开方式：make BOOT_SELFTEST=1（见 kernel/arch/x86_64/Makefile）。
     * ------------------------------------------------------------------ */
#if defined(HEPU_BOOT_SELFTEST) && HEPU_BOOT_SELFTEST
        /* V0.33: 电池汇总用的「预期异常数」（crash_jump.elf 是故意的坏地址测试） */
        static uint64_t g_static_expected_exceptions = 0;
        /* 安全回归：用户地址访问限制（内核地址/未映射地址应被拒绝，不崩溃） */
        extern int user_process_exec(const char *path);
        vga_puts("[SEC] Running user-address security regression /bin/sec_test.elf...\n");
        if (user_process_exec("/bin/sec_test.elf") < 0) {
            vga_puts("[SEC] sec_test.elf not available (optional)\n");
        }
        vga_puts("[SEC] Security regression returned, continuing boot\n");

        /* Linux 兼容层测试 */
        vga_puts("[LINUX] Running /bin/hello_linux.elf...\n");
        if (user_process_exec("/bin/hello_linux.elf") < 0) {
            vga_puts("[LINUX] hello_linux not available (optional)\n");
        }
        vga_puts("[LINUX] returned\n");

        /* V0.20.78: 动态链接测试 */
        vga_puts("[DYNLINK] Running /bin/dynhello.elf...\n");
        if (user_process_exec("/bin/dynhello.elf") < 0) {
            vga_puts("[DYNLINK] dynhello not available (optional)\n");
        }
        vga_puts("[DYNLINK] returned\n");

        /* V0.20.82: 带内部重定位的库测试 */
        vga_puts("[DYNLINK2] Running /bin/dynhello2.elf...\n");
        if (user_process_exec("/bin/dynhello2.elf") < 0) {
            vga_puts("[DYNLINK2] not available\n");
        }
        vga_puts("[DYNLINK2] returned\n");

        vga_puts("[STATIC] Running /bin/hello_static.elf...\n");
        {
            /* V0.33 SEC-12 更正：hello_static.elf **不是**故意的坏地址测试。
             *
             * 旧注释（以及旧版 expected 计数）把它当成「故意跳坏地址」，
             * 那是**误判** —— 反汇编显示它只是 write(1,msg,25) 然后 exit(42)，
             * 压根没有跳坏地址。它当时之所以炸，是因为它缺 .note.ABI-tag，
             * 被内核判成 Hepu 原生 ABI ⇒ Linux 号 exit(60) 在原生表里查不到
             * （原生 exit 是 6）⇒ 进程不肯退出 ⇒ 执行流落到后面的字符串
             * 数据上 ⇒ "He" 被解成 rex.W gs insb ⇒ CPL=3 执行 insb ⇒ #GP(0)。
             * 于是产生了那条 rip=0x4000102E 的假象。
             *
             * 现在 legacy/hello_static.c 补了 ABI note ⇒ exit(42) 正常生效 ⇒
             * 本程序干净退出，**不再产生任何异常**。
             * 真正需要「故意坏地址」来验证 SEC-07 的载体换成 crash_jump.elf。 */
            if (user_process_exec("/bin/hello_static.elf") < 0) {
                vga_puts("[STATIC] not available\n");
            }
            vga_puts("[STATIC] returned\n");
        }

        /* V0.33 SEC-12: 真正的「故意坏地址」异常测试 —— 验证 SEC-07
         * 「用户态异常只终止该进程、不整机 hlt」。
         * crash_jump.elf 显式往未映射地址 0xDEAD0000 写一个值 ⇒ #PF。
         * 它产生的 1 次用户态异常是**预期内**的，汇总判据时必须排除，
         * 否则每次都会误报 FAIL。 */
        vga_puts("[CRASH] Running /bin/crash_jump.elf (expected to fault)...\n");
        {
            extern volatile uint64_t g_user_exception_count;
            uint64_t exc_before_crash = g_user_exception_count;
            if (user_process_exec("/bin/crash_jump.elf") < 0) {
                vga_puts("[CRASH] not available\n");
            }
            g_static_expected_exceptions = g_user_exception_count - exc_before_crash;
            vga_puts("[CRASH] returned\n");
        }

        vga_puts("[GLIBC] Running /bin/glibc_test.elf...\n");
        if (user_process_exec("/bin/glibc_test.elf") < 0) {
            vga_puts("[GLIBC] not available\n");
        }
        vga_puts("[GLIBC] returned\n");

        vga_puts("[LINUX2] Running /bin/hello2.elf...\n");
        if (user_process_exec("/bin/hello2.elf") < 0) {
            vga_puts("[LINUX2] not available\n");
        }
        vga_puts("[LINUX2] returned\n");

        vga_puts("[LINUX3] Running /bin/hello3.elf...\n");
        if (user_process_exec("/bin/hello3.elf") < 0) {
            vga_puts("[LINUX3] not available\n");
        }
        vga_puts("[LINUX3] returned\n");

        vga_puts("[LINUX4] Running /bin/hello4.elf...\n");
        if (user_process_exec("/bin/hello4.elf") < 0) {
            vga_puts("[LINUX4] not available\n");
        }
        vga_puts("[LINUX4] returned\n");

        vga_puts("[L_C] Running /bin/hello_c2.elf...\n");
        if (user_process_exec("/bin/hello_c2.elf") < 0) {
            vga_puts("[L_C] not available\n");
        }
        vga_puts("[L_C] returned\n");

        /* V0.33: freestanding static-PIE 探针 —— 先用最小程序验证内核侧
         * （加载偏置 + RELATIVE 重定位 + 初始栈 auxv）是否正确，
         * 把 glibc 自身的 TLS/自重定位问题隔离在外。 */
        vga_puts("[PIEPROBE] Running /bin/pie_probe.elf...\n");
        if (user_process_exec("/bin/pie_probe.elf") < 0) {
            vga_puts("[PIEPROBE] not available (optional)\n");
        }
        vga_puts("[PIEPROBE] returned\n");

        /* V0.33: Linux 兼容层 PIE 测试（加载偏置 + Linux 号表翻译） */
        vga_puts("[LINUXPIE] Running /bin/linux_pie_demo.elf...\n");
        if (user_process_exec("/bin/linux_pie_demo.elf") < 0) {
            vga_puts("[LINUXPIE] not available (optional)\n");
        }
        vga_puts("[LINUXPIE] returned\n");

        /* fork 端到端测试 */
        vga_puts("[TEST] Running /bin/forktest.elf...\n");
        if (user_process_exec("/bin/forktest.elf") < 0) {
            vga_puts("[TEST] forktest not available (optional)\n");
        }
        vga_puts("[TEST] forktest returned\n");

        /* V0.33: 电池汇总 —— 给出可判定的结论（此前只有逐项日志，没有总判定）。
         * 判据：除 hello_static 的**故意**异常外，没有其它用户态异常
         *       （SEC-07 隔离路径计数为 0）。 */
        {
            extern volatile uint64_t g_user_exception_count;
            uint64_t total = g_user_exception_count;
            uint64_t unexpected = total - g_static_expected_exceptions;
            vga_puts("\n[SELFTEST] battery finished: exceptions total=");
            vga_put_hex(total);
            vga_puts(" expected(crash_jump)=");
            vga_put_hex(g_static_expected_exceptions);
            vga_puts(" unexpected=");
            vga_put_hex(unexpected);
            vga_puts(unexpected == 0 ? " [PASS]\n" : " [FAIL]\n");
        }

#else
    vga_puts("[SELFTEST] boot self-test battery disabled (build with BOOT_SELFTEST=1 to enable)\n");
#endif

    /* 初始化键盘驱动 */
    vga_puts("[INIT] Initializing 64-bit keyboard driver...\n");
    keyboard64_init();
    /* keyboard64_test();  跳过测试，加快启动 */



    /* 初始化PS/2鼠标 */
    vga_puts("[INIT] Initializing 64-bit PS/2 mouse...\n");
    mouse64_init(1024, 768);

    /* 初始化AHCI/SATA */
    vga_puts("[INIT] Initializing 64-bit AHCI/SATA...\n");
    ahci64_init();

    /* V0.13.63: 真机 SATA 盘走 AHCI — 系统盘 ext2 块设备切换到 AHCI 读入口,
     * 否则 P2/P3 挂载仍走 IDE (真机 SATA 读不到) */
    {
        extern int ahci64_disk_present(void);
        extern int ahci64_read_disk(uint32_t, uint8_t, uint8_t *);
        extern void ext2_64_set_block_device(int (*rd)(uint32_t, uint8_t, uint8_t *));
        if (ahci64_disk_present()) ext2_64_set_block_device(ahci64_read_disk);
    }

    /* 初始化16550 UART串口 */
    extern int uart64_init(int com);
    vga_puts("[INIT] Initializing 16550 UART serial (COM1)...\n");
    uart64_init(0);  /* COM1 */

    /* 初始化AC97声卡 */
    extern int ac97_init(void);
    vga_puts("[INIT] Initializing AC'97 audio...\n");
    ac97_init();

    /* 初始化HepuShell IPC */
    extern int hsipc_init(void);
    vga_puts("[INIT] Initializing HepuShell IPC...\n");
    hsipc_init();

    /* 初始化HepuShell服务 */
    extern int hs_services_init(void);
    vga_puts("[INIT] Initializing HepuShell services...\n");
    hs_services_init();

    /* 初始化HepuShell高级功能 */
    extern int hs_lock_init(const char *);
    extern int hs_animation_init(void);
    extern int hs_theme_engine_init(void);
    extern int hs_plugin_init(void);
    vga_puts("[INIT] Initializing HepuShell extras...\n");
    hs_lock_init("hepu");
    hs_animation_init();
    hs_theme_engine_init();
    hs_plugin_init();

    /* 注册所有驱动到统一框架 */
    extern void drv_register_all64(void);
    drv_register_all64();
    extern int reg64_init(void);   /* V0.13.19: 系统注册表加载 */
    reg64_init();
    extern void gps64_init(void);   /* V0.13.20: GPS 定位服务 */
    gps64_init();
    extern int l10n64_init(void);   /* V0.13.23: 多语言框架 */
    l10n64_init();
    extern void drv_mgr64_init(void);   /* V0.13.18: 驱动管理器(仓库/下载/安装) */
    drv_mgr64_init();
    vga_puts("[OK] All drivers registered to unified framework\n");

    /* V0.13.9: 网卡驱动接线 (协议栈已由上方 net64_init 初始化) */
    extern int e1000_init(void);
    extern int eth64_set_ip(uint32_t ip, uint32_t netmask, uint32_t gateway);
    if (e1000_init() == 0) {
        /* V0.13.9: 静态 IP (10.0.2.15/24) — 测试环境无 DHCP 服务器 */
        eth64_set_ip(0x0A00020F, 0xFFFFFF00, 0x0A000202);  /* 10.0.2.15 / 10.0.2.2 */
        vga_puts("[OK] Network up (RTL8139 + TCP/IP, static 10.0.2.15)\n");
        klog64_log("[BOOT] network up (RTL8139, static IP)");
    } else {
        vga_puts("[NET] No NIC found, network stack idle\n");
        klog64_log("[BOOT] no NIC found, network idle");
    }

    /* 初始化存储硬件抽象层（已在前面初始化，这里跳过） */
    /* storage_hal_init();  已在ext2初始化前调用 */

    /* 初始化网络硬件抽象层 */
    extern int net_hal_init(void);
    net_hal_init();

    /* 初始化BSD Socket API */
    extern int socket64_init(void);
    socket64_init();

    /* 初始化字体管理器 */
    vga_puts("[INIT] Initializing 64-bit font manager...\n");
    font64_init();

    /* 初始化窗口管理器 */
    vga_puts("[INIT] Initializing 64-bit window manager...\n");
    window64_init();

    /* 初始化文件管理器 */
    vga_puts("[INIT] Initializing 64-bit file manager...\n");
    fm64_init();

    /* 初始化系统设置 */
    vga_puts("[INIT] Initializing 64-bit system settings...\n");
    syssettings64_init();

    /* 初始化记事本 */
    vga_puts("[INIT] Initializing 64-bit notepad...\n");
    notes64_init();

    /* 初始化终端增强 */
    vga_puts("[INIT] Initializing 64-bit terminal enhancement...\n");
    term64_init();

    /* 初始化应用商店 */
    vga_puts("[INIT] Initializing 64-bit app store...\n");
    appstore64_init();

    /* 初始化RTC */
    vga_puts("[INIT] Initializing 64-bit RTC...\n");
    rtc64_init();

    /* V0.55 P2/P3: 加载账户; 首启向导延迟到桌面/文本通道确定后运行 */
    auth64_init();
    int need_oobe = !auth64_is_configured();
    if (need_oobe) {
        vga_puts("\n[HepuOS] First boot detected, setup wizard pending...\n\n");
        klog64_log("[OOBE] first boot, wizard pending");
    } else {
        klog64_log("[ACCOUNT] configured, skip OOBE");
    }

    /* 显示内存信息 */
    vga_puts("\n[INFO] Kernel stack: ");
    vga_put_hex((uint64_t)kernel_stack);
    vga_puts(" - ");
    vga_put_hex((uint64_t)kernel_stack_top);
    vga_puts("\n");

    /* 显示EFER MSR */
    uint64_t efer = x86_64_read_msr(0xC0000080);
    vga_puts("[INFO] EFER MSR: ");
    vga_put_hex(efer);
    vga_puts("\n");

    /* 初始化完成 */
    vga_puts("\n========================================\n");
    vga_puts("  64-bit Kernel Initialization Complete!\n");
    vga_puts("  Starting Shell...\n");
    vga_puts("========================================\n");

    /* 尝试启动桌面环境，失败则启动Shell */
    if (desktop64_init() == 0) {
        vga_puts("[INIT] Desktop environment ready\n");
        klog64_log("[GUI] desktop environment ready");

        /* Crypto 完整懒加载 + TLS (延迟到桌面后，启动更快) */
        extern void crypto_init_full_lazy(void);
        extern void hepu_tls_init(void);
        crypto_init_full_lazy();
        hepu_tls_init();
        vga_puts("[OK] Crypto full + TLS ready\n");

        desktop64_run();
    } else {
        vga_puts("[INIT] Falling back to text shell\n");
        /* 图形化不可用: 文本模式作为首启向导兜底通道 */
        if (need_oobe) {
            vga_puts("[HepuOS] Text-mode setup wizard (GUI unavailable)\n");
            oobe64_run();
        }
        shell64_run();
    }
}

/* 从汇编入口调用的C函数（设置Multiboot信息） */
void x86_64_set_boot_info(uint32_t magic, uint64_t mb_info)
{
    g_mb_magic = magic;
    g_mb_info_addr = mb_info;
}
