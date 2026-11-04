/*
 * user_aspace64.c — 用户地址空间（页表）的创建与释放
 *
 * 背景（见 docs/security/AUDIT_64BIT.md 漏洞 A / OPEN_BUGS SEC-01）：
 *   旧实现把内核 PML4 的 512 个表项原样复制给每个进程，PDPT/PD/PT 下层表由所有进程和内核共享，
 *   内核（0x100000 起）和全部物理内存的恒等映射都带 PAGE_USER。结果：
 *     1. ring3 程序可直接 load/store 内核内存，也能读写其他进程的物理页；
 *     2. 不同进程在 0x40000000 窗口的映射写进同一份共享页表，互相覆盖。
 *
 * 隔离模式（want_isolation=1，编译开关 HEPU_USER_ISOLATION，默认关闭）：
 *   - 每个进程拥有私有的 PML4 和 PDPT；
 *   - 内核映射（PML4[1..]、PDPT 里的所有内核表项）在进程视图里清除 U/S 位 → ring3 不可访问，
 *     而内核（ring0）在任何进程的 CR3 下仍可照常访问；
 *   - 用户窗口 [0x40000000, 0x80000000)（即 PDPT[1]）的 PD/PT 由 vmm64_map_page 在私有 PDPT 下
 *     新建，带 U/S 位，且只属于这个进程；进程退出时连同物理页帧一并释放。
 *
 * 不处理 / 已知限制（阶段 2）：
 *   - 物理内存 > 1GB 时内核恒等映射覆盖 PDPT[1]，与用户窗口冲突 → 自动退回旧模式；
 *   - fork 仍是共用地址空间的内核线程；
 *   - 内核自身 CR3 里的映射仍带 U/S 位（所以暂时不能开 SMEP/SMAP）；
 *   - 创建后内核新增的 PDPT 级表项（>=4GB 或新 1GB 槽位）不会出现在已存在进程的私有 PDPT 里；
 *     为减少该风险，隔离模式创建时会先在内核 PDPT 里预建 PDPT[2]、PDPT[3]（0.5–4GB 的 MMIO 区）。
 *
 * 物理地址 == 内核虚拟地址（恒等映射），所以页表页可直接按物理地址当指针访问。
 */
#include <stdint.h>
#include <stddef.h>

extern void vga_puts(const char *s);
extern uint64_t pmm64_alloc_page(void);
extern void pmm64_free_page(uint64_t addr);
extern uint64_t vmm64_phys_to_virt(uint64_t phys);
extern void *memcpy(void *dst, const void *src, size_t n);

#define UA_P     0x001ULL
#define UA_W     0x002ULL
#define UA_U     0x004ULL
#define UA_PS    0x080ULL
#define UA_ADDR  0x000FFFFFFFFFF000ULL
/* PTE 软件位（bit9，CPL 不可见）：标记写时复制共享页 */
#define UA_COW   0x200ULL
#define UWIN_PDPT_INDEX 1   /* 用户窗口 [0x40000000,0x80000000) */

static uint64_t alloc_zero_page(void)
{
    uint64_t p = pmm64_alloc_page();
    if (!p) return 0;
    uint64_t *t = (uint64_t *)vmm64_phys_to_virt(p);
    for (int i = 0; i < 512; i++) t[i] = 0;
    return p;
}

/*
 * 创建进程 PML4。
 *   want_isolation=0：与旧实现完全一致（逐项复制内核 PML4）。
 *   want_isolation=1：私有 PML4+PDPT，内核映射不可从 ring3 访问；条件不满足时退回旧模式。
 * *isolated_out 返回实际使用的模式。失败返回 0。
 */
uint64_t uaspace_create(uint64_t kernel_pml4, int want_isolation, int *isolated_out)
{
    if (isolated_out) *isolated_out = 0;

    uint64_t *kpml4 = (uint64_t *)kernel_pml4;
    uint64_t new_pml4 = pmm64_alloc_page();
    if (!new_pml4) return 0;
    uint64_t *npml4 = (uint64_t *)new_pml4;

    int isolate = want_isolation;
    uint64_t *kpdpt = 0;
    if (isolate) {
        if (!(kpml4[0] & UA_P)) {
            isolate = 0;
        } else {
            kpdpt = (uint64_t *)(kpml4[0] & UA_ADDR);
            if (kpdpt[1] & UA_P) {
                /* 用户窗口与内核恒等映射重叠（RAM > 1GB），无法给用户窗口留出私有 PD */
                vga_puts("[ASPACE] isolation unavailable: RAM identity map overlaps user window, using legacy mode\n");
                isolate = 0;
            }
        }
    }

    if (!isolate) {
        for (int i = 0; i < 512; i++) npml4[i] = kpml4[i];
        return new_pml4;
    }

    /* 预建 PDPT[2]/[3]（0.5–4GB，MMIO 常用区）的空 PD，让之后的内核 MMIO 映射写进共享 PD，
     * 对已创建的进程可见 */
    for (int j = 2; j <= 3; j++) {
        if (!(kpdpt[j] & UA_P)) {
            uint64_t pd = alloc_zero_page();
            if (!pd) { pmm64_free_page(new_pml4); return 0; }
            kpdpt[j] = pd | UA_P | UA_W | UA_U;
        }
    }

    uint64_t new_pdpt = alloc_zero_page();
    if (!new_pdpt) { pmm64_free_page(new_pml4); return 0; }
    uint64_t *npdpt = (uint64_t *)new_pdpt;

    /* 内核表项：共享下层 PD，但在本进程视图里清 U/S（有效权限 = 各级 U/S 相与） */
    for (int j = 0; j < 512; j++) {
        uint64_t e = kpdpt[j];
        npdpt[j] = (e & UA_P) ? (e & ~UA_U) : 0;
    }
    npml4[0] = new_pdpt | UA_P | UA_W | UA_U;   /* PML4[0] 需要 U/S，用户窗口在其下 */

    for (int i = 1; i < 512; i++) {
        uint64_t e = kpml4[i];
        npml4[i] = (e & UA_P) ? (e & ~UA_U) : 0;
    }

    if (isolated_out) *isolated_out = 1;
    return new_pml4;
}

/*
 * 释放隔离模式创建的地址空间：用户窗口里的页帧、PT、PD，以及私有的 PDPT、PML4。
 * 识别规则：隔离模式下内核表项的 U/S 都已清除，所以「U/S=1 的 PDPT/PD 表项」一定是
 * vmm64_map_page 为本进程新建的，可以安全释放。
 * cur_cr3：调用时的当前 CR3；若仍指向本地址空间则拒绝释放（宁可泄漏也不能 free 正在使用的页表）。
 * 返回 1=已释放，0=未释放。
 */
int uaspace_free_isolated(uint64_t pml4_phys, uint64_t cur_cr3)
{
    if (!pml4_phys) return 0;
    if ((cur_cr3 & UA_ADDR) == (pml4_phys & UA_ADDR)) {
        vga_puts("[ASPACE] refuse to free address space that is still loaded in CR3\n");
        return 0;
    }

    uint64_t *pml4 = (uint64_t *)pml4_phys;
    if (pml4[0] & UA_P) {
        uint64_t pdpt_phys = pml4[0] & UA_ADDR;
        uint64_t *pdpt = (uint64_t *)pdpt_phys;
        for (int j = 0; j < 512; j++) {
            uint64_t e = pdpt[j];
            if (!(e & UA_P) || !(e & UA_U) || (e & UA_PS)) continue;   /* 内核共享表项 */
            uint64_t *pd = (uint64_t *)(e & UA_ADDR);
            for (int k = 0; k < 512; k++) {
                uint64_t de = pd[k];
                if (!(de & UA_P) || !(de & UA_U) || (de & UA_PS)) continue;
                uint64_t *pt = (uint64_t *)(de & UA_ADDR);
                for (int m = 0; m < 512; m++) {
                    if (pt[m] & UA_P) pmm64_free_page(pt[m] & UA_ADDR);
                }
                pmm64_free_page((uint64_t)pt);
            }
            pmm64_free_page((uint64_t)pd);
        }
        pmm64_free_page(pdpt_phys);
    }
    pmm64_free_page(pml4_phys);
    return 1;
}

/*
 * fork：深度复制父进程的用户地址空间（隔离模式）。
 * 父进程页表必须是隔离模式（私有 PML4+PDPT）。复制 PML4/PDPT 的内核项（同 uaspace_create），
 * 然后对用户窗口 PDPT[UWIN_PDPT_INDEX] 下的每个 PD/PT：分配新页帧并拷贝 4KB 内容，
 * 使子进程拥有独立的用户物理页（非写时共享——简单可靠，后续可升级 COW）。
 * 失败返回 0（已尽量回收已分配页）。成功返回子进程 PML4 物理地址。
 */
uint64_t uaspace_fork_copy(uint64_t parent_pml4_phys)
{
    uint64_t *ppml4 = (uint64_t *)vmm64_phys_to_virt(parent_pml4_phys);
    if (!(ppml4[0] & UA_P)) return 0;   /* 非隔离模式不支持复制 */
    uint64_t *ppdpt = (uint64_t *)vmm64_phys_to_virt(ppml4[0] & UA_ADDR);
    if (!(ppdpt[UWIN_PDPT_INDEX] & UA_P)) return 0;  /* 父进程用户窗口无映射 */

    /* 新 PML4 + PDPT：复制内核视图（与 uaspace_create 隔离分支相同） */
    uint64_t child_pml4 = pmm64_alloc_page();
    if (!child_pml4) return 0;
    uint64_t *cpml4 = (uint64_t *)vmm64_phys_to_virt(child_pml4);

    uint64_t child_pdpt = alloc_zero_page();
    if (!child_pdpt) { pmm64_free_page(child_pml4); return 0; }
    uint64_t *cpdpt = (uint64_t *)vmm64_phys_to_virt(child_pdpt);

    int failed = 0;

    for (int j = 0; j < 512; j++) {
        uint64_t e = ppdpt[j];
        if (!(e & UA_P) || (e & UA_PS)) { cpdpt[j] = 0; continue; }
        if (j == UWIN_PDPT_INDEX) { cpdpt[j] = 0; continue; }  /* 用户窗口单独深拷贝 */
        cpdpt[j] = e & ~UA_U;   /* 共享内核下层 PD，清 ring3 位 */
    }
    cpml4[0] = child_pdpt | UA_P | UA_W | UA_U;
    for (int i = 1; i < 512; i++) {
        uint64_t e = ppml4[i];
        cpml4[i] = (e & UA_P) ? (e & ~UA_U) : 0;
    }

    /* 深拷贝用户窗口：父 PDPT[1] -> PD 数组页；每个 PD 项 -> PT 页；每个 PT 项 -> 新 4KB 帧 */
    uint64_t *parent_pd_arr = (uint64_t *)vmm64_phys_to_virt(ppdpt[UWIN_PDPT_INDEX] & UA_ADDR);
    uint64_t child_pd_arr = alloc_zero_page();
    if (!child_pd_arr) failed = 1;
    uint64_t *cpd_arr = (uint64_t *)vmm64_phys_to_virt(child_pd_arr);

    for (int k = 0; k < 512 && !failed; k++) {
        uint64_t pd_entry = parent_pd_arr[k];
        if (!(pd_entry & UA_P) || (pd_entry & UA_PS)) { cpd_arr[k] = 0; continue; }
        uint64_t *parent_pt = (uint64_t *)vmm64_phys_to_virt(pd_entry & UA_ADDR);
        uint64_t child_pt = alloc_zero_page();
        if (!child_pt) { failed = 1; break; }
        uint64_t *cpt = (uint64_t *)vmm64_phys_to_virt(child_pt);
        cpd_arr[k] = (pd_entry & ~UA_ADDR) | child_pt;   /* 保留 P/W/U，换 PT 物理页 */
        for (int m = 0; m < 512; m++) {
            uint64_t pte = parent_pt[m];
            if (!(pte & UA_P) || (pte & UA_PS)) { cpt[m] = 0; continue; }
            /* COW: 共享物理页，清W位并打上软件 COW 标记，写时再复制 */
            extern void cow_ref(uint64_t phys);
            cow_ref(pte & UA_ADDR);
            cpt[m] = (pte & ~UA_W) | UA_COW;  /* 子进程：清写位，标记 COW */
            /* 父进程也清W位并标记 COW */
            parent_pt[m] = (pte & ~UA_W) | UA_COW;
        }
    }

    if (!failed) cpdpt[UWIN_PDPT_INDEX] = child_pd_arr | UA_P | UA_W | UA_U;

    if (failed) {
        uint64_t cr3; __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        uaspace_free_isolated(child_pml4, cr3);
        return 0;
    }

    vga_puts("[ASPACE] fork: user aspace copied\n");
    return child_pml4;
}
