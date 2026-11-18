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
#define EXP_SKIP_IRQ    0   /* 1=诊断：跳过 GIC/定时器，EBS 直调，判断缺映射 vs 中断状态 */

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
"  stp x6, x7, [sp, #-16]!\n"
"  stp x8, x9, [sp, #-16]!\n"
"  stp x10, x11, [sp, #-16]!\n"
"  stp x12, x13, [sp, #-16]!\n"
"  stp x14, x15, [sp, #-16]!\n"
"  stp x16, x17, [sp, #-16]!\n"
"  stp x18, x19, [sp, #-16]!\n"
"  bl el1_irq_c\n"
"  ldp x18, x19, [sp], #16\n"
"  ldp x16, x17, [sp], #16\n"
"  ldp x14, x15, [sp], #16\n"
"  ldp x12, x13, [sp], #16\n"
"  ldp x10, x11, [sp], #16\n"
"  ldp x8, x9, [sp], #16\n"
"  ldp x6, x7, [sp], #16\n"
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
    uint32_t insn = *(volatile uint32_t *)elr;
    uart_puts("\n[EXC] insn@ELR=0x"); uart_hex(insn);
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

static void map_block_dev(uint64_t base)
{
    uint64_t l1 = (base >> 30) & 0x1FF;
    uint64_t l2i = (base >> 21) & 0x1FF;
    m1_l1[l1] = (uint64_t)m1_l2[l1] | TAB_DESC;
    m1_l2[l1][l2i] = base | BLOCK_DEV;
}

/* 整段 MMIO 按 2MB Device block 覆盖（EBS 内部会触碰 PCIe PIO/ECAM 等窗口） */
static void map_dev_range(uint64_t lo, uint64_t hi)
{
    lo &= ~0x1FFFFFULL;
    hi = (hi + 0x1FFFFFULL) & ~0x1FFFFFULL;
    for (uint64_t b = lo; b < hi; b += 0x200000UL)
        map_block_dev(b);
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
    /* virt MMIO 区间（GIC/UART/RTC/fwcfg/virtio-mmio/platform bus/PCIe MMIO+PIO+ECAM） */
    map_dev_range(0x04000000UL, 0x0A000000UL);
    map_dev_range(0x0A000000UL, 0x0C000000UL);
    map_dev_range(0x0C000000UL, 0x0E000000UL);
    map_dev_range(0x10000000UL, 0x40000000UL);
    /* 关键设备再补 4KB 页，确保属性明确 */
    map_page(0x08000000UL, PAGE_DEV);  /* GICD */
    map_page(0x08010000UL, PAGE_DEV);  /* GICC */
    map_page(0x09000000UL, PAGE_DEV);  /* PL011 UART */
}

/* GOP framebuffer 区间：退出前按 4KB 补齐映射。FB 走 Normal NC（AttrIdx2），
 * 避免 WB 脏行未清回内存导致 QEMU screendump 读到旧内容。 */
static uint64_t fb_base, fb_size;
static uint32_t fb_w, fb_h, fb_stride, fb_fmt;
#define PAGE_NORM_NC (0x3UL | (2UL << 2) | (3UL << 8) | (1UL << 10))

static void map_fb_region(uint64_t base, uint64_t size)
{
    if (!size) return;
    uint64_t lo = base & ~0xFFFULL;
    uint64_t hi = (base + size + 0xFFFULL) & ~0xFFFULL;
    for (uint64_t pa = lo; pa < hi; pa += 0x1000UL)
        map_page(pa, PAGE_NORM_NC);
    uart_puts("[M1d] FB mapped pages="); uart_dec((hi - lo) / 0x1000UL); uart_puts("\n");
}

/* ---------------- M2：最小 2D 绘制（8x8 字模，BGRX 32bpp） ----------------
 * fmt=1 = PixelBlueGreenRedReserved8BitPerColor：内存字节序 B,G,R,X；
 * 小端写 uint32 = 0x00RRGGBB 即得正确颜色。
 * 字模每位 = 一行一字节，bit7 在最左；只覆盖标题要用的字符。
 */
static const uint8_t font8[][8] = {
    /* 0 space */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 1 H */     {0x81,0x81,0x81,0xFF,0x81,0x81,0x81,0x00},
    /* 2 E */     {0xFF,0x80,0x80,0xFE,0x80,0x80,0xFF,0x00},
    /* 3 P */     {0xFE,0x81,0x81,0xFE,0x80,0x80,0x80,0x00},
    /* 4 U */     {0x81,0x81,0x81,0x81,0x81,0x81,0x7E,0x00},
    /* 5 A */     {0x7E,0x81,0x81,0xFF,0x81,0x81,0x81,0x00},
    /* 6 R */     {0xFE,0x81,0x81,0xFE,0x82,0x88,0x84,0x00},
    /* 7 M */     {0x81,0xC3,0xA5,0x99,0x81,0x81,0x81,0x00},
    /* 8 6 */     {0x3C,0x60,0x80,0xFE,0x81,0x81,0x7E,0x00},
    /* 9 4 */     {0x10,0x28,0x44,0x84,0xFE,0x10,0x10,0x00},
};

static int glyph_index(char c)
{
    switch (c) {
    case ' ': return 0;
    case 'H': return 1;
    case 'E': return 2;
    case 'P': return 3;
    case 'U': return 4;
    case 'A': return 5;
    case 'R': return 6;
    case 'M': return 7;
    case '6': return 8;
    case '4': return 9;
    default:  return 0;
    }
}

static volatile uint32_t *fb_at(uint32_t x, uint32_t y)
{
    return (volatile uint32_t *)(fb_base + ((uint64_t)y * fb_stride + x) * 4);
}

static void putpx(uint32_t x, uint32_t y, uint32_t color)
{
    if (x >= fb_w || y >= fb_h) return;
    *fb_at(x, y) = color;
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    for (uint32_t j = 0; j < h; j++) {
        uint32_t yy = y + j;
        if (yy >= fb_h) break;
        for (uint32_t i = 0; i < w; i++) {
            uint32_t xx = x + i;
            if (xx >= fb_w) break;
            *fb_at(xx, yy) = color;
        }
    }
}

/* 最小脏区提交：把裁剪后的矩形写进 FB，返回实际写入像素数。
 * 裁剪在区域级先算好（x/w、y/h 一次性 clamp），再按行写，避免逐像素判断时
 * x 越界按 stride 跨行串写到下一行。 */
static uint32_t present_region(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    if (x >= fb_w || y >= fb_h || w == 0 || h == 0) return 0;
    if (x + w > fb_w) w = fb_w - x;
    if (y + h > fb_h) h = fb_h - y;
    uint32_t written = 0;
    for (uint32_t j = 0; j < h; j++) {
        volatile uint32_t *row =
            (volatile uint32_t *)(fb_base + (uint64_t)(y + j) * fb_stride * 4);
        for (uint32_t i = 0; i < w; i++)
            row[x + i] = color;
        written += w;
    }
    return written;
}

static void draw_char(char ch, uint32_t x, uint32_t y, uint32_t scale,
                      uint32_t fg, uint32_t bg)
{
    const uint8_t *g = font8[glyph_index(ch)];
    for (uint32_t r = 0; r < 8; r++) {
        uint8_t row = g[r];
        for (uint32_t col = 0; col < 8; col++) {
            uint32_t on = (row >> (7 - col)) & 1;
            uint32_t c = on ? fg : bg;
            for (uint32_t sy = 0; sy < scale; sy++)
                for (uint32_t sx = 0; sx < scale; sx++)
                    putpx(x + col * scale + sx, y + r * scale + sy, c);
        }
    }
}

/* 文本宽（含字间 gap，末字不加） */
static uint32_t text_width(const char *s, uint32_t scale, uint32_t gap)
{
    uint32_t w = 0;
    for (; *s; s++) w += 8 * scale + gap;
    return w ? w - gap : 0;
}

static void draw_text_center(const char *s, uint32_t scale, uint32_t gap,
                             uint32_t fg, uint32_t bg)
{
    uint32_t tw = text_width(s, scale, gap);
    uint32_t x = fb_w > tw ? (fb_w - tw) / 2 : 0;
    uint32_t y = fb_h > 8 * scale ? (fb_h - 8 * scale) / 2 : 0;
    for (; *s; s++) {
        draw_char(*s, x, y, scale, fg, bg);
        x += 8 * scale + gap;
    }
}

static uint64_t cntv_freq;   /* 定时器频率（gic_timer_init 里读 CNTFRQ_EL0） */

static uint64_t cntvct(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
}
static uint64_t ms_elapsed(uint64_t t0)
{
    return (cntvct() - t0) * 1000 / cntv_freq;
}
static uint64_t us_elapsed(uint64_t t0)
{
    return (cntvct() - t0) * 1000000 / cntv_freq;
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
static uint32_t gic_ver;
static volatile int timer_armed;   /* 我们自己 arm 定时器后置 1；否则 handler 不重 arm */

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
    if (timer_armed) {
        /* 重 arm 虚拟定时器：TVVAL 是有符号偏移，正值=未来比较值（负值=已过期，会风暴） */
        __asm__ volatile("msr cntv_tval_el0, %0" :: "r"(cntv_freq / 10));
    } else {
        /* 固件遗留的 pending 定时器：直接关掉，不重 arm，避免风暴 */
        __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(0UL));
    }
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
    timer_armed = 1;
    uart_puts("[M1d] timer armed, unmask IRQ\n");
    __asm__ volatile("msr daifclr, #2");   /* 清 DAIF.IRQ 掩码 */
}

/* ---------------- M1d-3：自有 bss 栈 + ExitBootServices ---------------- */
#define NEW_STACK_SIZE (16UL * 1024UL)
static uint8_t new_stack[NEW_STACK_SIZE] __attribute__((aligned(16)));

void stack_probe(void) __attribute__((used));
uint64_t stack_switch_asm(uint64_t new_top);

/* 入参 x0=new_top；切到新栈调 stack_probe，再切回旧栈，旧 sp 经 x0 返回。
 * x19/x20/x30 在旧栈显式保存恢复；不依赖跨 bl 存活的调用者保存寄存器。 */
__asm__ (
".align 2\n"
".global stack_switch_asm\n"
"stack_switch_asm:\n"
"  stp x19, x20, [sp, #-16]!\n"
"  mov x19, sp\n"
"  mov x20, x30\n"
"  mov sp, x0\n"
"  bl stack_probe\n"
"  mov sp, x19\n"
"  mov x0, x19\n"
"  mov x30, x20\n"
"  ldp x19, x20, [sp], #16\n"
"  ret\n"
);

/* 注意：只被内联汇编 bl 引用，必须 used 防止链接器回收，否则分支落空 PLT
 * （同 M1c GOT 不落位坑）。 */
void __attribute__((used)) stack_probe(void)
{
    volatile uint64_t local = 0x1122334455667788UL;
    uint64_t spv;
    __asm__ volatile("mov %0, sp" : "=r"(spv));
    uart_puts("[M1d] on new stack: local=0x"); uart_hex(local);
    uart_puts(" &local=0x"); uart_hex((uint64_t)&local);
    uart_puts(" sp=0x"); uart_hex(spv); uart_puts("\n");
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
    __asm__ volatile("msr daifset, #2");   /* 先 mask IRQ，防止固件遗留定时器 pending 冲进新向量 */
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(0UL));  /* 先关固件遗留的虚拟定时器 */
    uart_puts("[M1d] vbar installed\n");

    /* MAIR：Attr0=Device nGnRE(0x04)，Attr1=Normal WB(0xFF)，Attr2=Normal NC(0x44) */
    uint64_t mair = 0x04FFUL | (0x44UL << 16);
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

    /* GOP 探测：ExitBootServices 之前取 framebuffer 基址/大小并补映射 */
    {
        EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
        EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
        EFI_STATUS gs = uefi_call_wrapper(st->BootServices->LocateProtocol, 3,
                &gop_guid, NULL, (void **)&gop);
        if (!EFI_ERROR(gs) && gop && gop->Mode && gop->Mode->Info) {
            fb_base = gop->Mode->FrameBufferBase;
            fb_size = gop->Mode->FrameBufferSize;
            fb_w = gop->Mode->Info->HorizontalResolution;
            fb_h = gop->Mode->Info->VerticalResolution;
            fb_fmt = gop->Mode->Info->PixelFormat;
            fb_stride = gop->Mode->Info->PixelsPerScanLine;
            uart_puts("[M1d] GOP ");
            uart_dec(fb_w); uart_putc('x');
            uart_dec(fb_h);
            uart_puts(" fmt="); uart_dec(fb_fmt);
            uart_puts(" stride="); uart_dec(fb_stride);
            uart_puts("\n[M1d] FB base=0x"); uart_hex(fb_base);
            uart_puts(" size="); uart_dec(fb_size); uart_puts("\n");
            map_fb_region(fb_base, fb_size);
        } else {
            uart_puts("[M1d] no GOP, Status="); uart_dec(gs); uart_puts("\n");
        }
    }

#if !EXP_SKIP_IRQ
    gic_timer_init();
    uint64_t main_ticks = 0;
    while (irq_count < 20) main_ticks++;
    uart_puts("[M1d] IRQ count="); uart_dec(irq_count);
    uart_puts(" main_ticks="); uart_dec(main_ticks);
    uart_puts(" last intid="); uart_dec(irq_last); uart_puts("\n");

    /* 关定时器、重新 mask IRQ，再关机 */
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(0UL));
    __asm__ volatile("msr daifset, #2");
#else
    uart_puts("[diag] IRQ section skipped\n");
#endif

    /* ---- M1d-3：ExitBootServices + 自有栈 + FB 写读回 ---- */
    {
        EFI_STATUS ebs;
        int tries = 0;
        uart_puts("[M1d] about to call ExitBootServices, MapKey=0x");
        uart_hex(map_key); uart_puts("\n");
        while (tries < 5) {
            uart_puts("[M1d] EBS attempt "); uart_dec(tries + 1); uart_puts("\n");
            ebs = uefi_call_wrapper(st->BootServices->ExitBootServices, 2,
                                    image_handle, map_key);
            uart_puts("[M1d] ExitBootServices Status="); uart_dec(ebs); uart_puts("\n");
            if (ebs == EFI_SUCCESS) break;
            /* INVALID_PARAMETER(2) 多半是 MapKey 过期：重取 MemoryMap 再试 */
            if (ebs == EFI_INVALID_PARAMETER) {
                uart_puts("[M1d] MapKey stale, refetch MemoryMap\n");
                fetch_memory_map(st);
                tries++;
            } else {
                break;
            }
        }
        if (ebs != EFI_SUCCESS) {
            uart_puts("[M1d] EBS failed, halt\n");
            for (;;) __asm__ volatile("wfi");
        }
        uart_puts("[M1d] after ExitBootServices: bare PL011 only\n");

        /* 自有栈切换：打印切换前后 sp 与新栈范围，probe 局部变量须落在新栈内 */
        uint64_t old_sp, sp_before;
        __asm__ volatile("mov %0, sp" : "=r"(sp_before));
        uart_puts("[M1d] stack switch: old sp=0x"); uart_hex(sp_before);
        uart_puts(" new stack=0x"); uart_hex((uint64_t)new_stack);
        uart_puts("..0x"); uart_hex((uint64_t)new_stack + NEW_STACK_SIZE);
        uart_puts("\n");
        old_sp = stack_switch_asm((uint64_t)new_stack + NEW_STACK_SIZE);
        uart_puts("[M1d] returned to old sp=0x"); uart_hex(old_sp); uart_puts("\n");

        /* ---- M2：在已验证可写的 FB 上自绘最小界面 ---- */
        if (fb_base) {
            /* 颜色打包 0x00RRGGBB（小端内存序 B,G,R,X） */
            const uint32_t COL_BG     = 0x141822UL;  /* 深底 */
            const uint32_t COL_BAR    = 0x1F6FEBUL;  /* 顶部状态栏蓝 */
            const uint32_t COL_ACCENT = 0xFF6B35UL;  /* accent 橙 */
            const uint32_t COL_FG     = 0xFFFFFFUL;  /* 标题白 */
            const uint32_t BAR_H = 40;
            const uint32_t SCALE = 6, GAP = 8;

            uart_puts("[M2] FB @0x"); uart_hex(fb_base);
            uart_puts(" w="); uart_dec(fb_w);
            uart_puts(" h="); uart_dec(fb_h);
            uart_puts(" stride="); uart_dec(fb_stride);
            uart_puts(" fmt="); uart_dec(fb_fmt); uart_puts("\n");

            uint64_t t0 = cntvct();

            /* 1) 整屏底色 */
            fill_rect(0, 0, fb_w, fb_h, COL_BG);
            uart_puts("[M2] rect bg    x=0 y=0 w="); uart_dec(fb_w);
            uart_puts(" h="); uart_dec(fb_h);
            uart_puts(" color=0x141822\n");

            /* 2) 顶部状态栏横条 + accent 色块 */
            fill_rect(0, 0, fb_w, BAR_H, COL_BAR);
            uart_puts("[M2] rect bar   x=0 y=0 w="); uart_dec(fb_w);
            uart_puts(" h="); uart_dec(BAR_H);
            uart_puts(" color=0x1F6FEB\n");
            fill_rect(16, 8, 24, 24, COL_ACCENT);
            uart_puts("[M2] rect accent x=16 y=8 w=24 h=24 color=0xFF6B35\n");

            /* 3) 屏幕中央大号文字 HEPU ARM64（8x8 字模 ×6） */
            const char *title = "HEPU ARM64";
            draw_text_center(title, SCALE, GAP, COL_FG, COL_BG);
            uart_puts("[M2] text \""); uart_puts(title);
            uart_puts("\" scale="); uart_dec(SCALE);
            uart_puts(" gap="); uart_dec(GAP);
            uart_puts(" fg=0xFFFFFF bg=0x141822\n");

            uint64_t t1 = cntvct();
            uart_puts("[M2] draw time="); uart_dec((t1 - t0) * 1000 / cntv_freq);
            uart_puts(" ms\n");

            /* 抽两个像素读回校验：accent 中心 + 标题区域不做强假设 */
            uint32_t a = *fb_at(28, 20);
            uart_puts("[M2] readback accent@(28,20)=0x"); uart_hex(a);
            uart_puts(a == COL_ACCENT ? " OK\n" : " MISMATCH\n");

            /* ---- M2S：变尺寸矩形扫描，拟合「固定开销 + 每像素成本」 ----
             * 尺寸数组驱动：24/48/96/192 在下方无遮挡带左右反弹（旧位填底色 + 新位画块），
             * 800x600 是整屏端点（present_region 全屏，dirty==全屏）。
             * px = 旧块写入 + 新块写入 − 重叠（实测裁剪后像素，不是 s²）。 */
            const uint32_t SIZES[5] = {24, 48, 96, 192, 800};
            const int32_t  STEP = 8;
            const uint32_t NFRAMES = 80;              /* 每尺寸测量帧数 */
            const uint32_t WARM = 5;                  /* 前 5 帧预热不计入平均 */
            const uint32_t NFILL = 7, FILL_WARM = 2;   /* 全屏/端点填充次数与预热 */
            const uint64_t FRAME_TICKS = cntv_freq * 24 / 1000;  /* 24ms/帧节拍 */
            const uint32_t TITLE_BOTTOM = 324;        /* 标题带下沿，方块不得侵入 */

            /* EBS 后重新使能 GIC；扫描窗口内 IRQ 只计数、不打印（避免 PL011 轮询污染耗时） */
            uint64_t irq0 = irq_count;
            mmio_write32(GICD_BASE + 0x00, 0x01);
            mmio_write32(GICD_BASE + GICD_ISENABLER0, 1u << PPI_VTIMER);
            mmio_write32(GICC_BASE + GICC_PMR, 0xFF);
            mmio_write32(GICC_BASE + GICC_CTLR, 0x01);
            __asm__ volatile("msr cntv_tval_el0, %0" :: "r"(cntv_freq / 10));
            __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(1UL));
            __asm__ volatile("isb");
            timer_armed = 1;
            __asm__ volatile("msr daifclr, #2");

            uart_puts("[M2S] sweep start, frames/size="); uart_dec(NFRAMES);
            uart_puts(" warm="); uart_dec(WARM);
            uart_puts(" frame_ticks="); uart_dec(FRAME_TICKS);
            uart_puts("\n");

            for (uint32_t si = 0; si < 5; si++) {
                uint32_t s = SIZES[si];
                int is_full = (s == fb_w);             /* 800x600 整屏端点 */

                /* full_us：同一套 present_region 路径整屏填 NFILL 次、去预热取均值。
                 * 计时段临时屏蔽 IRQ，避免定时器中断抢进把某一次填充抬高；pending IRQ
                 * 解封后仍会被计数。 */
                uint64_t fu_sum = 0;
                for (uint32_t k = 0; k < NFILL; k++) {
                    __asm__ volatile("msr daifset, #2");
                    uint64_t ft = cntvct();
                    present_region(0, 0, fb_w, fb_h, COL_BG);
                    uint64_t fu = us_elapsed(ft);
                    __asm__ volatile("msr daifclr, #2");
                    if (k >= FILL_WARM) fu_sum += fu;
                }
                uint64_t full_us = fu_sum / (NFILL - FILL_WARM);

                uint64_t px_sum = 0, du_sum = 0;
                uint32_t n = 0;

                if (is_full) {
                    /* 上端点：全屏 present_region，dirty == 全屏；同样多次取均值 */
                    uint32_t w = 0;
                    for (uint32_t k = 0; k < NFILL; k++) {
                        __asm__ volatile("msr daifset, #2");
                        uint64_t ds = cntvct();
                        w = present_region(0, 0, fb_w, fb_h, COL_BG);
                        uint64_t du = us_elapsed(ds);
                        __asm__ volatile("msr daifclr, #2");
                        if (k >= FILL_WARM) du_sum += du;
                    }
                    px_sum = w; du_sum /= (NFILL - FILL_WARM); n = 1;
                } else {
                    /* 垂直放在标题下方的下半区，整块不被底边裁剪（裁剪会让 px/成本失真） */
                    uint32_t y = (TITLE_BOTTOM + fb_h) / 2 - s / 2;
                    if (y < TITLE_BOTTOM + 16) y = TITLE_BOTTOM + 16;
                    int32_t bx = 0, dir = 1;
                    for (uint32_t f = 0; f < NFRAMES; f++) {
                        uint64_t fs = cntvct();
                        int32_t nx = bx + dir * STEP;
                        if (nx < 0) { nx = 0; dir = 1; }
                        if (nx > (int32_t)fb_w - (int32_t)s) { nx = fb_w - s; dir = -1; }

                        /* 同行同尺寸：重叠宽 = x 区间交叠，高恒为 s；重叠像素不重复计 */
                        int32_t lo = bx > nx ? bx : nx;
                        int32_t hi = (bx + (int32_t)s) < (nx + (int32_t)s)
                                   ? (bx + (int32_t)s) : (nx + (int32_t)s);
                        uint32_t overlap = (hi > lo ? (uint32_t)(hi - lo) : 0) * s;

                        /* 计时只包两次 present_region，不含节拍等待；段内屏蔽 IRQ 防抢占 */
                        __asm__ volatile("msr daifset, #2");
                        uint64_t ds = cntvct();
                        uint32_t wold = present_region((uint32_t)bx, y, s, s, COL_BG);
                        uint32_t wnew = present_region((uint32_t)nx, y, s, s, COL_ACCENT);
                        uint64_t du = us_elapsed(ds);
                        __asm__ volatile("msr daifclr, #2");
                        uint32_t dirty = wold + wnew - overlap;

                        bx = nx;
                        if (f >= WARM) { px_sum += dirty; du_sum += du; n++; }

                        uint64_t next = fs + FRAME_TICKS;
                        while (cntvct() < next) { }
                    }
                    present_region((uint32_t)bx, y, s, s, COL_BG);  /* 收尾清掉末帧块 */
                }

                uint64_t avg_px = n ? px_sum / n : 0;
                uint64_t avg_du = n ? du_sum / n : 0;
                uint64_t ns_px = avg_px ? avg_du * 1000 / avg_px : 0;
                uint64_t ratio_t = avg_du ? full_us * 10 / avg_du : 0;  /* 一位小数 ×10 */

                uart_puts("[M2S] size="); uart_dec(s);
                uart_puts("x"); uart_dec(is_full ? fb_h : s);
                uart_puts(" px="); uart_dec(avg_px);
                uart_puts(" full_us="); uart_dec(full_us);
                uart_puts(" dirty_us="); uart_dec(avg_du);
                uart_puts(" ns_per_px="); uart_dec(ns_px);
                uart_puts(" ratio="); uart_dec(ratio_t / 10);
                uart_putc('.'); uart_dec(ratio_t % 10);
                uart_puts("\n");
            }

            /* ---- M2S 形状实验：同 ≈38400px、不同行数，验证「每行开销」 ----
             * 400x96=96 行；96x400=400 行。同一个 present_region，50 帧去 5 预热。
             * 96x400 放在 (350,120) 会临时压住标题区，测完统一重绘静态场景。 */
            struct { uint32_t w, h, x, y; } SHAPES[2] = {
                {400, 96, 200, 450},
                {96, 400, 350, 120},
            };
            for (uint32_t si = 0; si < 2; si++) {
                uint32_t sw = SHAPES[si].w, sh = SHAPES[si].h;
                uint32_t sx = SHAPES[si].x, sy = SHAPES[si].y;
                uint64_t sdu = 0; uint32_t sn = 0;
                for (uint32_t f = 0; f < 50; f++) {
                    __asm__ volatile("msr daifset, #2");
                    uint64_t ds = cntvct();
                    present_region(sx, sy, sw, sh, COL_ACCENT);
                    uint64_t du = us_elapsed(ds);
                    __asm__ volatile("msr daifclr, #2");
                    if (f >= WARM) { sdu += du; sn++; }
                }
                uint64_t avg = sn ? sdu / sn : 0;
                uint64_t px = (uint64_t)sw * sh;
                uint64_t nsp = px ? avg * 1000 / px : 0;
                uart_puts("[M2S] shape="); uart_dec(sw);
                uart_puts("x"); uart_dec(sh);
                uart_puts(" px="); uart_dec(px);
                uart_puts(" rows="); uart_dec(sh);
                uart_puts(" dirty_us="); uart_dec(avg);
                uart_puts(" ns_per_px="); uart_dec(nsp);
                uart_puts("\n");
            }

            __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(0UL));
            __asm__ volatile("msr daifset, #2");

            /* 扫描把静态场景抹掉了，重绘底色/栏/块/标题，再保持几秒后关机 */
            fill_rect(0, 0, fb_w, fb_h, COL_BG);
            fill_rect(0, 0, fb_w, BAR_H, COL_BAR);
            present_region(16, 8, 24, 24, COL_ACCENT);
            draw_text_center(title, SCALE, GAP, COL_FG, COL_BG);

            uint64_t hold0 = cntvct();
            while (ms_elapsed(hold0) < 3000) { }

            uart_puts("[M2S] held "); uart_dec(ms_elapsed(hold0));
            uart_puts(" ms, IRQ total="); uart_dec(irq_count);
            uart_puts(" (sweep start="); uart_dec(irq_count - irq0);
            uart_puts(") vct="); uart_hex(cntvct()); uart_puts("\n");
        } else {
            uart_puts("[M2] no GOP, nothing to draw\n");
        }
    }

    uart_puts("[M2] poweroff\n");
    psci_off();
    return EFI_SUCCESS;
}
