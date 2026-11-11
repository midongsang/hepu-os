/* ============================================================================
 * 赫普OS - x86_64支持实现
 * V8.38: 初始实现（长模式切换+64位页表）
 * ========================================================================== */

#include "x86_64.h"
#include "../../include/vga.h"

/* 内存操作 */
extern void *memset(void *dst, int val, uint32_t n);
extern void *memcpy(void *dst, const void *src, uint32_t n);

/* 检测CPU是否支持x86_64（长模式） */
int x86_64_detect(void)
{
    uint32_t eax, ebx, ecx, edx;

    /* 检查CPUID是否支持扩展功能 */
    __asm__ volatile(
        "mov $0x80000000, %%eax\n"
        "cpuid\n"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        :
        : "memory"
    );

    if (eax < 0x80000001) {
        vga_puts("[x86_64] CPU does not support extended CPUID\n");
        return 0;
    }

    /* 检查长模式支持 */
    __asm__ volatile(
        "mov $0x80000001, %%eax\n"
        "cpuid\n"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        :
        : "memory"
    );

    if (edx & (1 << 29)) {  /* EDX bit 29 = LM (Long Mode) */
        vga_puts("[x86_64] CPU supports long mode (x86_64)\n");
        return 1;
    }

    vga_puts("[x86_64] CPU does not support long mode\n");
    return 0;
}

/* 读取MSR */
uint64_t x86_64_read_msr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

/* 写入MSR */
void x86_64_write_msr(uint32_t msr, uint64_t value)
{
    uint32_t lo = (uint32_t)value;
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

/* 检查是否在长模式 */
int x86_64_is_long_mode(void)
{
    uint64_t efer = x86_64_read_msr(EFER_MSR);
    return (efer & EFER_LMA) ? 1 : 0;
}

/* 启用系统调用扩展（SYSCALL/SYSRET） */
void x86_64_enable_sce(void)
{
    uint64_t efer = x86_64_read_msr(EFER_MSR);
    efer |= EFER_SCE;
    x86_64_write_msr(EFER_MSR, efer);
    vga_puts("[x86_64] SCE (System Call Extensions) enabled\n");
}

/* 启用不执行位（NX） */
void x86_64_enable_nxe(void)
{
    uint64_t efer = x86_64_read_msr(EFER_MSR);
    efer |= EFER_NXE;
    x86_64_write_msr(EFER_MSR, efer);
    vga_puts("[x86_64] NXE (No-Execute Enable) enabled\n");
}

/* 启用SMEP（监督模式执行保护） */
void x86_64_enable_smep(void)
{
    uint64_t cr4 = x86_64_read_cr4();
    cr4 |= CR4_SMEP;
    x86_64_write_cr4(cr4);
    vga_puts("[x86_64] SMEP enabled\n");
}

/* 启用SMAP（监督模式访问保护） */
void x86_64_enable_smap(void)
{
    uint64_t cr4 = x86_64_read_cr4();
    cr4 |= CR4_SMAP;
    x86_64_write_cr4(cr4);
    vga_puts("[x86_64] SMAP enabled\n");
}

/* V0.33: 使能用户态 SSE/SIMD 状态。
 *
 * 为什么要开：CR4.OSFXSR 没置位时，用户态执行任何 SSE 指令（movaps/pxor/
 * movdqa …）都会 #GP（error=0），连累整个进程。glibc 与 gcc 默认都会生成
 * SSE 指令（x86-64 架构本身就要求 SSE2），所以这不是「可选加速」而是
 * **用户态 ABI 的硬要求** —— Linux 的 start_thread() 同样会置
 * CR4.OSFXSR | CR4.OSXMMEXCPT。
 *
 * 现象（V0.33 自检定位）：启动自检里 hello_static.elf / comptest.elf
 * 在 movaps 处 #GP，rip 落在用户代码。
 */
void x86_64_enable_sse(void)
{
    uint64_t cr4 = x86_64_read_cr4();
    cr4 |= CR4_OSFXSR | CR4_OSXMMEXCPT;
    x86_64_write_cr4(cr4);
    vga_puts("[x86_64] SSE/SIMD user-mode enabled (CR4.OSFXSR|OSXMMEXCPT)\n");
}

/* 按 CPU 能力使能 SSE（探测结果单一事实源，同 SMEP/SMAP 的做法） */
int x86_64_enable_sse_if_supported(void)
{
    uint32_t feat = x86_64_detect_cpu_features();
    if (!(feat & CPUFEAT_SSE)) {
        vga_puts("[x86_64] SSE not supported by CPU (not enabled)\n");
        return 0;
    }
    x86_64_enable_sse();
    return 1;
}

/* ============================================================================
 * V0.33: CPU 能力探测
 *
 * 为什么要专门收口：SMEP/SMAP 是 x86-64 的**可选**特性。
 *   - CPUID.(EAX=7,ECX=0):EBX[7]  = SMEP 支持
 *   - CPUID.(EAX=7,ECX=0):EBX[20] = SMAP 支持
 * 旧代码在 kernel_main_64.c 里直接 `cpuid` leaf 7，既没检查 max_leaf>=7，
 * 也没把结果沉淀成可查询的能力状态：
 *   - 老 CPU 上 leaf 7 读到的是垃圾值，能力判定不可靠；
 *   - 「CPU 支不支持」和「CR4 里到底置没置」两件事混在一起，没有单一事实源；
 *   - 于是 copy_*_user 无条件执行 STAC，在不支持 SMAP 的机器上直接 #UD 死机。
 *
 * 现在：探测结果（CPU 支不支持）与硬件状态（CR4 置没置）分开，
 * 单一事实源是硬件本身。
 * ========================================================================== */
static uint32_t g_cpu_features = 0;
static uint32_t g_cpu_max_leaf = 0;

/* V0.33: 前向声明 —— 下面的 x86_64_enable_sse_if_supported() 要用它 */
uint32_t x86_64_detect_cpu_features(void);

/* CPUID 封装。注意：x86-64 下 RBX 可用于输出（非 PIC 编译）。 */
static inline void x86_64_cpuid(uint32_t leaf, uint32_t sub, uint32_t *a, uint32_t *b,
                                uint32_t *c, uint32_t *d)
{
    __asm__ volatile("cpuid"
                     : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf), "c"(sub));
}

uint32_t x86_64_cpu_features(void)
{
    return g_cpu_features;
}

int x86_64_smap_active(void)
{
    return (x86_64_read_cr4() & CR4_SMAP) ? 1 : 0;
}

int x86_64_smep_active(void)
{
    return (x86_64_read_cr4() & CR4_SMEP) ? 1 : 0;
}

/* V0.33: 用户态 SSE 状态（单一事实源 = CR4） */
int x86_64_sse_active(void)
{
    return (x86_64_read_cr4() & CR4_OSFXSR) ? 1 : 0;
}

/* 探测一次 CPU 能力（幂等）。 */
uint32_t x86_64_detect_cpu_features(void)
{
    if (g_cpu_features) return g_cpu_features;

    uint32_t a = 0, b = 0, c = 0, d = 0;
    x86_64_cpuid(0, 0, &a, &b, &c, &d);
    g_cpu_max_leaf = a;

    /* leaf 1: SSE2 (ECX bit 26)、RDRAND (ECX bit 30)、TSC (EDX bit 4) */
    if (g_cpu_max_leaf >= 1) {
        x86_64_cpuid(1, 0, &a, &b, &c, &d);
        if (c & (1u << 26)) g_cpu_features |= CPUFEAT_SSE;
        if (c & (1u << 30)) g_cpu_features |= CPUFEAT_RDRAND;
        if (d & (1u << 4))  g_cpu_features |= CPUFEAT_TSC;
    }

    /* leaf 7 subleaf 0: SMEP (EBX bit 7)、SMAP (EBX bit 20) */
    if (g_cpu_max_leaf >= 7) {
        x86_64_cpuid(7, 0, &a, &b, &c, &d);
        if (b & (1u << 7))  g_cpu_features |= CPUFEAT_SMEP_SUP;
        if (b & (1u << 20)) g_cpu_features |= CPUFEAT_SMAP_SUP;
    }

    /* NXE：EFER.NXE 实际状态 */
    if (x86_64_read_msr(EFER_MSR) & EFER_NXE) {
        g_cpu_features |= CPUFEAT_NXE;
    }
    return g_cpu_features;
}

/* 置 CR4.SMEP 前先确认 CPU 真的支持，否则不置（置了也不会生效，徒增不确定性）。 */
int x86_64_enable_smep_if_supported(void)
{
    uint32_t feat = x86_64_detect_cpu_features();
    if (!(feat & CPUFEAT_SMEP_SUP)) {
        vga_puts("[x86_64] SMEP not supported by CPU (not enabled)\n");
        return 0;
    }
    x86_64_enable_smep();
    return 1;
}

/* 置 CR4.SMAP 前先确认 CPU 真的支持。
 * 注意：SMAP 关着的时候，copy_*_user 里的 STAC 是非法指令，
 * 所以「不开 SMAP」必须有一条明确的安全路径（见 hepu_smap.h）。 */
int x86_64_enable_smap_if_supported(void)
{
    uint32_t feat = x86_64_detect_cpu_features();
    if (!(feat & CPUFEAT_SMAP_SUP)) {
        vga_puts("[x86_64] SMAP not supported by CPU (not enabled)\n");
        return 0;
    }
    x86_64_enable_smap();
    return 1;
}

/* 早期FPU/SSE启用（无打印，用于kernel_main最开头） */
void x86_64_enable_fpu_early(void)
{
    uint64_t cr0 = x86_64_read_cr0();
    cr0 &= ~(CR0_EM | CR0_TS);
    cr0 |= CR0_MP | CR0_NE | CR0_WP;  /* WP: 保护只读页不被写入 */
    x86_64_write_cr0(cr0);
    uint64_t cr4 = x86_64_read_cr4();
    cr4 |= CR4_OSFXSR | CR4_OSXMMEXCPT;
    x86_64_write_cr4(cr4);
    /* 初始化x87 FPU */
    __asm__ volatile("fninit");
    /* 初始化MXCSR：默认值0x1F80（所有异常屏蔽，舍入到最近） */
    uint32_t mxcsr = 0x1F80;
    __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));
}

/* 启用FPU和SSE */
void x86_64_enable_fpu(void)
{
    x86_64_enable_fpu_early();
    vga_puts("[x86_64] FPU/SSE enabled\n");
}

/* 初始化64位页表 */
void x86_64_init_paging(uint64_t *pml4)
{
    memset(pml4, 0, PAGE_SIZE_4K);

    /* 身份映射前4GB内存（使用2MB大页） */
    /* PML4[0] -> PDPT */
    /* PDPT[0..3] -> PD（每个PD映射1GB） */
    /* PD[0..511] -> 2MB页 */

    /* 简化：使用静态分配的页表 */
    static uint64_t pdpt[512] __attribute__((aligned(4096)));
    static uint64_t pd[4][512] __attribute__((aligned(4096)));

    memset(pdpt, 0, sizeof(pdpt));
    memset(pd, 0, sizeof(pd));

    /* PML4[0] 指向 PDPT */
    pml4[0] = (uint64_t)pdpt | PT64_PRESENT | PT64_WRITABLE;

    /* PDPT[0..3] 指向 PD */
    for (int i = 0; i < 4; i++) {
        pdpt[i] = (uint64_t)pd[i] | PT64_PRESENT | PT64_WRITABLE;
    }

    /* PD[0..511] 映射 2MB 大页 */
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 512; j++) {
            uint64_t phys_addr = (uint64_t)(i * 512 + j) * PAGE_SIZE_2M;
            pd[i][j] = phys_addr | PT64_PRESENT | PT64_WRITABLE | PT64_HUGE;
        }
    }

    vga_puts("[x86_64] 4-level paging initialized (identity mapped 4GB)\n");
}

/* 映射单个页面 */
void x86_64_map_page(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags)
{
    /* V0.55.1: 这是早期空桩, 不建立任何映射 —— 调用它会静默失败。
     * 需要真实映射请用 vmm64_map_page()(vmm64.c)。当前无调用者。 */
    uint64_t pml4_idx = (virt >> PML4_SHIFT) & PML4_MASK;
    uint64_t pdpt_idx = (virt >> PDPT_SHIFT) & PDPT_MASK;
    uint64_t pd_idx   = (virt >> PD_SHIFT) & PD_MASK;
    uint64_t pt_idx   = (virt >> PT_SHIFT) & PT_MASK;
    (void)pml4; (void)phys; (void)flags;
    (void)pml4_idx; (void)pdpt_idx; (void)pd_idx; (void)pt_idx;
}

/* 虚拟地址转物理地址 */
uint64_t x86_64_virt_to_phys(uint64_t *pml4, uint64_t virt)
{
    /* 简化：身份映射时直接返回 */
    return virt;
}

/* 进入长模式 */
int x86_64_enter_long_mode(void)
{
    vga_puts("[x86_64] Entering long mode...\n");

    /* 1. 检测CPU支持 */
    if (!x86_64_detect()) {
        vga_puts("[x86_64] Long mode not supported\n");
        return -1;
    }

    /* 2. 启用PAE (CR4.PAE) */
    uint64_t cr4 = x86_64_read_cr4();
    cr4 |= CR4_PAE;
    x86_64_write_cr4(cr4);

    /* 3. 设置EFER.LME (Long Mode Enable) */
    uint64_t efer = x86_64_read_msr(EFER_MSR);
    efer |= EFER_LME;
    x86_64_write_msr(EFER_MSR, efer);

    /* 4. 加载PML4到CR3 */
    static uint64_t pml4[512] __attribute__((aligned(4096)));
    x86_64_init_paging(pml4);
    x86_64_write_cr3((uint64_t)pml4);

    /* 5. 启用分页 (CR0.PG) - 这会激活长模式（兼容模式） */
    uint64_t cr0 = x86_64_read_cr0();
    cr0 |= CR0_PG;
    x86_64_write_cr0(cr0);

    /* 6. 验证长模式已激活 */
    if (x86_64_is_long_mode()) {
        vga_puts("[x86_64] Long mode activated (compatibility mode)\n");
    } else {
        vga_puts("[x86_64] Failed to activate long mode\n");
        return -1;
    }

    /* 7. 加载64位GDT（为进入64位模式做准备） */
    x86_64_init_gdt();

    /* 8. 启用NX */
    x86_64_enable_nxe();

    vga_puts("[x86_64] Ready for 64-bit mode (far jump required)\n");
    return 0;
}

/* 初始化64位GDT */
void x86_64_init_gdt(void)
{
    /* 64位GDT需要：
     * - 空描述符
     * - 内核代码段（64位）
     * - 内核数据段
     * - 用户代码段（64位）
     * - 用户数据段
     * - TSS描述符（16字节，系统描述符）
     */

    static gdt_entry64_t gdt[8] __attribute__((aligned(8)));
    static gdt_ptr64_t gdt_ptr;

    memset(gdt, 0, sizeof(gdt));

    /* 内核代码段：0x08 */
    gdt[1].limit_low = 0xFFFF;
    gdt[1].base_low = 0;
    gdt[1].base_mid = 0;
    gdt[1].access = 0x9A;  /* 存在、内核、代码、可读 */
    gdt[1].granularity = 0xAF;  /* 4KB粒度、64位、32位标志=0 */
    gdt[1].base_high = 0;

    /* 内核数据段：0x10 */
    gdt[2].limit_low = 0xFFFF;
    gdt[2].base_low = 0;
    gdt[2].base_mid = 0;
    gdt[2].access = 0x92;  /* 存在、内核、数据、可写 */
    gdt[2].granularity = 0xCF;
    gdt[2].base_high = 0;

    /* 用户代码段：0x18 */
    gdt[3].limit_low = 0xFFFF;
    gdt[3].base_low = 0;
    gdt[3].base_mid = 0;
    gdt[3].access = 0xFA;  /* 存在、用户、代码、可读 */
    gdt[3].granularity = 0xAF;
    gdt[3].base_high = 0;

    /* 用户数据段：0x20 */
    gdt[4].limit_low = 0xFFFF;
    gdt[4].base_low = 0;
    gdt[4].base_mid = 0;
    gdt[4].access = 0xF2;  /* 存在、用户、数据、可写 */
    gdt[4].granularity = 0xCF;
    gdt[4].base_high = 0;

    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint64_t)gdt;

    /* 加载GDT */
    __asm__ volatile("lgdt %0" : : "m"(gdt_ptr));

    vga_puts("[x86_64] GDT initialized (64-bit)\n");
}

/* 初始化64位IDT */
void x86_64_init_idt(void)
{
    /* 64位IDT条目是16字节（32位是8字节） */
    static idt_entry64_t idt[256] __attribute__((aligned(16)));
    memset(idt, 0, sizeof(idt));

    /* 简化：实际需要设置每个中断处理函数 */
    vga_puts("[x86_64] IDT initialized (64-bit, 256 entries)\n");
}

/* 初始化64位TSS */
void x86_64_init_tss(void)
{
    static tss64_t tss;
    memset(&tss, 0, sizeof(tss));

    /* 设置内核栈（需要实际分配） */
    tss.rsp0 = 0xFFFFFF0000000000ULL + 0x100000;  /* 示例地址 */
    tss.iomap_base = sizeof(tss64_t);

    vga_puts("[x86_64] TSS initialized (64-bit)\n");
}

/* 设置内核栈 */
void x86_64_set_kernel_stack(uint64_t stack)
{
    /* 需要更新TSS的rsp0 */
}

/* x86_64_kernel_main 已移至 kernel/kernel_main_64.c */

/* ============================================================================
 * 栈溢出保护（GCC stack protector支持）
 * ========================================================================== */

/* 栈canary值：编译器在函数序言中将此值写入栈帧，函数尾声检查 */
uintptr_t __stack_chk_guard = 0xDEADBEEFCAFEBABEULL;

/* 栈canary被破坏时调用 */
void __stack_chk_fail(void)
{
    vga_puts("\n!!! STACK SMASHING DETECTED !!!\n");
    /* 读取返回地址上方的返回地址，定位崩溃函数 */
    uint64_t *rbp;
    __asm__ volatile("movq %%rbp, %0" : "=r"(rbp));
    if (rbp) {
        vga_puts("Return addr: ");
        extern void vga_put_hex(uint64_t val);
        vga_put_hex(rbp[1]);
        vga_puts("\n");
    }
    /* 停止系统 */
    while (1) { __asm__ volatile("hlt"); }
}
