/* ============================================================================
 * 赫普OS - 进程调度器 (微内核原型)
 * 实现：简单轮转调度框架
 *
 * 【V2.01标注·死代码】此文件已被 kernel/sched/process.c + kernel/arch/x86/kernel_core.c
 * 的完整调度实现取代，Makefile 通过 filter-out 显式排除不参与编译(内核ELF中0符号)。
 * 保留仅供历史参考，勿修改；如需复用调度逻辑请参考 process.c。
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "sched.h"
#include "vga.h"

#define MAX_PROCESSES 64
#define PROC_NAME_LEN 32

/* 进程状态 */
typedef enum {
    PROC_UNUSED = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_ZOMBIE
} proc_state_t;

/* 进程控制块 */
typedef struct {
    uint32_t    pid;
    char        name[PROC_NAME_LEN];
    proc_state_t state;
    uint32_t    esp;        /* 栈指针 */
    uint32_t    eip;        /* 指令指针 */
    uint32_t    cr3;        /* 页目录 */
    uint32_t    priority;
    uint32_t    time_slice;
    uint32_t    sandbox_id; /* 沙盒隔离ID */
} process_t;

static process_t proc_table[MAX_PROCESSES];
static uint32_t  next_pid = 1;
static uint32_t  current_proc = 0;
static uint32_t  proc_count = 0;

void sched_init(void)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        proc_table[i].state = PROC_UNUSED;
        proc_table[i].pid = 0;
    }
    vga_puts("[SCHED] scheduler init done \n");
}

/* 创建进程（原型：仅注册，不实际切换上下文） */
uint32_t sched_create_process(const char *name, uint32_t entry, uint32_t priority)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_UNUSED) {
            proc_table[i].pid = next_pid++;
            proc_table[i].state = PROC_READY;
            proc_table[i].eip = entry;
            proc_table[i].priority = priority;
            proc_table[i].time_slice = 10;
            proc_table[i].sandbox_id = i;
            for (int j = 0; j < PROC_NAME_LEN - 1 && name[j]; j++) {
                proc_table[i].name[j] = name[j];
            }
            proc_table[i].name[PROC_NAME_LEN - 1] = '\0';
            proc_count++;
            return proc_table[i].pid;
        }
    }
    return 0; /* 进程表满 */
}

/* V4.51-E96: 性能指标——调度切换次数 + idle tick 计数 */
static uint32_t sched_switch_count = 0;
static uint32_t sched_idle_count = 0;

/* 调度器主循环（原型：轮转，未实现实际上下文切换） */
void sched_tick(void)
{
    /* 找到下一个READY状态的进程 */
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        uint32_t idx = (current_proc + 1 + i) % MAX_PROCESSES;
        if (proc_table[idx].state == PROC_READY) {
            if (proc_table[current_proc].state == PROC_RUNNING) {
                proc_table[current_proc].state = PROC_READY;
            }
            proc_table[idx].state = PROC_RUNNING;
            current_proc = idx;
            sched_switch_count++;   /* V4.51-E96: 调度切换 */
            return;
        }
    }
    sched_idle_count++;   /* V4.51-E96: 无就绪进程=idle */
}

/* 获取当前运行进程PID */
uint32_t sched_current_pid(void)
{
    if (proc_table[current_proc].state == PROC_RUNNING) {
        return proc_table[current_proc].pid;
    }
    return 0;
}

/* 进程统计 */
void sched_stats(uint32_t *total, uint32_t *running, uint32_t *ready)
{
    uint32_t r = 0, run = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_READY) r++;
        if (proc_table[i].state == PROC_RUNNING) run++;
    }
    if (total)   *total = proc_count;
    if (running) *running = run;
    if (ready)   *ready = r;
}

/* V4.51-E96: 性能指标查询 */
void sched_perf_stats(uint32_t *switches, uint32_t *idle_ticks)
{
    if (switches)   *switches = sched_switch_count;
    if (idle_ticks) *idle_ticks = sched_idle_count;
}
