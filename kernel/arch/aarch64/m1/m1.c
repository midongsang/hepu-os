/* Hepu OS ARM64 M1c —— 4KB 页（L3）最小 identity map + 异常向量接管
 *
 * 与 M1b 的差别：
 *  1. 页表从 2MB block（L2 叶子）换成 4KB 页（L0→L1→L2→L3 四级，L3 叶子）；
 *  2. 只白名单映射：image 所在 2MB（按运行时 pc/sp/表数组锚点反推）、
 *     UART(PL011 @0x09000000)、GICD(@0x08000000)/GICC(@0x08010000)，其余 invalid；
 *  3. 先装 VBAR_EL1 + 最小 el1_sync 向量，崩了能打出 ESR/ELR/FAR，不再是黑盒；
 *  4. 表描述符/页描述符都置 AF(bit10)，不依赖 TCR.HA 硬件回写访问标志。
 *
 * 实验开关见下方 EXP_*，每轮只翻一个，重跑留串口 + 退出码。
 */
#include <efi.h>
#include <efilib.h>

/* ---------------- 实验开关（每轮改一个） ---------------- */
#define EXP_FLUSH       1   /* 1=切表后 tlbi 刷 TLB；0=切表不刷（对照：旧 TLB 下应活着） */
#define EXP_MINIMAL     0   /* 1=只映射 pc 当前 4KB + UART 页（缩小表面积）；0=白名单整 2MB */
#define EXP_FLUSH_KIND  0   /* 0=tlbi vmalle1（全刷）；1=tlbi vaae1（单地址刷） */
#define EXP_MOFF        0   /* 1=标准序列：M=0 → 换 TTBR → 刷 TLB → 再开 M；0=MMU 开着热切 TTBR */
#define EXP_PROBE       0   /* 1=存活后故意读未映射 0x30000000，验证向量能打出 ESR/ELR/FAR */
#define EXP_WRITETCR    0   /* 1=切表前按固件值重写一遍 TCR_EL1（隔离 M1b 的 TCR 写入） */
#define EXP_BLOCK       0   /* 1=L2 直接放 2MB block 描述符（复现 M1b 形态，带 VBAR 抓异常）；0=L3 4KB 页 */

/* ---------------- PL011 ---------------- */
#define PL011_BASE 0x09000000UL
#define PL011_DR   (*(volatile uint32_t *)(PL011_BASE + 0x00))
#define PL011_FR   (*(volatile uint32_t *)(PL011_BASE + 0x18))
#define FR_TXFF    (1u << 5)

static void uart_putc(char c)
{
    if (c == '\n') uart_putc('\r');
    while (PL011_FR & FR_TXFF) { }
    PL011_DR = (uint32_t)c;
}
static void uart_puts(const char *s) { while (s && *s) uart_putc(*s++); }
static void uart_hex(unsigned long v)
{
    const char *h = "0123456789abcdef";
    for (int i = 60; i >= 0; i -= 4) uart_putc(h[(v >> i) & 0xF]);
}
static void uart_dec(unsigned long v)
{
    char b[24]; int n = 0;
    if (!v) { uart_putc('0'); return; }
    while (v && n < 23) { b[n++] = (char)('0' + v % 10); v /= 10; }
    while (n--) uart_putc(b[n]);
}

/* ---------------- 异常向量（放进 .text，PE 包装只收 .text/.data） ----------------
 * 向量表 2KB 对齐；16 个槽位，每个 128 字节。只实现 EL1 当前级同步异常（偏移 0x200），
 * 其余槽位跳到 hang。入口先保存 x0..x5，再把 ESR/ELR/FAR/CurrentEL 传给 C 处理函数。
 */
void el1_sync_c(uint64_t esr, uint64_t elr, uint64_t far, uint64_t el);
extern uint8_t vector_table[];

__asm__ (
".align 11\n"
".global vector_table\n"
"vector_table:\n"
/* 0x000 sync, EL1/EL0 with SP0 */
"  b .Lv_hang\n"
".align 7\n"
/* 0x080 irq, SP0 */
"  b .Lv_hang\n"
".align 7\n"
/* 0x100 fiq, SP0 */
"  b .Lv_hang\n"
".align 7\n"
/* 0x180 error, SP0 */
"  b .Lv_hang\n"
".align 7\n"
/* 0x200 sync, current EL with SP_EL1 —— 我们的入口 */
"  b .Lv_sync\n"
".align 7\n"
/* 0x280 irq, current EL */
"  b .Lv_hang\n"
".align 7\n"
/* 0x300 fiq, current EL */
"  b .Lv_hang\n"
".align 7\n"
/* 0x380 error, current EL */
"  b .Lv_hang\n"
".align 7\n"
".Lv_sync:\n"
"  stp x0, x1, [sp, #-16]!\n"
"  stp x2, x3, [sp, #-16]!\n"
"  stp x4, x5, [sp, #-16]!\n"
"  mrs x0, esr_el1\n"
"  mrs x1, elr_el1\n"
"  mrs x2, far_el1\n"
"  mrs x3, CurrentEL\n"
"  bl el1_sync_c\n"
".Lv_hang:\n"
"  b .\n"
);

void el1_sync_c(uint64_t esr, uint64_t elr, uint64_t far, uint64_t el)
{
    uart_puts("\n[EXC] synchronous exception at EL");
    uart_dec((el >> 2) & 0x3);
    uart_puts("\n[EXC] ESR_EL1=0x"); uart_hex(esr);
    uart_puts(" ELR_EL1=0x");     uart_hex(elr);
    uart_puts(" FAR_EL1=0x");     uart_hex(far);
    uart_puts("\n[EXC] EC=");     uart_dec((esr >> 26) & 0x3F);
    uart_puts(" ISS=0x");         uart_hex(esr & 0x1FFFFFFUL);
    uart_puts("\n[EXC] halting (no eret)\n");
    for (;;) __asm__ volatile("wfi");
}

/* ---------------- 页表（4KB 粒度，48 位 VA，四级） ----------------
 * VA 拆分（4KB 粒度）：
 *   L0 idx = VA[47:39]，L1 idx = VA[38:30]，L2 idx = VA[29:21]，
 *   L3 idx = VA[20:12]，页内偏移 VA[11:0]。
 *
 * 描述符位（stage1, EL1）：
 *   表描述符(L0/L1/L2)：bit0 valid=1，bit1 table=1；bit10 在表描述符上为 RES0，
 *      按本轮假设一并置 1 对照（真正的 AF 检查发生在叶子页描述符上）。
 *   页描述符(L3)：bit0 valid=1，bit1 page=1；
 *      bits[4:2] AttrIdx；bits[7:6] AP（00=EL1 RW，EL0 不可访问）；
 *      bits[9:8] SH（11=inner shareable）；bit10 AF=1；bit1 nG=0；
 *      bit53 PXN / bit54 UXN 保持 0（代码页不设执行禁止，避免自锁）。
 */
#define TAB_DESC  (0x3UL | (1UL << 10))
#define PAGE_DEV  (0x3UL | (0UL << 2) | (0UL << 8) | (1UL << 10))  /* Attr0 Device，SH=00 */
#define PAGE_NORM (0x3UL | (1UL << 2) | (3UL << 8) | (1UL << 10))  /* Attr1 Normal WB，SH=inner */
/* 2MB block（L2 叶子，bit1=0）：位布局同页描述符，仅叶子类型位不同 */
#define BLOCK_DEV  (0x1UL | (1UL << 10) | (0UL << 2) | (0UL << 8))
#define BLOCK_NORM (0x1UL | (1UL << 10) | (1UL << 2) | (3UL << 8))

#define L3_NORM_MAX 4

static uint64_t m1_l0[512]    __attribute__((aligned(4096)));
static uint64_t m1_l1[512]    __attribute__((aligned(4096)));
static uint64_t m1_l2_dev[512]  __attribute__((aligned(4096)));  /* L1[0]：0–1GB 设备 */
static uint64_t m1_l2_norm[512] __attribute__((aligned(4096)));  /* L1[1]：1–2GB RAM */
static uint64_t m1_l3_gic[512]  __attribute__((aligned(4096)));
static uint64_t m1_l3_uart[512] __attribute__((aligned(4096)));
static uint64_t m1_l3_norm[L3_NORM_MAX][512] __attribute__((aligned(4096)));

static uint64_t norm_blocks[L3_NORM_MAX];
static int      norm_block_cnt;

static void add_norm_block(uint64_t va)
{
    uint64_t base = va & ~0x1FFFFFULL;
    for (int i = 0; i < norm_block_cnt; i++)
        if (norm_blocks[i] == base) return;
    if (norm_block_cnt >= L3_NORM_MAX) {
        uart_puts("[!] norm whitelist full, drop 0x"); uart_hex(va); uart_puts("\n");
        return;
    }
    norm_blocks[norm_block_cnt++] = base;
}

static void mmu_build(uint64_t pc, uint64_t sp)
{
    /* 清表（freestanding，自己循环零） */
    for (int i = 0; i < 512; i++) {
        m1_l0[i] = 0; m1_l1[i] = 0;
        m1_l2_dev[i] = 0; m1_l2_norm[i] = 0;
        m1_l3_gic[i] = 0; m1_l3_uart[i] = 0;
        for (int b = 0; b < L3_NORM_MAX; b++) m1_l3_norm[b][i] = 0;
    }

    /* 白名单：pc、sp、页表数组自身所在的 2MB（页表必须落在已映射 Normal 内存里） */
    add_norm_block(pc);
    add_norm_block(sp);
    add_norm_block((uint64_t)m1_l0);
    add_norm_block((uint64_t)m1_l3_norm);

    uart_puts("[M1c] whitelist blocks:\n");
    for (int b = 0; b < norm_block_cnt; b++) {
        uart_puts("  blk=0x"); uart_hex(norm_blocks[b]);
        uart_puts(" l1="); uart_dec((norm_blocks[b] >> 30) & 0x1FF);
        uart_puts(" l2="); uart_dec((norm_blocks[b] >> 21) & 0x1FF);
        uart_puts("\n");
    }

    /* L0[0] -> L1（48 位下本机物理地址都在 L0 idx 0） */
    m1_l0[0] = (uint64_t)m1_l1 | TAB_DESC;
    /* L1[0] -> 设备 L2；L1[1] -> RAM L2 */
    m1_l1[0] = (uint64_t)m1_l2_dev | TAB_DESC;
    m1_l1[1] = (uint64_t)m1_l2_norm | TAB_DESC;

#if EXP_BLOCK
    /* 2MB block 模式：L2 直接放 block 描述符，不再下 L3（复现 M1b 描述符形态） */
    m1_l2_dev[64] = 0x08000000UL | BLOCK_DEV;   /* GIC 整块 2MB Device */
    m1_l2_dev[72] = 0x09000000UL | BLOCK_DEV;   /* UART 整块 2MB Device */
    for (int b = 0; b < norm_block_cnt; b++) {
        uint64_t base  = norm_blocks[b];
        uint64_t l1idx = (base >> 30) & 0x1FF;
        uint64_t l2idx = (base >> 21) & 0x1FF;
        if (l1idx != 1) {
            uart_puts("[!] norm block outside 1-2GB, l1="); uart_dec(l1idx); uart_puts("\n");
            continue;
        }
        m1_l2_norm[l2idx] = base | BLOCK_NORM;
    }
#else
    /* GIC：0x08000000 GICD、0x08010000 GICC，同属一个 2MB，各一个 4KB Device 页 */
    m1_l2_dev[64] = (uint64_t)m1_l3_gic | TAB_DESC;
    m1_l3_gic[0]  = 0x08000000UL | PAGE_DEV;
    m1_l3_gic[16] = 0x08010000UL | PAGE_DEV;
    /* UART PL011 @0x09000000，一个 4KB Device 页 */
    m1_l2_dev[72] = (uint64_t)m1_l3_uart | TAB_DESC;
    m1_l3_uart[0] = 0x09000000UL | PAGE_DEV;

    /* Normal 白名单块：整 2MB 的 512 个 4KB 页（identity） */
    for (int b = 0; b < norm_block_cnt; b++) {
        uint64_t base  = norm_blocks[b];
        uint64_t l1idx = (base >> 30) & 0x1FF;
        uint64_t l2idx = (base >> 21) & 0x1FF;
        if (l1idx != 1) {
            uart_puts("[!] norm block outside 1-2GB, l1="); uart_dec(l1idx); uart_puts("\n");
            continue;
        }
        m1_l2_norm[l2idx] = (uint64_t)m1_l3_norm[b] | TAB_DESC;
        if (EXP_MINIMAL) {
            /* 只留 pc 当前那一个 4KB 页，其余 invalid（缩小表面积用） */
            uint64_t pg = pc & ~0xFFFULL;
            m1_l3_norm[b][(pc >> 12) & 0x1FF] = pg | PAGE_NORM;
        } else {
            for (int i = 0; i < 512; i++)
                m1_l3_norm[b][i] = (base + (uint64_t)i * 0x1000UL) | PAGE_NORM;
        }
    }
#endif
}

static inline void tlb_flush(uint64_t va)
{
#if EXP_FLUSH_KIND == 0
    __asm__ volatile("tlbi vmalle1");
#else
    /* 单地址刷：Xt 放 VA>>12（VA[47:12] 挪到低位） */
    uint64_t x = va >> 12;
    __asm__ volatile("tlbi vaae1, %0" :: "r"(x));
#endif
    __asm__ volatile("dsb sy; isb");
}

static void mmu_switch(uint64_t ttbr, uint64_t cur_pc)
{
    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));

#if EXP_MOFF
    /* 标准序列：先关 MMU -> 换 TTBR -> 刷 TLB -> 再开 MMU */
    sctlr &= ~(1UL << 0);
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr));
    __asm__ volatile("isb");
    uart_puts("[dbg] M=0\n");
#endif

    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(ttbr));
    __asm__ volatile("isb");
    uart_puts("[dbg] ttbr ok\n");

#if EXP_FLUSH
    uart_puts("[dbg] about to tlbi\n");
    tlb_flush(cur_pc);
    uart_puts("[dbg] tlbi ok\n");
#else
    uart_puts("[dbg] no tlbi (baseline contrast)\n");
#endif

#if EXP_MOFF
    sctlr |= (1UL << 0);
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr));
    __asm__ volatile("isb");
    uart_puts("[dbg] M=1 again\n");
#endif
}

static void psci_off(void)
{
    __asm__ volatile("mov w0, #0x8\n movk w0, #0x8400, lsl #16\n hvc #0" : : : "x0");
    for (;;) __asm__ volatile("wfi");
}

EFI_STATUS
efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *st)
{
    InitializeLib(image_handle, st);

    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    uart_puts("\n[M1c] CurrentEL = EL");
    uart_dec((el >> 2) & 0x3);
    uart_putc('\n');

    uint64_t fw_tcr;
    {   /* 读固件当前 MMU 配置（只读不写；TCR 沿用固件值，见 M1b 结论） */
        uint64_t ttbr, sctlr;
        __asm__ volatile("mrs %0, tcr_el1" : "=r"(fw_tcr));
        __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr));
        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
        uart_puts("[fw] TCR_EL1=0x");  uart_hex(fw_tcr); uart_puts(" (T0SZ=");
        uart_dec(fw_tcr & 0x3F); uart_puts(")\n");
        uart_puts("[fw] TTBR0_EL1=0x"); uart_hex(ttbr); uart_puts("\n");
        uart_puts("[fw] SCTLR_EL1=0x"); uart_hex(sctlr); uart_puts("\n");
    }

    uint64_t pc, sp;
    __asm__ volatile("adr %0, ." : "=r"(pc));
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    uart_puts("[M1c] pc=0x"); uart_hex(pc);
    uart_puts(" sp=0x");     uart_hex(sp); uart_puts("\n");

    mmu_build(pc, sp);
    uart_puts("[M1c] page tables built (4KB pages, whitelist)\n");

    /* 先装异常向量：任何翻译/访问标志/权限异常都会走 el1_sync_c 打出 ESR/ELR/FAR。
     * 向量表地址用 PC 相对 adrp/add 现场算，不走 GOT（该 asm 符号的 RELATIVE 槽
     * 在 gnu-efi 自举里没落地，读回会是链接偏移）。 */
    {
        uint64_t vbar;
        __asm__ volatile(
            "adrp %0, vector_table\n"
            "add %0, %0, :lo12:vector_table\n"
            "msr vbar_el1, %0"
            : "=r"(vbar) :: "memory");
    }
    __asm__ volatile("isb");
    uart_puts("[M1c] vbar installed\n");

    /* MAIR：Attr0=Device nGnRE(0x04，nGnRnE 下 QEMU PL011 写会被吞)，Attr1=Normal WB(0xFF) */
    uint64_t mair = 0x04FFUL;
    __asm__ volatile("msr mair_el1, %0" :: "r"(mair));
    uart_puts("[M1c] mair ok\n");

    /* SCTLR：保 M/C/I，清 WXN(bit19)，不动 SA/A */
    {
        uint64_t sctlr;
        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
        sctlr |=  (1UL << 0);   /* M */
        sctlr |=  (1UL << 2);   /* C */
        sctlr |=  (1UL << 12);  /* I */
        sctlr &= ~(1UL << 19);  /* WXN=0 */
        __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr));
        __asm__ volatile("isb");
    }
    uart_puts("[M1c] sctlr ok\n");

#if EXP_WRITETCR
    /* 隔离项：按固件原值回写一遍 TCR（M1b 做过这一步），看同值重写是否影响后续取指 */
    __asm__ volatile("msr tcr_el1, %0" :: "r"(fw_tcr));
    __asm__ volatile("isb");
    uart_puts("[M1c] tcr rewritten (same value)\n");
#endif

    mmu_switch((uint64_t)m1_l0, pc);

    /* tlbi 之后的存活证据：取指地址 + 计数循环 + 再刷一次 TLB */
    {
        uint64_t pc2;
        __asm__ volatile("adr %0, ." : "=r"(pc2));
        uart_puts("[M1c] ALIVE after switch/tlbi, pc=0x"); uart_hex(pc2); uart_puts("\n");
    }
    for (unsigned long i = 0; i < 100; i++) {
        if (i % 10 == 0) { uart_puts("[M1c] count="); uart_dec(i); uart_puts("\n"); }
    }
    /* 切换后寄存器转储：核对 TTBR0 的 ASID/CnP、TCR.A1、SCTLR.A/SA */
    {
        uint64_t ttbr, tcr, sctlr, vbar;
        __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr));
        __asm__ volatile("mrs %0, tcr_el1"    : "=r"(tcr));
        __asm__ volatile("mrs %0, sctlr_el1"  : "=r"(sctlr));
        __asm__ volatile("mrs %0, vbar_el1"   : "=r"(vbar));
        uart_puts("[M1c] post TTBR0=0x"); uart_hex(ttbr);
        uart_puts(" ASID=");  uart_dec((ttbr >> 32) & 0xFFFF);
        uart_puts(" CnP=");   uart_dec(ttbr & 1);
        uart_puts("\n[M1c] post TCR=0x");  uart_hex(tcr);
        uart_puts(" A1=");    uart_dec((tcr >> 22) & 1);
        uart_puts("\n[M1c] post SCTLR=0x"); uart_hex(sctlr);
        uart_puts(" A=");     uart_dec((sctlr >> 1) & 1);
        uart_puts(" SA=");    uart_dec((sctlr >> 4) & 1);
        uart_puts("\n[M1c] post VBAR=0x"); uart_hex(vbar); uart_puts("\n");
    }
    uart_puts("[M1c] second tlbi\n");
    tlb_flush(0);
    uart_puts("[M1c] second tlbi done\n");

#if EXP_PROBE
    /* 故意访问未映射地址：应触发数据 abort，由 el1_sync_c 打出 ESR/ELR/FAR */
    {
        volatile uint32_t *p = (volatile uint32_t *)0x30000000UL;
        uart_puts("[M1c] probe unmapped 0x30000000\n");
        uint32_t x = *p;
        uart_puts("[M1c] probe returned 0x"); uart_hex(x); uart_puts("\n");
    }
#endif

    uart_puts("[M1c] poweroff\n");
    psci_off();
    return EFI_SUCCESS;
}
