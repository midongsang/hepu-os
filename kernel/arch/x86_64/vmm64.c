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
#define PAGE_COW        (1ULL << 9)   /* 软件位：写时复制共享页（CPL 不可见） */
#define PAGE_NX         (1ULL << 63)
#define PAGE_PHYS_MASK  0x000FFFFFFFFFF000ULL

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

/* SB-22: 跨核 TLB flush。页表改动只影响本核 TLB，别的核仍可能命中旧映射 ——
 * 共享内存里一个核写完 unmapped 页，另一个核还能读到，是最隐蔽的一类数据错误。
 * 注意：本函数会自旋等应答，不能在中断上下文里调用（handler 自己收不到应答）。
 * 主机测试环境无多核，整体退化为空实现。 */
#ifndef HEPU_HOST_TEST
#define VMM_FLUSH_OTHERS(v) vmm64_flush_tlb_others((uint64_t)(v))
#else
#define VMM_FLUSH_OTHERS(v) ((void)(v))
#endif

/* TLB 刷新（主机端测试里没有 invlpg 指令，用 -DHEPU_HOST_TEST 空实现；内核里行为与原来一致） */
#ifdef HEPU_HOST_TEST
#define VMM_INVLPG(v) ((void)(v))
#else
#define VMM_INVLPG(v) __asm__ volatile("invlpg (%0)" : : "r"(v) : "memory")

#ifndef HEPU_HOST_TEST
#include "smp.h"

/* SB-22: 已发出的跨核 shootdown 次数（诊断用，桌面启动后打印一次） */
static volatile uint64_t g_tlb_shootdowns;
uint64_t vmm64_tlb_shootdown_count(void) { return g_tlb_shootdowns; }

/* 通知其他在线核刷新该页（arg=0 表示整表刷新） */
void vmm64_flush_tlb_others(uint64_t virt)
{
    /* 单核系统不存在旧映射残留问题，直接返回，避免无意义的 IPI 往返 */
    if (smp_get_cpu_count() <= 1) return;

    uint64_t snap[MAX_CPUS];
    smp_ipi_snapshot(snap);
    int sent = smp_ipi_send_others(IPI_MSG_TLB_FLUSH, virt);
    if (sent <= 0) return;
    __sync_fetch_and_add(&g_tlb_shootdowns, (uint64_t)sent);
    /* 超时上限刻意给得短：shootdown 是同步路径，等太久会把调用方拖死。
     * 超时也不当作致命错误 —— 位图里在线的核可能刚好已经掉线。 */
    smp_ipi_wait_ack(snap, sent, 20000000ULL);
}
#endif
#endif

/* 用户窗口：PDPT[1] = 0x40000000~0x80000000（1GB）。低身份映射不得覆盖它，
 * 否则隔离进程的私有用户 PD 会与内核恒等映射冲突（uaspaced_create 检测到即退回旧模式）。
 * 物理内存 >= 0x40000000 的部分改映射到高半区直接映射区 KERNEL_DIRECT_BASE+phys，
 * 这样内核仍可访问任意物理页，而低地址用户窗口保持空闲给用户进程。 */
#define USER_WINDOW_LOW   0x0000000040000000ULL
#define USER_WINDOW_HIGH  0x0000000080000000ULL
#define KERNEL_DIRECT_BASE 0xFFFF800000000000ULL

/* 恒等映射实际覆盖到的物理上限。phys_to_virt 的"原样返回"不能只按 1GB 阈值判断:
 * 物理内存只有 979MB 时, 979MB~1GB 是恒等映射与高直接映射之间的空洞,
 * 而固件表(如 OVMF 的 ACPI, 实测在 0x3FB7D000 ≈ 1019MB)恰好落在空洞里。 */
uint64_t g_low_identity_end = 0x1000000ULL;

/* 物理地址 -> 可解引用的内核虚拟地址(供固件表等"不确定落在哪"的地址使用) */
uint64_t vmm64_phys_to_ptr(uint64_t phys)
{
    if (phys < g_low_identity_end) return phys;
    return KERNEL_DIRECT_BASE + phys;
}

/* 物理地址 -> 内核可用虚拟地址：<1GB 用低身份映射（保持现状），>=1GB 用高半区直接映射 */
uint64_t vmm64_phys_to_virt(uint64_t phys)
{
#ifdef HEPU_HOST_TEST
    /* 主机端测试：页表页用主机内存模拟，「物理地址」就是主机指针，
     * 直接当指针用即可（走 KERNEL_DIRECT_BASE 会解引用到非法地址）。 */
    return phys;
#else
    if (phys < USER_WINDOW_LOW) return phys;
    return KERNEL_DIRECT_BASE + phys;
#endif
}

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

    /* 低身份映射：0 ~ min(total_mem, 1GB) 连续恒等映射。
     * 用户窗口[0x40000000,0x80000000)不在低地址映射，PDPT[1] 保持 present=0，
     * 隔离进程可独占该 1GB。>=1GB 的物理内存走下面的高半区直接映射。 */
    vga_puts("[VMM64] Mapping low identity...\n");
    extern uint64_t pmm64_get_memory_size_mb(void);
    uint64_t total_mem = pmm64_get_memory_size_mb() * 1024 * 1024;
    uint64_t low_end = total_mem < USER_WINDOW_LOW ? total_mem : USER_WINDOW_LOW;
    g_low_identity_end = low_end;
    for (uint64_t addr = 0; addr < low_end; addr += 0x200000) {
        if (vmm64_map_page(pml4_addr, addr, addr,
                       PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE) != 0) {
            vga_puts("[VMM64] Low map failed\n");
            break;
        }
    }
    /* 高半区直接映射：phys[1GB,4GB) -> KERNEL_DIRECT_BASE+phys（ring0，不带 USER 位）。
     * 覆盖 >=1GB 的可用 RAM（页表/缓冲区）以及固件表(ACPI RSDT/XSDT)和低 MMIO 区。
     * [0,1GB) 已由低身份映射覆盖，这里不重复。 */
    vga_puts("[VMM64] Mapping high direct map...\n");
    uint64_t high_end = 0x100000000ULL;  /* 4GB */
    /* 起点必须是 2MB 对齐的 0, 不能是 low_end:
     * (1) 从固定 1GB 起 -> total_mem 不足 1GB 时 [total_mem,1GB) 是空洞, 固件表落进去 #PF;
     * (2) 从 low_end 起 -> low_end 往往不是 2MB 对齐, 2MB 大页物理地址低 21 位非零
     *     会置保留位, 触发 RSVD(#PF error=0x9)。恒等映射区重复映射无副作用(VA 不同)。 */
    for (uint64_t addr = 0; addr < high_end; addr += 0x200000) {
        uint64_t va = KERNEL_DIRECT_BASE + addr;
        if (vmm64_map_page(pml4_addr, va, addr,
                       PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE | PAGE_GLOBAL) != 0) {
            vga_puts("[VMM64] High map failed\n");
            break;
        }
    }
    vga_puts("[VMM64] Mapping done\n");

    vga_puts("[VMM64] Init complete\n");
}

/* 获取或创建页表 */
static pte_t *vmm64_get_or_create_table(pte_t *parent, int index, uint64_t flags)
{
    /* SEC-19 修复②: 大页防护。
     * 大页 PDE 同样是 present，原来会顺着 `parent[index] & PAGE_MASK` 把
     * 那 2MB 数据的物理基址当成页表基址返回，调用方接着往里面写 PTE ——
     * 写坏一块物理内存，而大页映射本身还在（"以为映射成功，实际把别人的
     * 数据改了"）。V0.33 只在 map_page 这个调用点插了 split，函数本体一直
     * 没有防护；现在把它变成函数自保：见到大页直接拒绝。 */
    if ((parent[index] & PAGE_PRESENT) && (parent[index] & PAGE_HUGE)) {
        vga_puts("[VMM64] ERROR: huge page used as table, index=");
        vga_put_hex((uint64_t)index);
        vga_puts("\n");
        return 0;
    }
    if (!(parent[index] & PAGE_PRESENT)) {
        uint64_t new_table = pmm64_alloc_page();
        if (!new_table) {
            vga_puts("[VMM64] ERROR: pmm64_alloc_page failed\n");
            return 0;
        }
        /* 清零新页表（用简单循环；新表页可能在>=1GB，走高半区直接映射访问） */
        pte_t *table = (pte_t *)vmm64_phys_to_virt(new_table);
        for (int i = 0; i < 512; i++) {
            table[i] = 0;
        }
        parent[index] = new_table | flags;
    }
    return (pte_t *)vmm64_phys_to_virt(parent[index] & PAGE_MASK);
}

/* V0.33: 2MB 大页拆分
 *
 * 缺陷（真实存在，已导致启动后内核无法访问自己的物理内存）：
 *   vmm64_map_page 在「大页覆盖的 2MB 区间」里做 4KB 映射时，
 *   vmm64_get_or_create_table 会把大页 PDE 的物理基址当成页表基址，
 *   然后把 PTE 写进那 2MB 数据的头 4KB —— 大页映射本身还在，于是
 *   「内核以为映射了新页，实际写坏了一整块物理内存」。
 *   （历史）vmm64_unmap_page 早期遇到大页直接 pd[i]=0，会把内核的低身份映射
 *   （0..1GB，2MB 大页）整段拆掉 —— 这一段已在 V0.33 改为「先 split 再清单个
 *   PTE」，如下方 vmm64_unmap_page 所示；此处保留记录是为了说明为什么那里
 *   必须先 split：
 *   表现为「写用户栈没写进去」「读回来是别的内容」这类神出鬼没的现象。
 *
 * 修复：需要在大页区域做 4KB 映射时，先把该 2MB 大页拆成一张 4KB 页表
 * （512 个 PTE 全部指向原大页内的对应 4KB 物理页），再改单个 PTE。
 * 拆分会吃掉一张页表页，失败则返回 -1（宁可不映射，也不写坏内存）。
 */
static int vmm64_split_huge_pde(pte_t *pd, int index, uint64_t pde)
{
    uint64_t base = pde & PAGE_MASK;          /* 2MB 大页的物理基址 */
    uint64_t flags = pde & ~(PAGE_MASK | PAGE_HUGE);
    uint64_t new_pt_phys = pmm64_alloc_page();
    if (!new_pt_phys) return -1;
    pte_t *new_pt = (pte_t *)vmm64_phys_to_virt(new_pt_phys);
    for (int i = 0; i < 512; i++) {
        new_pt[i] = (base + (uint64_t)i * 4096) | flags | PAGE_PRESENT;
    }
    pd[index] = new_pt_phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    return 0;
}

/* 映射一个页面 */
int vmm64_map_page(uint64_t pml4_addr, uint64_t virt, uint64_t phys, uint64_t flags)
{
    pte_t *pml4 = (pte_t *)vmm64_phys_to_virt(pml4_addr);

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
        VMM_INVLPG(virt);
        VMM_FLUSH_OTHERS(virt);
        return 0;
    }

    /* PD -> PT
     * V0.33: 若 PD 表项是 2MB 大页，必须先拆成 4KB 页表，
     * 否则会把 PTE 写进大页数据区（写坏物理内存），见 vmm64_split_huge_pde。 */
    if (pd[PD_INDEX(virt)] & PAGE_HUGE) {
        if (vmm64_split_huge_pde(pd, PD_INDEX(virt), pd[PD_INDEX(virt)]) != 0) {
            return -1;
        }
    }
    pte_t *pt = vmm64_get_or_create_table(pd, PD_INDEX(virt),
                                           PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    if (!pt) return -1;

    /* PT -> 物理页 */
    pt[PT_INDEX(virt)] = phys | flags;

    /* 刷新TLB */
    VMM_INVLPG(virt);
    VMM_FLUSH_OTHERS(virt);

    return 0;
}

/* 取消映射一个页面 */
void vmm64_unmap_page(uint64_t pml4_addr, uint64_t virt)
{
    pte_t *pml4 = (pte_t *)vmm64_phys_to_virt(pml4_addr);

    if (!(pml4[PML4_INDEX(virt)] & PAGE_PRESENT)) return;
    pte_t *pdpt = (pte_t *)vmm64_phys_to_virt(pml4[PML4_INDEX(virt)] & PAGE_MASK);

    if (!(pdpt[PDPT_INDEX(virt)] & PAGE_PRESENT)) return;
    pte_t *pd = (pte_t *)vmm64_phys_to_virt(pdpt[PDPT_INDEX(virt)] & PAGE_MASK);

    if (!(pd[PD_INDEX(virt)] & PAGE_PRESENT)) return;

    /* 检查是否是大页
     * V0.33: 大页不能直接清零 —— 低身份映射（0..1GB）整段都是 2MB 大页，
     * 清掉它等于把内核对自己物理内存的映射拆了。这里改为拆分后只清单个 PTE。 */
    if (pd[PD_INDEX(virt)] & PAGE_HUGE) {
        if (vmm64_split_huge_pde(pd, PD_INDEX(virt), pd[PD_INDEX(virt)]) != 0) return;
    }

    pte_t *pt = (pte_t *)vmm64_phys_to_virt(pd[PD_INDEX(virt)] & PAGE_MASK);
    pt[PT_INDEX(virt)] = 0;

    /* 刷新TLB */
    VMM_INVLPG(virt);
    VMM_FLUSH_OTHERS(virt);
}

/* V0.20.12: 修改页权限
 * prot 位: PROT_READ=1, PROT_WRITE=2, PROT_EXEC=4
 * 返回 0 成功, -1 未映射
 */
int vmm64_protect_page(uint64_t pml4_addr, uint64_t virt, uint64_t prot)
{
    pte_t *pml4 = (pte_t *)vmm64_phys_to_virt(pml4_addr);

    if (!(pml4[PML4_INDEX(virt)] & PAGE_PRESENT)) return -1;
    pte_t *pdpt = (pte_t *)vmm64_phys_to_virt(pml4[PML4_INDEX(virt)] & PAGE_MASK);

    if (!(pdpt[PDPT_INDEX(virt)] & PAGE_PRESENT)) return -1;
    pte_t *pd = (pte_t *)vmm64_phys_to_virt(pdpt[PDPT_INDEX(virt)] & PAGE_MASK);

    if (!(pd[PD_INDEX(virt)] & PAGE_PRESENT)) return -1;

    /* 大页 */
    if (pd[PD_INDEX(virt)] & PAGE_HUGE) {
        uint64_t flags = pd[PD_INDEX(virt)];
        flags &= ~(PAGE_WRITABLE | PAGE_NX);
        flags |= PAGE_PRESENT | PAGE_USER;
        if (prot & 2) flags |= PAGE_WRITABLE;
        if (!(prot & 4)) flags |= PAGE_NX;   /* 默认不可执行 */
        pd[PD_INDEX(virt)] = flags;
        VMM_INVLPG(virt);
        VMM_FLUSH_OTHERS(virt);
        return 0;
    }

    pte_t *pt = (pte_t *)vmm64_phys_to_virt(pd[PD_INDEX(virt)] & PAGE_MASK);
    if (!(pt[PT_INDEX(virt)] & PAGE_PRESENT)) return -1;

    uint64_t flags = pt[PT_INDEX(virt)];
    flags &= ~(PAGE_WRITABLE | PAGE_NX);
    flags |= PAGE_PRESENT | PAGE_USER;
    if (prot & 2) flags |= PAGE_WRITABLE;
    if (!(prot & 4)) flags |= PAGE_NX;
    pt[PT_INDEX(virt)] = flags;

    VMM_INVLPG(virt);
    VMM_FLUSH_OTHERS(virt);
    return 0;
}

/* 虚拟地址转物理地址 */
uint64_t vmm64_virt_to_phys(uint64_t pml4_addr, uint64_t virt)
{
    pte_t *pml4 = (pte_t *)vmm64_phys_to_virt(pml4_addr);

    if (!(pml4[PML4_INDEX(virt)] & PAGE_PRESENT)) return 0;
    pte_t *pdpt = (pte_t *)vmm64_phys_to_virt(pml4[PML4_INDEX(virt)] & PAGE_MASK);

    if (!(pdpt[PDPT_INDEX(virt)] & PAGE_PRESENT)) return 0;
    pte_t *pd = (pte_t *)vmm64_phys_to_virt(pdpt[PDPT_INDEX(virt)] & PAGE_MASK);

    if (!(pd[PD_INDEX(virt)] & PAGE_PRESENT)) return 0;

    /* 大页 */
    if (pd[PD_INDEX(virt)] & PAGE_HUGE) {
        return (pd[PD_INDEX(virt)] & PAGE_MASK & ~0x1FFFFF) | (virt & 0x1FFFFF);
    }

    pte_t *pt = (pte_t *)vmm64_phys_to_virt(pd[PD_INDEX(virt)] & PAGE_MASK);
    if (!(pt[PT_INDEX(virt)] & PAGE_PRESENT)) return 0;

    return (pt[PT_INDEX(virt)] & PAGE_MASK) | (virt & 0xFFF);
}

/* 页表存在性查询：不返回物理页号，只回答"这一页是否已映射"。
 * 供 VA 管理器和后续插件热重载在分配前做二次核对。 */
int vmm64_page_present(uint64_t pml4_addr, uint64_t virt)
{
    if (!pml4_addr) return 0;
    pte_t *pml4 = (pte_t *)vmm64_phys_to_virt(pml4_addr);

    if (!(pml4[PML4_INDEX(virt)] & PAGE_PRESENT)) return 0;
    pte_t *pdpt = (pte_t *)vmm64_phys_to_virt(pml4[PML4_INDEX(virt)] & PAGE_MASK);

    if (!(pdpt[PDPT_INDEX(virt)] & PAGE_PRESENT)) return 0;
    pte_t *pd = (pte_t *)vmm64_phys_to_virt(pdpt[PDPT_INDEX(virt)] & PAGE_MASK);

    if (!(pd[PD_INDEX(virt)] & PAGE_PRESENT)) return 0;

    /* 大页直接视为已映射 */
    if (pd[PD_INDEX(virt)] & PAGE_HUGE) return 1;

    pte_t *pt = (pte_t *)vmm64_phys_to_virt(pd[PD_INDEX(virt)] & PAGE_MASK);
    return (pt[PT_INDEX(virt)] & PAGE_PRESENT) ? 1 : 0;
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

/* ===== V0.13.59: 页换出/换入原语 (配合 swap64) ===== */
#define PAGE_SWAPPED (1ULL << 9)   /* PTE AVL位: 页已换出 */

extern int swap64_alloc_slot(void);
extern int swap64_free_slot(int slot);
extern int swap64_write(int slot, const uint8_t *page);
extern int swap64_read(int slot, uint8_t *page);

/* 获取PT项指针(允许未present, 供swap判断) */
static pte_t *vmm64_get_pte(uint64_t pml4_addr, uint64_t virt)
{
    pte_t *pml4 = (pte_t *)pml4_addr;
    if (!(pml4[PML4_INDEX(virt)] & PAGE_PRESENT)) return 0;
    pte_t *pdpt = (pte_t *)(pml4[PML4_INDEX(virt)] & PAGE_MASK);
    if (!(pdpt[PDPT_INDEX(virt)] & PAGE_PRESENT)) return 0;
    pte_t *pd = (pte_t *)(pdpt[PDPT_INDEX(virt)] & PAGE_MASK);
    if (pd[PD_INDEX(virt)] & PAGE_HUGE) return 0;   /* 大页不支持换出 */
    if (!(pd[PD_INDEX(virt)] & PAGE_PRESENT)) return 0;
    pte_t *pt = (pte_t *)(pd[PD_INDEX(virt)] & PAGE_MASK);
    return &pt[PT_INDEX(virt)];
}

/* SEC-19 自检：判断某虚拟地址在给定页表里是否「present + 用户可访问」。
 * 与 vmm64_get_pte 的区别是它会正确处理 2MB 大页（大页时没有 PT 级，
 * 直接看 PDE 的 U 位），用来在 exec 之后逐页核对用户栈是否真的全部进表。
 * 返回 1=可访问, 0=不可访问/未映射, -1=中间级表缺失。 */
int vmm64_check_user_page(uint64_t pml4_addr, uint64_t virt)
{
    pte_t *pml4 = (pte_t *)vmm64_phys_to_virt(pml4_addr);
    if (!(pml4[PML4_INDEX(virt)] & PAGE_PRESENT)) return -1;
    pte_t *pdpt = (pte_t *)vmm64_phys_to_virt(pml4[PML4_INDEX(virt)] & PAGE_MASK);
    if (!(pdpt[PDPT_INDEX(virt)] & PAGE_PRESENT)) return -1;
    pte_t *pd = (pte_t *)vmm64_phys_to_virt(pdpt[PDPT_INDEX(virt)] & PAGE_MASK);
    pte_t pde = pd[PD_INDEX(virt)];
    if (!(pde & PAGE_PRESENT)) return 0;
    if (pde & PAGE_HUGE) {
        /* 2MB 大页：只需看 PDE 自己的 U 位 */
        return (pde & PAGE_USER) ? 1 : 0;
    }
    pte_t *pt = (pte_t *)vmm64_phys_to_virt(pde & PAGE_MASK);
    pte_t pte = pt[PT_INDEX(virt)];
    if (!(pte & PAGE_PRESENT)) return 0;
    return (pte & PAGE_USER) ? 1 : 0;
}

/* 判断页是否已换出 */
int vmm64_is_swapped(uint64_t pml4_addr, uint64_t virt)
{
    pte_t *pte = vmm64_get_pte(pml4_addr, virt);
    if (!pte) return 0;
    return (*pte & PAGE_SWAPPED) ? 1 : 0;
}

/* 换出页: 内容写swap, PTE存槽号+标记, 释放物理页 */
int vmm64_swap_out(uint64_t pml4_addr, uint64_t virt)
{
    pte_t *pte = vmm64_get_pte(pml4_addr, virt);
    if (!pte) return -1;
    if (!(*pte & PAGE_PRESENT) || (*pte & PAGE_SWAPPED)) return -1;

    uint64_t phys = *pte & PAGE_MASK;
    uint8_t page[4096];
    memcpy(page, (const void *)phys, 4096);

    int slot = swap64_alloc_slot();
    if (slot < 0) return -2;   /* swap 满 */
    if (swap64_write(slot, page) != 0) {
        swap64_free_slot(slot);
        return -3;
    }

    /* PTE: 地址字段存槽号 + 换出标记(清present) */
    *pte = ((uint64_t)(slot & 0x3FF) << 12) | PAGE_SWAPPED;
    pmm64_free_page(phys);
    VMM_INVLPG(virt);
    VMM_FLUSH_OTHERS(virt);
    return 0;
}

/* 换入页: 从swap读回, 重新映射, 释放槽 */
int vmm64_swap_in(uint64_t pml4_addr, uint64_t virt)
{
    pte_t *pte = vmm64_get_pte(pml4_addr, virt);
    if (!pte || !(*pte & PAGE_SWAPPED)) return -1;

    int slot = (int)((*pte >> 12) & 0x3FF);
    uint8_t page[4096];
    if (swap64_read(slot, page) != 0) return -2;

    uint64_t phys = pmm64_alloc_page();
    if (!phys) return -3;
    memcpy((void *)phys, page, 4096);

    vmm64_map_page(pml4_addr, virt, phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    swap64_free_slot(slot);
    return 0;
}

/* 写时复制（COW）缺页处理：
 * fork 时父子共享的物理页被清 W 位并打上 PAGE_COW 软件标记。任一方写入触发缺页，
 * 这里为当前进程分配私有副本、复制内容、恢复 W 位，并清除本映射的 COW 标记；
 * 另一进程仍保留原页与其 COW 标记，它后续写入时会各自复制。
 * 只处理「present + write + user 且带 COW 标记」的页，普通只读页（代码段）不受影响。
 * 返回 0 表示已修复，异常返回后可重试指令。 */
static int vmm64_try_cow(uint64_t cr2)
{
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    pte_t *pml4 = (pte_t *)vmm64_phys_to_virt(cr3 & PAGE_PHYS_MASK);
    pte_t e = pml4[PML4_INDEX(cr2)];
    if (!(e & PAGE_PRESENT)) return -1;
    pte_t *pdpt = (pte_t *)vmm64_phys_to_virt(e & PAGE_PHYS_MASK);
    e = pdpt[PDPT_INDEX(cr2)];
    if (!(e & PAGE_PRESENT) || (e & PAGE_HUGE)) return -1;
    pte_t *pd = (pte_t *)vmm64_phys_to_virt(e & PAGE_PHYS_MASK);
    e = pd[PD_INDEX(cr2)];
    if (!(e & PAGE_PRESENT) || (e & PAGE_HUGE)) return -1;
    pte_t *pt = (pte_t *)vmm64_phys_to_virt(e & PAGE_PHYS_MASK);
    pte_t old = pt[PT_INDEX(cr2)];
    if (!(old & PAGE_PRESENT) || !(old & PAGE_USER)) return -1;
    if (!(old & PAGE_COW)) return -1;          /* 非 COW 页：交由上层处理 */

    uint64_t old_phys = old & PAGE_PHYS_MASK;
    uint64_t new_phys = pmm64_alloc_page();
    if (!new_phys) return -1;
    memcpy((void *)vmm64_phys_to_virt(new_phys),
           (void *)vmm64_phys_to_virt(old_phys), PAGE_SIZE);

    /* 新映射：新物理页 + W 位，清除 COW 标记，保留其余标志（含 NX） */
    pt[PT_INDEX(cr2)] = new_phys | (old & 0xFFF & ~PAGE_COW)
                        | (old & PAGE_NX) | PAGE_WRITABLE;

    extern void cow_unref(uint64_t);
    cow_unref(old_phys);
    VMM_INVLPG(cr2);
    return 0;
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

    /* V0.13.59: 换出页缺页 -> 自动换入(异常返回重试待异常框架, 本轮函数级) */
    if (vmm64_is_swapped((uint64_t)kernel_pml4, cr2)) {
        vga_puts("[VMM64] swap-in page at ");
        vga_put_hex(cr2);
        vga_puts("\n");
        if (vmm64_swap_in((uint64_t)kernel_pml4, cr2) == 0) {
            /* 注: 异常返回重试指令需要异常框架支持, 当前仅恢复映射 */
            return;
        }
    }

    /* COW：用户态写共享页（present+write+user）-> 分配私有副本后重试 */
    if ((error_code & 0x7) == 0x7) {
        if (vmm64_try_cow(cr2) == 0) {
            vga_puts("[VMM64] COW copy at ");
            vga_put_hex(cr2);
            vga_puts("\n");
            return;
        }
    }

    /* 暂时停机 */
    while (1) { __asm__ volatile("hlt"); }
}
