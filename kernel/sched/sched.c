/* ============================================================================
 * 赫普OS - 调度器
 * 从 process.c 拆分（V6.46 代码重构，大文件拆分目标<500行）
 * 包含：sched_tick/sched_mem_enforce/sched_pick_next/schedule/
 *       sched_init/sched_current_pid/sched_stats/sched_create_process/
 *       process_fork/process_execve/sys_fork_impl/sys_execve_impl
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "process.h"
#include "sched.h"
#include "paging.h"
#include "vga.h"
#include "syscall.h"
#include "elf.h"
#include "constants.h"
#include "kheap.h"
#include "smp.h"

/* 依赖 process.c 中的全局变量 */
extern spinlock_t proc_lock;
extern uint32_t proc_lock_flags;
extern uint32_t next_pid;
extern uint32_t sched_switches;
extern uint32_t sched_idle_ticks;
extern uint32_t sched_ticks_total;
extern process_t proc_table[];
extern uint32_t sched_ready_bitmap;
extern void sched_ready_mark(uint32_t prio);
extern void sched_ready_clear(uint32_t prio);

void sched_tick(void)
{
    /* V3.92-E38 修复: 系统 tick 移到入口——current 无效(idle)时同样
     * 计数. 此前在唤醒扫描后递增, current 无效时 sched_tick 提前
     * return, sched_ticks_total 恒=1, 导致 softirq 演示/定时类逻辑
     * (t - demo_last >= 25)永不触发, 睡眠唤醒判定也失真. */
    sched_ticks_total++;
    if (!proc_current_valid()) {
        current = NULL;
        sched_idle_ticks++;   /* V4.51-E96: 无运行进程=idle */
        return;
    }

    /* V3.89-E27 修复: 调度器活跃 = 系统活跃, 每 tick 喂软看门狗.
     * 此前 wd 只在键盘中断/睡眠/主循环喂狗——用户态进程(如调度
     * 演示 A/B, is_app=0 常驻类)长跑时无人喂狗 → 5s 误判心跳超时
     * → wd_trigger 硬复位(表现为 schtest 一执行系统就"重启"). */
    extern void wd_feed(void);
    wd_feed();

    /* V3.89-E27: 唤醒扫描——睡眠到点的进程转 READY.
     * 注意: 唤醒发生在计数前, 保证睡眠进程不消耗本片运行时间.
     * V3.89-E35: 唤醒扫描加表锁(中断上下文, irqsave 安全; 锁内仅改 state).
     * 注: 早前"中断持锁键盘失效"系 sys_exit 的 sti 被 irqrestore 撤销所致,
     * 已在 sys_exit 修复(sti 移至 release 后), 此处锁可安全持有. */
    proc_lock_acquire();
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        /* V3.95-E41: 阻塞时长统计——睡眠中的进程每 tick 累计(含永久睡眠) */
        if (proc_table[i].state == PROC_SLEEPING) {
            proc_table[i].blocked_ticks++;
        }
        if (proc_table[i].state == PROC_SLEEPING &&
            proc_table[i].sleep_until != 0 &&
            sched_ticks_total >= proc_table[i].sleep_until) {
            proc_table[i].state = PROC_READY;
            proc_table[i].sleep_until = 0;
            sched_ready_mark(proc_table[i].priority);   /* V3.90-E36 */
            /* V3.89-E30 修复: 唤醒"非当前进程"时必须置 need_resched——
             * 否则若当前进程是待回收(SLEEPING)或长时间占用, 新就绪的
             * B 永远不被调度(schtest A 退出后 B 卡死, 永不退出). */
            if (&proc_table[i] != current) {
                extern volatile int need_resched;
                need_resched = 1;
            }
        }
    }
    proc_lock_release();   /* V3.89-E35: 唤醒扫描结束释放表锁 */

    /* 当前进程若在睡眠(刚被唤醒扫描前仍是 SLEEPING)则不累计运行时间 */
    if (current->state == PROC_SLEEPING) {
        /* 当前正在睡眠: 不消耗时间片; 状态由唤醒扫描或 schedule 切换处理 */
        return;
    }

    current->time_used++;
    current->ticks_total++;

    /* V3.86: 用户 .hug 应用超时回收——Shell 跑在内核主线(不是调度器
     * 管理的进程), 死循环应用抢占后仍只有自己可跑 → 系统永远锁死.
     * 超时(默认2秒)强制走 sys_exit 清理路径(释放进程资源 + longjmp
     * 回 Shell, V3.85 已验证在中断上下文可安全执行).
     * 注意: 必须先手动发 EOI——irq_handler 的 pic_send_eoi 在
     * pit_handler 返回之后执行, longjmp 会跳过它 → 主PIC被屏蔽. */
    /* V3.91-E37: 内存限额处置——移入独立 noinline 函数 sched_mem_enforce()
     * (E-33 教训: sched_tick 中断路径栈帧增长触发偶发 ISR 6; 独立函数
     * 帧隔离, 主 tick 路径仅 +4B 调用返回地址). */
    extern void sched_mem_enforce(void);
    sched_mem_enforce();

    /* V3.87-E7/E10: CPU 限额——超限先降级 BKG; 降级后仍持续超限
     * 运行 1 秒(100 ticks)则强制回收(防单个进程永久霸占 CPU). */
    if (current->cpu_limit_ticks &&
        current->ticks_total > current->cpu_limit_ticks) {
        current->res_violations++;
        if (current->priority != 2 /* PRIO_BKG */) {
            current->priority = 2;
            vga_printf("[SCHED] pid=%d cpu-limit exceeded, demoted BKG\n",
                       current->pid);
        }
        if (current->res_violations >=
            current->cpu_limit_ticks + 100 /* 超限后再跑1秒 */) {
            extern void pic_send_eoi(uint32_t);
            extern uint32_t sys_exit(uint32_t, uint32_t, uint32_t, uint32_t);
            pic_send_eoi(0);
            
            vga_printf( "[SCHED] pid=%d cpu-limit kill (viol=%d ticks=%d)\n",
                            current->pid, current->res_violations,
                            current->ticks_total);
            vga_printf("[SCHED] pid=%d cpu-limit kill (viol=%d ticks=%d)\n",
                       current->pid, current->res_violations,
                       current->ticks_total);
            sys_exit(99, 0, 0, 0);
            return;   /* longjmp 后不可达 */
        }
    }

    if (current->is_app && current->ticks_total > USER_APP_TIMEOUT_TICKS) {
        extern void pic_send_eoi(uint32_t);
        extern uint32_t sys_exit(uint32_t, uint32_t, uint32_t, uint32_t);
        /* V3.87 调试: 打印卡死用户进程的中断帧 EIP/ESP (定位用户态死循环) */
        {
            uint32_t *fr = (uint32_t *)current->kernel_esp;
            /* 帧: ds@0 pusha@1-8 irq@9 err@10 eip@11 cs@12 eflags@13
             * user_esp@14 ss@15 */
            vga_printf("[SCHED] kill frame: eip=0x%x cs=0x%x esp=0x%x\n",
                       fr[11], fr[12], fr[14]);
            /* V3.87 B-2 诊断: dump 用户栈顶残留(PE 兼容进程) */
            if (current->compat_abi == 2 && current->page_dir) {
                extern uint32_t paging_get_phys(page_directory_t *,
                                                uint32_t);
                uint32_t usp = fr[14];
                uint32_t i;
                for (i = 0; i < 12; i++) {
                    uint32_t va = usp - 0x20 + i * 4;
                    uint32_t phys = paging_get_phys(current->page_dir, va);
                    vga_printf("[SCHED] usp%c0x%x: 0x%x\n",
                               (i % 3 == 0) ? ' ' : ' ',
                               va, phys ?
                               *(const uint32_t *)(uintptr_t)phys : 0);
                }
            }
        }
        pic_send_eoi(0);
        vga_printf("[SCHED] user app timeout kill pid=%d ticks=%d\n",
                   current->pid, current->ticks_total);
        sys_exit(99 /* SIGKILL 模拟 */, 0, 0, 0);
        return;   /* longjmp 后不可达 */
    }

    if (current->time_used >= current->time_slice) {
        /* V3.89-E27: IO 感知动态时间片——按本片内换出次数调整下一片:
         * 频繁让出(>=4)=交互/IO型 → 短片8(更快响应);
         * 零让出=计算型 → 长片30(减少上下文切换开销);
         * 1-3 次=混合 → 默认20.
         * RT(2) 固定不参与(实时性优先), BKG(5) 固定(后台不抢时间). */
        if (current->priority == 1 /* PRIO_NORM */) {
            uint32_t ns;
            if (current->preemptions >= 4)      ns = 8;
            else if (current->preemptions == 0) ns = 30;
            else                                ns = 20;
            if (ns != current->time_slice) {
                current->time_slice = ns;
                extern uint32_t slice_adj_count;
                slice_adj_count++;   /* V3.89-E27: 动态调整统计 */
            }
        }
        current->preemptions = 0;
        current->time_used = 0;
        need_resched = 1;
    }

}

/* V3.91-E37: 内存限额处置(独立 noinline 函数——E-33 教训: 中断路径
 * 栈帧增长触发偶发 ISR 6, 故不内联进 sched_tick; 无参数, 仅 +4B 返回
 * 地址, sched_tick 自身栈帧零增长). 仅 current 有效且超限时执行. */
__attribute__((noinline))
void sched_mem_enforce(void)
{
    if (!current) return;
    if (!current->mem_limit) { current->mem_over_ticks = 0; return; }
    if (current->mem_used > current->mem_limit) {
        current->mem_over_ticks++;
        if (current->priority != PRIO_BKG) {
            current->priority = PRIO_BKG;
            vga_printf("[SCHED] pid=%d mem-limit exceeded, demoted BKG\n",
                       current->pid);
        }
        if (current->mem_over_ticks >= 200) {
            extern void pic_send_eoi(uint32_t);
            extern uint32_t sys_exit(uint32_t, uint32_t, uint32_t, uint32_t);
            
            pic_send_eoi(0);
            vga_printf( "[SCHED] pid=%d mem-limit kill (over=%d ticks used=%d/%d)\n",
                            current->pid, current->mem_over_ticks,
                            current->mem_used, current->mem_limit);
            vga_printf("[SCHED] pid=%d mem-limit kill (over=%d ticks used=%d/%d)\n",
                       current->pid, current->mem_over_ticks,
                       current->mem_used, current->mem_limit);
            sys_exit(99, 0, 0, 0);
            return;   /* longjmp 后不可达 */
        }
    } else {
        current->mem_over_ticks = 0;
    }
}

/* 调度器主函数：选择下一个就绪进程
 * 注意：此函数在中断上下文中调用，调用者已保存当前esp
 * 返回新的current指针，调用者负责切换栈 */
process_t *sched_pick_next(void)
{
    if (!current) return NULL;

    /* V3.90-E36: 就绪位图加速——ctz 定位最高优先级(最小 prio)级,
     * 级内从 current 之后轮转; 位图仅作提示, 级内扫描以实际
     * state+priority 为准, 位图滞后则回退下一级/全表扫描兜底.
     * RT 就绪时抢占低优先级(与 V3.87 语义一致). */
    int current_idx = current - proc_table;
    uint32_t bm = sched_ready_bitmap;
    while (bm) {
        int prio = __builtin_ctz(bm);   /* 最低置位 bit = 最高优先级 */
        bm &= bm - 1;
        if (prio > PRIO_BKG) continue;  /* 防御: 越界 bit */
        /* V6.68: 跳过current_idx本身，避免调度器总是选择当前进程 */
        for (int i = 1; i < MAX_PROCESSES; i++) {
            int idx = (current_idx + i) % MAX_PROCESSES;
            if (proc_table[idx].state == PROC_READY &&
                proc_table[idx].priority == prio &&
                (proc_table[idx].cpu_affinity & 0x1u)) {   /* E-42 亲和 */
                return &proc_table[idx];
            }
        }
        /* 该级无实际就绪(位图滞后) → 尝试下一级 */
    }

    /* 兜底: 全表扫描(位图与状态不同步时)——等价 V3.87 原逻辑 */
    for (int prio = PRIO_RT; prio <= PRIO_BKG; prio++) {
        /* V6.68: 跳过current_idx本身 */
        for (int i = 1; i < MAX_PROCESSES; i++) {
            int idx = (current_idx + i) % MAX_PROCESSES;
            if (proc_table[idx].state == PROC_READY &&
                proc_table[idx].priority == prio &&
                (proc_table[idx].cpu_affinity & 0x1u)) {   /* E-42 亲和 */
                return &proc_table[idx];
            }
        }
    }

    /* 没有其他就绪进程，继续运行当前进程 */
    return current;
}

/* 执行调度：更新current和状态
 * 由汇编代码调用，在保存当前esp之后、切换栈之前 */
void schedule(void)
{
    if (!current) return;

    process_t *next = sched_pick_next();
    if (next == current) {
        need_resched = 0;
        return;
    }

    /* V3.87-E1: 上下文切换统计 */
    sched_switches++;

    /* V3.89-E27: 被换出进程的片内让出计数(动态时间片输入信号) */
    current->preemptions++;

    /* 状态切换 */
    if (current->state == PROC_RUNNING) {
        current->state = PROC_READY;
        sched_ready_mark(current->priority);   /* V3.90-E36 */
    }
    next->state = PROC_RUNNING;
    sched_ready_clear(next->priority);   /* V3.90-E36 */

    current = next;
    need_resched = 0;

    /* 设置TSS内核栈 */
    extern void tss_set_kernel_stack(uint32_t);
    tss_set_kernel_stack(current->kernel_stack);
}

/* ============================================================================
 * sched.h 接口兼容层
 * ========================================================================== */

void sched_init(void)
{
    process_init();
}

uint32_t sched_current_pid(void)
{
    /* V2.23修复: 仅当前进程仍处于 RUNNING 时返回其PID;
     * 进程冻结/销毁后返回 0, 避免残留已销毁PID导致数据不一致. */
    if (current && current->state == PROC_RUNNING) return current->pid;
    return 0;
}

void sched_stats(uint32_t *total, uint32_t *running, uint32_t *ready)
{
    process_stats(total, running, ready);
}

uint32_t sched_create_process(const char *name, uint32_t entry, uint32_t priority)
{
    process_t *proc = process_create(name, entry, PRIV_USER, priority);
    if (proc) return proc->pid;
    return 0;
}

/* === V0.42 新增：fork/execve 多进程支持 === */

/* fork：创建当前进程的副本
 * 父进程返回子进程PID，子进程返回0
 */
int process_fork(void)
{
    if (!current) return -1;

    /* 找空闲PCB */
    process_t *child = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_UNUSED) {
            child = &proc_table[i];
            break;
        }
    }
    if (!child) return -1;

    /* 复制父进程状态 */
    uint32_t parent_pid = current->pid;
    memcpy(child, current, sizeof(process_t));

    /* 子进程独立属性 */
    child->pid = next_pid++;
    child->state = PROC_READY;
    sched_ready_mark(child->priority);   /* V3.90-E36 */
    child->parent_pid = parent_pid;
    child->time_used = 0;
    child->time_slice = current->time_slice;

    /* 分配新的内核栈（V6.66: 页对齐分配，避免与用户栈共享物理页导致栈内容被覆盖） */
    uint32_t kstack_raw = (uint32_t)kmalloc(KERNEL_STACK_SIZE + PAGE_SIZE);
    if (!kstack_raw) return -1;
    uint32_t kstack_aligned = (kstack_raw + PAGE_SIZE - 1) & 0xFFFFF000;
    child->kernel_stack = kstack_aligned + KERNEL_STACK_SIZE;
    child->kernel_stack_raw = kstack_raw;  /* V6.67: 保存原始地址供退出释放 */

    /* 复制内核栈内容 */
    uint32_t stack_size = current->kernel_stack - current->kernel_esp;
    if (stack_size > KERNEL_STACK_SIZE) stack_size = KERNEL_STACK_SIZE;
    memcpy((void *)(child->kernel_stack - stack_size),
           (void *)current->kernel_esp, stack_size);
    child->kernel_esp = child->kernel_stack - stack_size;

    /* 子进程名称加后缀 */
    strncat(child->name, "_child", PROC_NAME_LEN - strlen(child->name) - 1);

    /* 用户态进程：创建独立页目录和用户栈 */
    if (current->privilege == PRIV_USER) {
        /* V6.65修复: fork后子进程必须有独立的页目录和用户栈，
         * 否则父子进程共享用户栈导致互相干扰，子进程立即崩溃。 */
        page_directory_t *new_dir = paging_create_dir();
        if (!new_dir) {
            vga_puts("[PROC] fork: cannot create page dir\n");
            child->page_dir = current->page_dir;
            child->page_dir_owned = 0;
        } else {
            /* 复制父进程页目录的用户空间映射（浅共享页表）
             * V6.66: 跳过用户栈(idx=256)和内核栈(idx=0xE)对应的PDE，
             * 用户栈需要独立映射并逐页复制；内核栈保持内核共享页表 */
            for (int i = 0; i < 768; i++) {  /* 用户空间：0-3GB */
                if (i == 256) continue;  /* USER_STACK_ADDR=0x40000000 */
                if (i == 0xE) continue;  /* 内核栈区域，保持内核页表 */
                new_dir->entries[i] = current->page_dir->entries[i];
            }
            child->page_dir = new_dir;
            child->page_dir_owned = 1;

            /* 映射子进程的内核栈到新页目录
             * V6.66: kbot向下页对齐、ktop向上页对齐，确保覆盖所有内核栈页
             * （原代码kbot非页对齐导致循环跳过顶部页，CR3切换后内核栈内容全0） */
            page_directory_t *kdir = paging_get_kernel_dir();
            uint32_t kbot = (child->kernel_stack - KERNEL_STACK_SIZE) & 0xFFFFF000;
            uint32_t ktop = (child->kernel_stack + 0xFFF) & 0xFFFFF000;
            for (uint32_t a = kbot; a < ktop; a += PAGE_SIZE) {
                uint32_t phys = paging_get_phys(kdir, a);
                if (phys) {
                    paging_map(new_dir, a, phys,
                               PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
                }
            }

            /* 映射VGA文本显存 */
            for (uint32_t a = 0xB8000; a < 0xB8000 + 0x8000; a += PAGE_SIZE) {
                paging_map(new_dir, a, a,
                           PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
            }

            /* 为子进程分配独立的用户栈
             * V6.67: 页对齐分配，避免paging_map页对齐后映射到分配范围外的内存，
             * 覆盖其他内核对象导致子进程崩溃（高PID下更易触发） */
            uint32_t ustack_raw = (uint32_t)kmalloc(USER_STACK_SIZE + PAGE_SIZE);
            if (ustack_raw) {
                uint32_t ustack_aligned = (ustack_raw + PAGE_SIZE - 1) & 0xFFFFF000;
                /* 先映射子进程用户栈 */
                for (uint32_t i = 0; i < USER_STACK_SIZE; i += PAGE_SIZE) {
                    paging_map(new_dir,
                               USER_STACK_ADDR + i,
                               ustack_aligned + i,
                               PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
                }
                /* V6.66 修复: 逐页复制用户栈内容
                 * 原因：user_stack_raw非页对齐，paging_map会页对齐物理地址，
                 * 直接memcpy(raw, raw, size)会导致虚拟地址偏移错误 */
                for (uint32_t i = 0; i < USER_STACK_SIZE; i += PAGE_SIZE) {
                    uint32_t parent_phys = paging_get_phys(current->page_dir, USER_STACK_ADDR + i);
                    uint32_t child_phys = paging_get_phys(new_dir, USER_STACK_ADDR + i);
                    if (parent_phys && child_phys) {
                        memcpy((void *)child_phys, (void *)parent_phys, PAGE_SIZE);
                    }
                }
                child->user_stack_raw = ustack_raw;  /* 保存原始地址供释放 */
                child->user_stack = USER_STACK_ADDR + USER_STACK_SIZE;
            } else {
                child->user_stack_raw = 0;
                child->user_stack = current->user_stack;
            }
        }
    }

    vga_printf("[PROC] fork: PID=%d -> PID=%d\n", parent_pid, child->pid);


    /* 子进程返回0，父进程返回子PID
     * 这里通过设置子进程eax=0实现
     */
    cpu_state_t *child_state = (cpu_state_t *)(child->kernel_esp + 4);
    child_state->eax = 0;

    return child->pid;
}

/* execve：用新程序替换当前进程
 * 简化版本：加载.hug程序，重置进程状态
 */
int process_execve(const char *path)
{
    if (!current || !path) return -1;

    vga_printf("[PROC] execve: PID=%d load %s\n", current->pid, path);

    /* 通过VFS打开文件 */
    extern int vfs_open(const char *path, int flags);
    extern int vfs_read(int fd, void *buf, uint32_t size);
    extern int vfs_close(int fd);

    int fd = vfs_open(path, 0);
    if (fd < 0) {
        vga_printf("[PROC] execve: cannot open %s\n", path);
        return -1;
    }

    /* 读取.hug头部（简化：直接重置进程入口） */
    uint8_t header[64];
    int read = vfs_read(fd, header, 64);
    vfs_close(fd);

    if (read < 64 || header[0] != 'H' || header[1] != 'U' ||
        header[2] != 'G') {
        vga_printf("[PROC] execve: invalid .hug format \n");
        return -1;
    }

    /* 重置进程状态（简化：重新设置入口点） */
    /* 实际应加载代码段到内存，这里仅标记成功 */
    current->state = PROC_READY;
    sched_ready_mark(current->priority);   /* V3.90-E36 */
    current->time_used = 0;

    vga_printf("[PROC] execve ok: %s\n", path);
    return 0;
}

/* 系统调用实现 */
int sys_fork_impl(void)
{
    return process_fork();
}

int sys_execve_impl(const char *path, char *const argv[], char *const envp[])
{
    (void)argv; (void)envp;
    return process_execve(path);
}
