#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stddef.h>
#include "paging.h"

#include "constants.h"  /* 统一 MAX_PROCESSES/PROC_NAME_LEN/KERNEL_STACK_SIZE/USER_STACK_SIZE 定义来源 */

/* V3.90-E36: 调度优先级分级(自 process.c 提升为公共定义) */
#define PRIO_RT    0   /* 实时: 抢占, 短片2, 交互/中断敏感 */
#define PRIO_NORM  1   /* 普通: 默认, 片20(IO感知 8/20/30 动态) */
#define PRIO_BKG   2   /* 后台: 片5, 仅无更高级就绪时运行 */

/* 进程状态 */
typedef enum {
    PROC_UNUSED = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_SLEEPING,   /* V2.25: 常驻进程挂起(PCB保留不销毁, 不参与调度) */
    PROC_BLOCKED,
    PROC_FROZEN,      /* 后台冻结：完全挂起，零调度零耗电 */
    PROC_ZOMBIE
} proc_state_t;

/* 进程特权级 */
typedef enum {
    PRIV_KERNEL = 0,
    PRIV_USER = 3
} privilege_t;

/* CPU寄存器状态（用于上下文切换） */
typedef struct {
    uint32_t edi, esi, ebp, esp;
    uint32_t ebx, edx, ecx, eax;
    uint32_t eip, cs, eflags;
    uint32_t user_esp, ss;  /* 用户态栈（中断返回时用） */
} cpu_state_t;

/* 进程控制块 PCB */
typedef struct process {
    uint32_t    pid;
    uint32_t    parent_pid;   /* 父进程PID */
    char        name[PROC_NAME_LEN];
    proc_state_t state;
    privilege_t privilege;

    uint32_t    kernel_stack;   /* 内核栈顶 */
    uint32_t    kernel_esp;     /* 进程切换时的内核栈指针 */
    uint32_t    user_stack;     /* 用户栈顶 */
    uint32_t    user_stack_raw; /* 用户栈 kheap 原始地址 (V3.77: 供退出释放, fork 子进程置 0 共享父栈) */
    uint32_t    entry_point;    /* 入口地址 */

    page_directory_t *page_dir; /* 进程页目录 */
    uint32_t    page_dir_owned; /* V3.78: 1=独占页目录(退出归还), 0=共享(内核目录/fork子进程) */

    cpu_state_t *cpu_state;     /* 寄存器状态保存位置 */

    /* V4.00-E45: 缓存友好布局——热字段聚簇.
     * 调度器每 tick(sched_tick)访问的字段集中到缓存行 64-127:
     *   priority@76 time_slice@80 time_used@84 preemptions@88
     *   sleep_until@92 ticks_total@96 blocked_ticks@100
     * 配合 state@40(缓存行0-63), 热路径仅触 2 个缓存行(原 4 行).
     * 注意: kernel_stack@48/kernel_esp@52/page_dir@68 汇编硬编码偏移
     * 在 72 之前, 本重排不影响; 72 之后字段顺序可自由调整. */
    uint32_t    priority;
    uint32_t    time_slice;
    uint32_t    time_used;      /* 已用时间片 */
    uint32_t    preemptions;    /* IO 感知动态时间片——被换出/主动让出次数 */

    uint32_t    sleep_until;    /* 真睡眠——唤醒 tick(绝对, 100Hz) */
    uint32_t    ticks_total;    /* 累计运行tick(100Hz), 供超时回收 */
    uint32_t    blocked_ticks;  /* 阻塞(睡眠)累计时长 tick(100Hz) */

    uint32_t    cpu_affinity;   /* CPU 亲和位图(bit i=允许跑在 CPU i; 默认 0x1) */
    uint32_t    is_app;         /* 1=用户 .hug 应用(可超时回收), 0=系统进程(常驻) */

    uint32_t    sandbox_id;     /* 沙盒隔离ID */
    struct process *next;       /* 调度链表 */

    uint32_t    compat_abi;     /* 0=HEPU原生, 1=Linux i386 */
    uint32_t    heap_start;     /* 用户堆 (brk) 区间 [heap_start, heap_end) */
    uint32_t    heap_end;

    uint32_t    mem_limit;        /* 内存限额(字节), 0=无限制 */
    uint32_t    mem_used;         /* 已记账内存(字节) */
    uint32_t    cpu_limit_ticks;  /* CPU 限额(tick@100Hz), 0=无限制; 超限降级 BKG */
    uint32_t    res_violations;   /* 资源违规次数 */
    uint32_t    mem_over_ticks;   /* 内存持续超限 tick 计数(100Hz) */
    int         exit_code;        /* V6.67: 进程退出码，供waitpid获取 */
    uint32_t    kernel_stack_raw; /* V6.67: 内核栈 kheap 原始地址 (页对齐分配时供退出释放) */
} process_t;

/* V3.87-E7: 应用默认内存限额(16MB)——.hug/ELF 用户程序创建时生效 */
#define APP_DEFAULT_MEM_LIMIT   (16U * 1024U * 1024U)

/* 用户 .hug 应用最大运行 tick(100Hz): 200 = 2 秒.
 * 超时视为失控(死循环), 调度器强制回收回 Shell. */
#define USER_APP_TIMEOUT_TICKS  200

/* 初始化进程管理 */
void process_init(void);

/* 全局进程表（供后台冻结等模块遍历） */
extern process_t proc_table[];
extern void proc_lock_acquire(void);   /* V3.89-E35: proc_table 表级锁 */
extern void proc_lock_release(void);

/* 创建进程 */
process_t *process_create(const char *name, uint32_t entry,
                          privilege_t priv, uint32_t priority);

/* V3.87-E7: 设置资源限额 */
void process_set_limits(process_t *p, uint32_t mem_limit,
                        uint32_t cpu_limit_ticks);

/* V3.87-E7: 内存记账——成功返回0; 超限拒绝并返回-1 */
int process_mem_charge(process_t *p, uint32_t bytes);

/* V3.87-E7: 内存退款(释放时核销) */
void process_mem_refund(process_t *p, uint32_t bytes);

/* 启动第一个用户进程（从内核态切换） */
void process_enter_user_mode(process_t *proc);

/* 上下文切换（汇编实现） */
extern void process_switch(process_t *next);

/* 当前运行进程（process.c 全局，汇编/兼容层可访问） */
extern process_t *current;

/* 调度器需要切换标志 */
extern volatile int need_resched;

/* 调度器主函数（选择下一个进程并切换） */
void schedule(void);

/* 获取当前进程 */
process_t *process_current(void);

/* 进程统计 */
void process_stats(uint32_t *total, uint32_t *running, uint32_t *ready);

#endif /* PROCESS_H */

/* === V0.42 新增：fork/execve 多进程支持 === */
int sys_fork_impl(void);
int sys_execve_impl(const char *path, char *const argv[], char *const envp[]);
int process_fork(void);
int process_execve(const char *path);
