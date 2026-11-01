/* ============================================================================
 * 赫普OS 64位 - 用户态进程管理（增强版）
 * V0.10.11: 优先级调度、时间片、进程统计、阻塞/唤醒
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "spinlock.h"

#define MAX_PROCESSES 32
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
} __attribute__((packed)) process_context_t;

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
} process_t;

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);
extern uint64_t pmm64_alloc_page(void);
extern uint64_t pmm64_alloc_pages(size_t count);
extern void pmm64_free_page(uint64_t addr);

static process_t processes[MAX_PROCESSES];
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

static void proc_memcpy(void *dst, const void *src, int n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}

static void proc_memset(void *dst, uint8_t val, int n) {
    uint8_t *d = (uint8_t*)dst;
    for (int i = 0; i < n; i++) d[i] = val;
}

/* 初始化进程管理 */
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
    processes[0].default_slice = 10;
    processes[0].time_slice = 10;
    processes[0].context.rsp = 0;  /* 运行时由context_switch保存 */
    processes[0].context.rbp = 0;
    proc_memset(processes[0].name, 0, 32);
    processes[0].name[0] = 'i'; processes[0].name[1] = 'd';
    processes[0].name[2] = 'l'; processes[0].name[3] = 'e';
    current_pid = 0;

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
    processes[idx].pid = next_pid++;
    processes[idx].state = PROC_STATE_READY;
    processes[idx].priority = (priority >= 0 && priority < PRIORITY_COUNT) ? priority : PRIORITY_NORMAL;

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
        return -1;
    }
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

    /* 从高优先级到低优先级查找就绪进程（优先选择分配给当前CPU的进程） */
    for (int prio = PRIORITY_REALTIME; prio >= PRIORITY_IDLE; prio--) {
        for (int i = 0; i < ready_queue_count[prio]; i++) {
            int idx = ready_queue[prio][i];
            /* 优先选择分配给当前CPU的进程 */
            if (processes[idx].cpu_id == (int)cur_cpu || processes[idx].cpu_id == 0) {
                /* 从队列移除 */
                for (int j = i; j < ready_queue_count[prio] - 1; j++) {
                    ready_queue[prio][j] = ready_queue[prio][j+1];
                }
                ready_queue_count[prio]--;

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
                if (old && old->stack) {
                    extern void context_switch_64(uint64_t *old_ctx, uint64_t *new_ctx);
                    context_switch_64((uint64_t *)&old->context, (uint64_t *)&processes[idx].context);
                }

                return processes[idx].pid;
            }
        }
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

            if (old && old->stack) {
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

void process64_tick(void)
{
    process_t *curr = process64_get_current();
    if (!curr) return;

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
