/* Hepu OS ARM64 M1 —— 用自己的页表与异常向量接管（M1a）
 *
 * 目标：EFI 应用里确认 CurrentEL，建 identity map（2GB，1GB block），
 * 切 TTBR0_EL1 到自己的表，设 VBAR_EL1，然后验证串口仍工作。
 * 这一版不 ExitBootServices（保住固件服务，便于出错时还有控制台）。
 */
#include <efi.h>
#include <efilib.h>

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
    uart_putc('0'); uart_putc('x');
    for (int i = 60; i >= 0; i -= 4) uart_putc(h[(v >> i) & 0xF]);
}
static void uart_dec(unsigned long v)
{
    char b[24]; int n = 0;
    if (!v) { uart_putc('0'); return; }
    while (v && n < 23) { b[n++] = (char)('0' + v % 10); v /= 10; }
    while (n--) uart_putc(b[n]);
}

/* ---------------- 页表 ---------------- */
/* 1GB block 描述符（L1 级）：
 * bit0=1(valid) bit1=0(block@L1) bit10=AF AttrIdx(bits4:2) SH(bits9:8) */
#define DESC_BLOCK_DEVICE  (1UL | (1UL << 10) | (0UL << 2))            /* AttrIdx 0 = Device nGnRnE */
#define DESC_BLOCK_NORMAL  (1UL | (1UL << 10) | (1UL << 2))            /* AttrIdx 1 = Normal WB */

/* 39 位 VA 布局：L0(512 项, 每项 512GB) -> L1(每项 1GB 区) -> L2(每项 2MB block)。
 * 与固件用的 T0SZ 保持一致，这样只换 TTBR0、TCR 沿用同一个 T0SZ 也不会
 * 在切换窗口里出现"新 TCR + 旧表"的错位翻译。 */
const uint64_t DESC_TABLE = 0x3;              /* valid + table */
const uint64_t DESC_BLOCK2M_DEV  = 0x1 | (1UL << 10) | (0UL << 2);   /* 2MB block, Attr0 Device */
const uint64_t DESC_BLOCK2M_NORM = 0x1 | (1UL << 10) | (1UL << 2);   /* 2MB block, Attr1 Normal */

uint64_t m1_l0[512] __attribute__((aligned(4096)));
uint64_t m1_l1[512] __attribute__((aligned(4096)));
uint64_t m1_l2[2][512] __attribute__((aligned(4096)));

static void mmu_build(void)
{
    /* L0[0] -> L1（覆盖 0..512GB 的入口） */
    m1_l0[0] = ((uint64_t)m1_l1) | DESC_TABLE;
    /* L1[0] -> L2[0]（0..1GB，设备）；L1[1] -> L2[1]（1..2GB，RAM） */
    m1_l1[0] = ((uint64_t)m1_l2[0]) | DESC_TABLE;
    m1_l1[1] = ((uint64_t)m1_l2[1]) | DESC_TABLE;
    /* L2：每项 2MB block，identity */
    for (int i = 0; i < 512; i++) {
        m1_l2[0][i] = ((uint64_t)(i * 0x200000UL)) | DESC_BLOCK2M_DEV;
        m1_l2[1][i] = (0x40000000UL + (uint64_t)(i * 0x200000UL)) | DESC_BLOCK2M_NORM;
    }
    /* UART(PL011 @0x09000000) 所在那一个 2MB block 单独给 Normal：
     * QEMU 的 PL011 在 Device-nGnRnE 下写入会被吞，表现为"切表后串口静音"。 */

}

static inline void mmu_enable_own(void)
{
    /* MAIR：Attr0=Device nGnRnE(0x00)、Attr1=Normal WB(0xFF) */
    uint64_t mair = 0x04FFUL;  /* Attr0=Device nGnRE(0x04)，nGnRnE 下 QEMU 的 PL011 写会被吞 */
    __asm__ volatile("msr mair_el1, %0" :: "r"(mair));
    uart_puts("[dbg] mair ok\n");

    /* TCR_EL1：T0SZ=31 -> 33 位 VA（0~8GB），这样 TTBR0 直接指向 L1，
     * L1 里的 1GB block 就是最终映射，不需要再建 L0 一级。
     * 之前用 T0SZ=25(39bit) 却只建了 L1，切表后 VA 翻译错位，串口立刻失联。 */
    /* 沿用固件的 48 位布局（T0SZ=16）：本机固件实测 TCR_EL1=0x580803510。
     * 改 TCR 的任何位都会影响紧接着的取指，与固件不一致就会当场失联；
     * 所以这里不重写 TCR，只换 TTBR0（新表按 48 位四级布局建）。 */
    uint64_t tcr = 0x580803510UL;
    __asm__ volatile("msr tcr_el1, %0" :: "r"(tcr));
    uart_puts("[dbg] tcr ok\n");

    /* 一级表地址 */
    uint64_t ttbr = (uint64_t)m1_l0;
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(ttbr));
    __asm__ volatile("isb");
    uart_puts("[dbg] ttbr ok\n");

    /* 打开 MMU（SCTLR_EL1.M=1），同时确保 C/I 开着 */
    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1UL << 0);   /* M */
    sctlr |= (1UL << 2);   /* C */
    sctlr |= (1UL << 12);  /* I */
    uart_puts("[dbg] sctlr before\n");
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(sctlr));
    uart_puts("[dbg] sctlr ok\n");
    uart_puts("[dbg] about to flush\n");
    __asm__ volatile("dsb sy");
    uart_puts("[dbg] dsb ok\n");
    /* 最小判据：tlbi 之后什么都不打印，直接 PSCI 关机。
     * QEMU 正常退出 => 系统活着，只是串口在切表后被吞；不退出 => 真的崩了。 */
    /* 固件 SCTLR_EL1 的 WXN(bit19)=1：可写页一律不可执行。新表的 Normal block 是
     * EL1 RW，代码就落在那里 —— 所以 TLB 刷新后取指会被判"执行禁止"。
     * 这里清掉 WXN 再做 tlbi，验证这个假设。 */
    {
        uint64_t s2;
        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(s2));
        s2 &= ~(1UL << 19);          /* WXN = 0 */
        __asm__ volatile("msr sctlr_el1, %0" :: "r"(s2));
        __asm__ volatile("isb");
    }
    uart_puts("[dbg] wxn cleared\n");
    __asm__ volatile("tlbi vmalle1");
    __asm__ volatile("dsb sy; isb");
    uart_puts("[dbg] tlbi ok\n");
    __asm__ volatile("mov w0, #0x8\n movk w0, #0x8400, lsl #16\n hvc #0" : : : "x0");
    uart_puts("[dbg] PSCI returned (should not happen)\n");
    {
        uint64_t pc; __asm__ volatile("adr %0, ." : "=r"(pc));
        uart_puts("[dbg] pc=0x"); uart_hex(pc); uart_puts("\n");
        uart_puts("[dbg] l0[0]=0x"); uart_hex(m1_l0[0]);
        uart_puts(" l1[1]=0x"); uart_hex(m1_l1[1]);
        unsigned long l2i = (pc >> 21) & 0x1FF;
        uart_puts(" l2i="); uart_dec(l2i);
        uart_puts(" l2blk=0x"); uart_hex(m1_l2[1][l2i]);
        uart_puts("\n");
    }
    __asm__ volatile("tlbi vmalle1");
    uart_puts("[dbg] tlbi ok\n");
    __asm__ volatile("dsb sy; isb");
    uart_puts("[dbg] dsb2+isb ok\n");
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
    uart_puts("\n[M1] CurrentEL = EL");
    uart_dec((el >> 2) & 0x3);
    uart_putc('\n');

    {   /* 先把固件当前的 MMU 配置读出来，决定新表要用什么布局 */
        uint64_t tcr0, ttbr, sctlr;
        __asm__ volatile("mrs %0, tcr_el1" : "=r"(tcr0));
        __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr));
        __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
        uart_puts("[fw] TCR_EL1=0x");  uart_hex(tcr0); uart_puts(" (T0SZ=");
        uart_dec(tcr0 & 0x3F); uart_puts(")\n");
        uart_puts("[fw] TTBR0_EL1=0x"); uart_hex(ttbr); uart_puts("\n");
        uart_puts("[fw] SCTLR_EL1=0x"); uart_hex(sctlr); uart_puts("\n");
    }

    mmu_build();
    uart_puts("[M1] page tables built (L0->L1->L2: 0-1GB Device, 1-2GB Normal)\n");

    mmu_enable_own();
    uart_puts("[M1] own page tables active (TTBR0_EL1 switched)\n");
    uart_puts("[M1] uart still alive over identity map\n");

    uefi_call_wrapper(st->BootServices->Stall, 1, 3000000);
    uart_puts("[M1] poweroff\n");
    psci_off();
    return EFI_SUCCESS;
}
