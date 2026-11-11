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
} process_t;

_Static_assert((__builtin_offsetof(process_t, context) % 16) == 0,
               "process_context_t 在 PCB 里要 16 字节对齐, 否则 fx_area 失配");

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);
extern uint64_t pmm64_alloc_page(void);
extern uint64_t pmm64_alloc_pages(size_t count);
extern void pmm64_free_page(uint64_t addr);

static process_t processes[MAX_PROCESSES];

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
static int current_pid = 0;
static int next_pid = 1;
static int process_initialized = 0;
static uint64_t scheduler_ticks = 0;

/* Per-CPU当前进程索引（每个CPU独立运行） */
static volatile int per_cpu_current_idx[16];  /* 16 = MAX_CPUS */
static int next_cpu_assign = 0;  /* 轮询分配CPU */

/* 调度器自旋锁（保护多CPU下的共享进程表和就绪队列） */
static spinlock_t scheduler_lock;

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

    /* 初始化调度器自旋锁 */
    spinlock_init(&scheduler_lock);

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
    current_pid = 0;

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

/* 添加到就绪队列 */
static void add_to_ready_queue(int idx)
{
    int prio = processes[idx].priority;
    if (prio < 0 || prio >= PRIORITY_COUNT) prio = PRIORITY_NORMAL;
    if (ready_queue_count[prio] < MAX_PROCESSES) {
        ready_queue[prio][ready_queue_count[prio]++] = idx;
    }
}

/* 从就绪队列移除 */
static void remove_from_ready_queue(int idx)
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
    processes[idx].parent_pid = current_pid;
    processes[idx].domain_id = domain64_env_to_id(processes[idx].env_type);

    /* 复制名称 */
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

    /* 添加到就绪队列 */
    add_to_ready_queue(idx);

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
    int pid = process64_create(name, entry, priority);
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
    /* 回退：通过PID查找 */
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == current_pid) {
            return &processes[i];
        }
    }
    return NULL;
}

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

/* 优先级调度器（支持per-CPU） */
int process64_schedule(void)
{
    if (!process_initialized) return -1;

    /* 获取当前CPU ID */
    extern uint32_t smp_get_current_cpu(void);
    uint32_t cur_cpu = smp_get_current_cpu();

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
            add_to_ready_queue(old - processes);
        }

        processes[idx].state = PROC_STATE_RUNNING;
        processes[idx].context_switches++;
        current_pid = processes[idx].pid;
        per_cpu_current_idx[cur_cpu] = idx;

        /* 实际上下文切换 */
        if (old) {
            extern void context_switch_64(uint64_t *old_ctx, uint64_t *new_ctx);
            context_switch_64((uint64_t *)&old->context, (uint64_t *)&processes[idx].context);
        }

        return processes[idx].pid;
    }

    /* 如果当前CPU没有专属进程，选择任意就绪进程（工作窃取） */
    for (int prio = PRIORITY_REALTIME; prio >= PRIORITY_IDLE; prio--) {
        if (ready_queue_count[prio] > 0) {
            int idx = ready_queue[prio][0];
            for (int i = 0; i < ready_queue_count[prio] - 1; i++) {
                ready_queue[prio][i] = ready_queue[prio][i+1];
            }
            ready_queue_count[prio]--;

            process_t *old = process64_get_current();
            if (old && old->state == PROC_STATE_RUNNING) {
                old->state = PROC_STATE_READY;
                old->time_slice = old->default_slice;
                add_to_ready_queue(old - processes);
            }

            processes[idx].state = PROC_STATE_RUNNING;
            processes[idx].context_switches++;
            current_pid = processes[idx].pid;
            per_cpu_current_idx[cur_cpu] = idx;

            if (old) {
                extern void context_switch_64(uint64_t *old_ctx, uint64_t *new_ctx);
                context_switch_64((uint64_t *)&old->context, (uint64_t *)&processes[idx].context);
            }

            return processes[idx].pid;
        }
    }

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
    process_t *proc = process64_get_by_pid(pid);
    if (!proc || proc->state != PROC_STATE_READY) return -1;

    proc->state = PROC_STATE_BLOCKED;
    remove_from_ready_queue(proc - processes);
    return 0;
}

/* 进程唤醒 */
int process64_wakeup(int pid)
{
    process_t *proc = process64_get_by_pid(pid);
    if (!proc || proc->state != PROC_STATE_BLOCKED) return -1;

    proc->state = PROC_STATE_READY;
    add_to_ready_queue(proc - processes);
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
    current_pid = 0;
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

/* 修改进程优先级 */
int process64_set_priority(int pid, int prio) {
    if (prio < 0 || prio >= PRIORITY_COUNT) return -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].state != PROC_STATE_UNUSED) {
            processes[i].priority = prio;
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
