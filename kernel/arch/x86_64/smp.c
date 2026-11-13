/* ============================================================================
 * 赫普OS - 64位多处理器支持（SMP）实现
 * V0.10.142: ACPI MADT解析 + AP启动框架
 * ========================================================================== */

#include "smp.h"
#include "acpi.h"
#include "ap_trampoline_data.h"

/* 外部函数 */
extern void vga_puts(const char *str);
extern void vga_put_hex(uint64_t val);
extern uint32_t apic64_get_id(void);
extern void apic64_send_init(uint32_t apic_id);
extern void apic64_send_startup(uint32_t apic_id, uint8_t vector);
extern void *memcpy(void *dst, const void *src, unsigned long n);
extern void *memset(void *dst, int c, unsigned long n);

/* AP trampoline符号（预编译二进制，在ap_trampoline_data.h中定义） */

/* 全局CPU信息表 */
static cpu_info_t cpu_table[MAX_CPUS];
static uint32_t cpu_count = 0;
static uint32_t bsp_cpu_id = 0;
static volatile int ap_started_count = 0;

/* BSP完成IDT填充/加载后置1，AP核此前自旋等待（不启用中断）。
   由 irq64_init() 结束时设置。 */
volatile int g_apic_idt_ready = 0;

/* Per-CPU调度状态 */
static volatile int per_cpu_current_proc[MAX_CPUS];  /* 每个CPU当前运行的进程索引 */
static volatile uint64_t per_cpu_timer_ticks[MAX_CPUS];  /* 每个CPU的定时器tick计数 */

/* AP启动地址（低内存） */
#define AP_TRAMPOLINE_ADDR 0x8000

/* CPUID指令封装 */
static inline void cpuid(uint32_t eax, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    __asm__ volatile(
        "cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(eax)
        : "memory"
    );
}

/* 检测CPU核心数量（CPUID方式，作为后备） */
static uint32_t detect_cpu_count_cpuid(void)
{
    uint32_t eax, ebx, ecx, edx;
    uint32_t max_leaf = 0;
    uint32_t cores = 1;

    cpuid(0, &max_leaf, &ebx, &ecx, &edx);

    if (max_leaf >= 1) {
        cpuid(1, &eax, &ebx, &ecx, &edx);
        cores = (ebx >> 16) & 0xFF;
        if (cores == 0) cores = 1;
    }

    if (cores > MAX_CPUS) cores = MAX_CPUS;
    return cores;
}

/* AP C入口函数 */

/* ---------------------------------------------------------------------------
 * SB-18: AP 侧的本核串口输出
 *
 * 用独立实现而不是复用 lib64.c 的 serial_puts_local —— 那一个是 static,
 * 而且它跟 vga 输出共用一个上层锁; AP 早期还在初始化阶段, 直接打端口最省事。
 * ------------------------------------------------------------------------- */
static inline uint8_t sb18_inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void sb18_outb(uint16_t port, uint8_t v)
{
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static void sb18_serial_putc(char c)
{
    while ((sb18_inb(0x3FD) & 0x20) == 0) { __asm__ volatile("pause"); }
    sb18_outb(0x3F8, (uint8_t)c);
}

static void sb18_serial_puts(const char *str)
{
    while (*str) {
        if (*str == '\n') sb18_serial_putc('\r');
        sb18_serial_putc(*str++);
    }
}

static void sb18_serial_hex(uint64_t v)
{
    sb18_serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        int d = (int)((v >> i) & 0xF);
        sb18_serial_putc((char)(d < 10 ? ('0' + d) : ('a' + d - 10)));
    }
}

/* 读当前栈指针 */
static inline uint64_t sb18_rsp(void)
{
    uint64_t r;
    __asm__ volatile("movq %%rsp, %0" : "=r"(r));
    return r;
}

void ap_c_entry(void)
{
    /* 先设置标记，确认进入C入口 */
    volatile uint32_t *mark11 = (volatile uint32_t *)0x9128;
    *mark11 = 0xB;

    /* 启用FPU/SSE（必须在任何浮点运算之前） */
    extern void x86_64_enable_fpu(void);
    x86_64_enable_fpu();

    /* 获取APIC ID（此时apic_mode已在BSP启动AP前置1，可正确读到本核ID） */
    uint32_t cpu_id = apic64_get_id();

    /* 初始化per-CPU状态（不依赖中断） */
    if (cpu_id < MAX_CPUS) {
        per_cpu_current_proc[cpu_id] = 0;  /* idle进程 */
        per_cpu_timer_ticks[cpu_id] = 0;
        cpu_table[cpu_id].online = 1;
    }

    /* 到达安全点：中断保持关闭。ap_started_count 递增是BSP的同步点，
       表示本核已完成FPU/定时器相关初始化。 */
    ap_started_count++;

    /* (1) AP核必须切换回内核GDT: trampoline 的 ap_gdt 中 0x08 是32位代码段,
     *     而 IDT 所有中断门用 selector 0x08, 若 AP 保持 ap_gdt, 首个中断
     *     (定时器 0x40) 经 IDT 跳转时会以 0x08 段进入长模式触发 #GP(8)
     *     → #DF → triple fault。切换后 CS=0x08 与 BSP 一致, 中断正常。
     *
     * (2) SB-18: 再加载本核自己的 TSS。
     *     全核原本共用一份 kernel_tss64, rsp0 指向 BSP 的 64KB 内核栈。
     *     AP 现在只在内核态收中断(不切栈)所以还没出事, 但一旦 AP 上有用户态
     *     进程, 它的中断会用 tss.rsp0 切栈 —— 两个核写同一份栈就踩。
     *     tss64_init() 已为每个核填好 TSS 与 GDT 描述符(核 i 选择子 = 0x28+i*16)。 */
    {
        extern uint16_t kernel_gdt64_ptr[];
        extern void gdt64_load_asm(uint64_t gdt_ptr_addr);
        extern uint16_t gdt64_tss_selector_for_cpu(int cpu);
        extern uint64_t gdt64_kstack_top_for_cpu(int cpu);

        gdt64_load_asm((uint64_t)kernel_gdt64_ptr);

        uint16_t tss_sel = gdt64_tss_selector_for_cpu((int)cpu_id);
        __asm__ volatile("ltr %%ax" : : "a"(tss_sel));

        sb18_serial_puts("[SB18] cpu=");
        sb18_serial_hex(cpu_id);
        sb18_serial_puts(" tss_sel=");
        sb18_serial_hex(tss_sel & 0xFFFF);
        sb18_serial_puts(" rsp0=");
        sb18_serial_hex(gdt64_kstack_top_for_cpu((int)cpu_id));
        sb18_serial_puts(" rsp_now=");
        sb18_serial_hex(sb18_rsp());
        sb18_serial_puts("\n");
    }

    /* (3) SB-17: 给本核装一份独立的 syscall per-CPU 存储。
     *     之前只有 BSP 写过 MSR_KERNEL_GS_BASE, AP 的 GS 基址是 0 ——
     *     AP 一旦进 syscall, swapgs 之后 %gs:0x00/0x08 落不到自己的栈槽,
     *     会和 BSP 共用同一份。这里每核写自己的 MSR。 */
    {
        extern void syscall64_init_for_cpu(int cpu, uint64_t kstack_top);
        extern uint64_t gdt64_kstack_top_for_cpu(int cpu);

        uint64_t ktop = gdt64_kstack_top_for_cpu((int)cpu_id);
        syscall64_init_for_cpu((int)cpu_id, ktop);

        extern void sb18_serial_puts(const char *s);
        extern void sb18_serial_hex(uint64_t v);
        extern uint64_t x86_64_read_msr(uint32_t msr);
        sb18_serial_puts("[SB17] cpu=");
        sb18_serial_hex(cpu_id);
        sb18_serial_puts(" gs_percpu=");
        sb18_serial_hex(x86_64_read_msr(0xC0000102));   /* IA32_KERNEL_GS_BASE */
        sb18_serial_puts("\n");
    }

    /* 等待BSP完成IDT填充/加载（irq64_init 结束时置位） */
    while (!g_apic_idt_ready) {
        __asm__ volatile("pause" ::: "memory");
    }

    /* 为AP核加载IDT（IDTR是per-CPU的，trampoline只加载了GDT/CR3） */
    extern void idt64_load_for_ap(void);
    idt64_load_for_ap();

    /* 启用AP核本地APIC（SVR软件使能位per-CPU） */
    extern void apic64_enable_for_ap(void);
    apic64_enable_for_ap();

    /* 启动LAPIC定时器（周期模式约1000Hz，向量0x40） */
    extern void apic64_init_timer(void);
    apic64_init_timer();

    /* AP核启用中断：定时器tick/EOI均不触碰渲染与共享数据，
       仅递增 per_cpu_timer_ticks（smp_timer_tick 中AP调度已禁用） */
    __asm__ volatile("sti");
    while (1) {
        __asm__ volatile("hlt");
    }
}

/* SMP定时器tick（每个CPU的Local APIC定时器中断调用） */
void smp_timer_tick(void)
{
    uint32_t cpu_id = apic64_get_id();
    if (cpu_id < MAX_CPUS) {
        per_cpu_timer_ticks[cpu_id]++;
    }

    /* AP也参与调度：如果有就绪进程，触发调度 */
    extern int process64_get_running_count(void);
    if (process64_get_running_count() > 1) {
        extern int process64_schedule(void);
        /* 非BSP CPU可以调度进程
           暂时禁用AP调度，先验证apic_mode修复的渲染稳定性 */
        if (cpu_id != bsp_cpu_id && 0) {
            process64_schedule();
        }
    }
}

/* 获取per-CPU定时器tick计数 */
uint64_t smp_get_cpu_ticks(uint32_t cpu_id)
{
    if (cpu_id >= MAX_CPUS) return 0;
    return per_cpu_timer_ticks[cpu_id];
}

/* IPI核间中断处理 */
void smp_ipi_handler(void)
{
    uint32_t cpu_id = apic64_get_id();
    /* IPI用于唤醒空闲CPU或通知调度 */
    /* 目前只计数，后续可加入调度唤醒逻辑 */
    if (cpu_id < MAX_CPUS) {
        per_cpu_timer_ticks[cpu_id]++;  /* 复用计数 */
    }
}

/* 启动单个AP */
static int smp_start_single_ap(uint32_t cpu_id, uint64_t pml4_addr)
{
    if (cpu_id >= MAX_CPUS) return -1;
    if (cpu_id == bsp_cpu_id) return -1;

    vga_puts("[SMP] Starting AP ");
    vga_put_hex(cpu_id);
    vga_puts("...\n");

    /* 分配AP栈（每个AP 16KB） */
    static uint8_t ap_stacks[MAX_CPUS][16384] __attribute__((aligned(16)));
    uint64_t stack_top = (uint64_t)&ap_stacks[cpu_id][16384];

    /* 填充trampoline参数（固定地址0x9000，避免与GDT冲突） */
    /* 0x9000 = PML4地址（4字节） */
    volatile uint32_t *pml4_ptr = (volatile uint32_t *)0x9000;
    /* 0x9008 = 栈指针（8字节） */
    volatile uint64_t *stack_ptr = (volatile uint64_t *)0x9008;
    /* 0x9010 = ap_c_entry地址（8字节） */
    volatile uint64_t *entry_ptr = (volatile uint64_t *)0x9010;

    *pml4_ptr = (uint32_t)pml4_addr;
    *stack_ptr = stack_top;
    *entry_ptr = (uint64_t)ap_c_entry;

    /* 发送INIT IPI */
    apic64_send_init(cpu_id);

    /* 等待10ms */
    for (volatile int i = 0; i < 1000000; i++) {}

    /* 发送STARTUP IPI（vector = AP_TRAMPOLINE_ADDR >> 12 = 0x8） */
    apic64_send_startup(cpu_id, 0x08);

    /* 短暂延迟，让AP有时间执行 */
    for (volatile int i = 0; i < 100000; i++) {}

    /* 不等待，立即检查标记 */

    /* 检查trampoline标记 */
    volatile uint32_t *m1 = (volatile uint32_t *)0x9100;
    volatile uint32_t *m2 = (volatile uint32_t *)0x9104;
    volatile uint32_t *m3 = (volatile uint32_t *)0x9108;
    volatile uint32_t *m4 = (volatile uint32_t *)0x910C;
    volatile uint32_t *m5 = (volatile uint32_t *)0x9110;
    volatile uint32_t *m6 = (volatile uint32_t *)0x9114;
    volatile uint32_t *m7 = (volatile uint32_t *)0x9118;
    volatile uint32_t *m8 = (volatile uint32_t *)0x911C;
    volatile uint32_t *m9 = (volatile uint32_t *)0x9120;
    volatile uint32_t *m10 = (volatile uint32_t *)0x9124;
    volatile uint32_t *m11 = (volatile uint32_t *)0x9128;
    vga_puts("[SMP] AP ");
    vga_put_hex(cpu_id);
    vga_puts(" marks: ");
    vga_put_hex(*m1);
    vga_puts("/");
    vga_put_hex(*m2);
    vga_puts("/");
    vga_put_hex(*m3);
    vga_puts("/");
    vga_put_hex(*m4);
    vga_puts("/");
    vga_put_hex(*m5);
    vga_puts("/");
    vga_put_hex(*m6);
    vga_puts("/");
    vga_put_hex(*m7);
    vga_puts("/");
    vga_put_hex(*m8);
    vga_puts("/");
    vga_put_hex(*m9);
    vga_puts("/");
    vga_put_hex(*m10);
    vga_puts("/");
    vga_put_hex(*m11);
    vga_puts("\n");

    return 0;
}

/* SMP初始化 */
int smp_init(void)
{
    vga_puts("[SMP] Initializing multi-processor support...\n");

    bsp_cpu_id = apic64_get_id();

    /* 尝试ACPI MADT解析 */
    int acpi_result = acpi_init();

    if (acpi_result == 0 && acpi_cpu_count > 0) {
        /* 使用ACPI信息 */
        cpu_count = acpi_cpu_count;
        vga_puts("[SMP] Using ACPI MADT, CPU count: ");
        vga_put_hex(cpu_count);

        /* 初始化CPU信息表 */
        for (int i = 0; i < acpi_cpu_count && i < MAX_CPUS; i++) {
            uint32_t id = acpi_cpus[i].apic_id;
            cpu_table[id].cpu_id = id;
            cpu_table[id].online = (id == bsp_cpu_id) ? 1 : 0;
            cpu_table[id].is_bsp = (id == bsp_cpu_id) ? 1 : 0;
            cpu_table[id].stack_ptr = 0;
        }
    } else {
        /* 后备：使用CPUID检测 */
        cpu_count = detect_cpu_count_cpuid();
        vga_puts("[SMP] Using CPUID fallback, CPU count: ");
        vga_put_hex(cpu_count);

        for (uint32_t i = 0; i < MAX_CPUS; i++) {
            cpu_table[i].cpu_id = i;
            cpu_table[i].online = (i == bsp_cpu_id) ? 1 : 0;
            cpu_table[i].is_bsp = (i == bsp_cpu_id) ? 1 : 0;
            cpu_table[i].stack_ptr = 0;
        }
    }

    vga_puts("[SMP] BSP APIC ID: ");
    vga_put_hex(bsp_cpu_id);

    /* 复制AP trampoline到低内存 */
    vga_puts("[SMP] Copying trampoline...\n");
    uint64_t trampoline_size = sizeof(ap_trampoline_bin);
    vga_puts("[SMP] trampoline_size=");
    vga_put_hex(trampoline_size);
    memcpy((void *)AP_TRAMPOLINE_ADDR, ap_trampoline_bin, trampoline_size);
    vga_puts("[SMP] memcpy done\n");

    /* 使用trampoline内置GDT */

    vga_puts("[SMP] AP trampoline copied to 0x8000 (");
    vga_put_hex(trampoline_size);
    vga_puts(" bytes)\n");

    /* 为BSP启用Local APIC定时器。校准的两个等待循环已加有界超时(V0.57.0),
       失败会返回 -1 并退化到保守 ICR; 进桌面后由 apic64_timer_recalibrate 补校准。 */
    extern void apic64_init_timer(void);
    apic64_init_timer();
    per_cpu_current_proc[bsp_cpu_id] = 0;
    per_cpu_timer_ticks[bsp_cpu_id] = 0;
    vga_puts("[SMP] BSP APIC timer enabled\n");

    /* 启动其他AP */
    if (cpu_count > 1) {
        uint64_t pml4_addr;
        __asm__ volatile("mov %%cr3, %0" : "=r"(pml4_addr));

        for (int i = 0; i < acpi_cpu_count && i < MAX_CPUS; i++) {
            uint32_t id = acpi_cpus[i].apic_id;
            if (id != bsp_cpu_id && acpi_cpus[i].enabled) {
                smp_start_single_ap(id, pml4_addr);
            }
        }
    }

    /* 同步屏障：等待所有AP完成FPU初始化和定时器设置后再继续。
       否则AP在后台异步执行FPU/APIC初始化，会与BSP的TTF字体/桌面
       渲染初始化产生时序竞争，导致首帧渲染不完整（多核下偶发缺面板）。
       ap_started_count 在 ap_c_entry 中于 FPU初始化+定时器设置之后、
       sti 之前递增，是可靠的同步点。 */
    int expected_aps = cpu_count - 1;
    if (expected_aps > 0) {
        uint64_t timeout = 0;
        while (ap_started_count < expected_aps && timeout < 500000000ULL) {
            __asm__ volatile("pause" ::: "memory");
            timeout++;
        }
        if (ap_started_count >= expected_aps) {
            vga_puts("[SMP] All ");
            vga_put_hex(ap_started_count);
            vga_puts(" APs ready (synced)\n");
        } else {
            vga_puts("[SMP] AP sync timeout, started=");
            vga_put_hex(ap_started_count);
            vga_puts(" expected=");
            vga_put_hex(expected_aps);
            vga_puts("\n");
        }
    }

    vga_puts("[SMP] Initialization complete, ");
    vga_put_hex(ap_started_count + 1);
    vga_puts(" CPUs online\n");

    return 0;
}

/* 获取CPU数量 */
uint32_t smp_get_cpu_count(void)
{
    return cpu_count;
}

/* 获取当前CPU ID */
uint32_t smp_get_current_cpu(void)
{
    return apic64_get_id();
}

/* 获取CPU信息 */
cpu_info_t *smp_get_cpu_info(uint32_t cpu_id)
{
    if (cpu_id >= MAX_CPUS) return 0;
    return &cpu_table[cpu_id];
}

/* 启动应用处理器（AP） */
int smp_start_ap(uint32_t cpu_id)
{
    if (cpu_id >= MAX_CPUS) return -1;
    if (cpu_id == bsp_cpu_id) return -1;
    if (cpu_table[cpu_id].online) return 0;

    uint64_t pml4_addr;
    __asm__ volatile("mov %%cr3, %0" : "=r"(pml4_addr));

    return smp_start_single_ap(cpu_id, pml4_addr);
}

/* 检查是否是BSP */
int smp_is_bsp(void)
{
    return (apic64_get_id() == bsp_cpu_id) ? 1 : 0;
}
