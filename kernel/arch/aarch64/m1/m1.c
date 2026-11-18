/* Hepu OS ARM64 M1d —— 全 RAM 映射 + GIC/定时器中断 + ExitBootServices 换栈
 *
 * 基于 M1c（4KB L3 白名单映射 + VBAR/el1_sync 向量）往上做，分三个里程碑：
 *   M1d-1：GetMemoryMap 取描述符，混合映射全 RAM（整 2MB 对齐用 L2 block，
 *          首尾不齐用 L3 4KB 页），远端可用 RAM 写读回校验；
 *   M1d-2：GIC 版本判定 + 虚拟定时器 PPI27 中断计数（EOIR 后重 arm）；
 *   M1d-3：ExitBootServices + 描述符静态副本 + 自有 bss 栈切换。
 *
 * 实验开关见下方 EXP_*。
 */
#include <efi.h>
#include <efilib.h>

/* ---------------- 实验开关 ---------------- */
#define EXP_FLUSH       1   /* 1=切表后 tlbi 刷 TLB；0=不刷（对照） */
#define EXP_FLUSH_KIND  0   /* 0=vmalle1 全刷；1=vaae1 单地址刷 */
#define EXP_NO_EOIR     0   /* 1=对照：handler 不写 EOIR，验证后续中断不再来 */

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

/* ---------------- 异常向量（.text，2KB 对齐，每槽 128 字节） ---------------- */
void el1_sync_c(uint64_t esr, uint64_t elr, uint64_t far, uint64_t el);
extern uint8_t vector_table[];

__asm__ (
".align 11\n"
".global vector_table\n"
"vector_table:\n"
/* 0x000 sync, SP0 */
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
/* 0x200 sync, current EL, SP_EL1 */
"  b .Lv_sync\n"
".align 7\n"
/* 0x280 irq, current EL, SP_EL1 */
"  b .Lv_irq\n"
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
".Lv_irq:\n"
"  stp x0, x1, [sp, #-16]!\n"
"  stp x2, x3, [sp, #-16]!\n"
"  stp x4, x5, [sp, #-16]!\n"
"  bl el1_irq_c\n"
"  ldp x4, x5, [sp], #16\n"
"  ldp x2, x3, [sp], #16\n"
"  ldp x0, x1, [sp], #16\n"
"  eret\n"
".Lv_hang:\n"
"  b .\n"
);

void el1_irq_c(void);

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

/* ---------------- 页表（4KB 粒度，48 位 VA，四级；混合 block/page） ----------------
 * VA 拆分：L0[47:39] L1[38:30] L2[29:21] L3[20:12] 页内[11:0]。
 * 表描述符：valid|table，bit10(AF 位，表描述符上 RES0，置上对照)。
 * 叶子：valid；页 bit1=1 / block bit1=0；AttrIdx bits[4:2]；AP bits[7:6]；
 * SH bits[9:8]；AF bit10；PXN bit53 / UXN bit54 留 0（不锁执行）。
 */
#define TAB_DESC  (0x3UL | (1UL << 10))
#define PAGE_DEV  (0x3UL | (0UL << 2) | (0UL << 8) | (1UL << 10))
#define PAGE_NORM (0x3UL | (1UL << 2) | (3UL << 8) | (1UL << 10))
#define BLOCK_DEV  (0x1UL | (1UL << 10) | (0UL << 2) | (0UL << 8))
#define BLOCK_NORM (0x1UL | (1UL << 10) | (1UL << 2) | (3UL << 8))

#define MAX_L3 48   /* 部分覆盖 2MB 块的 L3 表池（每表 4KB） */

static uint64_t m1_l0[512]     __attribute__((aligned(4096)));
static uint64_t m1_l1[512]     __attribute__((aligned(4096)));
static uint64_t m1_l2[2][512]   __attribute__((aligned(4096)));
static uint64_t m1_l3_pool[MAX_L3][512] __attribute__((aligned(4096)));
static uint64_t l3_block_owner[MAX_L3];
static int      l3_cnt;

/* 映射统计 */
static uint64_t stat_blocks;    /* L2 block 数（每个 2MB） */
static uint64_t stat_pages;     /* L3 4KB 页数 */
static uint64_t stat_bytes;     /* 映射 RAM 字节数 */

static void map_block_norm(uint64_t base)
{
    uint64_t l1 = (base >> 30) & 0x1FF;
    uint64_t l2i = (base >> 21) & 0x1FF;
    m1_l1[l1] = (uint64_t)m1_l2[l1] | TAB_DESC;
    m1_l2[l1][l2i] = base | BLOCK_NORM;
}

static uint64_t *get_l3(uint64_t block_base)
{
    for (int i = 0; i < l3_cnt; i++)
        if (l3_block_owner[i] == block_base) return m1_l3_pool[i];
    if (l3_cnt >= MAX_L3) {
        uart_puts("[!] L3 pool full at 0x"); uart_hex(block_base); uart_puts("\n");
        return 0;
    }
    int i = l3_cnt++;
    l3_block_owner[i] = block_base;
    uint64_t l1 = (block_base >> 30) & 0x1FF;
    uint64_t l2i = (block_base >> 21) & 0x1FF;
    m1_l1[l1] = (uint64_t)m1_l2[l1] | TAB_DESC;
    m1_l2[l1][l2i] = (uint64_t)m1_l3_pool[i] | TAB_DESC;
    return m1_l3_pool[i];
}

static void map_page(uint64_t pa, uint64_t desc)
{
    uint64_t *l3 = get_l3(pa & ~0x1FFFFFULL);
    if (l3) l3[(pa >> 12) & 0x1FF] = pa | desc;
}

/* 接受进 Normal WB 映射的固件内存类型；其余（Reserved/Unusable/MMIO/IOPort/Pal）跳过 */
static int ram_type_ok(uint32_t t)
{
    switch (t) {
    case 1: /* LoaderCode */
    case 2: /* LoaderData */
    case 3: /* BootServicesCode */
    case 4: /* BootServicesData */
    case 5: /* RuntimeServicesCode */
    case 6: /* RuntimeServicesData */
    case 7: /* ConventionalMemory */
    case 9: /* ACPIReclaimMemory */
    case 10:/* ACPIMemoryNVS */
    case 14:/* PersistentMemory */
        return 1;
    default:
        return 0;
    }
}

static void map_descriptor(EFI_MEMORY_DESCRIPTOR *d)
{
    uint64_t start = d->PhysicalStart;
    uint64_t end   = start + d->NumberOfPages * 4096UL;
    uint64_t b = start & ~0x1FFFFFULL;

    while (b < end) {
        uint64_t bend = b + 0x200000UL;
        if (start <= b && bend <= end) {
            /* 整 2MB 且完全被本描述符覆盖 → L2 block */
            map_block_norm(b);
            stat_blocks++;
        } else {
            /* 首尾不齐 → 该 2MB 内按 4KB 页映射覆盖区间 */
            uint64_t lo = start > b ? start : b;
            uint64_t hi = end < bend ? end : bend;
            for (uint64_t pa = lo & ~0xFFFULL; pa < hi; pa += 0x1000UL)
                map_page(pa, PAGE_NORM);
            stat_pages += (hi - lo) / 0x1000UL;
        }
        b = bend;
    }
    stat_bytes += end - start;
}

/* ---- GIC 寄存器 ---- */
#define GICD_BASE 0x08000000UL
#define GICC_BASE 0x08010000UL

static uint32_t mmio_read32(uint64_t a)  { return *(volatile uint32_t *)a; }
static void     mmio_write32(uint64_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

/* 设备 MMIO：UART / GICD / GICC，各给 4KB Device 页 */
static void map_devices(void)
{
    map_page(0x08000000UL, PAGE_DEV);  /* GICD */
    map_page(0x08010000UL, PAGE_DEV);  /* GICC */
    map_page(0x09000000UL, PAGE_DEV);  /* PL011 UART */
}

/* ---------------- MemoryMap 取取与打印 ---------------- */
#define MAP_BUF_SIZE (32u * 1024u)
static uint8_t  map_buf[MAP_BUF_SIZE] __attribute__((aligned(8)));
static UINTN    map_size, map_key, desc_size;
static UINT32   desc_ver;
static EFI_MEMORY_DESCRIPTOR *map_first;
static UINTN    map_count;

static EFI_STATUS fetch_memory_map(EFI_SYSTEM_TABLE *st)
{
    EFI_STATUS ms;
    /* 第一遍：Size=0/NULL 缓冲，让固件填所需 Size */
    map_size = 0;
    ms = uefi_call_wrapper(st->BootServices->GetMemoryMap, 5,
            &map_size, (EFI_MEMORY_DESCRIPTOR *)NULL, &map_key, &desc_size, &desc_ver);
    if (ms != EFI_BUFFER_TOO_SMALL) {
        uart_puts("[M1d] map first call Status="); uart_dec(ms); uart_puts("\n");
        return ms;
    }
    if (map_size > MAP_BUF_SIZE) {
        uart_puts("[M1d] map too big Size="); uart_dec(map_size); uart_puts("\n");
        return EFI_OUT_OF_RESOURCES;
    }
    /* 留两个描述符余量后重取 */
    map_size += desc_size * 2;
    ms = uefi_call_wrapper(st->BootServices->GetMemoryMap, 5,
            &map_size, (EFI_MEMORY_DESCRIPTOR *)map_buf, &map_key, &desc_size, &desc_ver);
    if (EFI_ERROR(ms)) {
        uart_puts("[M1d] map second call Status="); uart_dec(ms); uart_puts("\n");
        return ms;
    }
    map_count = map_size / desc_size;
    map_first = (EFI_MEMORY_DESCRIPTOR *)map_buf;

    uart_puts("[M1d] MemoryMap: descriptors="); uart_dec(map_count);
    uart_puts(" DescSize="); uart_dec(desc_size);
    uart_puts(" Ver="); uart_dec(desc_ver);
    uart_puts(" Key="); uart_hex(map_key); uart_puts("\n");
    EFI_MEMORY_DESCRIPTOR *d = map_first;
    for (UINTN i = 0; i < map_count; i++) {
        uart_puts("  ["); uart_dec(i); uart_puts("] type=");
        uart_dec(d->Type);
        uart_puts(" start=0x"); uart_hex(d->PhysicalStart);
        uart_puts(" pages="); uart_dec(d->NumberOfPages);
        uart_puts(" attr=0x"); uart_hex(d->Attribute);
        uart_puts(d->Type <= 14 && ram_type_ok(d->Type) ? " [map]" : " [skip]");
        uart_puts("\n");
        d = NextMemoryDescriptor(d, desc_size);
    }
    return EFI_SUCCESS;
}

static void mmu_build_from_map(void)
{
    for (int i = 0; i < 512; i++) {
        m1_l0[i] = 0; m1_l1[i] = 0;
        m1_l2[0][i] = 0; m1_l2[1][i] = 0;
        for (int k = 0; k < MAX_L3; k++) m1_l3_pool[k][i] = 0;
    }
    l3_cnt = 0;
    stat_blocks = stat_pages = stat_bytes = 0;

    m1_l0[0] = (uint64_t)m1_l1 | TAB_DESC;

    EFI_MEMORY_DESCRIPTOR *d = map_first;
    for (UINTN i = 0; i < map_count; i++) {
        if (ram_type_ok(d->Type)) map_descriptor(d);
        d = NextMemoryDescriptor(d, desc_size);
    }
    map_devices();

    uart_puts("[M1d] mapped: blocks="); uart_dec(stat_blocks);
    uart_puts(" pages="); uart_dec(stat_pages);
    uart_puts(" RAM="); uart_dec(stat_bytes / (1024UL * 1024UL));
    uart_puts("MB L3tables="); uart_dec(l3_cnt); uart_puts("\n");
}

static inline void tlb_flush(uint64_t va)
{
#if EXP_FLUSH_KIND == 0
    __asm__ volatile("tlbi vmalle1");
#else
    uint64_t x = va >> 12;
    __asm__ volatile("tlbi vaae1, %0" :: "r"(x));
#endif
    __asm__ volatile("dsb sy; isb");
}

static void mmu_switch(uint64_t ttbr, uint64_t cur_pc)
{
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
}

static void psci_off(void)
{
    __asm__ volatile("mov w0, #0x8\n movk w0, #0x8400, lsl #16\n hvc #0" : : : "x0");
    for (;;) __asm__ volatile("wfi");
}

static void ram_rw_check(const char *tag, uint64_t addr)
{
    volatile uint64_t *p = (volatile uint64_t *)addr;
    uint64_t w0 = 0xA55A0001DEAD0001UL;
    uint64_t w1 = 0x123456789ABCDEF0UL;
    p[0] = w0; p[1] = w1;
    uint64_t r0 = p[0], r1 = p[1];
    uart_puts("[M1d] "); uart_puts(tag);
    uart_puts(" @0x"); uart_hex(addr);
    uart_puts(" w0=0x"); uart_hex(w0); uart_puts(" r0=0x"); uart_hex(r0);
    uart_puts("\n[M1d]       w1=0x"); uart_hex(w1); uart_puts(" r1=0x"); uart_hex(r1);
    uart_puts(r0 == w0 && r1 == w1 ? " OK\n" : " MISMATCH\n");
}

/* 远端可用 RAM 写读回校验：低地址大块 conventional + 一段远离 image 的 conventional */
static void remote_ram_check(uint64_t image_base)
{
    ram_rw_check("low RAM ", 0x40000000UL);
    EFI_MEMORY_DESCRIPTOR *d = map_first;
    for (UINTN i = 0; i < map_count; i++) {
        if (d->Type == 7 && d->PhysicalStart > image_base + 0x1000000UL) {
            ram_rw_check("remote RAM", d->PhysicalStart);
            return;
        }
        d = NextMemoryDescriptor(d, desc_size);
    }
    uart_puts("[M1d] no remote conventional RAM found\n");
}

/* ---------------- GICv2 + 虚拟定时器（PPI 27） ---------------- */
#define GICD_ISENABLER0   0x100
#define GICD_ITARGETSR0   0x800
#define GICC_CTLR         0x00
#define GICC_PMR          0x04
#define GICC_IAR          0x0C
#define GICC_EOIR         0x10
#define PPI_VTIMER        27

static volatile uint64_t irq_count;
static volatile uint32_t irq_last;
static uint64_t cntv_freq;
static uint32_t gic_ver;

void el1_irq_c(void)
{
    uint32_t iar = mmio_read32(GICC_BASE + GICC_IAR);
    uint32_t intid = iar & 0x3FFu;
    irq_last = intid;
    irq_count++;
    if (irq_count <= 3 || irq_count % 10 == 0) {
        uint64_t esr;
        __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
        uart_puts("[IRQ] enter vector, n="); uart_dec(irq_count);
        uart_puts(" intid="); uart_dec(intid);
        uart_puts(" ESR=0x"); uart_hex(esr); uart_puts("\n");
    }
    /* EOIR 必须写回 IAR 值；漏写会导致后续中断不再来（EXP_NO_EOIR 对照） */
#if !EXP_NO_EOIR
    mmio_write32(GICC_BASE + GICC_EOIR, iar);
#endif
    /* 重 arm 虚拟定时器：TVVAL 是有符号偏移，正值=未来比较值（负值=已过期，会风暴） */
    __asm__ volatile("msr cntv_tval_el0, %0" :: "r"(cntv_freq / 10));
}

static void gic_timer_init(void)
{
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(cntv_freq));
    uart_puts("[M1d] CNTFRQ="); uart_dec(cntv_freq); uart_puts("\n");

    if (gic_ver == 2) {
        /* GICv2：distributor 先使能，再配 PPI27 路由，最后开 CPU interface */
        mmio_write32(GICD_BASE + 0x00, 0x01);                       /* GICD_CTLR */
        mmio_write32(GICD_BASE + GICD_ISENABLER0, 1u << PPI_VTIMER);/* 使能 PPI27 */
        /* ITARGETSR27：字内字节 lane = 27%4，目标 CPU0 */
        uint32_t off = GICD_ITARGETSR0 + (PPI_VTIMER / 4) * 4;
        uint32_t lane = PPI_VTIMER % 4;
        uint32_t w = mmio_read32(GICD_BASE + off);
        w &= ~(0xFFu << (lane * 8));
        w |= 0x01u << (lane * 8);
        mmio_write32(GICD_BASE + off, w);
        mmio_write32(GICC_BASE + GICC_PMR, 0xFF);                   /* 优先级掩码全放 */
        mmio_write32(GICC_BASE + GICC_CTLR, 0x01);                  /* GICC_CTLR enable */
        uart_puts("[M1d] GICv2: D/C enabled, PPI27 -> CPU0\n");
    } else {
        uart_puts("[M1d] GICv3 路径未实现（PIDR2 判定非 v2）\n");
    }

    /* 虚拟定时器：100ms 周期，ENABLE=1、IMASK=0；TVVAL 正值=未来偏移 */
    __asm__ volatile("msr cntv_tval_el0, %0" :: "r"(cntv_freq / 10));
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(1UL));
    __asm__ volatile("isb");
    uart_puts("[M1d] timer armed, unmask IRQ\n");
    __asm__ volatile("msr daifclr, #2");   /* 清 DAIF.IRQ 掩码 */
}

EFI_STATUS
efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *st)
{
    InitializeLib(image_handle, st);

    uint64_t el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
    uart_puts("\n[M1d] CurrentEL = EL");
    uart_dec((el >> 2) & 0x3);
    uart_putc('\n');

    uint64_t fw_tcr;
    {
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
    uart_puts("[M1d] pc=0x"); uart_hex(pc);
    uart_puts(" sp=0x");     uart_hex(sp); uart_puts("\n");

    EFI_STATUS ms = fetch_memory_map(st);
    if (EFI_ERROR(ms)) {
        uart_puts("[M1d] memory map unavailable, halt\n");
        for (;;) __asm__ volatile("wfi");
    }

    mmu_build_from_map();
    uart_puts("[M1d] page tables built (map-driven mixed block/page)\n");

    /* GIC 版本判定：PIDR2 bits[7:4]，0x2=v2，0x3=v3 */
    {
        uint32_t pidr2 = mmio_read32(GICD_BASE + 0xFE8);
        uint32_t typer = mmio_read32(GICD_BASE + 0x004);
        gic_ver = (pidr2 >> 4) & 0xF;
        uint32_t ver = gic_ver;
        uart_puts("[M1d] GICD_PIDR2=0x"); uart_hex(pidr2);
        uart_puts(" TYPER=0x"); uart_hex(typer);
        uart_puts(" version="); uart_dec(ver);
        uart_puts(ver == 2 ? " (GICv2)\n" : ver == 3 ? " (GICv3)\n" : " (unknown)\n");
    }

    /* 异常向量：PC 相对 adrp/add 取址（不走 GOT，见 M1c 踩坑记录） */
    {
        uint64_t vbar;
        __asm__ volatile(
            "adrp %0, vector_table\n"
            "add %0, %0, :lo12:vector_table\n"
            "msr vbar_el1, %0"
            : "=r"(vbar) :: "memory");
    }
    __asm__ volatile("isb");
    uart_puts("[M1d] vbar installed\n");

    /* MAIR：Attr0=Device nGnRE(0x04)，Attr1=Normal WB(0xFF) */
    uint64_t mair = 0x04FFUL;
    __asm__ volatile("msr mair_el1, %0" :: "r"(mair));
    uart_puts("[M1d] mair ok\n");

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
    uart_puts("[M1d] sctlr ok\n");

    mmu_switch((uint64_t)m1_l0, pc);

    {
        uint64_t pc2;
        __asm__ volatile("adr %0, ." : "=r"(pc2));
        uart_puts("[M1d] ALIVE after switch/tlbi, pc=0x"); uart_hex(pc2); uart_puts("\n");
    }

    remote_ram_check(pc & ~0x1FFFFFULL);

    gic_timer_init();
    uint64_t main_ticks = 0;
    while (irq_count < 20) main_ticks++;
    uart_puts("[M1d] IRQ count="); uart_dec(irq_count);
    uart_puts(" main_ticks="); uart_dec(main_ticks);
    uart_puts(" last intid="); uart_dec(irq_last); uart_puts("\n");

    /* 关定时器、重新 mask IRQ，再关机 */
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(0UL));
    __asm__ volatile("msr daifset, #2");

    uart_puts("[M1d] poweroff\n");
    psci_off();
    return EFI_SUCCESS;
}
