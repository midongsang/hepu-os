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
/* SB-20: 每核到位标记。计数只能告诉你「几个核到了」，报超时时说不清
 * 是哪几个核没到 —— 有了位图，超时可以直接列出缺失核的 APIC ID。 */
static volatile int g_ap_arrived[MAX_CPUS];
/* SB-20: AP 栈底哨兵。AP 栈向下增长，栈底（低地址）是最深位置；
 * 被踩说明该核栈溢出到了相邻核的栈区。 */
#define AP_STACK_SENTINEL 0xA5A5C3C3DEAD5A5AULL
static uint64_t g_ap_stack_sentinel_before[MAX_CPUS];

/* BSP完成IDT填充/加载后置1，AP核此前自旋等待（不启用中断）。
   由 irq64_init() 结束时设置。 */
volatile int g_apic_idt_ready = 0;

/* ---------------------------------------------------------------------------
 * SB-21: IPI 消息通道
 *
 * 原来 0x50 的处理器只把 per_cpu_timer_ticks 当计数器复用一下，既没有消息
 * 也没有应答 —— 跨核协同（唤醒/刷 TLB/停核）根本没有通道可用。
 * 这里补最小的消息协议：每核一个消息槽（类型 + 参数 + pending），
 * 发送方填槽后发固定投递 IPI，接收方在 0x50 里分发、清 pending 并累加 ack。
 *
 * 槽只有一份：正在 pending 时再来新消息一律拒收并把 g_ipi_dropped 加一，
 * 绝不覆盖 —— 覆盖会让前一条消息的参数落到后一条处理逻辑里，比丢消息更糟。
 * ------------------------------------------------------------------------- */
#define IPI_MSG_RESCHEDULE  1
#define IPI_MSG_TLB_FLUSH   2
#define IPI_MSG_STOP        3

typedef struct {
    volatile int      pending;
    volatile int      type;
    volatile uint64_t arg;      /* TLB_FLUSH: 目标虚拟地址, 0 = 整表刷新 */
} ipi_slot_t;

static ipi_slot_t g_ipi_slot[MAX_CPUS];
static volatile uint64_t g_ipi_ack[MAX_CPUS];      /* 每核已处理的消息总数 */
static volatile uint64_t g_ipi_dropped;            /* 发送时槽忙被拒的次数 */

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

/* 注意: 这里的输出和 BSP 的 vga_puts 会并发写同一个串口, 必须持同一把
 * print_lock, 否则两个核的日志会互相插进半个词。 */
static void sb18_serial_puts(const char *str)
{
    extern uint64_t print_lock_acquire(void);
    extern void print_lock_release(uint64_t flags);

    uint64_t f = print_lock_acquire();
    while (*str) {
        if (*str == '\n') sb18_serial_putc('\r');
        sb18_serial_putc(*str++);
    }
    print_lock_release(f);
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

    /* 到达安全点：中断保持关闭。ap_started_count 是BSP的同步点，
       表示本核已完成FPU/TSS/GDT/IDT/LAPIC/定时器初始化。
       SB-20: 原子递增 —— 原来是裸 ++，4 核以上同时到位会丢计数，
       BSP 的屏障永远等不齐（表现为启动挂死或超时误报）。 */
    if (cpu_id < MAX_CPUS) g_ap_arrived[cpu_id] = 1;
    __sync_fetch_and_add((int *)&ap_started_count, 1);

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
    /* SB-19: 确认本核 LAPIC 状态按核独立(不再是 BSP 的全局单值) */
    {
        extern uint32_t apic64_pcpu_id(int cpu);
        extern uint32_t apic64_pcpu_version(int cpu);
        sb18_serial_puts("[SB19] cpu=");
        sb18_serial_hex((uint64_t)cpu_id);
        sb18_serial_puts(" lapic_id=");
        sb18_serial_hex((uint64_t)apic64_pcpu_id((int)cpu_id));
        sb18_serial_puts(" ver=");
        sb18_serial_hex((uint64_t)apic64_pcpu_version((int)cpu_id));
        sb18_serial_puts("\n");
    }

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

/* SB-25 总开关: AP 参与调度。
 *
 * 0 = 关闭。基础设施(每核 idle、IPI 重调度、每核 TSS/syscall/LAPIC)已就位,
 *     但 PLAN_SMP_HARDENING_R5 §7.3 要求顺序: 先清 §7.1 的三处单核假设
 *     (fork 父快照 / readdir 静态表 / 裸盘静态缓冲), 再打开这里。
 *     倒序打开会得到"一开就崩、崩在哪说不清"。
 * 1 = 打开(仅在上述三处清完之后)。 */
#define HS_AP_SCHED_ENABLED 0

/* SMP定时器tick（每个CPU的Local APIC定时器中断调用） */
void smp_timer_tick(void)
{
    uint32_t cpu_id = apic64_get_id();
    if (cpu_id < MAX_CPUS) {
        per_cpu_timer_ticks[cpu_id]++;
    }

    /* AP也参与调度：如果有就绪进程，触发调度。
     *
     * V0.60.x: 取消 && 0 的临时禁用。原先禁用的理由是"先验证 apic_mode
     * 修复后的渲染稳定性", 而当时把 AP 调度打开会踩两个坑, 现在都已补齐:
     *   ① 每核 TSS / syscall per-CPU 存储 / 每核 LAPIC (SB-17/18/19)
     *   ② 每核专属 idle —— 否则 AP 会拿 BSP 的 idle processes[0] 当 old,
     *      把 AP 的栈写进 BSP 的上下文(见 process64 的 proc_bind_cpu_idles)
     * 仍然只在 process64 初始化完成后才允许(process64_ap_sched_ready)。 */
    extern int process64_get_running_count(void);
    if (cpu_id != bsp_cpu_id && HS_AP_SCHED_ENABLED && process64_get_running_count() > 1) {
        extern int process64_ap_sched_ready(void);
        extern int process64_schedule(void);
        if (HS_AP_SCHED_ENABLED && process64_ap_sched_ready()) {
            int ap_pid = process64_schedule();
            if (ap_pid >= 0) {
                /* 诊断: 证明 AP 真的切到了进程(限流, 约每 500 次打印一行) */
                static uint64_t ap_sw = 0;
                if ((++ap_sw % 500) == 0) {
                    extern void vga_puts(const char *);
                    extern void vga_put_hex(uint64_t);
                    vga_puts("[APSCHED] cpu=");
                    vga_put_hex(cpu_id);
                    vga_puts(" pid=");
                    vga_put_hex((uint64_t)ap_pid);
                    vga_puts(" sw=");
                    vga_put_hex(ap_sw);
                    vga_puts("\n");
                }
            }
        }
    }
}

/* 获取per-CPU定时器tick计数 */
uint64_t smp_get_cpu_ticks(uint32_t cpu_id)
{
    if (cpu_id >= MAX_CPUS) return 0;
    return per_cpu_timer_ticks[cpu_id];
}

/* IPI核间中断处理：按消息类型分发，处理完清 pending 并写应答 */
static inline void sb21_invlpg(uint64_t va)
{
    __asm__ volatile("invlpg (%0)" : : "r"(va) : "memory");
}

static inline void sb21_flush_all_tlb(void)
{
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

void smp_ipi_handler(void)
{
    uint32_t cpu_id = apic64_get_id();
    if (cpu_id >= MAX_CPUS) return;

    /* 伪中断 / 已被处理过的迟到 IPI：什么都不做，也不要累加应答，
     * 否则汇聚方会多算一次。 */
    if (!g_ipi_slot[cpu_id].pending) return;

    int type = g_ipi_slot[cpu_id].type;
    uint64_t arg = g_ipi_slot[cpu_id].arg;

    switch (type) {
    case IPI_MSG_TLB_FLUSH:
        if (arg) sb21_invlpg(arg);
        else     sb21_flush_all_tlb();
        break;
    case IPI_MSG_RESCHEDULE: {
        /* AP 调度已打开(smp_timer_tick 里无 && 0)。由别的核点名要求重新调度,
         * 与定时器 tick 走同一个入口; 未就绪时静默跳过。 */
        extern int process64_ap_sched_ready(void);
        extern int process64_schedule(void);
        if (process64_ap_sched_ready()) process64_schedule();
        break;
    }
    case IPI_MSG_STOP:
        /* 先清 pending 再停机，避免汇聚方等一个永远不会到的应答；
         * 停住的核只能靠 INIT/复位唤醒。 */
        g_ipi_slot[cpu_id].pending = 0;
        __sync_fetch_and_add(&g_ipi_ack[cpu_id], 1);
        __asm__ volatile("sti");
        while (1) { __asm__ volatile("hlt"); }
        break;
    default:
        break;
    }

    g_ipi_slot[cpu_id].pending = 0;
    __asm__ volatile("" ::: "memory");
    __sync_fetch_and_add(&g_ipi_ack[cpu_id], 1);
}

/* 点对点发送。槽忙则拒收（返回 -1），由调用方决定重试或放弃。 */
int smp_ipi_send(int target, int type, uint64_t arg)
{
    if (target < 0 || target >= MAX_CPUS) return -1;
    if (target == (int)bsp_cpu_id) return -1;   /* BSP 自己不需要 IPI */

    if (g_ipi_slot[target].pending) {
        __sync_fetch_and_add(&g_ipi_dropped, 1);
        return -1;
    }
    g_ipi_slot[target].type = type;
    g_ipi_slot[target].arg = arg;
    /* 参数先落地，pending 后置：接收方看到 pending 时参数一定已可见 */
    __asm__ volatile("" ::: "memory");
    g_ipi_slot[target].pending = 1;
    __asm__ volatile("mfence" ::: "memory");

    apic64_send_ipi((uint32_t)target, 0x50);
    return 0;
}

/* 广播给所有在线 AP（逐核填槽，比 others 简码更可靠：能逐核判槽忙） */
int smp_ipi_send_others(int type, uint64_t arg)
{
    int sent = 0;
    for (int id = 0; id < MAX_CPUS; id++) {
        if (id == (int)bsp_cpu_id) continue;
        if (!cpu_table[id].online) continue;
        if (smp_ipi_send(id, type, arg) == 0) sent++;
    }
    return sent;
}

/* 取每核 ack 快照 */
void smp_ipi_snapshot(uint64_t *out)
{
    if (!out) return;
    for (int i = 0; i < MAX_CPUS; i++) out[i] = g_ipi_ack[i];
}

/* 汇聚：等到全核 ack 总和相对 snapshot 增长 expect 次；超时返回 -1。
 * 只算总和而不是逐核比对 —— 调用方关心的是「所有目标都回了」，
 * 某一核回了两条也满足条件。 */
int smp_ipi_wait_ack(const uint64_t *snapshot, int expect, uint64_t timeout)
{
    uint64_t base = 0, now = 0;
    for (int i = 0; i < MAX_CPUS; i++) base += snapshot[i];

    for (uint64_t t = 0; t < timeout; t++) {
        now = 0;
        for (int i = 0; i < MAX_CPUS; i++) now += g_ipi_ack[i];
        if ((int)(now - base) >= expect) return 0;
        __asm__ volatile("pause" ::: "memory");
    }
    return -1;
}

/* 读每核应答计数（诊断用） */
uint64_t smp_ipi_ack_count(int cpu)
{
    if (cpu < 0 || cpu >= MAX_CPUS) return 0;
    return g_ipi_ack[cpu];
}

uint64_t smp_ipi_dropped_count(void) { return g_ipi_dropped; }

/* SB-20: 供屏障后复核 AP 栈底哨兵（ap_stacks 在启动函数内是 static，
 * 这里用同一份符号做读取入口） */
static uint8_t *g_ap_stack_base[MAX_CPUS];
uint64_t smp_ap_stack_sentinel_probe(int cpu)
{
    if (cpu < 0 || cpu >= MAX_CPUS || !g_ap_stack_base[cpu]) return AP_STACK_SENTINEL;
    return *(uint64_t *)g_ap_stack_base[cpu];
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
    /* SB-20: 栈底哨兵。先记录写入前的原值（静态区应为 0），
     * 屏障之后如果发现哨兵被改写，说明该核栈溢出了 16KB。 */
    g_ap_stack_base[cpu_id] = &ap_stacks[cpu_id][0];
    g_ap_stack_sentinel_before[cpu_id] = *(uint64_t *)&ap_stacks[cpu_id][0];
    *(uint64_t *)&ap_stacks[cpu_id][0] = AP_STACK_SENTINEL;

    /* 填充trampoline参数（固定地址0x9000，避免与GDT冲突） */
    /* 0x9000 = PML4地址（4字节） */
    volatile uint32_t *pml4_ptr = (volatile uint32_t *)0x9000;
    /* 0x9008 = 栈指针（8字节） */
    volatile uint64_t *stack_ptr = (volatile uint64_t *)0x9008;
    /* 0x9010 = ap_c_entry地址（8字节） */
    volatile uint64_t *entry_ptr = (volatile uint64_t *)0x9010;

    /* SB-20: 参数发布顺序固化 —— CR3 → RSP → entry，三步之间各插一个
     * 编译器屏障，最后整体 mfence 再发 IPI。trampoline 一上电就按
     * 0x9000 / 0x9008 / 0x9010 顺序读，若编译器或 CPU 把「先写 RSP 后写
     * CR3」重排出去了，AP 会用旧 CR3 上的栈地址跑，直接跑飞。 */
    *pml4_ptr = (uint32_t)pml4_addr;
    __asm__ volatile("" ::: "memory");
    *stack_ptr = stack_top;
    __asm__ volatile("" ::: "memory");
    *entry_ptr = (uint64_t)ap_c_entry;
    __asm__ volatile("mfence" ::: "memory");

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
            vga_puts(" missing_apic_ids=");
            for (uint32_t id = 0; id < MAX_CPUS; id++) {
                if (id == bsp_cpu_id) continue;
                if (!cpu_table[id].online) continue;
                if (g_ap_arrived[id]) continue;
                vga_put_hex(id);
                vga_puts(" ");
            }
            vga_puts("\n");
        }

        /* SB-20: 栈哨兵复核。屏障之后所有 AP 都停在自己的 hlt 循环里，
         * 栈使用量已稳定，此时检查是最有意义的时点。 */
        for (int id = 0; id < MAX_CPUS; id++) {
            if (id == bsp_cpu_id || !g_ap_arrived[id]) continue;
            extern uint64_t smp_ap_stack_sentinel_probe(int cpu);
            uint64_t now = smp_ap_stack_sentinel_probe(id);
            if (now != AP_STACK_SENTINEL) {
                vga_puts("[SMP] WARN: AP stack overflow on cpu=");
                vga_put_hex(id);
                vga_puts(" sentinel=");
                vga_put_hex(now);
                vga_puts(" (was ");
                vga_put_hex(g_ap_stack_sentinel_before[id]);
                vga_puts(")\n");
            }
        }
    }

    vga_puts("[SMP] Initialization complete, ");
    vga_put_hex(ap_started_count + 1);
    vga_puts(" CPUs online\n");

    /* SB-21 自检：给所有在线 AP 发一次 TLB_FLUSH（整表），汇聚应答。
     * 这是唯一能在启动期证明「消息能发到、能处理、应答能数回来」的
     * 端到端证据 —— 否则 IPI 通道坏了也只会在需要 shootdown 时才暴露。 */
    if (cpu_count > 1) {
        uint64_t snap[MAX_CPUS];
        smp_ipi_snapshot(snap);
        int sent = smp_ipi_send_others(IPI_MSG_TLB_FLUSH, 0);
        int rc = smp_ipi_wait_ack(snap, sent, 200000000ULL);
        vga_puts("[SB21] ipi selftest sent=");
        vga_put_hex(sent);
        vga_puts(" acked=");
        vga_put_hex(rc == 0 ? (uint64_t)sent : 0);
        vga_puts(rc == 0 ? " ok\n" : " TIMEOUT\n");
        if (smp_ipi_dropped_count()) {
            vga_puts("[SB21] WARN: ipi dropped=");
            vga_put_hex(smp_ipi_dropped_count());
            vga_puts("\n");
        }
    }

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

/* 该核是否已上线（供进程管理为 AP 绑定专属 idle 用） */
int smp_cpu_is_online(uint32_t cpu_id)
{
    if (cpu_id >= MAX_CPUS) return 0;
    return cpu_table[cpu_id].online ? 1 : 0;
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
