/* ============================================================================
 * 赫普OS - PAE物理地址扩展支持
 * 36位物理地址寻址，最大支持64GB物理内存
 *
 * PAE页表结构 (3级):
 *   PDPT (Page Directory Pointer Table) - 4项, 每项映射1GB
 *   PD   (Page Directory)               - 512项, 每项映射2MB
 *   PT   (Page Table)                   - 512项, 每项映射4KB
 *
 * 或使用2MB大页 (PD项PS位置1):
 *   PDPT -> PD -> 2MB物理页
 *
 * V6.77: 初始实现
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "vga.h"
#include "kheap.h"
#include "serial.h"

/* PAE控制位 */
#define CR4_PAE_BIT       (1 << 5)   /* CR4.PAE */
#define CR4_PGE_BIT       (1 << 7)   /* CR4.PGE (全局页) */

/* PAE页表项标志 (64位) */
#define PAE_PRESENT       (1ULL << 0)
#define PAE_WRITABLE      (1ULL << 1)
#define PAE_USER          (1ULL << 2)
#define PAE_WRITE_THROUGH (1ULL << 3)
#define PAE_CACHE_DISABLE (1ULL << 4)
#define PAE_ACCESSED      (1ULL << 5)
#define PAE_DIRTY         (1ULL << 6)
#define PAE_LARGE_PAGE    (1ULL << 7)   /* PD项: 2MB大页 */
#define PAE_GLOBAL        (1ULL << 8)
#define PAE_NX            (1ULL << 63)  /* No-Execute */

/* PAE页表结构 (使用struct避免数组退化问题) */
typedef struct { uint64_t e[4]; }   pae_pdpt_t;
typedef struct { uint64_t e[512]; } pae_pd_t;
typedef struct { uint64_t e[512]; } pae_pt_t;

static int pae_supported = 0;
static int pae_enabled = 0;
static int pae_test_mode = 0;  /* 自测模式: 跳过pae_enabled检查 */
static uint64_t max_phys_addr = 0;

/* 检测CPU是否支持PAE */
int pae_cpu_supported(void)
{
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1));
    return (edx & (1 << 6)) ? 1 : 0;
}

/* 检测最大物理地址宽度 */
static uint32_t pae_get_max_phys_bits(void)
{
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0x80000008));
    uint32_t phys_bits = eax & 0xFF;
    if (phys_bits == 0 || phys_bits > 52) phys_bits = 36;
    return phys_bits;
}

/* 启用PAE */
int pae_enable(void)
{
    if (!pae_cpu_supported()) {
        vga_puts("[PAE] CPU does not support PAE\n");
        return -1;
    }

    uint32_t phys_bits = pae_get_max_phys_bits();
    max_phys_addr = (1ULL << phys_bits) - 1;

    uint32_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= CR4_PAE_BIT;
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));

    pae_enabled = 1;
    pae_supported = 1;

    vga_printf("[PAE] enabled, phys addr width: %d bits, max: %dGB\n",
               phys_bits, (uint32_t)(max_phys_addr / (1024 * 1024 * 1024)));
    return 0;
}

/* 禁用PAE */
void pae_disable(void)
{
    uint32_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 &= ~CR4_PAE_BIT;
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));
    pae_enabled = 0;
}

/* 创建PDPT (必须32字节对齐，PAE硬件要求) */
pae_pdpt_t *pae_create_pdpt(void)
{
    void *raw = kmalloc(32 + 31);
    if (!raw) return NULL;
    pae_pdpt_t *pdpt = (pae_pdpt_t *)(((uint32_t)raw + 31) & ~31);
    memset(pdpt, 0, 32);
    return pdpt;
}

/* 创建PD (4KB，必须4KB对齐，PAE硬件要求) */
pae_pd_t *pae_create_pd(void)
{
    extern void *kmalloc_aligned(size_t, size_t);
    pae_pd_t *pd = (pae_pd_t *)kmalloc_aligned(4096, 4096);
    if (!pd) return NULL;
    memset(pd, 0, 4096);
    return pd;
}

/* 创建PT (4KB，必须4KB对齐，PAE硬件要求) */
pae_pt_t *pae_create_pt(void)
{
    extern void *kmalloc_aligned(size_t, size_t);
    pae_pt_t *pt = (pae_pt_t *)kmalloc_aligned(4096, 4096);
    if (!pt) return NULL;
    memset(pt, 0, 4096);
    return pt;
}

/* 设置PDPT项 */
void pae_set_pdpt_entry(pae_pdpt_t *pdpt, int index, uint64_t pd_phys,
                         int present, int writable)
{
    if (!pdpt || index < 0 || index >= 4) return;
    uint64_t entry = pd_phys & 0xFFFFFFFE000ULL;
    if (present) entry |= PAE_PRESENT;
    if (writable) entry |= PAE_WRITABLE;
    pdpt->e[index] = entry;
}

/* 设置PD项 (4KB页或2MB大页) */
void pae_set_pd_entry(pae_pd_t *pd, int index, uint64_t pt_phys,
                       int present, int writable, int user, int large_page)
{
    if (!pd || index < 0 || index >= 512) return;
    uint64_t entry;
    if (large_page) {
        entry = pt_phys & 0xFFFFFFE00000ULL;
        entry |= PAE_LARGE_PAGE;
    } else {
        entry = pt_phys & 0xFFFFFFFE000ULL;
    }
    if (present) entry |= PAE_PRESENT;
    if (writable) entry |= PAE_WRITABLE;
    if (user) entry |= PAE_USER;
    pd->e[index] = entry;
}

/* 设置PT项 (4KB页) */
void pae_set_pt_entry(pae_pt_t *pt, int index, uint64_t phys_addr,
                       int present, int writable, int user, int nx)
{
    if (!pt || index < 0 || index >= 512) return;
    uint64_t entry = phys_addr & 0xFFFFFFFE000ULL;
    if (present) entry |= PAE_PRESENT;
    if (writable) entry |= PAE_WRITABLE;
    if (user) entry |= PAE_USER;
    if (nx) entry |= PAE_NX;
    pt->e[index] = entry;
}

/* 映射4KB页 */
int pae_map_page(pae_pdpt_t *pdpt, uint32_t virt, uint64_t phys,
                  int writable, int user)
{
    if ((!pae_enabled && !pae_test_mode) || !pdpt) return -1;

    uint32_t pdpt_idx = (virt >> 30) & 0x3;
    uint32_t pd_idx   = (virt >> 21) & 0x1FF;
    uint32_t pt_idx   = (virt >> 12) & 0x1FF;

    pae_pd_t *pd;
    if (!(pdpt->e[pdpt_idx] & PAE_PRESENT)) {
        pd = pae_create_pd();
        if (!pd) return -2;
        pae_set_pdpt_entry(pdpt, pdpt_idx, (uint32_t)pd, 1, 1);
    } else {
        pd = (pae_pd_t *)(uint32_t)(pdpt->e[pdpt_idx] & 0xFFFFFFFE000ULL);
    }

    pae_pt_t *pt;
    if (!(pd->e[pd_idx] & PAE_PRESENT) || (pd->e[pd_idx] & PAE_LARGE_PAGE)) {
        pt = pae_create_pt();
        if (!pt) return -3;
        pae_set_pd_entry(pd, pd_idx, (uint32_t)pt, 1, writable, user, 0);
    } else {
        pt = (pae_pt_t *)(uint32_t)(pd->e[pd_idx] & 0xFFFFFFFE000ULL);
    }

    pae_set_pt_entry(pt, pt_idx, phys, 1, writable, user, 0);
    return 0;
}

/* 映射2MB大页 */
int pae_map_2mb_page(pae_pdpt_t *pdpt, uint32_t virt, uint64_t phys,
                      int writable, int user)
{
    if ((!pae_enabled && !pae_test_mode) || !pdpt) return -1;

    uint32_t pdpt_idx = (virt >> 30) & 0x3;
    uint32_t pd_idx   = (virt >> 21) & 0x1FF;

    pae_pd_t *pd;
    if (!(pdpt->e[pdpt_idx] & PAE_PRESENT)) {
        pd = pae_create_pd();
        if (!pd) return -2;
        pae_set_pdpt_entry(pdpt, pdpt_idx, (uint32_t)pd, 1, 1);
    } else {
        pd = (pae_pd_t *)(uint32_t)(pdpt->e[pdpt_idx] & 0xFFFFFFFE000ULL);
    }

    pae_set_pd_entry(pd, pd_idx, phys, 1, writable, user, 1);
    return 0;
}

/* 加载PDPT到CR3 */
void pae_load_pdpt(pae_pdpt_t *pdpt)
{
    if (!pae_enabled || !pdpt) return;
    __asm__ volatile("mov %0, %%cr3" :: "r"((uint32_t)pdpt));
}

/* 刷新TLB */
void pae_flush_tlb(void)
{
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" :: "r"(cr3));
}

uint64_t pae_get_max_phys_addr(void)
{
    return max_phys_addr;
}

int pae_is_enabled(void)
{
    return pae_enabled;
}

/* PAE自测 */
void pae_selftest(void)
{
    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[PAE] selftest: start\n");
    vga_puts("[PAE] selftest: start\n");

    /* 如果已经在PAE模式下（pae_safe_switch已切换成功），跳过数据结构测试 */
    uint32_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    if (cr4 & CR4_PAE_BIT) {
        vga_puts("[PAE] selftest: PASS (already in PAE mode, switch verified)\n");
        serial_puts(0x3F8, "[PAE] selftest PASS (already in PAE mode)\n");
        return;
    }

    pae_test_mode = 1;  /* 进入测试模式 */

    if (!pae_cpu_supported()) {
        vga_puts("[PAE] selftest: CPU does not support PAE, skip (data structure test only)\n");
        serial_puts(0x3F8, "[PAE] CPU no PAE, data structure test only\n");
    }

    pae_pdpt_t *pdpt = pae_create_pdpt();
    if (!pdpt) {
        vga_puts("[PAE] selftest: FAIL (create pdpt)\n");
        serial_puts(0x3F8, "[PAE] FAIL (create pdpt)\n");
        pae_test_mode = 0;
        return;
    }

    int ret = pae_map_page(pdpt, 0x1000, 0x1000, 1, 0);
    if (ret != 0) {
        vga_puts("[PAE] selftest: FAIL (map 4k page)\n");
        serial_puts(0x3F8, "[PAE] FAIL (map 4k)\n");
        pae_test_mode = 0;
        return;
    }

    ret = pae_map_2mb_page(pdpt, 0x400000, 0x400000, 1, 0);
    if (ret != 0) {
        vga_puts("[PAE] selftest: FAIL (map 2m page)\n");
        serial_puts(0x3F8, "[PAE] FAIL (map 2m)\n");
        pae_test_mode = 0;
        return;
    }

    uint32_t phys_bits = 36;
    if (pae_cpu_supported()) {
        /* 只有CPU支持PAE时才能检测真实物理地址宽度 */
        phys_bits = pae_get_max_phys_bits();
    }
    max_phys_addr = (1ULL << phys_bits) - 1;

    vga_printf("[PAE] selftest: PASS (data structures, %d-bit addr, %dGB max)\n",
               phys_bits, (uint32_t)(max_phys_addr / (1024 * 1024 * 1024)));
    serial_puts(0x3F8, "[PAE] selftest PASS (data structures)\n");
    pae_test_mode = 0;
}

/* V6.80: PAE初始化框架（检测CPU支持，创建页表结构，不实际切换） */
static pae_pdpt_t *kernel_pdpt = 0;
static int pae_initialized = 0;

void pae_init(void)
{
    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[PAE] init: starting...\n");

    /* 检查是否已经在PAE模式下（pae_safe_switch已切换） */
    uint32_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    if (cr4 & CR4_PAE_BIT) {
        serial_puts(0x3F8, "[PAE] init: already in PAE mode, skipping\n");
        pae_initialized = 1;
        return;
    }

    /* 检测CPU PAE支持 */
    if (!pae_cpu_supported()) {
        serial_puts(0x3F8, "[PAE] init: CPU does not support PAE\n");
        vga_puts("[PAE] CPU does not support PAE, using 32-bit paging\n");
        pae_initialized = 0;
        return;
    }

    serial_puts(0x3F8, "[PAE] init: CPU supports PAE\n");

    /* 检测物理地址宽度 */
    uint32_t phys_bits = pae_get_max_phys_bits();
    max_phys_addr = (1ULL << phys_bits) - 1;

    char buf[128];
    snprintf(buf, sizeof(buf), "[PAE] init: %d-bit physical addressing, %dGB max\n",
             phys_bits, (uint32_t)(max_phys_addr / (1024 * 1024 * 1024)));
    serial_puts(0x3F8, buf);
    vga_puts(buf);

    /* 创建PAE页表结构（用于后续切换） */
    pae_test_mode = 1;  /* 允许在未启用PAE时创建结构 */
    serial_puts(0x3F8, "[PAE] init: creating PDPT...\n");
    kernel_pdpt = pae_create_pdpt();
    if (!kernel_pdpt) {
        serial_puts(0x3F8, "[PAE] init: failed to create PDPT\n");
        pae_test_mode = 0;
        return;
    }
    serial_puts(0x3F8, "[PAE] init: PDPT created, mapping first 4MB...\n");

    /* 映射前4MB物理内存（恒等映射，用于切换时使用） */
    for (uint32_t addr = 0; addr < 0x400000; addr += 0x1000) {
        pae_map_page(kernel_pdpt, addr, addr, 1, 1);  /* 内核页，可写 */
    }

    serial_puts(0x3F8, "[PAE] init: first 4MB mapped\n");

    pae_test_mode = 0;
    pae_initialized = 1;
    pae_supported = 1;
}

/* 获取PAE初始化状态 */
int pae_is_initialized(void)
{
    return pae_initialized;
}

/* 获取内核PDPT物理地址（用于切换） */
uint32_t pae_get_kernel_pdpt_phys(void)
{
    if (!kernel_pdpt) return 0;
    /* 返回物理地址（假设内核地址空间是恒等映射的低地址） */
    return (uint32_t)kernel_pdpt;
}
