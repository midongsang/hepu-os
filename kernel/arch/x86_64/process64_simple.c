/* ============================================================================
 * 赫普OS - 64位进程管理（简化版）
 * V0.10.6: 初始实现（进程创建、调度框架）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 进程状态 */
#define PROCESS_STATE_READY   0
#define PROCESS_STATE_RUNNING 1
#define PROCESS_STATE_BLOCKED 2
#define PROCESS_STATE_ZOMBIE  3

/* 最大进程数 */
#define MAX_PROCESSES 64

/* 进程控制块 */
typedef struct {
    uint64_t pid;              /* 进程ID */
    uint64_t state;            /* 进程状态 */
    uint64_t rip;              /* 指令指针 */
    uint64_t rsp;              /* 栈指针 */
    uint64_t rbp;              /* 栈基址 */
    uint64_t rax, rbx, rcx, rdx;  /* 通用寄存器 */
    uint64_t rsi, rdi, r8, r9;    /* 通用寄存器 */
    uint64_t r10, r11, r12, r13;  /* 通用寄存器 */
    uint64_t r14, r15;            /* 通用寄存器 */
    uint64_t cr3;              /* 页表基址 */
    uint64_t kernel_stack;     /* 内核栈 */
    uint64_t user_stack;       /* 用户栈 */
    char name[32];             /* 进程名 */
} process64_t;

/* 进程表 */
static process64_t process_table[MAX_PROCESSES];
static uint64_t process_count = 0;
static uint64_t current_pid = 0;
static uint64_t next_pid = 1;

/* 外部函数 */
extern void vga_puts(const char *str);
extern void vga_put_hex(uint64_t val);
extern void *memset(void *dst, int val, size_t n);
extern void *memcpy(void *dst, const void *src, size_t n);

/* 初始化进程管理 */
void process64_init(void)
{
    vga_puts("[PROC64] Initializing 64-bit process manager...\n");

    /* 清零进程表 */
    memset(process_table, 0, sizeof(process_table));
    process_count = 0;
    current_pid = 0;
    next_pid = 1;

    /* 创建空闲进程（PID 0） */
    process64_t *idle = &process_table[0];
    idle->pid = 0;
    idle->state = PROCESS_STATE_RUNNING;
    idle->name[0] = 'i';
    idle->name[1] = 'd';
    idle->name[2] = 'l';
    idle->name[3] = 'e';
    idle->name[4] = '\0';

    current_pid = 0;
    process_count = 1;

    vga_puts("[PROC64] Process manager initialized\n");
    vga_puts("[PROC64] Idle process created (PID 0)\n");
}

/* 创建新进程 */
uint64_t process64_create(const char *name, uint64_t entry_point, uint64_t stack_size)
{
    if (process_count >= MAX_PROCESSES) {
        vga_puts("[PROC64] ERROR: Process table full\n");
        return -1;
    }

    /* 找到空闲槽位 */
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROCESS_STATE_ZOMBIE ||
            (process_table[i].pid == 0 && i != 0)) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        vga_puts("[PROC64] ERROR: No free slot\n");
        return -1;
    }

    /* 初始化进程 */
    process64_t *proc = &process_table[slot];
    memset(proc, 0, sizeof(process64_t));

    proc->pid = next_pid++;
    proc->state = PROCESS_STATE_READY;
    proc->rip = entry_point;
    proc->rsp = stack_size;
    proc->rbp = stack_size;
    proc->kernel_stack = (uint64_t)proc + 4096;  /* 简单的内核栈 */

    /* 复制进程名 */
    if (name) {
        int i = 0;
        while (name[i] && i < 31) {
            proc->name[i] = name[i];
            i++;
        }
        proc->name[i] = '\0';
    }

    process_count++;

    vga_puts("[PROC64] Created process: ");
    vga_puts(proc->name);
    vga_puts(" (PID ");
    vga_put_hex(proc->pid);
    vga_puts(")\n");

    return proc->pid;
}

/* 终止进程 */
void process64_exit(uint64_t pid)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid == pid) {
            process_table[i].state = PROCESS_STATE_ZOMBIE;
            process_count--;

            vga_puts("[PROC64] Process exited: PID ");
            vga_put_hex(pid);
            vga_puts("\n");

            /* 如果是当前进程，切换到空闲进程 */
            if (pid == current_pid) {
                current_pid = 0;
            }
            return;
        }
    }
}

/* 获取当前进程 */
process64_t *process64_get_current(void)
{
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid == current_pid) {
            return &process_table[i];
        }
    }
    return &process_table[0];  /* 返回空闲进程 */
}

/* 调度器（简单的轮询调度） */
void process64_schedule(void)
{
    /* 找到下一个就绪进程 */
    uint64_t next_pid_to_run = 0;
    int found = 0;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROCESS_STATE_READY) {
            next_pid_to_run = process_table[i].pid;
            found = 1;
            break;
        }
    }

    if (found) {
        /* 保存当前进程状态 */
        process64_t *current = process64_get_current();
        if (current->state == PROCESS_STATE_RUNNING) {
            current->state = PROCESS_STATE_READY;
        }

        /* 切换到新进程 */
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (process_table[i].pid == next_pid_to_run) {
                process_table[i].state = PROCESS_STATE_RUNNING;
                current_pid = next_pid_to_run;
                break;
            }
        }
    }
}

/* 获取进程统计 */
void process64_get_stats(uint64_t *total, uint64_t *running, uint64_t *ready)
{
    *total = process_count;
    *running = 0;
    *ready = 0;

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROCESS_STATE_RUNNING) {
            (*running)++;
        } else if (process_table[i].state == PROCESS_STATE_READY) {
            (*ready)++;
        }
    }
}

/* 列出所有进程 */
void process64_list(void)
{
    vga_puts("\n[PROC64] Process list:\n");
    vga_puts("  PID  State  Name\n");
    vga_puts("  ---  -----  ----\n");

    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].pid != 0 || i == 0) {
            if (process_table[i].state != PROCESS_STATE_ZOMBIE) {
                vga_puts("  ");
                vga_put_hex(process_table[i].pid);
                vga_puts("  ");

                switch (process_table[i].state) {
                    case PROCESS_STATE_RUNNING: vga_puts("RUN  "); break;
                    case PROCESS_STATE_READY:   vga_puts("READY"); break;
                    case PROCESS_STATE_BLOCKED: vga_puts("BLOCK"); break;
                    default: vga_puts("???  "); break;
                }

                vga_puts("  ");
                vga_puts(process_table[i].name);
                vga_puts("\n");
            }
        }
    }
}
