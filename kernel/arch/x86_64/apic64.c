/* ============================================================================
 * 赫普OS 64位 - APIC高级可编程中断控制器
 * 支持本地APIC、I/O APIC、中断路由
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* I/O端口读写 */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);

/* APIC基地址（使用低规范地址0xF0000000，避免PML4第511项冲突） */
#define APIC_BASE_ADDR      0xF0000000  /* Local APIC虚拟地址 */
#define APIC_PHYS_ADDR      0xFEE00000  /* Local APIC物理地址 */

/* 外部函数 */
extern int vmm64_map_page(uint64_t pml4_addr, uint64_t virt, uint64_t phys, uint64_t flags);
extern uint64_t vmm64_get_pml4(void);

#define PAGE_PRESENT    0x1
#define PAGE_WRITABLE   0x2

/* APIC寄存器偏移 */
#define APIC_ID             0x020  /* ID寄存器 */
#define APIC_VERSION        0x030  /* 版本寄存器 */
#define APIC_TPR            0x080  /* 任务优先级寄存器 */
#define APIC_EOI            0x0B0  /* 中断结束寄存器 */
#define APIC_LDR            0x0D0  /* 逻辑目标寄存器 */
#define APIC_DFR            0x0E0  /* 目标格式寄存器 */
#define APIC_SVR            0x0F0  /* 假中断向量寄存器 */
#define APIC_ISR_BASE       0x100  /* 中断服务寄存器基址 */
#define APIC_TMR_BASE       0x180  /* 触发模式寄存器基址 */
#define APIC_IRR_BASE       0x200  /* 中断请求寄存器基址 */
#define APIC_ESR            0x280  /* 错误状态寄存器 */
#define APIC_ICR_LOW        0x300  /* 中断命令寄存器低32位 */
#define APIC_ICR_HIGH       0x310  /* 中断命令寄存器高32位 */
#define APIC_LVT_TIMER      0x320  /* LVT定时器寄存器 */
#define APIC_LVT_THERMAL    0x330  /* LVT温度传感器寄存器 */
#define APIC_LVT_PERF       0x340  /* LVT性能监控寄存器 */
#define APIC_LVT_LINT0      0x350  /* LVT LINT0寄存器 */
#define APIC_LVT_LINT1      0x360  /* LVT LINT1寄存器 */
#define APIC_LVT_ERROR      0x370  /* LVT错误寄存器 */
#define APIC_TIMER_ICR      0x380  /* 定时器初始计数寄存器 */
#define APIC_TIMER_CCR      0x390  /* 定时器当前计数寄存器 */
#define APIC_TIMER_DCR      0x3E0  /* 定时器分频配置寄存器 */

/* I/O APIC基地址（低规范地址，紧跟APIC之后） */
#define IOAPIC_BASE_ADDR    0xF0001000  /* I/O APIC虚拟地址 */
#define IOAPIC_PHYS_ADDR    0xFEC00000  /* I/O APIC物理地址 */

/* I/O APIC寄存器 */
#define IOAPIC_REGSEL       0x00   /* 寄存器选择 */
#define IOAPIC_WINDOW       0x10   /* 数据窗口 */
#define IOAPIC_ID           0x00   /* ID寄存器 */
#define IOAPIC_VER          0x01   /* 版本寄存器 */
#define IOAPIC_ARB          0x02   /* 仲裁ID寄存器 */
#define IOAPIC_REDTBL_BASE  0x10   /* 重定向表基址 */

/* APIC寄存器读写 */
static inline uint32_t apic_read(uint32_t reg)
{
    return *(volatile uint32_t *)(APIC_BASE_ADDR + reg);
}

static inline void apic_write(uint32_t reg, uint32_t val)
{
    *(volatile uint32_t *)(APIC_BASE_ADDR + reg) = val;
}

/* I/O APIC寄存器读写 */
static inline uint32_t ioapic_read(uint32_t reg)
{
    *(volatile uint32_t *)(IOAPIC_BASE_ADDR + IOAPIC_REGSEL) = reg;
    return *(volatile uint32_t *)(IOAPIC_BASE_ADDR + IOAPIC_WINDOW);
}

static inline void ioapic_write(uint32_t reg, uint32_t val)
{
    *(volatile uint32_t *)(IOAPIC_BASE_ADDR + IOAPIC_REGSEL) = reg;
    *(volatile uint32_t *)(IOAPIC_BASE_ADDR + IOAPIC_WINDOW) = val;
}

/* MSR读写 */
static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val)
{
    uint32_t lo = (uint32_t)val;
    uint32_t hi = (uint32_t)(val >> 32);
    __asm__ volatile ("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

/* IA32_APIC_BASE MSR */
#define IA32_APIC_BASE_MSR  0x1B
#define APIC_BASE_ENABLE    (1 << 11)
#define APIC_BASE_BSP       (1 << 8)

/* 全局状态 */
static volatile int apic_initialized = 0;
static volatile int apic_mode = 0;  /* 0=PIC模式, 1=APIC模式（volatile：AP核需可见BSP的切换） */
static uint32_t apic_id = 0;
static uint32_t apic_version = 0;
static int ioapic_available = 0;
static uint32_t ioapic_version = 0;
static uint32_t ioapic_max_redir = 0;

/* 函数原型 */
int apic64_enable_irq(uint8_t irq, uint8_t vector);
int apic64_disable_irq(uint8_t irq);

/* 静态页表用于映射高地址设备（APIC/IOAPIC） */
static uint64_t apic_pdpt[512] __attribute__((aligned(4096)));
static uint64_t apic_pd[512] __attribute__((aligned(4096)));
static uint64_t apic_pt[512] __attribute__((aligned(4096)));
static uint64_t ioapic_pd[512] __attribute__((aligned(4096)));
static uint64_t ioapic_pt[512] __attribute__((aligned(4096)));

/* 直接映射高地址（使用静态页表，避免动态分配） */
static void apic_map_high_address(uint64_t virt, uint64_t phys)
{
    vga_puts("[MAP] start\n");
    /* 读取CR3获取PML4 */
    uint64_t pml4_addr;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(pml4_addr));
    uint64_t *pml4 = (uint64_t *)pml4_addr;
    vga_puts("[MAP] cr3 read\n");

    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx = (virt >> 21) & 0x1FF;
    uint64_t pt_idx = (virt >> 12) & 0x1FF;

    /* PML4 -> PDPT（不覆盖已有条目） */
    uint64_t *pdpt;
    if (pml4[pml4_idx] & 1) {
        pdpt = (uint64_t *)(pml4[pml4_idx] & ~0xFFF);
        vga_puts("[MAP] using existing PDPT\n");
    } else {
        pdpt = apic_pdpt;
        pml4[pml4_idx] = (uint64_t)pdpt | PAGE_PRESENT | PAGE_WRITABLE;
        vga_puts("[MAP] created new PDPT\n");
    }

    /* PDPT -> PD（不覆盖已有条目） */
    uint64_t *pd;
    vga_puts("[MAP] reading PDPT entry\n");
    if (pdpt[pdpt_idx] & 1) {
        pd = (uint64_t *)(pdpt[pdpt_idx] & ~0xFFF);
        vga_puts("[MAP] using existing PD\n");
    } else {
        pd = (virt == APIC_BASE_ADDR) ? apic_pd : ioapic_pd;
        pdpt[pdpt_idx] = (uint64_t)pd | PAGE_PRESENT | PAGE_WRITABLE;
        vga_puts("[MAP] created new PD\n");
    }

    /* PD -> PT（不覆盖已有条目） */
    uint64_t *pt;
    vga_puts("[MAP] reading PD entry\n");
    if (pd[pd_idx] & 1) {
        pt = (uint64_t *)(pd[pd_idx] & ~0xFFF);
        vga_puts("[MAP] using existing PT\n");
    } else {
        pt = (virt == APIC_BASE_ADDR) ? apic_pt : ioapic_pt;
        pd[pd_idx] = (uint64_t)pt | PAGE_PRESENT | PAGE_WRITABLE;
        vga_puts("[MAP] created new PT\n");
    }

    /* PT -> 物理页 */
    vga_puts("[MAP] setting PT entry\n");
    pt[pt_idx] = phys | PAGE_PRESENT | PAGE_WRITABLE;

    /* 刷新TLB */
    vga_puts("[MAP] flush TLB\n");
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    vga_puts("[MAP] done\n");
}

/* 发送EOI（中断结束） */
void apic64_send_eoi(void)
{
    apic_write(APIC_EOI, 0);
}

/* 初始化本地APIC - PIC兼容模式（LINT0保持ExtINT，不切换IMCR） */
void apic64_init(void)
{
    /* 映射APIC和IOAPIC地址
       MMIO必须标记为Uncacheable（PCD=bit4|PWT=bit3），否则AP核可能
       从缓存读到BSP的LAPIC ID值，导致CPU识别错误。 */
    uint64_t pml4 = vmm64_get_pml4();
    if (pml4) {
        vmm64_map_page(pml4, APIC_BASE_ADDR, APIC_PHYS_ADDR,
                       PAGE_PRESENT | PAGE_WRITABLE | (1 << 4) | (1 << 3));
        vmm64_map_page(pml4, IOAPIC_BASE_ADDR, IOAPIC_PHYS_ADDR,
                       PAGE_PRESENT | PAGE_WRITABLE | (1 << 4) | (1 << 3));
    }

    /* 读取APIC ID和版本 */
    apic_id = apic_read(APIC_ID) >> 24;
    apic_version = apic_read(APIC_VERSION) & 0xFF;
    vga_puts("[APIC64] Local APIC ID=");
    char buf[8];
    int n = apic_id, i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
    vga_puts(buf);
    vga_puts(" Ver=");
    vga_put_hex(apic_version);
    vga_puts("\n");

    /* 设置TPR=0（允许所有中断） */
    apic_write(APIC_TPR, 0);

    /* 设置SVR：启用APIC，spurious向量0xFF */
    apic_write(APIC_SVR, 0x1FF);

    /* 设置LVT错误：屏蔽（避免APIC内部错误触发中断） */
    apic_write(APIC_LVT_ERROR, 0x100FE);

    /* PIC兼容模式：LINT0保持默认ExtINT模式（PIC中断通道），不屏蔽
     * LINT1和TIMER屏蔽 */
    apic_write(APIC_LVT_LINT1, 0x10000);
    apic_write(APIC_LVT_TIMER, 0x10000);

    apic_initialized = 1;
    apic_mode = 0;  /* PIC模式 */
    ioapic_available = 0;
    vga_puts("[APIC64] Local APIC initialized (PIC compatible mode)\n");
}

/* 设置I/O APIC中断重定向 */
int apic64_set_irq_redirect(uint8_t irq, uint8_t vector, uint8_t delivery_mode,
                              uint8_t dest_mode, uint8_t polarity,
                              uint8_t trigger_mode, uint8_t dest_id)
{
    if (!ioapic_available) return -1;
    if (irq > ioapic_max_redir) return -1;

    uint32_t low = vector |
                   (delivery_mode << 8) |
                   (dest_mode << 11) |
                   (polarity << 13) |
                   (trigger_mode << 15);
    uint32_t high = dest_id << 24;

    ioapic_write(IOAPIC_REDTBL_BASE + irq * 2, low);
    ioapic_write(IOAPIC_REDTBL_BASE + irq * 2 + 1, high);

    return 0;
}

/* 启用IRQ */
int apic64_enable_irq(uint8_t irq, uint8_t vector)
{
    if (!ioapic_available) return -1;
    /* 固定投递模式、物理目标、高电平有效、边沿触发、目标CPU0 */
    return apic64_set_irq_redirect(irq, vector, 0, 0, 0, 0, apic_id);
}

/* 禁用IRQ */
int apic64_disable_irq(uint8_t irq)
{
    if (!ioapic_available) return -1;
    if (irq > ioapic_max_redir) return -1;

    uint32_t low = ioapic_read(IOAPIC_REDTBL_BASE + irq * 2);
    low |= 0x10000;  /* 设置屏蔽位 */
    ioapic_write(IOAPIC_REDTBL_BASE + irq * 2, low);

    return 0;
}

/* 获取APIC ID
   注意：必须直接读取LAPIC硬件寄存器，而不是返回全局变量，
   否则SMP下所有AP核都会读到BSP初始化时的0，误认为自己是CPU0。
   每个CPU读取本地LAPIC（0xF0000020）得到自己的真实ID。 */
uint32_t apic64_get_id(void)
{
    if (apic_mode) {
        return (*(volatile uint32_t *)(APIC_BASE_ADDR + APIC_ID) >> 24) & 0xFF;
    }
    return apic_id;  /* PIC模式下只有BSP */
}

/* 检查APIC是否已初始化 */
int apic64_is_initialized(void)
{
    return apic_initialized;
}

/* 检查I/O APIC是否可用 */
int apic64_ioapic_available(void)
{
    return ioapic_available;
}

/* 检查当前是否为APIC模式 */
int apic64_is_apic_mode(void)
{
    return apic_mode;
}

/* 通过IMCR切换中断模式（0=PIC, 1=APIC） */
static void imcr_set_mode(int apic_mode)
{
    /* IMCR寄存器选择 */
    outb(0x22, 0x00);
    /* 写入模式值 */
    outb(0x23, apic_mode ? 0x01 : 0x00);
}

/* 初始化I/O APIC */
static void ioapic_init(void)
{
    /* 读取I/O APIC ID */
    uint32_t ioapic_id = (ioapic_read(IOAPIC_ID) >> 24) & 0xFF;
    /* 读取版本 */
    ioapic_version = ioapic_read(IOAPIC_VER) & 0xFF;
    /* 最大重定向条目数 */
    ioapic_max_redir = (ioapic_read(IOAPIC_VER) >> 16) & 0xFF;

    vga_puts("[IOAPIC] ID=");
    char buf[8];
    int n = ioapic_id, i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
    buf[i] = 0;
    vga_puts(buf);
    vga_puts(" Ver=");
    vga_put_hex(ioapic_version);
    vga_puts(" MaxRedir=");
    n = ioapic_max_redir + 1; i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
    buf[i] = 0;
    vga_puts(buf);
    vga_puts("\n");

    /* 屏蔽所有中断（先全部屏蔽，再逐个启用） */
    for (int irq = 0; irq <= ioapic_max_redir; irq++) {
        ioapic_write(IOAPIC_REDTBL_BASE + irq * 2, 0x10000);  /* 屏蔽位 */
        ioapic_write(IOAPIC_REDTBL_BASE + irq * 2 + 1, 0);
    }

    ioapic_available = 1;
    vga_puts("[IOAPIC] Initialized\n");
}

/* 切换到纯APIC模式（IMCR + I/O APIC重定向） */
int apic64_switch_to_apic_mode(void)
{
    if (!apic_initialized) {
        vga_puts("[APIC] ERROR: Local APIC not initialized\n");
        return -1;
    }

    vga_puts("[APIC] Switching to pure APIC mode...\n");

    /* 1. 初始化I/O APIC */
    ioapic_init();
    if (!ioapic_available) {
        vga_puts("[APIC] ERROR: I/O APIC init failed\n");
        return -1;
    }

    /* 2. 屏蔽PIC（所有中断） */
    outb(0x21, 0xFF);  /* 主PIC全部屏蔽 */
    outb(0xA1, 0xFF);  /* 从PIC全部屏蔽 */

    /* 3. 通过IMCR切换到APIC模式 */
    imcr_set_mode(1);

    /* 4. 配置关键中断重定向。
       注意：enable_irq 的第一个参数是 IOAPIC 引脚（= GSI），不是 ISA IRQ。
       必须经 MADT 中断源覆盖翻译，否则在 QEMU 上 PIT(ISA IRQ0 被覆盖到
       GSI 2) 会被错误配到 pin 0，中断永远不投递，桌面渲染计时冻结。 */
    extern int acpi_irq_to_gsi(int irq);
    /* PIT -> vector 32 (0x20) */
    apic64_enable_irq((uint8_t)acpi_irq_to_gsi(0), 0x20);
    /* 键盘 -> vector 33 (0x21) */
    apic64_enable_irq((uint8_t)acpi_irq_to_gsi(1), 0x21);
    /* IRQ2: 级联（通常不需要） */
    /* PS/2鼠标 -> vector 44 (0x2C) */
    if (ioapic_max_redir >= 12) {
        apic64_enable_irq((uint8_t)acpi_irq_to_gsi(12), 0x2C);
    }

    /* 5. 设置LINT0为固定投递模式（不再使用ExtINT/PIC通道） */
    apic_write(APIC_LVT_LINT0, 0x10000);  /* 屏蔽LINT0 */

    apic_mode = 1;  /* APIC模式 */
    vga_puts("[APIC] Switched to pure APIC mode\n");
    return 0;
}

/* ============================================================================
 * IPI（处理器间中断）发送 - V0.10.142
 * ========================================================================== */

/* 发送INIT IPI到指定APIC ID */
void apic64_send_init(uint32_t apic_id)
{
    vga_puts("[APIC] send INIT to CPU ");
    vga_put_hex(apic_id);
    vga_puts("\n");

    /* 设置目标APIC ID */
    apic_write(APIC_ICR_HIGH, apic_id << 24);

    /* 发送INIT IPI：delivery mode=5(INIT), level=1(assert), trigger=0(edge) */
    apic_write(APIC_ICR_LOW, (5 << 8) | (1 << 14) | (1 << 15));

    vga_puts("[APIC] INIT sent\n");
}

/* 发送STARTUP IPI到指定APIC ID */
void apic64_send_startup(uint32_t apic_id, uint8_t vector)
{
    vga_puts("[APIC] send STARTUP vector=");
    vga_put_hex(vector);
    vga_puts(" to CPU ");
    vga_put_hex(apic_id);
    vga_puts("\n");

    /* 设置目标APIC ID */
    apic_write(APIC_ICR_HIGH, apic_id << 24);

    /* 发送STARTUP IPI：delivery mode=6(STARTUP), vector=页框号 */
    apic_write(APIC_ICR_LOW, (6 << 8) | vector);

    vga_puts("[APIC] STARTUP sent\n");
}

/* 发送通用IPI到指定APIC ID（固定投递模式） */
void apic64_send_ipi(uint32_t apic_id, uint8_t vector)
{
    /* 设置目标APIC ID */
    apic_write(APIC_ICR_HIGH, apic_id << 24);

    /* 发送固定投递IPI：delivery mode=0(Fixed), level=1(assert), trigger=0(edge) */
    apic_write(APIC_ICR_LOW, (0 << 8) | (1 << 14) | vector);
}

/* 发送IPI到所有CPU（包括自己，广播模式） */
void apic64_send_ipi_all(uint8_t vector)
{
    /* 目标字段：11 = 所有CPU（包括自己） */
    apic_write(APIC_ICR_HIGH, 0xFF << 24);

    /* 发送固定投递IPI */
    apic_write(APIC_ICR_LOW, (0 << 8) | (1 << 14) | (3 << 18) | vector);
}

/* 发送IPI到所有其他CPU（不包括自己） */
void apic64_send_ipi_others(uint8_t vector)
{
    /* 目标字段：10 = 所有其他CPU */
    apic_write(APIC_ICR_HIGH, 0xFF << 24);

    /* 发送固定投递IPI，destination shorthand=2(others) */
    apic_write(APIC_ICR_LOW, (0 << 8) | (1 << 14) | (2 << 18) | vector);
}

/* APIC定时器向量 */
#define APIC_TIMER_VECTOR 0x40

/* 为当前AP核启用其本地APIC（SVR的软件使能位是per-CPU的，BSP的
   apic64_init() 不会启用AP核的LAPIC）。AP核在启动定时器前必须调用，
   否则LAPIC定时器不投递中断。 */
void apic64_enable_for_ap(void)
{
    /* 设置TPR=0（允许所有优先级中断） */
    apic_write(APIC_TPR, 0);

    /* 设置SVR：bit8=1 启用本地APIC，spurious向量0xFF */
    apic_write(APIC_SVR, 0x1FF);

    /* LVT错误屏蔽，避免APIC内部错误触发异常 */
    apic_write(APIC_LVT_ERROR, 0x100FE);

    /* 清掉可能存在的错误状态（写ESR再读） */
    apic_write(0x280, 0);
    (void)apic_read(0x280);
}

/* 初始化当前CPU的Local APIC定时器（周期模式，约1000Hz） */
void apic64_init_timer(void)
{
    /* 设置分频配置：16分频 */
    apic_write(APIC_TIMER_DCR, 0x3);

    /* 设置LVT定时器：周期模式，向量0x40，不屏蔽 */
    apic_write(APIC_LVT_TIMER, (1 << 17) | APIC_TIMER_VECTOR);

    /* 设置初始计数（QEMU中约100MHz总线，16分频后6.25MHz，1000Hz需要6250计数） */
    apic_write(APIC_TIMER_ICR, 6250);
}

/* APIC定时器中断处理（发送EOI） */
void apic64_timer_eoi(void)
{
    apic_write(APIC_EOI, 0);
}
