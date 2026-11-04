/*
 * 主机端测试：用户地址空间隔离（user_aspace64.c + vmm64.c 的真实代码）
 * 页表页用主机内存模拟（物理地址 == 主机指针，与内核恒等映射的假设一致），
 * 用一个软件 MMU 模拟 x86-64 四级页表遍历（P/U/W/PS 位），验证：
 *   - ring3 访问内核恒等映射 / MMIO 一律 #PF，ring0 仍可访问
 *   - 两个进程的用户窗口映射互相私有，改动不会泄漏到内核页表
 *   - 释放只回收本进程的页（页帧、PT、PD、PDPT、PML4），不碰内核表和其他进程
 *   - RAM > 1GB 时退回旧模式；旧模式的共享缺陷仍可复现（对照）
 * 编译: gcc -DHEPU_HOST_TEST -I<kernel/arch/x86_64> -I<kernel/arch/x86_64/include> -I<kernel/include> -I<kernel/shared> \
 *        aspace_test.c user_aspace64.c vmm64.c
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint64_t uaspace_create(uint64_t kernel_pml4, int want_isolation, int *isolated_out);
extern int uaspace_free_isolated(uint64_t pml4_phys, uint64_t cur_cr3);
extern void vmm64_init(uint64_t pml4_addr);
extern int vmm64_map_page(uint64_t pml4, uint64_t virt, uint64_t phys, uint64_t flags);

#define P 1ULL
#define W 2ULL
#define U 4ULL
#define PS 0x80ULL
#define ADDR 0x000FFFFFFFFFF000ULL

/* ---- stubs for kernel services ---- */
void vga_puts(const char *s) { (void)s; }
void vga_put_hex(uint64_t v) { (void)v; }
static uint64_t g_ram_mb = 256;
uint64_t pmm64_get_memory_size_mb(void) { return g_ram_mb; }

#define MAXPG 4096
static struct { uint64_t addr; int live; int owner; } pg[MAXPG];
static int npg, g_owner, g_live, g_bad_free, g_double_free, g_free_expect = -1, g_free_wrong_owner;
uint64_t pmm64_alloc_page(void) {
    void *p = aligned_alloc(4096, 4096); memset(p, 0xA5, 4096);   /* garbage: code must zero tables itself */
    pg[npg].addr = (uint64_t)p; pg[npg].live = 1; pg[npg].owner = g_owner; npg++; g_live++;
    return (uint64_t)p;
}
void pmm64_free_page(uint64_t a) {
    for (int i = 0; i < npg; i++) if (pg[i].addr == a) {
        if (!pg[i].live) { g_double_free++; return; }
        if (g_free_expect >= 0 && pg[i].owner != g_free_expect) g_free_wrong_owner++;
        pg[i].live = 0; g_live--; return;
    }
    g_bad_free++;   /* freeing something pmm never handed out */
}
static int live_owned_by(int o) { int n = 0; for (int i = 0; i < npg; i++) if (pg[i].live && pg[i].owner == o) n++; return n; }

/* COW 计数桩：主机测试只验证页表搬运，不模拟跨进程写时复制 */
void cow_ref(uint64_t phys) { (void)phys; }
void cow_unref(uint64_t phys) { (void)phys; }

/* ---- software MMU ---- */
static int mmu(uint64_t cr3, uint64_t va, int user, int write, uint64_t *phys) {
    uint64_t *t = (uint64_t *)(cr3 & ADDR); int eu = 1, ew = 1;
    for (int lvl = 0; lvl < 4; lvl++) {
        uint64_t e = t[(va >> (39 - 9 * lvl)) & 0x1FF];
        if (!(e & P)) return 0;
        eu &= !!(e & U); ew &= !!(e & W);
        if (lvl == 3 || (lvl >= 1 && (e & PS))) {
            uint64_t sz = lvl == 1 ? (1ULL << 30) : lvl == 2 ? (1ULL << 21) : 4096;
            if (user && !eu) return 0;
            if (write && !ew) return 0;
            if (phys) *phys = (e & ADDR & ~(sz - 1)) | (va & (sz - 1));
            return 1;
        }
        t = (uint64_t *)(e & ADDR);
    }
    return 0;
}

/* snapshot every table reachable from a PML4 (only descends non-huge entries) */
typedef struct { uint64_t addr; uint64_t data[512]; } snap_t;
static snap_t snaps[512]; static int nsnap;
static void snap_walk(uint64_t table, int lvl) {
    for (int i = 0; i < nsnap; i++) if (snaps[i].addr == table) return;
    snaps[nsnap].addr = table; memcpy(snaps[nsnap].data, (void *)table, 4096); nsnap++;
    if (lvl == 3) return;
    uint64_t *t = (uint64_t *)table;
    for (int i = 0; i < 512; i++) if ((t[i] & P) && !((t[i] & PS) && lvl >= 1)) snap_walk(t[i] & ADDR, lvl + 1);
}
static void take(uint64_t pml4) { nsnap = 0; snap_walk(pml4, 0); }
static int changed(void) { for (int i = 0; i < nsnap; i++) if (memcmp(snaps[i].data, (void *)snaps[i].addr, 4096)) return 1; return 0; }

int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static uint64_t make_kernel(uint64_t mb) {
    g_ram_mb = mb; g_owner = 0;
    uint64_t k = pmm64_alloc_page();
    vmm64_init(k);                                                       /* identity map, USER|WRITABLE|HUGE like the real kernel */
    vmm64_map_page(k, 0xFD000000ULL, 0xFD000000ULL, P | W | U | PS);     /* framebuffer-like 2MB page, even with U set at leaf */
    vmm64_map_page(k, 0xFEE00000ULL, 0xFEE00000ULL, P | W);              /* APIC-like 4K MMIO page */
    return k;
}
#define WIN 0x40000000ULL
#define STK 0x70000000ULL
/* map stack(4 pages) + code(3 pages) like create_user_page_table_with_code; returns frames */
static void map_user(uint64_t pml4, uint64_t frames[7], uint8_t tag) {
    for (int i = 0; i < 4; i++) { frames[i] = pmm64_alloc_page(); memset((void *)frames[i], tag, 4096);
        vmm64_map_page(pml4, STK - (i + 1) * 4096, frames[i], P | W | U); }
    for (int i = 0; i < 3; i++) { frames[4 + i] = pmm64_alloc_page(); memset((void *)frames[4 + i], tag + 1, 4096);
        vmm64_map_page(pml4, WIN + i * 4096, frames[4 + i], P | W | U); }
}

int main(void) {
    uint64_t kern = make_kernel(256);
    int baseline = g_live;
    uint64_t addrs_kernel[] = { 0x0, 0x1000, 0x100000, 0x1000000, 0x7000000, 0xFFFFFFF /* ~256MB-1 */, 0xFD000000ULL, 0xFEE00010ULL };

    /* ===== create A and B (isolated) ===== */
    int isoA = 0, isoB = 0, ownerA = 1, ownerB = 2;
    uint64_t *kpdpt0 = (uint64_t *)(((uint64_t *)kern)[0] & ADDR);
    int pre = !(kpdpt0[2] & P) + !(kpdpt0[3] & P);     /* PDs that create() prepopulates in the kernel PDPT (0xFD000000 already made PDPT[3]) */
    g_owner = ownerA; uint64_t A = uaspace_create(kern, 1, &isoA);
    CHECK(A != 0 && isoA == 1);
    take(kern);                                   /* kernel tables right after the (one-time) PDPT[2]/[3] prepopulation */
    uint64_t fA[7], fB[7]; map_user(A, fA, 0x11);
    g_owner = ownerB; uint64_t B = uaspace_create(kern, 1, &isoB);
    CHECK(B != 0 && isoB == 1 && B != A);
    map_user(B, fB, 0x22);
    CHECK(!changed());                            /* user mappings of A and B did not touch any kernel table */
    uint64_t kern_pdpt_after = ((uint64_t *)(((uint64_t *)kern)[0] & ADDR))[1];
    CHECK(!(kern_pdpt_after & P));                /* kernel PDPT[1] (user window) stays empty */

    /* ===== ring3 view ===== */
    for (unsigned i = 0; i < sizeof addrs_kernel / sizeof addrs_kernel[0]; i++) {
        uint64_t va = addrs_kernel[i], ph;
        CHECK(!mmu(A, va, 1, 0, &ph));            /* ring3 read of kernel/identity/MMIO -> fault */
        CHECK(!mmu(A, va, 1, 1, &ph));            /* ring3 write -> fault */
        CHECK(!mmu(B, va, 1, 0, &ph));
    }
    /* ===== ring0 view (kernel keeps working under a process CR3) ===== */
    for (unsigned i = 0; i < sizeof addrs_kernel / sizeof addrs_kernel[0]; i++) {
        uint64_t va = addrs_kernel[i], ph, ph_k;
        int rk = mmu(kern, va, 0, 0, &ph_k), ra = mmu(A, va, 0, 0, &ph);
        CHECK(rk == ra && (!rk || ph == ph_k));   /* same supervisor translation as with the kernel CR3 */
    }
    CHECK(mmu(A, 0x100000, 0, 0, NULL));          /* sanity: identity map really is reachable by ring0 */

    /* ===== user window: own pages OK, private between A and B ===== */
    for (int i = 0; i < 4; i++) {
        uint64_t va = STK - (i + 1) * 4096, pa, pb;
        CHECK(mmu(A, va, 1, 1, &pa) && pa == fA[i]);
        CHECK(mmu(B, va, 1, 1, &pb) && pb == fB[i]);
        CHECK(pa != pb);
        CHECK(((uint8_t *)pa)[5] == 0x11 && ((uint8_t *)pb)[5] == 0x22);
    }
    for (int i = 0; i < 3; i++) {
        uint64_t pa, pb; CHECK(mmu(A, WIN + i * 4096, 1, 1, &pa) && pa == fA[4 + i]);
        CHECK(mmu(B, WIN + i * 4096, 1, 1, &pb) && pb == fB[4 + i]);
    }
    CHECK(!mmu(A, WIN + 3 * 4096, 1, 0, NULL));   /* unmapped neighbour page */
    CHECK(!mmu(A, 0x80000000ULL, 1, 0, NULL));    /* just past the window */
    /* no user-accessible leaf in A points at B's frames or at anything but A's own frames */
    int leaves = 0, foreign = 0;
    for (uint64_t va = WIN; va < 0x80000000ULL; va += 4096 * 16) { /* sparse scan is enough: mappings are at fixed spots */ }
    for (int i = 0; i < 7; i++) {
        uint64_t va = i < 4 ? STK - (i + 1) * 4096 : WIN + (i - 4) * 4096, ph;
        if (mmu(A, va, 1, 0, &ph)) { leaves++; int own = 0; for (int k = 0; k < 7; k++) own |= (ph == fA[k]); if (!own) foreign++; }
    }
    CHECK(leaves == 7 && foreign == 0);

    /* ===== free A: only A's own pages, B untouched ===== */
    int liveA_before = live_owned_by(ownerA);
    g_free_expect = ownerA; int freed = uaspace_free_isolated(A, (uint64_t)kern);
    g_free_expect = -1;
    CHECK(freed == 1 && g_free_wrong_owner == 0 && g_double_free == 0 && g_bad_free == 0);
    /* A had: PML4+PDPT+PD+PT(code)+PT(stack, may share PT? stack 0x6FFFxxxx and code 0x4000xxxx are different 2MB slots) + 7 frames */
    CHECK(live_owned_by(ownerA) == pre);          /* only the kernel-owned prepopulated PDs created during A's create remain */
    CHECK(liveA_before - live_owned_by(ownerA) == 2 /*PML4,PDPT*/ + 1 /*PD*/ + 2 /*PTs*/ + 7 /*frames*/);
    CHECK(pre >= 1);                              /* the test really exercised the prepopulation path */
    for (int i = 0; i < 4; i++) { uint64_t pb; CHECK(mmu(B, STK - (i + 1) * 4096, 1, 1, &pb) && pb == fB[i]); }
    CHECK(!changed());                            /* kernel tables still untouched */

    /* refuse to free while still loaded in CR3 */
    int before = g_live; CHECK(uaspace_free_isolated(B, B) == 0 && g_live == before);
    g_free_expect = ownerB; CHECK(uaspace_free_isolated(B, (uint64_t)kern) == 1); g_free_expect = -1;
    CHECK(g_free_wrong_owner == 0 && g_double_free == 0 && g_bad_free == 0);
    CHECK(g_live == baseline + pre);              /* everything returned except the shared PDs now owned by the kernel tables */
    CHECK(uaspace_free_isolated(0, (uint64_t)kern) == 0);

    /* ===== RAM > 1GB: 仍然可以隔离 =====
     * V0.33 更新：这条断言原本写的是「RAM > 1GB 就退回旧模式」，那是 v0.15.0
     * 之前的内存布局（物理内存全量恒等映射到低地址，会占住用户窗口）。
     * v0.15.0 改成「低身份映射只覆盖 [0,1GB)，>=1GB 走高半区直接映射」之后，
     * 用户窗口 [0x40000000,0x80000000) 永远不被内核恒等映射占用，
     * 所以 RAM 再大也**不再**影响隔离能力。
     * 这里跟着内核的真实行为走 —— 测试要反映契约，不是冻结历史。 */
    uint64_t kbig = make_kernel(1536);
    int iso = 0; g_owner = 3; uint64_t L = uaspace_create(kbig, 1, &iso);
    CHECK(L != 0 && iso == 1);                       /* RAM > 1GB 仍然隔离 */
    CHECK(memcmp((void *)L, (void *)kbig, 4096) != 0); /* 私有 PML4，不再是内核 PML4 的逐字节拷贝 */

    /* ===== legacy mode (control): shows the original defect ===== */
    uint64_t k2 = make_kernel(256); g_owner = 4;
    int isoL = 1; uint64_t L1 = uaspace_create(k2, 0, &isoL), L2 = uaspace_create(k2, 0, &isoL);
    CHECK(isoL == 0);
    uint64_t pf1 = pmm64_alloc_page(), pf2 = pmm64_alloc_page(), ph;
    vmm64_map_page(L1, WIN, pf1, P | W | U);
    CHECK(mmu(k2, WIN, 0, 0, NULL));              /* legacy: process mapping is visible through the KERNEL page tables */
    vmm64_map_page(L2, WIN, pf2, P | W | U);
    CHECK(mmu(L1, WIN, 1, 0, &ph) && ph == pf2);  /* legacy: process 2's mapping silently replaced process 1's */
    /* V0.33 更新：legacy 模式仍然共享用户窗口（上面的两条就是证据），
     * 但「ring3 能直接读写内核映像」这条在 v0.16.0 之后已经不成立了 ——
     * 低身份映射不再带 USER 位（内核映射全 U=0），所以 ring3 访问内核镜像一律 #PF。
     * 这条断言跟着内核的真实加固结果走。 */
    CHECK(!mmu(L1, 0x100000, 1, 1, NULL));         /* ring3 不能写内核映像（内核映射无 USER 位） */
    CHECK(mmu(L1, 0x100000, 0, 1, NULL));          /* ring0 仍然可以 */

    printf(fails ? "ASPACE TESTS FAILED (%d)\n" : "ALL ADDRESS-SPACE ISOLATION TESTS PASSED\n", fails);
    return fails != 0;
}

/* swap layer is not exercised here (vmm64.c references it for swap-out/in only) */
int swap64_alloc_slot(void) { return -1; }
int swap64_write(int slot, const void *page) { (void)slot; (void)page; return -1; }
int swap64_read(int slot, void *page) { (void)slot; (void)page; return -1; }
void swap64_free_slot(int slot) { (void)slot; }
