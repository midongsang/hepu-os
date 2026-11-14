/* ============================================================================
 * 赫普OS 64位 - 用户态进程管理（增强版）
 * V0.10.11: 优先级调度、时间片、进程统计、阻塞/唤醒
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "spinlock.h"
#include "process64.h"
#include "domain64.h"
#include "capability64.h"
#include "devfs64.h"
#include "security64.h"

#define MAX_PROCESSES PROCESS64_MAX_SLOTS
#define PROCESS_STACK_SIZE 8192
#define USER_CODE_BASE 0x400000
#define USER_STACK_BASE 0x700000

/* 优先级定义 */
#define PRIORITY_IDLE     0
#define PRIORITY_LOW      1
#define PRIORITY_NORMAL   2
#define PRIORITY_HIGH     3
#define PRIORITY_REALTIME 4
#define PRIORITY_COUNT    5

/* 默认时间片（毫秒） */
#define DEFAULT_TIME_SLICE 100

/* 进程状态 */
#define PROC_STATE_UNUSED  0
#define PROC_STATE_READY   1
#define PROC_STATE_RUNNING 2
#define PROC_STATE_BLOCKED 3
#define PROC_STATE_ZOMBIE  4

/* 寄存器上下文（x86_64） */
typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, rflags;
    uint64_t cs, ss, ds, es, fs, gs;
    uint64_t cr3;
    /* V0.57.0: SSE/FPU 状态保存区。
     * 内核主体用 -mno-sse 编, 但 Lua(以及将来的 FreeType)是 -msse2 —— 一旦
     * 线程切换不保存 xmm, 两个线程的浮点状态会互相污染, 表现为"偶发数字错误"。
     * FXSAVE 要求 512 字节缓冲且 16 字节对齐: 结构整体 aligned(16), cr3 结束在
     * [200], 这里用 8 字节占位把 fx_area 推到 [208] (208 = 13*16)。 */
    uint64_t _fxpad;
    uint8_t fx_area[512];
} __attribute__((packed, aligned(16))) process_context_t;

/* 汇编(entry_64.S 的 fxsave 208(%rdi) / fxrstor 208(%rsi))写死了 fx_area 的偏移,
 * 结构一改就会静默保存/恢复到错误位置 —— 用编译期断言把它钉住。 */
_Static_assert(__builtin_offsetof(process_context_t, fx_area) == 208,
               "fx_area 必须在 context 偏移 208, 与 entry_64.S 的 fxsave/fxrstor 一致");
_Static_assert((__builtin_offsetof(process_context_t, fx_area) % 16) == 0,
               "FXSAVE 缓冲必须 16 字节对齐");

/* 进程控制块 */
typedef struct {
    int pid;
    int state;
    int priority;
    int cpu_id;           /* 分配的CPU ID */
    uint64_t time_slice;       /* 当前剩余时间片 */
    uint64_t default_slice;    /* 默认时间片 */
    uint64_t runtime;          /* 总运行时间（tick） */
    uint64_t context_switches; /* 上下文切换次数 */
    uint64_t create_time;      /* 创建时间 */
    process_context_t context;
    uint8_t *stack;
    uint64_t stack_size;
    uint8_t *page_table;  /* PML4 */
    char name[32];
    int exit_code;
    int parent_pid;
    int env_type;          /* V0.20.46: 0=native, 1=linux_compat, 2=win_compat(预留) */
    int domain_id;         /* V0.54: 所属隔离域 ID */
    uint64_t mem_pages;    /* 已映射物理页数（RSS） */
    uint64_t mem_limit;    /* 内存上限(页), 0=无限制 */
    uint32_t cap_mask;     /* 能力位图(域ACL) */
    uint64_t signal_handlers[32];  /* signal handler函数指针 */
    int pending_signals;       /* 待处理信号位图 */
    /* V0.58.4: 归属标记 —— 本进程当前正在哪个 CPU 上执行, -1 = 未运行。
       存在的意义: context_switch_64 是「保存 old、从 new 恢复」, 若两个核
       同时对同一进程做这件事, 栈指针会被两个核互相覆盖, 而且不报错、只是
       静默写坏内存。选进程时用这个标记把「已被别的核占着」的进程跳过。 */
    volatile int running_on;
} process_t;

_Static_assert((__builtin_offsetof(process_t, context) % 16) == 0,
               "process_context_t 在 PCB 里要 16 字节对齐, 否则 fx_area 失配");

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);
extern uint64_t pmm64_alloc_page(void);
extern uint64_t pmm64_alloc_pages(size_t count);
extern void pmm64_free_page(uint64_t addr);

static process_t processes[MAX_PROCESSES];

/* V0.58.3: 供 sys_ps 用的取值接口。
 * syscall64_full.c 之前自己 typedef 了一份 process_t，字段顺序与这里的真实
 * 结构不一致（真实结构里 name[32] 排在 process_context_t/stack/page_table
 * 之后，偏移远大于调用方以为的 24），于是 ps 读到的 name 是别的字段的字节，
 * mem_pages 也永远是 0。把取值收进本文件，调用方不必也不该复制结构定义。
 * 返回 1 = 该槽有效，0 = 空槽跳过。 */
int process64_get_ps_fields(int idx, int *pid, int *ppid, int *state, int *prio,
                            uint64_t *mem_pages, char name[32])
{
    if (idx < 0 || idx >= MAX_PROCESSES) return 0;
    if (processes[idx].state == 0) return 0;
    if (pid) *pid = processes[idx].pid;
    if (ppid) *ppid = processes[idx].parent_pid;
    if (state) *state = processes[idx].state;
    if (prio) *prio = processes[idx].priority;
    if (mem_pages) *mem_pages = processes[idx].mem_pages;
    if (name) {
        int k = 0;
        for (; k < 31 && processes[idx].name[k]; k++) name[k] = processes[idx].name[k];
        for (; k < 32; k++) name[k] = 0;   /* 尾部补零, 不留上一条的残字 */
        name[31] = 0;
    }
    return 1;
}

/* V0.20.62: 公开访问器（hepu ps 用） */
process_t *process64_get_table(void) { return processes; }

/* 视图层唯一只读快照：不暴露 process_t 布局 */
int process64_snapshot(process64_stat_t *out, int max)
{
    int count = 0;

    if (!out || max <= 0) return 0;

    for (int i = 0; i < MAX_PROCESSES && count < max; i++) {
        if (processes[i].state == PROC_STATE_UNUSED) continue;

        out[count].pid = processes[i].pid;
        out[count].parent_pid = processes[i].parent_pid;
        out[count].state = processes[i].state;
        out[count].priority = processes[i].priority;
        out[count].cpu_id = processes[i].cpu_id;
        out[count].env_type = processes[i].env_type;
        out[count].domain_id = processes[i].domain_id;
        out[count].runtime = processes[i].runtime;
        out[count].mem_pages = processes[i].mem_pages;
        out[count].mem_limit = processes[i].mem_limit;

        int n = 0;
        while (n < 31 && processes[i].name[n]) {
            out[count].name[n] = processes[i].name[n];
            n++;
        }
        out[count].name[n] = 0;
        count++;
    }
    return count;
}
/* V0.58.4: current_pid 原来是全局单值。SMP 下两个核各跑各的进程，单值会被
   另一个核覆盖 —— process64_get_current() 的 PID 回退分支、
   process64_get_current_pid()、以及 process64_create 记录 parent_pid 都会拿到
   "另一个核的当前进程"。改为 per-CPU。单核行为完全不变。 */
static volatile int per_cpu_current_pid[16];  /* 16 = MAX_CPUS */

/* 取当前 CPU 的当前 PID（内部用，不加锁：调度路径本来就在锁内调用） */
/* helper 实现在 per_cpu_current_idx 之后 */
static int next_pid = 1;
static int process_initialized = 0;
/* V0.60.x: "本次 create 不要入就绪队列" 的一次性开关。
 *
 * create_suspended 原来是"先 create(已入队), 再摘队"。中间有一个 tick 窗口:
 * 新进程优先级若高于 idle(0), 那一刻 BSP 就会被切到它, 而它是不打算跑的常驻线程
 * (无限循环不 yield), 桌面主循环再也拿不回 CPU —— 表现为启动停在
 * "Created process: khouse" 之后。回归里 desk=0 的偶发就是它。
 * 改成从源头不入队, 窗口消失。 */
static int g_create_no_enqueue = 0;
static uint64_t scheduler_ticks = 0;

/* Per-CPU当前进程索引（每个CPU独立运行） */
static volatile int per_cpu_current_idx[16];  /* 16 = MAX_CPUS */

/* SMP 完整化: 每核专属 idle 槽。
 *
 * 为什么必须有: per_cpu_current_idx[] 原来统一初始化为 0, 也就是各核都指向
 * BSP 的 processes[0]。AP 一旦参与调度, 会把这个"属于 BSP 的 idle"当成自己的
 * old —— 把 AP 的 rsp/rbp 写进它的 context, 等 BSP 回到 idle 时用的是 AP 的栈。
 * 所以每个参与调度的核必须有自己的一份 idle PCB。
 *
 * 约定: pid = PROC_CPU_IDLE_PID_BASE + cpu_id, 用 pid 判定而不加结构体字段,
 * 避免改动 process_t 布局(其 context 偏移被 _Static_assert 钉住)。
 * idle 永不进就绪队列, 只在"本核无活可干"时被切回。 */
#define PROC_CPU_IDLE_PID_BASE 900
static int g_cpu_idle_idx[16];
static int g_ap_sched_ready = 0;
static void proc_bind_cpu_idles(void);
static int proc_is_cpu_idle(const process_t *p)
{
    /* pid=0 是 BSP 的 idle —— 它承载的正是桌面主循环, 同样不能入就绪队列:
     * 一旦入队, AP 会把它"窃取"走, 两个核同时执行桌面主循环。 */
    return p && (p->pid == 0 || p->pid >= PROC_CPU_IDLE_PID_BASE);
}

static int sched_cur_pid_internal(void)
{
    extern uint32_t smp_get_current_cpu(void);
    uint32_t cur = smp_get_current_cpu();
    return (cur < 16) ? per_cpu_current_pid[cur] : 0;
}

/* 导出当前 CPU 的当前进程 PID。V0.58.4: 原实现缺失（syscall64_full.c 里只
   extern 声明过，从未定义），且语义是全局单值。现在按 CPU 取。 */
int process64_get_current_pid(void)
{
    return sched_cur_pid_internal();
}
static int next_cpu_assign = 0;  /* 轮询分配CPU */

/* V0.58.4: 这里原本有个 scheduler_lock，只在 init 里 spinlock_init 过一次、
   从来没有被 lock/unlock —— 死变量。真正在保护调度路径的是 g_sched_lock
   （见 process64_schedule 上方），删掉这个误导性的旧锁。 */

/* 优先级队列（每个优先级一个链表头索引） */
static int ready_queue[PRIORITY_COUNT][MAX_PROCESSES];
static int ready_queue_count[PRIORITY_COUNT];

static void proc_memset(void *dst, uint8_t val, int n) {
    uint8_t *d = (uint8_t*)dst;
    for (int i = 0; i < n; i++) d[i] = val;
}

/* 初始化进程管理 */

/* FXSAVE 区的默认值: 全 0 的 MXCSR 会屏蔽所有 SSE 异常, 全 0 的 FCW 会让 x87
 * 在有异常时行为怪异; 两者都用 Intel 复位后的标准值。 */
static void proc_init_fx(process_t *p)
{
    uint8_t *fx = p->context.fx_area;
    *(uint16_t *)(fx + 0)  = 0x037F;   /* FCW: 64 位精度, 所有异常屏蔽 */
    *(uint32_t *)(fx + 24) = 0x1F80;   /* MXCSR: 所有异常屏蔽 + round-to-nearest */
}

void process64_init(void)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        processes[i].state = PROC_STATE_UNUSED;
        processes[i].pid = -1;
    }
    for (int i = 0; i < PRIORITY_COUNT; i++) {
        ready_queue_count[i] = 0;
    }
    process_initialized = 1;
    scheduler_ticks = 0;

    /* 初始化per-CPU当前进程为idle（索引0） */
    for (int i = 0; i < 16; i++) {
        per_cpu_current_idx[i] = 0;
    }
    next_cpu_assign = 0;

    /* 初始化当前主线程为idle进程（PID=0） */
    processes[0].pid = 0;
    processes[0].state = PROC_STATE_RUNNING;
    processes[0].priority = PRIORITY_IDLE;
    processes[0].cpu_id = 0;  /* idle在BSP上 */
    processes[0].domain_id = DOMAIN_ID_KERNEL;  /* idle 属于内核域 */
    processes[0].default_slice = 10;
    processes[0].time_slice = 10;
    processes[0].context.rsp = 0;  /* 运行时由context_switch保存 */
    processes[0].context.rbp = 0;
    proc_init_fx(&processes[0]);
    proc_memset(processes[0].name, 0, 32);
    processes[0].name[0] = 'i'; processes[0].name[1] = 'd';
    processes[0].name[2] = 'l'; processes[0].name[3] = 'e';
    for (int _i = 0; _i < 16; _i++) { per_cpu_current_pid[_i] = 0; }
    {
        /* 用真实 APIC ID 而不是硬编码 0: QEMU 下 BSP=0, 但不能指望 */
        extern uint32_t smp_get_current_cpu(void);
        processes[0].running_on = (int)smp_get_current_cpu();
    }
    /* 其余槽位默认 0, 用 UNUSED 判定跳过, 这里统一置 -1 以明确语义 */
    for (int _i = 1; _i < MAX_PROCESSES; _i++) { processes[_i].running_on = -1; }

    /* 初始化隔离域表（创建 kernel/system/gui/linux-compat-0 预置域） */
    domain64_init();
    /* P2-3: 初始化域级 QoS */
    extern void qos_init(void);
    qos_init();
    /* P4: 初始化 hepua AI 助手 */
    extern void hepua_init(void);
    hepua_init();
    /* 初始化能力系统（mint/derive/revoke/check） */
    cap64_init();
    /* Dev-1: 初始化 dev capability（hepu_dev_mode → capability 化） */
    extern void hepu_dev_cap_init(void);
    hepu_dev_cap_init();
    /* 初始化 devfs（/dev/null, /dev/zero, /dev/console） */
    devfs64_init();
    /* 初始化安全裁决层（基于能力的域间访问控制） */
    sec64_init();

    /* SMP: 为每个在线 AP 绑一份自己的 idle, 并置 AP 调度就绪标志。
       必须在 process_initialized=1 之后(上面已置), 且 smp_init 已完成(online 表有效)。 */
    proc_bind_cpu_idles();

    vga_puts("[PROC64] Process manager initialized (32 procs, 5 priorities, idle=PID0)\n");
}

/* 获取进程数量 */
int process64_get_count(void)
{
    int count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state != PROC_STATE_UNUSED) count++;
    }
    return count;
}

/* 获取运行中进程数量 */
int process64_get_running_count(void)
{
    int count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_STATE_READY ||
            processes[i].state == PROC_STATE_RUNNING) count++;
    }
    return count;
}

/* SB-23: 队列访问分「已持锁」与「自取锁」两套入口。
 * 调度器内部本来就在 sched_lock 里，直接调 _locked 版；create/唤醒这类
 * 无锁上下文调公开版。刻意不依赖 spinlock_lock 的同核递归 —— 递归
 * unlock 会把内层的 sti 提前执行，SB-14 已经在这上面栽过一次。 */
static uint64_t sched_lock_acquire(void);
static void sched_lock_release(uint64_t f);

/* 假定调用方已持 sched_lock */
static void add_to_ready_queue_locked(int idx)
{
    int prio = processes[idx].priority;
    if (prio < 0 || prio >= PRIORITY_COUNT) prio = PRIORITY_NORMAL;
    if (ready_queue_count[prio] < MAX_PROCESSES) {
        ready_queue[prio][ready_queue_count[prio]++] = idx;
        return;
    }
    /* 队列满：原来静默 return，进程留在 PROC_STATE_READY 却不在任何队列里，
     * 再也选不到 —— 变成僵尸态。至少让它可观测。 */
    {
        extern void vga_puts(const char *);
        extern void vga_put_hex(uint64_t);
        vga_puts("[SCHED] ready queue full prio=");
        vga_put_hex((uint64_t)prio);
        vga_puts(" idx=");
        vga_put_hex((uint64_t)idx);
        vga_puts("\n");
    }
}

static void add_to_ready_queue(int idx)
{
    uint64_t f = sched_lock_acquire();
    add_to_ready_queue_locked(idx);
    sched_lock_release(f);
}

/* 假定调用方已持 sched_lock */
static void remove_from_ready_queue_locked(int idx)
{
    int prio = processes[idx].priority;
    if (prio < 0 || prio >= PRIORITY_COUNT) return;
    for (int i = 0; i < ready_queue_count[prio]; i++) {
        if (ready_queue[prio][i] == idx) {
            for (int j = i; j < ready_queue_count[prio] - 1; j++) {
                ready_queue[prio][j] = ready_queue[prio][j+1];
            }
            ready_queue_count[prio]--;
            return;
        }
    }
}

static void remove_from_ready_queue(int idx)
{
    uint64_t f = sched_lock_acquire();
    remove_from_ready_queue_locked(idx);
    sched_lock_release(f);
}

/* 创建进程 */
int process64_create(const char *name, void (*entry)(void), int priority)
{
    if (!process_initialized) return -1;

    /* 找空闲PCB */
    int idx = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_STATE_UNUSED) {
            idx = i;
            break;
        }
    }
    if (idx == -1) {
        vga_puts("[PROC64] No free process slot\n");
        return -1;
    }

    /* 初始化PCB */
    proc_memset(&processes[idx], 0, sizeof(process_t));
    proc_init_fx(&processes[idx]);
    processes[idx].pid = next_pid++;
    processes[idx].state = PROC_STATE_READY;
    processes[idx].priority = (priority >= 0 && priority < PRIORITY_COUNT) ? priority : PRIORITY_NORMAL;
    processes[idx].mem_pages = 4;  /* 初始估计：代码页+栈页 */

    /* 轮询分配CPU */
    extern uint32_t smp_get_cpu_count(void);
    uint32_t cpu_cnt = smp_get_cpu_count();
    if (cpu_cnt == 0) cpu_cnt = 1;
    processes[idx].cpu_id = next_cpu_assign % cpu_cnt;
    next_cpu_assign++;
    processes[idx].default_slice = DEFAULT_TIME_SLICE * (PRIORITY_REALTIME - processes[idx].priority + 1) / 2;
    if (processes[idx].default_slice < 10) processes[idx].default_slice = 10;
    processes[idx].time_slice = processes[idx].default_slice;
    processes[idx].runtime = 0;
    processes[idx].context_switches = 0;
    processes[idx].create_time = scheduler_ticks;
    processes[idx].running_on = -1;  /* 新建进程还没在任何核上跑 */
    processes[idx].parent_pid = sched_cur_pid_internal();
    processes[idx].domain_id = domain64_env_to_id(processes[idx].env_type);

    /* 复制名称。V0.58.3: 先清零再拷 —— 槽位复用时会残留上一条进程的名字尾巴,
     * ps 里表现为 NAME 后面跟着乱码字节。 */
    proc_memset(processes[idx].name, 0, 32);
    if (name) {
        int i = 0;
        while (name[i] && i < 31) {
            processes[idx].name[i] = name[i];
            i++;
        }
        processes[idx].name[i] = 0;
    } else {
        processes[idx].name[0] = 'u';
        processes[idx].name[1] = 'n';
        processes[idx].name[2] = 'k';
        processes[idx].name[3] = 0;
    }

    /* 设置入口点和栈 */
    processes[idx].context.rip = (uint64_t)entry;
    processes[idx].context.cs = 0x08;
    processes[idx].context.ss = 0x10;
    processes[idx].context.rflags = 0x202; /* IF=1 */

    /* 分配内核线程栈（16KB） */
    processes[idx].stack_size = 16384;
    uint64_t stack_phys = pmm64_alloc_pages(4);  /* 4页 = 16KB */
    if (!stack_phys) {
        vga_puts("[PROC64] Failed to allocate stack\n");
        /* V0.57.0: 失败即回收槽位。原实现直接 return -1, 把已经 memset 过、pid 已分配的
         * PCB 留在 READY 之外却又不是 UNUSED 的状态 —— 反复失败会耗尽 32 个槽位。 */
        processes[idx].state = PROC_STATE_UNUSED;
        processes[idx].pid = -1;
        return -1;
    }
    /* 注意: pmm64_alloc_pages 返回的是物理地址, 这里直接当虚拟地址用 —— 依赖内核段
     * 恒等映射(rdi 覆盖全部 RAM)。一旦以后引入非恒等映射的内核地址空间, 这里必须先把
     * 物理页映射进内核虚拟区再写栈顶, 否则就是往映射外的地址写。 */
    processes[idx].stack = (uint8_t *)stack_phys;

    /* 设置线程栈：栈顶压入入口地址，context_switch的ret会跳转 */
    uint64_t *stack_top = (uint64_t *)(processes[idx].stack + processes[idx].stack_size);
    *(--stack_top) = (uint64_t)entry;  /* 返回地址 = 入口函数 */
    processes[idx].context.rsp = (uint64_t)stack_top;
    processes[idx].context.rbp = (uint64_t)(processes[idx].stack + processes[idx].stack_size);

    /* 初始化所有寄存器为0（避免旧线程值导致问题） */
    processes[idx].context.rax = 0;
    processes[idx].context.rcx = 0;
    processes[idx].context.rdx = 0;
    processes[idx].context.rsi = 0;
    processes[idx].context.rdi = 0;
    processes[idx].context.r8 = 0;
    processes[idx].context.r9 = 0;
    processes[idx].context.r10 = 0;
    processes[idx].context.r11 = 0;
    processes[idx].context.rbx = 0;
    processes[idx].context.r12 = 0;
    processes[idx].context.r13 = 0;
    processes[idx].context.r14 = 0;
    processes[idx].context.r15 = 0;

    /* 添加到就绪队列 —— 挂起创建的线程从源头就不入队(见 g_create_no_enqueue) */
    if (!g_create_no_enqueue) add_to_ready_queue(idx);

    vga_puts("[PROC64] Created process: ");
    vga_puts(processes[idx].name);
    vga_puts(" (pid=");
    char pidbuf[8];
    int n = processes[idx].pid, i = 0;
    if (n == 0) pidbuf[i++] = '0';
    while (n > 0) { pidbuf[i++] = '0' + n % 10; n /= 10; }
    pidbuf[i] = 0;
    for (int j = 0; j < i / 2; j++) {
        char t = pidbuf[j]; pidbuf[j] = pidbuf[i-1-j]; pidbuf[i-1-j] = t;
    }
    vga_puts(pidbuf);
    vga_puts(", prio=");
    char priobuf[4];
    priobuf[0] = '0' + processes[idx].priority;
    priobuf[1] = 0;
    vga_puts(priobuf);
    vga_puts(")\n");

    return processes[idx].pid;
}

/* V0.57.0: 常驻内核服务线程入口。
 * 契约: 内核线程入口永不返回(返回会 ret 到栈顶之上的未定义地址), 且必须主动让出,
 * 否则单核上会把桌面主循环饿死。这里只做一次计数 + yield, 不做重活。 */
volatile uint64_t g_ksvc_beat = 0;

void process64_yield(void);   /* 前向声明: 定义在本文件靠后, 否则会隐式声明成 int() */

void ksvc_beat_entry(void)
{
    extern void vga_puts(const char *s);
    extern uint64_t pit64_get_ms(void);
    uint64_t last_log = 0;
    for (;;) {
        g_ksvc_beat++;
        /* 探针按时间节流。原来按次数(每 1000 次)根本节制不住: 空转循环一秒能跑上万轮,
         * 串口又慢, 实测把日志刷到 6 万行并把整机拖住。 */
        uint64_t now = pit64_get_ms();
        if (now - last_log >= 5000) { last_log = now; vga_puts("[KSVC] beat alive\n"); }
        process64_yield();
    }
}

/* V0.57.1: 另外两个常驻服务线程。
 * 都不是"凑数进程": 各自有真实职责与可观测产出, 没有职责就不建。
 *   khouse: 每秒唤醒一次桌面合成 —— 按需合成模式下, 时钟/温度这类"自己会变"的内容
 *           没有输入事件就不会重绘, 需要一个外部心跳。
 *   kstat : 维护 uptime 秒计数, 任务管理器摘要行直接读它显示。
 */
volatile uint32_t g_ksvc_uptime_s = 0;

void khouse_entry(void)
{
    extern void hpdesk_mark_dirty(void);
    extern uint64_t pit64_get_ms(void);
    uint64_t last = 0;
    for (;;) {
        uint64_t now = pit64_get_ms();
        if (now - last >= 1000) {
            last = now;
            hpdesk_mark_dirty();
        }
        process64_yield();
    }
}

void kstat_entry(void)
{
    extern uint64_t pit64_get_ms(void);
    for (;;) {
        g_ksvc_uptime_s = (uint32_t)(pit64_get_ms() / 1000);
        process64_yield();
    }
}

/* V0.57.1: 创建"只登记、不参与调度"的线程。
 *
 * 为什么需要: 当前调度器只在 ready_queue 里挑人, 而 idle(PID0) 从未被放进就绪队列 ——
 * 于是只要有 >=2 个会主动 yield 的线程, 它们就会互相切换、永远不回到桌面主循环
 * (BSP 上跑的就是 idle 上下文)。实测: 建 3 个线程后整机停在启动阶段, 串口只剩心跳。
 * 所以除 kbeat 之外的常驻线程先"登记 PCB + 摘出就绪队列", 等 idle 能被正常切回
 * 再启用。任务管理器按真实状态显示, 不假装它们在跑。 */
int process64_create_suspended(const char *name, void (*entry)(void), int priority)
{
    g_create_no_enqueue = 1;
    int pid = process64_create(name, entry, priority);
    g_create_no_enqueue = 0;
    if (pid <= 0) return pid;
    int idx = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid) { idx = i; break; }
    }
    if (idx < 0) return pid;
    int prio = processes[idx].priority;
    if (prio < 0 || prio >= PRIORITY_COUNT) return pid;
    for (int i = 0; i < ready_queue_count[prio]; i++) {
        if (ready_queue[prio][i] == idx) {
            for (int j = i; j < ready_queue_count[prio] - 1; j++)
                ready_queue[prio][j] = ready_queue[prio][j + 1];
            ready_queue_count[prio]--;
            break;
        }
    }
    return pid;
}

/* 当前 CPU 上正在运行的进程槽索引（无有效值时 -1）。
 * sys_fork 用它给"父进程一份专属快照槽"，避免多进程并发 fork 互踩。 */
int process64_current_index(void)
{
    extern uint32_t smp_get_current_cpu(void);
    if (!process_initialized) return -1;
    uint32_t c = smp_get_current_cpu();
    if (c >= 16) return -1;
    int idx = per_cpu_current_idx[c];
    if (idx < 0 || idx >= MAX_PROCESSES) return -1;
    return idx;
}

/* 获取当前进程 */
process_t* process64_get_current(void)
{
    if (!process_initialized) return NULL;
    /* 返回当前CPU运行的进程 */
    extern uint32_t smp_get_current_cpu(void);
    uint32_t cur_cpu = smp_get_current_cpu();
    int idx = per_cpu_current_idx[cur_cpu];
    if (idx >= 0 && idx < MAX_PROCESSES && processes[idx].state != PROC_STATE_UNUSED) {
        return &processes[idx];
    }
    /* 回退：通过本 CPU 的当前 PID 查找 */
    int cur_pid = (cur_cpu < 16) ? per_cpu_current_pid[cur_cpu] : 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == cur_pid) {
            return &processes[i];
        }
    }
    return NULL;
}

/* SMP: 为每个在线 AP 建立专属 idle PCB 并绑定 per-CPU 当前进程。
 * 由 process64_init 末尾调用（此时 smp_init 的 online 表已有效）。 */
static void proc_bind_cpu_idles(void)
{
    extern uint32_t smp_get_current_cpu(void);
    extern int smp_cpu_is_online(uint32_t cpu_id);

    uint32_t bsp = smp_get_current_cpu();
    int bound = 0;

    for (int i = 0; i < 16; i++) g_cpu_idle_idx[i] = -1;
    if (bsp < 16) g_cpu_idle_idx[bsp] = 0;   /* BSP 的 idle 就是 processes[0] */

    for (int c = 0; c < 16; c++) {
        if (c == (int)bsp) continue;
        if (!smp_cpu_is_online((uint32_t)c)) continue;
        int slot = -1;
        for (int s = 1; s < MAX_PROCESSES; s++) {
            if (processes[s].state == PROC_STATE_UNUSED) { slot = s; break; }
        }
        if (slot < 0) break;
        process_t *p = &processes[slot];
        proc_memset(p, 0, (int)sizeof(process_t));
        p->pid = PROC_CPU_IDLE_PID_BASE + c;
        p->state = PROC_STATE_RUNNING;
        p->priority = PRIORITY_IDLE;
        p->cpu_id = c;
        p->domain_id = DOMAIN_ID_KERNEL;
        p->default_slice = 10;
        p->time_slice = 10;
        p->context.rsp = 0;
        p->context.rbp = 0;
        p->context_switches = 0;
        p->parent_pid = 0;
        p->running_on = c;
        proc_init_fx(p);
        proc_memset(p->name, 0, 32);
        p->name[0] = 'i'; p->name[1] = 'd'; p->name[2] = 'l'; p->name[3] = 'e';
        p->name[4] = (char)('0' + (c % 10));
        g_cpu_idle_idx[c] = slot;
        per_cpu_current_idx[c] = slot;
        per_cpu_current_pid[c] = p->pid;
        bound++;
    }

    g_ap_sched_ready = 1;
    {
        extern void vga_puts(const char *);
        extern void vga_put_hex(uint64_t);
        vga_puts("[SMP] per-CPU idle bound: ");
        vga_put_hex((uint64_t)bound);
        vga_puts("\n");
    }
}

/* AP 调度是否可用(process64_init 完成且每核 idle 已绑定) */
int process64_ap_sched_ready(void) { return g_ap_sched_ready; }

/* Dev-1: 导出当前进程的 domain_id，供 hepu_dev 等模块使用 */
int process64_get_current_domain_id(void)
{
    process_t *cur = process64_get_current();
    return cur ? cur->domain_id : -1;
}

/* 获取进程上下文指针（用于测试） */
void* process64_get_context(int pid)
{
    if (!process_initialized) return NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid) return &processes[i].context;
    }
    return NULL;
}

/* 通过PID获取进程 */
process_t* process64_get_by_pid(int pid)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid) {
            return &processes[i];
        }
    }
    return NULL;
}

/* V0.58.4: 调度器自旋锁。
 * process64_schedule 会被时钟中断(smp_timer_tick)与普通上下文同时调用, SMP 下
 * 两个核会并发改 ready_queue[]/current_pid/per_cpu_current_idx[] —— 原实现全程
 * 无锁。这里补一把锁, 并在加锁时 cli: spinlock 本身不关中断, 若持锁时被本核
 * 中断打断、而中断里又要同一把锁, 就会自死锁。
 * 关键约束: 绝不能在持锁状态下 context_switch —— 切完是另一个线程在跑,
 * 锁会永远留在上一个线程手里。所以切换点都是"先释放, 再切"。 */
/* SB-23: 改为静态初始化。原来的懒初始化（if (!ready) spinlock_init()）
 * 有两个核同时首次进入的可能：B 核的 spinlock_init 会把 A 核正持有的锁
 * 清成 {lock=0, owner=0xFFFFFFFF}，第三个核随即直接进临界区。
 * 启动期 BSP 与 AP 的定时器几乎同时开始 tick，这个窗口是真实存在的。 */
static spinlock_t g_sched_lock = { 0u, 0xFFFFFFFFu, 0u };

static uint64_t sched_lock_acquire(void)
{
    uint64_t f;
    __asm__ volatile("pushfq\n\tpopq %0" : "=r"(f) :: "memory");
    __asm__ volatile("cli" ::: "memory");
    spinlock_lock(&g_sched_lock);
    return f;
}

static void sched_lock_release(uint64_t f)
{
    spinlock_unlock(&g_sched_lock);
    if (f & 0x200ULL) __asm__ volatile("sti" ::: "memory");  /* 只恢复调用方原本的 IF */
}

/* 优先级调度器（支持per-CPU） */
int process64_schedule(void)
{
    if (!process_initialized) return -1;

    /* 获取当前CPU ID */
    extern uint32_t smp_get_current_cpu(void);
    uint32_t cur_cpu = smp_get_current_cpu();

    uint64_t sched_iflags = sched_lock_acquire();

    scheduler_ticks++;

    /* 从高优先级到低优先级查找就绪进程（优先选择分配给当前CPU的进程）
     * P2-3: 同优先级内按域 QoS 权重加权选择 */
    extern uint32_t qos_get_weight(int domain_id);
    for (int prio = PRIORITY_REALTIME; prio >= PRIORITY_IDLE; prio--) {
        /* 收集当前CPU的候选进程 */
        int cands[64];
        uint32_t weights[64];
        int cn = 0;
        for (int i = 0; i < ready_queue_count[prio] && cn < 64; i++) {
            int idx = ready_queue[prio][i];
            /* 归属过滤: 已被别的核占着的进程不能被本核选中 —— 否则两个核会
               对同一个进程各做一次 context_switch, 把它的栈指针写串。 */
            int ro = processes[idx].running_on;
            if (ro >= 0 && ro != (int)cur_cpu) continue;
            if (processes[idx].cpu_id == (int)cur_cpu || processes[idx].cpu_id == 0) {
                cands[cn] = idx;
                int did = processes[idx].domain_id;
                weights[cn] = (did >= 0) ? qos_get_weight(did) : 1u;
                cn++;
            }
        }
        if (cn == 0) continue;

        /* 加权选择 */
        int chosen = cands[0];
        if (cn > 1) {
            uint32_t total = 0;
            for (int i = 0; i < cn; i++) total += weights[i];
            if (total > 0) {
                uint32_t pick = scheduler_ticks % total;
                uint32_t acc = 0;
                for (int i = 0; i < cn; i++) {
                    acc += weights[i];
                    if (pick < acc) { chosen = cands[i]; break; }
                }
            }
        }

        /* 从队列移除选中的进程 */
        int chosen_pos = -1;
        for (int i = 0; i < ready_queue_count[prio]; i++) {
            if (ready_queue[prio][i] == chosen) { chosen_pos = i; break; }
        }
        if (chosen_pos >= 0) {
            for (int j = chosen_pos; j < ready_queue_count[prio] - 1; j++) {
                ready_queue[prio][j] = ready_queue[prio][j+1];
            }
            ready_queue_count[prio]--;
        }

        int idx = chosen;
        /* 切换到这个进程 */
        process_t *old = process64_get_current();
        if (old && old->state == PROC_STATE_RUNNING) {
            old->state = PROC_STATE_READY;
            old->time_slice = old->default_slice;
            /* 本核 idle 只置 READY, 不入队: 它靠"无活可干"时的回退切回,
               入队会和其它核重复入队(队列不去重)导致同一 idle 被切多次。 */
            if (!proc_is_cpu_idle(old)) add_to_ready_queue_locked(old - processes);
        }

        processes[idx].state = PROC_STATE_RUNNING;
        processes[idx].context_switches++;
        per_cpu_current_pid[cur_cpu] = processes[idx].pid;
        per_cpu_current_idx[cur_cpu] = idx;
        /* 归属交接: old 不再由本核执行, new 改由本核执行 */
        if (old) old->running_on = -1;
        processes[idx].running_on = (int)cur_cpu;

        /* 实际上下文切换 —— 必须先释放调度锁并恢复调用方的 IF, 见函数头注释 */
        {
            extern void context_switch_64(uint64_t *old_ctx, uint64_t *new_ctx);
            int ret_pid = processes[idx].pid;
            sched_lock_release(sched_iflags);
            if (old) {
                context_switch_64((uint64_t *)&old->context, (uint64_t *)&processes[idx].context);
            }
            return ret_pid;
        }
    }

    /* 如果当前CPU没有专属进程，选择任意就绪进程（工作窃取） */
    for (int prio = PRIORITY_REALTIME; prio >= PRIORITY_IDLE; prio--) {
        if (ready_queue_count[prio] > 0) {
            /* 归属过滤: 队列首项可能被别的核占着, 换一个 */
            int pick_pos = -1;
            for (int i = 0; i < ready_queue_count[prio]; i++) {
                int cand = ready_queue[prio][i];
                int ro = processes[cand].running_on;
                if (ro < 0 || ro == (int)cur_cpu) { pick_pos = i; break; }
            }
            if (pick_pos < 0) continue;
            int idx = ready_queue[prio][pick_pos];
            for (int i = pick_pos; i < ready_queue_count[prio] - 1; i++) {
                ready_queue[prio][i] = ready_queue[prio][i+1];
            }
            ready_queue_count[prio]--;

            process_t *old = process64_get_current();
            if (old && old->state == PROC_STATE_RUNNING) {
                old->state = PROC_STATE_READY;
                old->time_slice = old->default_slice;
                if (!proc_is_cpu_idle(old)) add_to_ready_queue_locked(old - processes);
            }

            processes[idx].state = PROC_STATE_RUNNING;
            processes[idx].context_switches++;
            per_cpu_current_pid[cur_cpu] = processes[idx].pid;
            per_cpu_current_idx[cur_cpu] = idx;
            if (old) old->running_on = -1;
            processes[idx].running_on = (int)cur_cpu;

            {
                extern void context_switch_64(uint64_t *old_ctx, uint64_t *new_ctx);
                int ret_pid = processes[idx].pid;
                sched_lock_release(sched_iflags);
                if (old) {
                    context_switch_64((uint64_t *)&old->context, (uint64_t *)&processes[idx].context);
                }
                return ret_pid;
            }
        }
    }

    /* 无活可干: 本核若有专属 idle, 切回去。
     * AP 靠这一步回到 hlt 循环; 没有它, AP 会在最后一个进程(可能已 ZOMBIE)
     * 的上下文里继续执行。BSP 的 idle 就是当前上下文, 不需要切。 */
    {
        process_t *old = process64_get_current();
        int idle_idx = (cur_cpu < 16) ? g_cpu_idle_idx[cur_cpu] : -1;
        if (old && idle_idx >= 0 && (int)(old - processes) != idle_idx) {
            extern void context_switch_64(uint64_t *old_ctx, uint64_t *new_ctx);
            process_t *ip = &processes[idle_idx];
            int ret_pid = ip->pid;
            ip->state = PROC_STATE_RUNNING;
            per_cpu_current_idx[cur_cpu] = idle_idx;
            per_cpu_current_pid[cur_cpu] = ip->pid;
            if (old->state == PROC_STATE_RUNNING) old->state = PROC_STATE_READY;
            old->running_on = -1;
            ip->running_on = (int)cur_cpu;
            sched_lock_release(sched_iflags);
            context_switch_64((uint64_t *)&old->context, (uint64_t *)&ip->context);
            return ret_pid;
        }
    }

    sched_lock_release(sched_iflags);
    return -1;  /* 没有可运行的进程 */
}

/* 进程让出CPU */
void process64_yield(void)
{
    process_t *curr = process64_get_current();
    if (curr) {
        curr->time_slice = 0; /* 时间片用完，触发调度 */
    }
    process64_schedule();
}

/* 调度器tick（从PIT中断调用） */
static int need_resched = 0;

static uint32_t g_total_ticks = 0;
static uint32_t g_idle_ticks = 0;

/* V0.13.39: CPU使用率统计 (idle=PID0, 基于调度tick) */
int process64_cpu_usage(void)
{
    if (g_total_ticks < 50) return 0;
    uint32_t busy = g_total_ticks - g_idle_ticks;
    return (int)((uint64_t)busy * 100 / g_total_ticks);
}

/* V0.56.1: 供任务管理器计算进程级 CPU 占比(runtime / 总 tick) */
uint32_t process64_total_ticks(void)
{
    return g_total_ticks;
}

void process64_tick(void)
{
    process_t *curr = process64_get_current();
    if (!curr) return;

    g_total_ticks++;
    if (curr->pid == 0) g_idle_ticks++;

    curr->runtime++;

    if (curr->time_slice > 0) {
        curr->time_slice--;
    }

    /* 时间片用完时触发调度 */
    if (curr->time_slice == 0) {
        curr->time_slice = curr->default_slice;
        scheduler_ticks++;
        need_resched = 1;
        /* V0.57.4: 用户态中断里绝对不能切内核上下文。
         * 这条 tick 是在用户态被打断时进来的，当前 CPU 栈上正躺着用户中断帧；
         * context_switch 会把栈换掉，IRQ 返回时就从别人的栈上弹寄存器，
         * 表现就是 rip/rax 变成垃圾（实测 rip=0、子线程 rax!=0）。
         * 用户线程由 uthread64.c 的轮转负责，这里直接让路。 */
        extern int g_user_in_user_mode;
        if (g_user_in_user_mode) return;
        /* 直接调用调度器（当前只有内核线程，上下文切换安全） */
        if (ready_queue_count[PRIORITY_NORMAL] > 0 ||
            ready_queue_count[PRIORITY_LOW] > 0 ||
            ready_queue_count[PRIORITY_HIGH] > 0) {
            process64_schedule();
        }
    }
}

/* 检查是否需要调度（中断返回前调用） */
int process64_need_resched(void)
{
    return need_resched;
}

void process64_clear_resched(void)
{
    need_resched = 0;
}

/* 进程阻塞 */
int process64_block(int pid)
{
    /* SB-23: 「查状态 -> 改状态 -> 出队」整段持锁。原来三步分开，
     * 另一个核在中间看到 READY 就把它选走，本核随后又把 RUNNING 进程
     * 从队列里摘掉 —— 该进程凭空消失。 */
    uint64_t f = sched_lock_acquire();
    process_t *proc = process64_get_by_pid(pid);
    if (!proc || proc->state != PROC_STATE_READY) {
        sched_lock_release(f);
        return -1;
    }

    proc->state = PROC_STATE_BLOCKED;
    remove_from_ready_queue_locked(proc - processes);
    sched_lock_release(f);
    return 0;
}

/* 进程唤醒 */
int process64_wakeup(int pid)
{
    uint64_t f = sched_lock_acquire();
    process_t *proc = process64_get_by_pid(pid);
    if (!proc || proc->state != PROC_STATE_BLOCKED) {
        sched_lock_release(f);
        return -1;
    }

    proc->state = PROC_STATE_READY;
    add_to_ready_queue_locked(proc - processes);
    sched_lock_release(f);
    return 0;
}

/* 进程退出 */
void process64_exit(int exit_code)
{
    process_t *proc = process64_get_current();
    if (!proc) return;

    proc->state = PROC_STATE_ZOMBIE;
    proc->exit_code = exit_code;
    remove_from_ready_queue(proc - processes);

    vga_puts("[PROC64] Process ");
    vga_puts(proc->name);
    vga_puts(" exited with code ");
    char codestr[8];
    int n = exit_code, i = 0;
    char tmp[8];
    if (n < 0) n = -n;
    do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    if (exit_code < 0) tmp[i++] = '-';
    int j = 0;
    while (i > 0) codestr[j++] = tmp[--i];
    codestr[j] = 0;
    vga_puts(codestr);
    vga_puts("\n");

    /* 释放资源 */
    proc->state = PROC_STATE_UNUSED;
    proc->pid = -1;
    {
        extern uint32_t smp_get_current_cpu(void);
        uint32_t _cur = smp_get_current_cpu();
        if (_cur < 16) per_cpu_current_pid[_cur] = 0;
    }
}

/* V0.34.1: 记录子进程为zombie (子进程exit时调用) */
int process64_set_zombie(int code)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid > 0 && processes[i].state != PROC_STATE_UNUSED
            && processes[i].name[0] == 'f' && processes[i].name[1] == 'o') {
            processes[i].state = PROC_STATE_ZOMBIE;
            processes[i].exit_code = code;
            return processes[i].pid;
        }
    }
    return -1;
}

/* V0.20.19: 判断 pid 是否是 parent 的子进程 */
int process64_is_child(int pid, int parent)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].parent_pid == parent) return 1;
    }
    return 0;
}

/* V0.20.17: waitpid - 找已退出的子进程, 回收 */
int process64_waitpid(int pid, int *status)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_STATE_UNUSED) continue;
        if (processes[i].pid == pid) {
            if (processes[i].state == PROC_STATE_ZOMBIE) {
                if (status) *status = processes[i].exit_code;
                int found_pid = processes[i].pid;
                /* V0.34.4: 释放子进程页表 */
                if (processes[i].page_table) {
                    pmm64_free_page((uint64_t)processes[i].page_table);
                    processes[i].page_table = 0;
                }
                processes[i].state = PROC_STATE_UNUSED;
                processes[i].pid = -1;
                return found_pid;
            }
            /* 子进程还在跑, 非阻塞返回 */
            return -11; /* EAGAIN */
        }
    }
    /* 没找到: 非阻塞返回 -ECHILD */
    return -10;
}

/* 列出所有进程 */
int process64_list(process_t *list, int max)
{
    int count = 0;
    for (int i = 0; i < MAX_PROCESSES && count < max; i++) {
        if (processes[i].state != PROC_STATE_UNUSED) {
            list[count++] = processes[i];
        }
    }
    return count;
}

/* 获取进程数量 */
int process64_count(void)
{
    int count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state != PROC_STATE_UNUSED) count++;
    }
    return count;
}

/* 获取调度器tick数 */
uint64_t process64_get_ticks(void)
{
    return scheduler_ticks;
}

/* 获取进程统计 */
int process64_get_stats(int pid, uint64_t *runtime, uint64_t *switches, int *priority)
{
    process_t *proc = process64_get_by_pid(pid);
    if (!proc) return -1;
    if (runtime) *runtime = proc->runtime;
    if (switches) *switches = proc->context_switches;
    if (priority) *priority = proc->priority;
    return 0;
}

/* 设置信号handler */
void process64_set_signal_handler(int pid, int signum, uint64_t handler) {    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].state != PROC_STATE_UNUSED) {
            if (signum >= 1 && signum < 32) {
                processes[i].signal_handlers[signum] = handler;
            }
            return;
        }
    }
}

/* 发送信号到进程：设置pending bitmap */
void process64_send_signal(int pid, int signum) {
    if (signum < 1 || signum >= 32) return;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].state != PROC_STATE_UNUSED) {
            processes[i].pending_signals |= (1 << signum);
            return;
        }
    }
}

/* 获取当前进程待处理信号位图 */
int process64_get_pending(int pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].state != PROC_STATE_UNUSED) {
            return processes[i].pending_signals;
        }
    }
    return 0;
}

/* 清除已处理的信号 */
void process64_clear_pending(int pid, int signum) {
    if (signum < 1 || signum >= 32) return;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].state != PROC_STATE_UNUSED) {
            processes[i].pending_signals &= ~(1 << signum);
            return;
        }
    }
}

/* 获取信号handler */
uint64_t process64_get_handler(int pid, int signum) {
    if (signum < 1 || signum >= 32) return 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].state != PROC_STATE_UNUSED) {
            return processes[i].signal_handlers[signum];
        }
    }
    return 0;
}

/* 信号投递：修改内核栈上的返回RIP到signal handler
 * 内核栈布局（rsp=0）：
 *   [rsp+0x48] = rdi (arg1)
 *   [rsp+0x60] = rcx (用户返回RIP)
 * 我们把rcx改成handler地址，rdi改成signum
 */
void process64_deliver_signals(uint64_t *kstack) {
    extern int user_process_get_current_pid(void);
    int pid = user_process_get_current_pid();
    int pending = process64_get_pending(pid);
    if (!pending) return;

    /* 找第一个pending的信号 */
    for (int sig = 1; sig < 32; sig++) {
        if (pending & (1 << sig)) {
            uint64_t handler = process64_get_handler(pid, sig);
            if (handler && handler != 1) {  /* 1=SIG_IGN, 0=SIG_DFL */
                /* 修改返回RIP到handler */
                kstack[0x60/8] = handler;   /* rcx = handler */
                kstack[0x48/8] = (uint64_t)sig;  /* rdi = signum */
                /* 清除pending */
                process64_clear_pending(pid, sig);
                vga_puts("[SIG] delivered to handler\n");
            }
            return;  /* 一次只投一个信号 */
        }
    }
}

/* 修改进程优先级
 * V0.59.3: 时间片要跟着一起重算 —— 旧实现只改了 priority，default_slice 仍是
 * 创建时的值，调度器按 slice 判轮转，于是 renice 看起来生效、实际权重没变。
 * 公式与 process64_create 保持一致。 */
int process64_set_priority(int pid, int prio) {
    if (prio < 0 || prio >= PRIORITY_COUNT) return -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].state != PROC_STATE_UNUSED) {
            processes[i].priority = prio;
            processes[i].default_slice = DEFAULT_TIME_SLICE * (PRIORITY_REALTIME - prio + 1) / 2;
            if (processes[i].default_slice < 10) processes[i].default_slice = 10;
            if (processes[i].time_slice > processes[i].default_slice)
                processes[i].time_slice = processes[i].default_slice;
            return 0;
        }
    }
    return -1;
}

/* 按环境类型统计进程数（内部按 domain_id 过滤） */
int process64_count_by_env(int env) {
    int count = 0;
    int dom = domain64_env_to_id(env);
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state != PROC_STATE_UNUSED && processes[i].domain_id == dom)
            count++;
    }
    return count;
}

/* OOM killer：杀掉该域内存最大的进程（内部按 domain_id 过滤） */
/* ============================================================================
 * V0.59.3: 外部控制 API —— kill / killall / renice / taskset 的内核侧入口
 *
 * 只改 PCB 字段与状态，不碰栈、页表、CR3：真正的资源回收交给父进程 waitpid
 * 或进程退出路径。两处同时释放同一块内存是这类"加一个 kill 却引出随机崩溃"
 * 的典型来路，所以这里刻意只做标记。
 * ========================================================================== */
static int proc_name_eq(const char *a, const char *b)
{
    int i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == 0 && b[i] == 0;
}

static process_t *proc_find(int pid)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].state != PROC_STATE_UNUSED) return &processes[i];
    }
    return 0;
}

/* 按 pid 终止：标 SIGKILL、置 ZOMBIE。返回 0 成功 / -1 无此进程 / -2 拒绝(idle 或自己) */
int process64_kill_pid(int pid, int code)
{
    if (pid <= 0) return -2;                 /* idle 与 pid 0 不允许杀 */
    if (pid == sched_cur_pid_internal()) return -2;
    process_t *pr = proc_find(pid);
    if (!pr) return -1;
    pr->pending_signals |= (1 << 9);         /* SIGKILL 记账, 供信号分发看 */
    pr->state = PROC_STATE_ZOMBIE;
    pr->exit_code = code;
    return 0;
}

/* 按名字终止：返回被杀数量 */
int process64_kill_by_name(const char *name, int code)
{
    if (!name || !name[0]) return -1;
    int cur = sched_cur_pid_internal();
    int n = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_STATE_UNUSED) continue;
        if (processes[i].pid <= 0 || processes[i].pid == cur) continue;
        if (!proc_name_eq(processes[i].name, name)) continue;
        processes[i].pending_signals |= (1 << 9);
        processes[i].state = PROC_STATE_ZOMBIE;
        processes[i].exit_code = code;
        n++;
    }
    return n;
}

/* 改 CPU 亲和性。注意本系统 AP 调度尚未开放（SB-25），绑到 AP 只是记账，
 * 真正生效要等 SB-25 —— 命令输出里会明确提示，不假装成功。 */
int process64_set_affinity(int pid, int cpu)
{
    extern uint32_t smp_get_cpu_count(void);
    uint32_t cnt = smp_get_cpu_count();
    if (cnt == 0) cnt = 1;
    if (cpu < 0) cpu = 0;
    if (cpu >= (int)cnt) cpu = (int)cnt - 1;
    process_t *pr = proc_find(pid);
    if (!pr) return -1;
    pr->cpu_id = cpu;
    return cpu;
}

int process64_get_priority(int pid)
{
    process_t *pr = proc_find(pid);
    return pr ? pr->priority : -1;
}

int process64_get_affinity(int pid)
{
    process_t *pr = proc_find(pid);
    return pr ? pr->cpu_id : -1;
}

int process64_oom_kill(int env) {
    int worst = -1;
    uint64_t worst_mem = 0;
    int dom = domain64_env_to_id(env);
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_STATE_UNUSED) continue;
        if (processes[i].domain_id != dom) continue;
        if (processes[i].pid == 1) continue;  /* 不杀init */
        if (processes[i].mem_pages > worst_mem) {
            worst_mem = processes[i].mem_pages;
            worst = i;
        }
    }
    if (worst >= 0) {
        vga_puts("[OOM] killing pid=");
        vga_put_hex(processes[worst].pid);
        vga_puts(" (");
        vga_put_hex(worst_mem);
        vga_puts(" pages)\n");
        extern int user_process_kill(int pid);
        user_process_kill(processes[worst].pid);
        return processes[worst].pid;
    }
    return -1;
}

/* 能力定义(域ACL) */
#define CAP_SYS_ADMIN    (1 << 0)  /* 系统管理 */
#define CAP_NET_RAW      (1 << 1)  /* 原始网络 */
#define CAP_RAW_DISK     (1 << 2)  /* 裸盘访问 */
#define CAP_X11          (1 << 3)  /* 图形访问 */

/* 检查进程是否有指定能力 */
int process64_has_cap(int pid, uint32_t cap) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].state != PROC_STATE_UNUSED) {
            return (processes[i].cap_mask & cap) != 0;
        }
    }
    return 0;
}

/* 设置进程能力 */
void process64_set_cap(int pid, uint32_t cap, int enable) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].state != PROC_STATE_UNUSED) {
            if (enable) processes[i].cap_mask |= cap;
            else processes[i].cap_mask &= ~cap;
            return;
        }
    }
}

/* ============================================================
 * 域统计（domain64.h 声明，实现在此因需访问 processes[]）
 * ============================================================ */

/* 按域 ID 统计内存占用（页） */
uint64_t domain64_mem_usage(int domain_id)
{
    uint64_t total = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state != PROC_STATE_UNUSED &&
            processes[i].domain_id == domain_id)
            total += processes[i].mem_pages;
    }
    return total;
}

/* 刷新所有域的 proc_count / mem_pages 统计 */
void domain64_refresh_stats(void)
{
    /* 先清零所有活动域的统计 */
    for (int d = 0; d < DOMAIN64_MAX_SLOTS; d++) {
        domain_t *dom = domain64_get(d);
        if (dom) {
            dom->proc_count = 0;
            dom->mem_pages = 0;
        }
    }
    /* 遍历进程，按 domain_id 累加 */
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_STATE_UNUSED) continue;
        domain_t *dom = domain64_get(processes[i].domain_id);
        if (dom) {
            dom->proc_count++;
            dom->mem_pages += processes[i].mem_pages;
        }
    }
}

/* 兼容旧接口：按 env_type 统计内存（内部转 domain_id） */
uint64_t process64_domain_mem_usage(int env)
{
    return domain64_mem_usage(domain64_env_to_id(env));
}
