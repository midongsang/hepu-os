/* ============================================================================
 * 赫普OS - 64位进程管理和调度
 * V8.88: 初始实现
 * 进程上下文、创建、调度、退出
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "x86_64.h"
#include "../../include/vga.h"
#include "../../include/kheap.h"

/* 外部汇编函数 */
extern void context_switch_64(void *old_ctx, void *new_ctx);
extern void process_start_64(void);

/* 前向声明 */
static void idle_loop(void);

/* 全局变量 */
#define MAX_PROCESSES_64 128
static process_64_t g_processes[MAX_PROCESSES_64];
static int g_process_count = 0;
static int g_current_pid = -1;
static int g_next_pid = 1;
static int g_scheduler_initialized = 0;

/* 空闲进程 */
static process_64_t *g_idle_process = NULL;

/* ============================================================================
 * 进程管理
 * ============================================================================ */

int process_64_init(void)
{
    if (g_scheduler_initialized) return 0;

    memset(g_processes, 0, sizeof(g_processes));
    g_process_count = 0;
    g_current_pid = -1;
    g_next_pid = 1;

    /* 创建空闲进程 */
    g_idle_process = process_64_create("idle", (uint64_t)idle_loop, 0, 0);
    if (g_idle_process) {
        g_idle_process->state = PROC64_READY;
        g_idle_process->priority = 0;  /* 最低优先级 */
    }

    g_scheduler_initialized = 1;
    vga_puts("[PROC64] 64-bit process manager initialized\n");
    return 0;
}

/* 空闲循环 */
static void idle_loop(void)
{
    while (1) {
        __asm__ volatile("hlt");
    }
}

process_64_t *process_64_create(const char *name, uint64_t entry,
                                int priority, int flags)
{
    if (!g_scheduler_initialized) return NULL;

    /* 查找空闲PCB */
    process_64_t *proc = NULL;
    for (int i = 0; i < MAX_PROCESSES_64; i++) {
        if (!g_processes[i].used) {
            proc = &g_processes[i];
            break;
        }
    }
    if (!proc) {
        vga_puts("[PROC64] No free process slot\n");
        return NULL;
    }

    /* 初始化PCB */
    memset(proc, 0, sizeof(process_64_t));
    proc->pid = g_next_pid++;
    if (name) {
        strncpy(proc->name, name, 63);
    }
    proc->state = PROC64_READY;
    proc->priority = priority > 0 ? priority : 10;
    proc->time_slice = proc->priority * 10;  /* 时间片与优先级相关 */
    proc->remaining_time = proc->time_slice;
    proc->entry_point = entry;
    proc->used = 1;

    /* 分配内核栈 */
    proc->stack_bottom = (uint64_t)kmalloc(65536);  /* 64KB */
    if (!proc->stack_bottom) {
        vga_puts("[PROC64] Failed to allocate kernel stack\n");
        proc->used = 0;
        return NULL;
    }
    proc->stack_top = proc->stack_bottom + 65536;

    /* 设置初始上下文 */
    proc->context.rsp = proc->stack_top - 8;
    proc->context.rbp = proc->context.rsp;
    proc->context.rip = entry;
    proc->context.cr3 = 0;  /* 使用内核页表 */

    /* 在栈上设置返回地址（指向process_start_64） */
    uint64_t *stack = (uint64_t *)proc->context.rsp;
    *stack = (uint64_t)process_start_64;

    g_process_count++;
    vga_printf("[PROC64] Created process %d: %s\n", proc->pid, proc->name);
    return proc;
}

int process_64_destroy(int pid)
{
    for (int i = 0; i < MAX_PROCESSES_64; i++) {
        if (g_processes[i].used && g_processes[i].pid == pid) {
            /* 释放栈 */
            if (g_processes[i].stack_bottom) {
                kfree((void *)g_processes[i].stack_bottom);
            }
            g_processes[i].used = 0;
            g_process_count--;
            vga_printf("[PROC64] Destroyed process %d\n", pid);
            return 0;
        }
    }
    return -1;
}

process_64_t *process_64_get(int pid)
{
    for (int i = 0; i < MAX_PROCESSES_64; i++) {
        if (g_processes[i].used && g_processes[i].pid == pid) {
            return &g_processes[i];
        }
    }
    return NULL;
}

process_64_t *process_64_current(void)
{
    if (g_current_pid < 0) return NULL;
    return process_64_get(g_current_pid);
}

int process_64_count(void)
{
    return g_process_count;
}

/* ============================================================================
 * 调度器
 * ============================================================================ */

/* 选择下一个要运行的进程 */
static process_64_t *scheduler_pick_next(void)
{
    process_64_t *best = NULL;
    int best_priority = -1;

    for (int i = 0; i < MAX_PROCESSES_64; i++) {
        process_64_t *proc = &g_processes[i];
        if (!proc->used || proc->state != PROC64_READY) continue;

        /* 选择优先级最高的进程 */
        if (proc->priority > best_priority) {
            best = proc;
            best_priority = proc->priority;
        }
    }

    /* 如果没有就绪进程，返回空闲进程 */
    return best ? best : g_idle_process;
}

/* 调度器主函数（在定时器中断中调用） */
void scheduler_64_tick(void)
{
    process_64_t *current = process_64_current();
    if (!current) return;

    /* 减少时间片 */
    current->remaining_time--;
    current->user_time++;

    /* 时间片用完或当前进程阻塞，进行调度 */
    if (current->remaining_time <= 0 ||
        current->state != PROC64_RUNNING) {
        scheduler_64_yield();
    }
}

/* 主动让出CPU */
void scheduler_64_yield(void)
{
    process_64_t *current = process_64_current();
    process_64_t *next = scheduler_pick_next();

    if (!next || next == current) return;

    /* 更新状态 */
    if (current && current->state == PROC64_RUNNING) {
        current->state = PROC64_READY;
    }
    next->state = PROC64_RUNNING;
    next->remaining_time = next->time_slice;
    next->context_switches++;

    g_current_pid = next->pid;

    /* 上下文切换 */
    if (current) {
        context_switch_64(&current->context, &next->context);
    } else {
        /* 第一次调度，直接加载新上下文 */
        context_switch_64(NULL, &next->context);
    }
}

/* 阻塞当前进程 */
int process_64_block(void)
{
    process_64_t *current = process_64_current();
    if (!current) return -1;

    current->state = PROC64_BLOCKED;
    scheduler_64_yield();
    return 0;
}

/* 唤醒进程 */
int process_64_wakeup(int pid)
{
    process_64_t *proc = process_64_get(pid);
    if (!proc) return -1;

    if (proc->state == PROC64_BLOCKED ||
        proc->state == PROC64_SLEEPING) {
        proc->state = PROC64_READY;
        return 0;
    }
    return -1;
}

/* 进程睡眠 */
int process_64_sleep(uint64_t milliseconds)
{
    process_64_t *current = process_64_current();
    if (!current) return -1;

    /* 简化：直接阻塞，实际应该设置唤醒时间 */
    current->state = PROC64_SLEEPING;
    scheduler_64_yield();
    return 0;
}

/* 进程退出 */
void process_64_exit(int exit_code)
{
    process_64_t *current = process_64_current();
    if (!current) return;

    vga_printf("[PROC64] Process %d exited with code %d\n",
               current->pid, exit_code);

    current->state = PROC64_ZOMBIE;
    scheduler_64_yield();
}

/* ============================================================================
 * 统计信息
 * ============================================================================ */

void process_64_get_stats(int *total, int *running, int *ready,
                          int *blocked, int *sleeping)
{
    if (total) *total = 0;
    if (running) *running = 0;
    if (ready) *ready = 0;
    if (blocked) *blocked = 0;
    if (sleeping) *sleeping = 0;

    for (int i = 0; i < MAX_PROCESSES_64; i++) {
        if (!g_processes[i].used) continue;
        if (total) (*total)++;
        switch (g_processes[i].state) {
            case PROC64_RUNNING: if (running) (*running)++; break;
            case PROC64_READY: if (ready) (*ready)++; break;
            case PROC64_BLOCKED: if (blocked) (*blocked)++; break;
            case PROC64_SLEEPING: if (sleeping) (*sleeping)++; break;
            default: break;
        }
    }
}

/* 列出所有进程 */
int process_64_list(process_64_t **out, int max)
{
    if (!out || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < MAX_PROCESSES_64 && count < max; i++) {
        if (g_processes[i].used) {
            out[count++] = &g_processes[i];
        }
    }
    return count;
}
