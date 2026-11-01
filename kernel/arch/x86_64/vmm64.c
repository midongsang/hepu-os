/* ============================================================================
 * 赫普OS - 64位虚拟内存管理（简化版）
 * V0.10.6: 初始实现（4级页表、身份映射）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 页表级别 */
#define PML4_INDEX(addr)  (((addr) >> 39) & 0x1FF)
#define PDPT_INDEX(addr)  (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)    (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)    (((addr) >> 12) & 0x1FF)

/* 页表项标志 */
#define PAGE_PRESENT    (1 << 0)
#define PAGE_WRITABLE   (1 << 1)
#define PAGE_USER       (1 << 2)
#define PAGE_WRITE_THROUGH (1 << 3)
#define PAGE_CACHE_DISABLE (1 << 4)
#define PAGE_ACCESSED   (1 << 5)
#define PAGE_DIRTY      (1 << 6)
#define PAGE_HUGE       (1 << 7)
#define PAGE_GLOBAL     (1 << 8)
#define PAGE_NX         (1ULL << 63)

/* 页大小 */
#define PAGE_SIZE 4096
#define PAGE_MASK (~(PAGE_SIZE - 1))

/* 页表项类型 */
typedef uint64_t pte_t;

/* 外部函数 */
extern void vga_puts(const char *str);
extern void vga_put_hex(uint64_t val);
extern void *memset(void *dst, int val, size_t n);
extern void *memcpy(void *dst, const void *src, size_t n);
extern uint64_t pmm64_alloc_page(void);
extern void pmm64_free_page(uint64_t addr);

/* 内核页表（PML4） */
static pte_t *kernel_pml4 = 0;

/* 获取当前PML4地址 */
uint64_t vmm64_get_pml4(void)
{
    return (uint64_t)kernel_pml4;
}

/* 函数原型 */
int vmm64_map_page(uint64_t pml4_addr, uint64_t virt, uint64_t phys, uint64_t flags);

/* 初始化虚拟内存管理 */
void vmm64_init(uint64_t pml4_addr)
{
    vga_puts("[VMM64] Init start\n");
    kernel_pml4 = (pte_t *)pml4_addr;
    vga_puts("[VMM64] PML4 ptr set\n");

    /* 清零PML4（直接用地址，不用指针变量） */
    volatile uint64_t *pml4 = (volatile uint64_t *)pml4_addr;
    for (int i = 0; i < 512; i++) {
        pml4[i] = 0;
    }
    vga_puts("[VMM64] PML4 cleared\n");

    /* 身份映射前256MB（2MB大页，用户可访问，暂时不隔离） */
    vga_puts("[VMM64] Mapping 256MB...\n");
    for (uint64_t addr = 0; addr < 0x10000000ULL; addr += 0x200000) {
        if (vmm64_map_page(pml4_addr, addr, addr,
                       PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER | PAGE_HUGE) != 0) {
            vga_puts("[VMM64] Map failed\n");
            break;
        }
    }
    vga_puts("[VMM64] Mapping done\n");

    vga_puts("[VMM64] Init complete\n");
}

/* 获取或创建页表 */
static pte_t *vmm64_get_or_create_table(pte_t *parent, int index, uint64_t flags)
{
    if (!(parent[index] & PAGE_PRESENT)) {
        uint64_t new_table = pmm64_alloc_page();
        if (!new_table) {
            vga_puts("[VMM64] ERROR: pmm64_alloc_page failed\n");
            return 0;
        }
        /* 清零新页表（用简单循环） */
        pte_t *table = (pte_t *)new_table;
        for (int i = 0; i < 512; i++) {
            table[i] = 0;
        }
        parent[index] = new_table | flags;
    }
    return (pte_t *)(parent[index] & PAGE_MASK);
}

/* 映射一个页面 */
int vmm64_map_page(uint64_t pml4_addr, uint64_t virt, uint64_t phys, uint64_t flags)
{
    pte_t *pml4 = (pte_t *)pml4_addr;

    /* PML4 -> PDPT */
    pte_t *pdpt = vmm64_get_or_create_table(pml4, PML4_INDEX(virt),
                                             PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    if (!pdpt) return -1;

    /* PDPT -> PD */
    pte_t *pd = vmm64_get_or_create_table(pdpt, PDPT_INDEX(virt),
                                           PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    if (!pd) return -1;

    /* 检查是否是大页 */
    if (flags & PAGE_HUGE) {
        pd[PD_INDEX(virt)] = phys | flags;
        __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
        return 0;
    }

    /* PD -> PT */
    pte_t *pt = vmm64_get_or_create_table(pd, PD_INDEX(virt),
                                           PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    if (!pt) return -1;

    /* PT -> 物理页 */
    pt[PT_INDEX(virt)] = phys | flags;

    /* 刷新TLB */
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");

    return 0;
}

/* 取消映射一个页面 */
void vmm64_unmap_page(uint64_t pml4_addr, uint64_t virt)
{
    pte_t *pml4 = (pte_t *)pml4_addr;

    if (!(pml4[PML4_INDEX(virt)] & PAGE_PRESENT)) return;
    pte_t *pdpt = (pte_t *)(pml4[PML4_INDEX(virt)] & PAGE_MASK);

    if (!(pdpt[PDPT_INDEX(virt)] & PAGE_PRESENT)) return;
    pte_t *pd = (pte_t *)(pdpt[PDPT_INDEX(virt)] & PAGE_MASK);

    if (!(pd[PD_INDEX(virt)] & PAGE_PRESENT)) return;

    /* 检查是否是大页 */
    if (pd[PD_INDEX(virt)] & PAGE_HUGE) {
        pd[PD_INDEX(virt)] = 0;
        return;
    }

    pte_t *pt = (pte_t *)(pd[PD_INDEX(virt)] & PAGE_MASK);
    pt[PT_INDEX(virt)] = 0;

    /* 刷新TLB */
    __asm__ volatile("invlpg (%0)" : : "r"(virt));
}

/* 虚拟地址转物理地址 */
uint64_t vmm64_virt_to_phys(uint64_t pml4_addr, uint64_t virt)
{
    pte_t *pml4 = (pte_t *)pml4_addr;

    if (!(pml4[PML4_INDEX(virt)] & PAGE_PRESENT)) return 0;
    pte_t *pdpt = (pte_t *)(pml4[PML4_INDEX(virt)] & PAGE_MASK);

    if (!(pdpt[PDPT_INDEX(virt)] & PAGE_PRESENT)) return 0;
    pte_t *pd = (pte_t *)(pdpt[PDPT_INDEX(virt)] & PAGE_MASK);

    if (!(pd[PD_INDEX(virt)] & PAGE_PRESENT)) return 0;

    /* 大页 */
    if (pd[PD_INDEX(virt)] & PAGE_HUGE) {
        return (pd[PD_INDEX(virt)] & PAGE_MASK & ~0x1FFFFF) | (virt & 0x1FFFFF);
    }

    pte_t *pt = (pte_t *)(pd[PD_INDEX(virt)] & PAGE_MASK);
    if (!(pt[PT_INDEX(virt)] & PAGE_PRESENT)) return 0;

    return (pt[PT_INDEX(virt)] & PAGE_MASK) | (virt & 0xFFF);
}

/* 加载页表 */
void vmm64_load_cr3(uint64_t pml4_addr)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_addr));
    vga_puts("[VMM64] CR3 loaded: ");
    vga_put_hex(pml4_addr);
    vga_puts("\n");
}

/* 获取内核页表 */
uint64_t vmm64_get_kernel_pml4(void)
{
    return (uint64_t)kernel_pml4;
}

/* 页错误处理 */
void vmm64_page_fault_handler(uint64_t error_code, uint64_t cr2)
{
    vga_puts("\n[PAGE FAULT] CR2=");
    vga_put_hex(cr2);
    vga_puts(" Error=");
    vga_put_hex(error_code);
    vga_puts("\n");

    /* 显示错误原因 */
    if (error_code & 0x1) vga_puts("  Protection violation\n");
    else vga_puts("  Page not present\n");

    if (error_code & 0x2) vga_puts("  Write operation\n");
    else vga_puts("  Read operation\n");

    if (error_code & 0x4) vga_puts("  User mode\n");
    else vga_puts("  Kernel mode\n");

    /* 暂时停机 */
    while (1) { __asm__ volatile("hlt"); }
}
