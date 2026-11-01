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
    uint64_t pml4_idx = (virt >> PML4_SHIFT) & PML4_MASK;
    uint64_t pdpt_idx = (virt >> PDPT_SHIFT) & PDPT_MASK;
    uint64_t pd_idx   = (virt >> PD_SHIFT) & PD_MASK;
    uint64_t pt_idx   = (virt >> PT_SHIFT) & PT_MASK;

    /* 简化：需要动态分配页表 */
    /* 实际实现需要分配PDPT/PD/PT页面 */

    /* 这里只做框架，实际映射需要完整的页表分配器 */
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
