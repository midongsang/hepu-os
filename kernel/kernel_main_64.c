/* ============================================================================
 * 赫普OS - 64位内核主入口
 * V0.10.6: 初始64位内核实现（简化版，仅核心功能）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

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

/* Multiboot信息指针（从引导器传递） */
uint64_t g_mb_info_addr = 0;
uint32_t g_mb_magic = 0;

/* 64位内核主函数 */
void x86_64_kernel_main(void)
{
    /* 最早期启用FPU/SSE，防止编译器生成的SSE指令在启用前触发#UD */
    extern void x86_64_enable_fpu_early(void);
    x86_64_enable_fpu_early();

    /* 显示启动信息 */
    vga_puts("========================================\n");
    vga_puts("  HepuOS 64-bit Kernel v0.10.170\n");
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
    pmm64_init(2ULL * 1024 * 1024 * 1024, 0);  /* 2GB最大内存支持 */
    vga_puts("[OK] Physical memory manager initialized\n");

    /* 初始化统一驱动框架 */
    extern void drv_init(void);
    drv_init();

    uint64_t mem_total, mem_used, mem_free;
    pmm64_get_stats(&mem_total, &mem_used, &mem_free);
    vga_puts("[INFO] Memory: total=");
    vga_put_hex(mem_total / (1024 * 1024));
    vga_puts("MB used=");
    vga_put_hex(mem_used / (1024 * 1024));
    vga_puts("MB free=");
    vga_put_hex(mem_free / (1024 * 1024));
    vga_puts("MB\n");

    /* 初始化内核堆（4MB，放在0x300000） */
    vga_puts("[INIT] Initializing kernel heap (4MB)...\n");
    kheap64_init((void*)0x300000, 4 * 1024 * 1024);
    vga_puts("[OK] Kernel heap initialized\n");

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

    /* 初始化APIC中断控制器（VMM之后，可映射高地址） */
    vga_puts("[INIT] Initializing APIC (step 1: Local APIC only)...\n");
    apic64_init();
    vga_puts("[OK] APIC initialized\n");

    /* 切换到纯APIC模式（必须在启动AP之前）：
       否则 apic_mode 全局标志保持0，AP核启动后 apic64_get_id() 与
       irq_handler_64 的FPU保存区选择都走PIC分支，导致所有AP核误判自己
       为CPU0、FXSAVE写入BSP的保存区，破坏多核渲染稳定性。 */
    extern int apic64_switch_to_apic_mode(void);
    apic64_switch_to_apic_mode();

    /* 初始化多处理器支持（SMP）。
       AP核启动后会在ap_c_entry中自旋等待BSP完成IDT初始化（g_apic_idt_ready），
       因此即使irq64_init在smp_init之后，AP核也不会在IDT无效时收到中断。 */
    extern int smp_init(void);
    smp_init();

    /* GDT/TSS已在汇编入口处加载（entry_64.S） */
    vga_puts("[INIT] Initializing 64-bit GDT/TSS...\n");
    vga_puts("[OK] GDT loaded at entry (kernel GDT active)\n");
    tss64_init();

    /* 初始化中断处理（IDT填充后通知AP核继续启用中断） */
    vga_puts("[INIT] Initializing 64-bit IDT...\n");
    irq64_init();

    /* 初始化系统调用 */
    vga_puts("[INIT] Initializing 64-bit syscall...\n");
    syscall64_init();
    syscall64_test();
    syscall64_full_init();

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

                /* 优先挂载最后一个分区（根分区），失败时回退到第一个（initrd恢复） */
                if (part_count > 0) {
                    int root_part = part_count - 1;  /* 最后一个分区作为根 */
                    ext2_64_set_partition_offset(part_lba[root_part]);
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
                vga_puts("[INIT] No MBR signature, using raw disk\n");
            }
        }
    }

    /* 提前初始化存储HAL（ext2需要通过它读取磁盘） */
    extern int storage_hal_init(void);
    storage_hal_init();

    /* 初始化ext2文件系统 */
    vga_puts("[INIT] Initializing 64-bit ext2 filesystem...\n");
    ext2_64_init();

    /* 加载TrueType字体 */
    vga_puts("[INIT] Loading TrueType font...\n");
    uint8_t *font_buf = (uint8_t *)pmm64_alloc_pages(192);  /* 768KB */
    if (font_buf) {
        int fsize = ext2_64_read_file("/fonts/DejaVuSans.ttf", font_buf, 192 * 4096);
        if (fsize > 0) {
            vga_puts("[INIT] Font loaded: ");
            vga_put_hex(fsize);
            vga_puts(" bytes\n");
            ttf64_init(font_buf, fsize);
        } else {
            vga_puts("[INIT] Font file not found, using bitmap font\n");
        }
    } else {
        vga_puts("[INIT] No memory for font, using bitmap font\n");
    }

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
    /* 只创建进程，不切换（切换在所有初始化完成后） */
    user_process_create("hello", user_program_hello);

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
    vga_puts("[OK] All drivers registered to unified framework\n");

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
        desktop64_run();
    } else {
        vga_puts("[INIT] Falling back to text shell\n");
        shell64_run();
    }
}

/* 从汇编入口调用的C函数（设置Multiboot信息） */
void x86_64_set_boot_info(uint32_t magic, uint64_t mb_info)
{
    g_mb_magic = magic;
    g_mb_info_addr = mb_info;
}
