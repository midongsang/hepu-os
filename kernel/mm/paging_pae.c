/* ============================================================================
 * 赫普OS - PAE分页管理 (V6.79)
 * 36位物理地址寻址，三级页表 (PDPT-PD-PT)
 *
 * 与非PAE分页的区别:
 * - PDPT (4项64位) 替代 页目录根
 * - PD (512项64位) 替代 页目录(1024项32位)
 * - PT (512项64位) 替代 页表(1024项32位)
 * - CR3指向PDPT而非PD
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "paging.h"
#include "pae.h"
#include "kheap.h"
#include "vga.h"
#include "serial.h"

/* PAE页表项标志 */
#define PAE_PRESENT  0x1
#define PAE_WRITE    0x2
#define PAE_USER     0x4
#define PAE_PWT      0x8
#define PAE_PCD      0x10
#define PAE_ACCESSED 0x20
#define PAE_DIRTY    0x40
#define PAE_PS       0x80   /* PD项: 2MB大页 */
#define PAE_GLOBAL   0x100
#define PAE_NX       (1ULL << 63)

static pae_pdpt_t *kernel_pdpt = NULL;
static pae_pdpt_t *current_pdpt = NULL;
static int pae_paging_enabled = 0;

/* 前向声明 */
int paging_pae_map(void *pdpt_v, uint32_t virt, uint64_t phys, uint32_t flags);

/* 获取虚拟地址的各索引 */
static inline uint32_t pae_pdpt_idx(uint32_t virt) { return (virt >> 30) & 0x3; }
static inline uint32_t pae_pd_idx(uint32_t virt)   { return (virt >> 21) & 0x1FF; }
static inline uint32_t pae_pt_idx(uint32_t virt)   { return (virt >> 12) & 0x1FF; }

/* 初始化PAE分页 */
void paging_pae_init(uint32_t mem_size_kb)
{
    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[PAE-PAGING] initializing...\n");

    /* 分配PDPT (必须32字节对齐，PAE硬件要求) */
    /* 分配63字节，然后对齐到32字节边界 */
    void *raw = kmalloc(32 + 31);
    if (!raw) {
        serial_puts(0x3F8, "[PAE-PAGING] failed to alloc PDPT\n");
        return;
    }
    kernel_pdpt = (pae_pdpt_t *)(((uint32_t)raw + 31) & ~31);
    memset(kernel_pdpt, 0, 32);
    serial_puts(0x3F8, "[PAE-PAGING] PDPT allocated (32-byte aligned)\n");

    /* 映射前64MB物理内存 (内核区域，BSS约58MB) */
    serial_puts(0x3F8, "[PAE-PAGING] mapping kernel memory (64MB)...\n");
    for (uint32_t phys = 0; phys < 64 * 1024 * 1024; phys += 4096) {
        paging_pae_map(kernel_pdpt, phys, phys, PAE_PRESENT | PAE_WRITE);
    }
    serial_puts(0x3F8, "[PAE-PAGING] kernel memory mapped\n");

    /* 映射帧缓冲区 0x80000000 (映射4MB) */
    serial_puts(0x3F8, "[PAE-PAGING] mapping framebuffer...\n");
    for (uint32_t phys = 0x80000000; phys < 0x80000000 + 4 * 1024 * 1024; phys += 4096) {
        paging_pae_map(kernel_pdpt, phys, phys, PAE_PRESENT | PAE_WRITE | PAE_PCD);
    }
    serial_puts(0x3F8, "[PAE-PAGING] framebuffer mapped\n");

    current_pdpt = kernel_pdpt;
    pae_paging_enabled = 1;
    serial_puts(0x3F8, "[PAE-PAGING] init done\n");
}

/* 启用PAE分页 */
void paging_pae_enable(void)
{
    if (!pae_paging_enabled || !kernel_pdpt) return;

    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[PAE-PAGING] enabling PAE...\n");

    /* 设置CR4.PAE */
    uint32_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 5);  /* PAE bit */
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));

    /* 加载PDPT到CR3 */
    uint32_t pdpt_phys = (uint32_t)kernel_pdpt;
    __asm__ volatile("mov %0, %%cr3" :: "r"(pdpt_phys));

    /* 启用分页 (CR0.PG) */
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;  /* PG bit */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

    serial_puts(0x3F8, "[PAE-PAGING] PAE paging enabled!\n");
}

/* 映射页面 */
int paging_pae_map(void *pdpt_v, uint32_t virt, uint64_t phys, uint32_t flags)
{
    pae_pdpt_t *pdpt = (pae_pdpt_t *)pdpt_v;
    if (!pdpt) return -1;

    uint32_t pidx = pae_pdpt_idx(virt);
    uint32_t didx = pae_pd_idx(virt);
    uint32_t ptidx = pae_pt_idx(virt);

    /* 获取或创建PD（必须4KB对齐） */
    pae_pd_t *pd;
    if (!(pdpt->e[pidx] & PAE_PRESENT)) {
        extern void *kmalloc_aligned(size_t, size_t);
        pd = (pae_pd_t *)kmalloc_aligned(4096, 4096);
        if (!pd) return -2;
        memset(pd, 0, 4096);
        pdpt->e[pidx] = ((uint64_t)(uint32_t)pd & 0xFFFFF000ULL) | PAE_PRESENT | PAE_WRITE;
    } else {
        pd = (pae_pd_t *)(uint32_t)(pdpt->e[pidx] & 0xFFFFF000ULL);
    }

    /* 获取或创建PT（必须4KB对齐） */
    pae_pt_t *pt;
    if (!(pd->e[didx] & PAE_PRESENT) || (pd->e[didx] & PAE_PS)) {
        extern void *kmalloc_aligned(size_t, size_t);
        pt = (pae_pt_t *)kmalloc_aligned(4096, 4096);
        if (!pt) return -3;
        memset(pt, 0, 4096);
        uint64_t entry = ((uint64_t)(uint32_t)pt & 0xFFFFF000ULL) | PAE_PRESENT;
        if (flags & PAE_WRITE) entry |= PAE_WRITE;
        if (flags & PAE_USER) entry |= PAE_USER;
        pd->e[didx] = entry;
    } else {
        pt = (pae_pt_t *)(uint32_t)(pd->e[didx] & 0xFFFFF000ULL);
    }

    /* 设置PT项 */
    uint64_t pt_entry = (phys & 0xFFFFF000ULL) | PAE_PRESENT;
    if (flags & PAE_WRITE) pt_entry |= PAE_WRITE;
    if (flags & PAE_USER) pt_entry |= PAE_USER;
    pt->e[ptidx] = pt_entry;

    return 0;
}

/* 取消映射 */
void paging_pae_unmap(void *pdpt_v, uint32_t virt)
{
    pae_pdpt_t *pdpt = (pae_pdpt_t *)pdpt_v;
    if (!pdpt) return;

    uint32_t pidx = pae_pdpt_idx(virt);
    uint32_t didx = pae_pd_idx(virt);
    uint32_t ptidx = pae_pt_idx(virt);

    if (!(pdpt->e[pidx] & PAE_PRESENT)) return;
    pae_pd_t *pd = (pae_pd_t *)(uint32_t)(pdpt->e[pidx] & 0xFFFFF000ULL);
    if (!(pd->e[didx] & PAE_PRESENT) || (pd->e[didx] & PAE_PS)) return;
    pae_pt_t *pt = (pae_pt_t *)(uint32_t)(pd->e[didx] & 0xFFFFF000ULL);
    pt->e[ptidx] = 0;

    /* 刷新TLB */
    __asm__ volatile("invlpg (%0)" :: "r"(virt));
}

/* 获取物理地址 */
uint64_t paging_pae_get_phys(void *pdpt_v, uint32_t virt)
{
    pae_pdpt_t *pdpt = (pae_pdpt_t *)pdpt_v;
    if (!pdpt) return 0;

    uint32_t pidx = pae_pdpt_idx(virt);
    uint32_t didx = pae_pd_idx(virt);
    uint32_t ptidx = pae_pt_idx(virt);

    if (!(pdpt->e[pidx] & PAE_PRESENT)) return 0;
    pae_pd_t *pd = (pae_pd_t *)(uint32_t)(pdpt->e[pidx] & 0xFFFFF000ULL);
    if (!(pd->e[didx] & PAE_PRESENT)) return 0;
    if (pd->e[didx] & PAE_PS) {
        /* 2MB大页 */
        return (pd->e[didx] & 0xFFE00000ULL) | (virt & 0x1FFFFF);
    }
    pae_pt_t *pt = (pae_pt_t *)(uint32_t)(pd->e[didx] & 0xFFFFF000ULL);
    if (!(pt->e[ptidx] & PAE_PRESENT)) return 0;
    return (pt->e[ptidx] & 0xFFFFF000ULL) | (virt & 0xFFF);
}

/* 切换页表 */
void paging_pae_switch(void *pdpt_v)
{
    pae_pdpt_t *pdpt = (pae_pdpt_t *)pdpt_v;
    if (!pdpt || !pae_paging_enabled) return;
    current_pdpt = pdpt;
    __asm__ volatile("mov %0, %%cr3" :: "r"((uint32_t)pdpt));
}

/* 创建新的地址空间 */
void *paging_pae_create_dir(void)
{
    /* PDPT必须32字节对齐 */
    void *raw = kmalloc(32 + 31);
    if (!raw) return NULL;
    pae_pdpt_t *pdpt = (pae_pdpt_t *)(((uint32_t)raw + 31) & ~31);
    memset(pdpt, 0, 32);

    /* 复制内核空间映射 (0xC0000000以上) */
    if (kernel_pdpt) {
        for (int i = 0; i < 4; i++) {
            if (kernel_pdpt->e[i] & PAE_PRESENT) {
                pdpt->e[i] = kernel_pdpt->e[i];
            }
        }
    }
    return pdpt;
}

void *paging_pae_get_kernel_dir(void) { return kernel_pdpt; }
void *paging_pae_get_current_dir(void) { return current_pdpt; }
int paging_pae_is_enabled(void) { return pae_paging_enabled; }

/* ============================================================================
 * V6.80 PAE分支: 页错误处理
 * ========================================================================== */
static uint32_t pae_pf_total = 0;
static uint32_t pae_pf_kernel = 0;
static uint32_t pae_pf_user = 0;
static uint32_t pae_pf_protection = 0;

/* PAE页错误处理（简化版，只处理内核空间按需分配） */
void paging_pae_fault_handler(uint32_t fault_addr, uint32_t error_code)
{
    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);
    int present = error_code & 0x1;
    int write = error_code & 0x2;
    int user = error_code & 0x4;
    int reserved = error_code & 0x8;

    pae_pf_total++;
    if (user) pae_pf_user++; else pae_pf_kernel++;

    /* 详细诊断输出 */
    serial_puts(0x3F8, "[PAE-PF] fault at 0x");
    serial_puthex(0x3F8, fault_addr);
    serial_puts(0x3F8, " err=0x");
    serial_puthex(0x3F8, error_code);
    serial_puts(0x3F8, " (");
    if (present) serial_puts(0x3F8, "prot"); else serial_puts(0x3F8, "not-present");
    if (write) serial_puts(0x3F8, " write"); else serial_puts(0x3F8, " read");
    if (user) serial_puts(0x3F8, " user"); else serial_puts(0x3F8, " kernel");
    if (reserved) serial_puts(0x3F8, " reserved");
    serial_puts(0x3F8, ")\n");

    if (reserved) {
        serial_puts(0x3F8, "[PAE-PF] reserved violation, fatal\n");
        goto fatal;
    }

    /* 内核空间：尝试按需分配（所有内核地址） */
    if (!present && !user) {
        extern void *kmalloc(unsigned int size);
        void *page = kmalloc(4096);
        if (page) {
            memset(page, 0, 4096);
            uint64_t phys = (uint64_t)(uint32_t)page;
            int ret = paging_pae_map(current_pdpt, fault_addr & 0xFFFFF000, phys,
                          PAE_PRESENT | PAE_WRITE);
            if (ret == 0) {
                __asm__ volatile("invlpg (%0)" :: "r"(fault_addr));
                serial_puts(0x3F8, "[PAE-PF] demand paging ok\n");
                return;
            }
            serial_puts(0x3F8, "[PAE-PF] map failed\n");
        } else {
            serial_puts(0x3F8, "[PAE-PF] out of memory\n");
        }
        goto fatal;
    }

    if (write) {
        pae_pf_protection++;
        serial_puts(0x3F8, " write protection\n");
    }

fatal:
    vga_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
    vga_printf("\n[PAE-PAGING] fatal page fault\n");
    vga_printf(" fault addr: 0x%x\n", fault_addr);
    vga_printf(" type: %s %s %s%s\n",
               present ? "protection" : "not present",
               write ? " write" : " read",
               user ? " user" : " kernel",
               reserved ? " reserved" : "");
    vga_printf(" stats: total=%d kernel=%d user=%d prot=%d\n",
               pae_pf_total, pae_pf_kernel, pae_pf_user, pae_pf_protection);
    vga_puts(" system halted.\n");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* PAE页错误统计 */
void paging_pae_fault_stats(uint32_t *total, uint32_t *kernel,
                             uint32_t *user, uint32_t *protection)
{
    if (total) *total = pae_pf_total;
    if (kernel) *kernel = pae_pf_kernel;
    if (user) *user = pae_pf_user;
    if (protection) *protection = pae_pf_protection;
}
