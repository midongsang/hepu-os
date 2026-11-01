/* ============================================================================
 * 赫普OS - 系统调用：PROCESS
 * ========================================================================== */

#include "types.h"
#include <stdint.h>
#include <stddef.h>
#include "syscall.h"
#include "vga.h"
#include "kheap.h"
#include "sched.h"
#include "hepu.h"
#include "fingerprint.h"
#include "vfs.h"
#include "ipc.h"
#include "process.h"

/* V4.02-E47: 主线(非进程)专用中断栈——独立于 __stack_top 主线栈.
 * 内核主线(cur=NULL)运行于链接器保留栈, 原 TSS.ESP0=__stack_top 使
 * PIT/键盘中断帧压入主线栈顶, 主线深调用弹栈可覆盖中断帧 → 偶发
 * ISR 6/13(iret 弹残留返回地址 0x18EEE4 等). 改为主线中断帧压入
 * 本静态栈(bss 区, kheap 与主线栈均不触碰), 彻底分离. */
static uint8_t mainline_irq_stack[8192] __attribute__((aligned(16)));
extern const char *hepu_kernel_version;

/* V3.90-E36: 就绪位图标记(process.c 提供) */
extern void sched_ready_mark(uint32_t);
extern void sched_ready_clear(uint32_t);

/* V2.19: 用户态进程退出后启动内核Shell.
 * 背景: V2.18修复kheap后, init/procb用户态进程真正进入ring3运行,
 * 且为无限循环, process_enter_user_mode(iret)永不返回,
 * kernel.c 里 process_enter_user_mode 之后的 shell_main() 永远执行不到,
 * 导致系统停在 "enter user proc: init" 而无 Shell 界面.
 * 修复: 用户态进程改为有限演示后 SYS_EXIT; 此处冻结其余用户进程
 * (防止调度器再切换/刷屏), 并启动内核 Shell(只启动一次). */
static int shell_launched = 0;

/* 标记Shell已启动（系统启动时由kernel.c调用，避免首次sys_exit误判） */
void shell_mark_launched(void)
{
    shell_launched = 1;
}

u32 sys_exit(u32 code, u32 arg2,
                         u32 arg3, u32 arg4)
{
    (void)arg2; (void)arg3; (void)arg4;
    vga_printf("[SYSCALL] proc exit(code=%d)\n", code);
    if (!shell_launched) {
        shell_launched = 1;
        /* V2.23修复内存泄漏: 释放当前触发进程(procb)内核栈.
         * V2.25: init(PID 1) 是常驻进程——置 PROC_SLEEPING 挂起保留PCB/栈,
         *        永不销毁; procb(触发exit)才冻结释放.
         * 注意1: 当前进程(触发exit)的栈正被本函数与Shell使用, 不释放.
         * 注意2: V3.77 起 user_stack_raw 保存用户栈 kheap 原始地址,
         *         随进程退出一并释放(此前原始地址丢失, 每进程泄漏64KB). */
        extern process_t proc_table[MAX_PROCESSES];
        extern uint32_t sched_current_pid(void);
        extern void kfree(void *);
        uint32_t cur_pid = sched_current_pid();
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (proc_table[i].state != PROC_UNUSED) {
                if (proc_table[i].pid != cur_pid) {
                    /* init 常驻: 挂起但保留PCB/栈, 不参与调度 */
                    proc_table[i].state = PROC_SLEEPING;
                    sched_ready_clear(proc_table[i].priority);   /* V3.90-E36 */
                } else {
                    /* procb(触发exit): 冻结并释放内核栈 + 用户栈 + 独占页目录 */
                    if (proc_table[i].page_dir_owned && proc_table[i].page_dir) {
                        /* V3.78: 先切回内核目录再归还页目录帧, 防 CR3 悬空 */
                        extern page_directory_t *paging_get_kernel_dir(void);
                        extern void paging_switch_dir(page_directory_t *);
                        extern void paging_free_dir(page_directory_t *);
                        paging_switch_dir(paging_get_kernel_dir());
                        paging_free_dir(proc_table[i].page_dir);
                        proc_table[i].page_dir = NULL;
                        proc_table[i].page_dir_owned = 0;
                    }
                    /* V4.02-E47 修复: procb 内核栈保留不释放——主线 Shell
                     * 将在本栈上继续运行(shell_main 复用 sys_exit 调用栈帧).
                     * 原实现 kfree 后主线仍用已释放栈, 后续进程(winargs/
                     * macho_demo/schtest A/B) kmalloc 复用该块即踩踏主线栈
                     * → 偶发 ISR 6/13(iret 弹已释放栈垃圾 0x6B2xxxx). */
                    /* 用户栈/页目录照常释放(主线不再使用) */
                    if (proc_table[i].user_stack_raw) {
                        kfree((void *)proc_table[i].user_stack_raw);
                        proc_table[i].user_stack_raw = 0;
                    }
                    proc_table[i].state = PROC_UNUSED;
                    proc_table[i].kernel_stack = 0;   /* PCB 标记, 块本身保留 */
                    sched_ready_clear(proc_table[i].priority);   /* V3.90-E36 */
                }
            }
        }
        /* V2.23: 打印堆统计, 验证进程栈释放后内存归还(用于OOM排查) */
        extern void kheap_stats(uint32_t *, uint32_t *, uint32_t *);
        {
            uint32_t hu = 0, hf = 0, ht = 0;
            kheap_stats(&hu, &hf, &ht);
            vga_printf("[KHEAP] after proc cleanup: used=%d free=%d total=%d\n",
                       hu, hf, ht);
        }
        vga_puts("(start Shell...)\n");
        /* V4.02-E47 根治: 主线 Shell 锚定到链接器保留栈 __stack_top
         * (kheap 管理区之外, 永不被 kmalloc 复用). 原实现 shell_main
         * 在"触发首次 exit 的进程内核栈"上启动, 该栈位于 kheap 堆内,
         * 后续进程 kmalloc 复用即踩踏主线栈 → 偶发 ISR 6/13. */
        extern void shell_main(void);
        extern void hepu_enter_mainline(void (*)(void));
        hepu_enter_mainline(shell_main);   /* 不返回 */
    }
    /* V3.85+V3.89-E28: 用户进程退出——多进程并存时交还调度器, 全部
     * 退出后回收并回到主线 Shell.
     * 修复内容(相对原实现):
     *   1. 不再把"其他进程"强制冻结为 SLEEPING(原实现会把仍就绪的进程
     *      冻结成无 sleep_until 的僵尸; 且 sched_current_pid() 返回 0 时
     *      连退出者自身都会被冻结 → ps 残留).
     *   2. 若仍有其他 READY 进程(如 schtest 的 A/B 先后退出), 本进程
     *      转为"待回收"(SLEEPING, 保留栈/页目录)并置 need_resched, 由
     *      中断返回路径调度到下一个就绪进程继续跑——彻底避免"current=
     *      NULL 后就绪进程无人调度"的永久占位.
     *   3. 无其他就绪进程时, 统一回收所有"待回收"进程 + 退出者自身,
     *      longjmp 回主线 Shell. */
    {
        extern process_t proc_table[MAX_PROCESSES];
        extern uint32_t sched_current_pid(void);
        extern void kfree(void *);
        extern volatile int need_resched;
        uint32_t cur_pid = sched_current_pid();

        /* V3.89-E35: sys_exit 全程持表锁(has_live 检查→wake-all→回收),
         * longjmp 前释放——中断上下文 irqsave 安全. */
        proc_lock_acquire();

        /* V6.67: 保存退出码，供waitpid获取 */
        current->exit_code = (int)code;

        /* 先找是否有其他"活进程"可接管 CPU——READY, 或正在正常睡眠
         * (SLEEPING 且 sleep_until!=0, 会被唤醒扫描转 READY) 的都算.
         * ZOMBIE状态的进程不算活进程（已退出，等待父进程回收）
         * E-30 修复: 此前只认 READY——若 B 恰在 100ms 睡眠中, A 退出会
         * 误判"无活进程"→ 回收分支误杀仍在运行的 B(栈被 kfree 而 current
         * 仍是 B) → B 用已释放栈继续跑, esi 损坏 → 永不退出. */
        int has_live = 0;
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (proc_table[i].pid == cur_pid) continue;
            if (proc_table[i].state == PROC_READY) { has_live = 1; break; }
            if (proc_table[i].state == PROC_RUNNING) { has_live = 1; break; }
            if (proc_table[i].state == PROC_SLEEPING &&
                proc_table[i].sleep_until != 0) { has_live = 1; break; }
        }
        if (has_live) {
            /* V3.89-E30 修复: 立即唤醒全部正常睡眠进程(SLEEPING 且
             * sleep_until!=0)——若仅置 need_resched 依赖 sched_tick 唤醒
             * 扫描, 在"本进程待回收(SLEEPING)+另一进程恰在睡眠"的时序下
             * B 可能不再被调度(schtest B 卡死, shell 永失响应). 先全部
             * 转 READY, 返回路径调度立即接管 B. */
            for (int i = 0; i < MAX_PROCESSES; i++) {
                if (proc_table[i].state == PROC_SLEEPING &&
                    proc_table[i].sleep_until != 0) {
                    proc_table[i].state = PROC_READY;
                    proc_table[i].sleep_until = 0;
                    sched_ready_mark(proc_table[i].priority);   /* V3.90-E36 */
                }
            }
            /* V6.67: 本进程转为ZOMBIE状态（保留栈/页目录/退出码），
             * 等待父进程waitpid回收。交还调度器切到下一就绪进程 */
            extern process_t *current;
            current->state = PROC_ZOMBIE;
            current->sleep_until = 0;
            sched_ready_clear(current->priority);   /* V3.90-E36 */
            proc_lock_release();
            need_resched = 1;
            return 0;   /* 中断返回路径检查 need_resched → schedule */
        }

        /* 无其他就绪进程: 回收全部待回收进程.
         * V3.89-E32 根治(偶发 ISR 6): 此前"自己"的页目录/内核栈也在本循环
         * 里就地释放——但 sys_exit 正运行在"自己"的内核栈上! kfree 后仍在
         * 已释放内存上继续执行(kfree/sti/longjmp 的 call 压栈), 期间 PIT
         * 中断(TSS.ESP0 指向已释放栈)或 kmalloc 复用该块 → 栈被踩 →
         * longjmp 现场损坏 → 跳飞 Invalid Opcode(ISR 6), 偶发系统 halt.
         * 修复: 先回收"其他"进程, 自己切到内核初始栈后再释放自己的资源. */
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (proc_table[i].state == PROC_UNUSED) continue;
            if (proc_table[i].pid == 1) continue;
            if (proc_table[i].pid == cur_pid) continue;  /* 自己最后处理 */
            /* 回收: 页目录 + 内核栈 + 用户栈 */
            if (proc_table[i].page_dir_owned && proc_table[i].page_dir) {
                extern page_directory_t *paging_get_kernel_dir(void);
                extern void paging_switch_dir(page_directory_t *);
                extern void paging_free_dir(page_directory_t *);
                paging_switch_dir(paging_get_kernel_dir());
                paging_free_dir(proc_table[i].page_dir);
                proc_table[i].page_dir = NULL;
                proc_table[i].page_dir_owned = 0;
            }
            if (proc_table[i].kernel_stack) {
                /* V3.89-E34: 销毁前检查栈底 canary——被踩说明存在内核栈溢出
                 * (中断嵌套/深递归), 打印定位线索后仍正常释放. */
                if (*(volatile uint32_t *)(proc_table[i].kernel_stack - KERNEL_STACK_SIZE)
                        != KERNEL_STACK_CANARY) {
                    vga_printf("[CANARY] %s(PID=%d) kernel stack OVERFLOW\n",
                               proc_table[i].name, proc_table[i].pid);
                }
                /* V6.67: 优先用kernel_stack_raw释放（页对齐分配时保存了原始地址） */
                if (proc_table[i].kernel_stack_raw) {
                    kfree((void *)proc_table[i].kernel_stack_raw);
                    proc_table[i].kernel_stack_raw = 0;
                } else {
                    kfree((void *)(proc_table[i].kernel_stack - KERNEL_STACK_SIZE));
                }
                proc_table[i].kernel_stack = 0;
            }
            if (proc_table[i].user_stack_raw) {
                kfree((void *)proc_table[i].user_stack_raw);
                proc_table[i].user_stack_raw = 0;
            }
            proc_table[i].state = PROC_UNUSED;
            sched_ready_clear(proc_table[i].priority);   /* V3.90-E36 */
        }
        /* 重置调度器状态: current=NULL + TSS.ESP0=内核初始栈顶, 保证
         * 释放自己栈之后的中断(键盘/PIT)不再压到悬垂栈. */
        extern process_t *current;
        extern void tss_set_kernel_stack(uint32_t);
        extern char __stack_top[];
        current = NULL;
        tss_set_kernel_stack((uint32_t)mainline_irq_stack
                            + sizeof(mainline_irq_stack));
        /* V4.00-E45: 释放表锁后保持 IF=0(中断入口 cli 状态), 不再显式 sti——
         * 第二循环(释放自己页目录/用户栈/内核栈)全程无中断窗口, 彻底杜绝
         * 中断压入"已释放/将释放"栈. 开中断职责统一由 hepu_longjmp 内 sti
         * 承担(E-44). 此前 kfree(用户栈) 到 kfree(内核栈) 前的 sti 窗口
         * 仍可被 PIT/键盘中断压栈, 是 ISR 6 残留触发点(iret 弹出显存
         * 0xA0000/0xB8965). */
        __asm__ volatile ("cli");
        /* V3.85: sys_exit 在中断上下文(int 0x80)里 longjmp, 绕过恢复
         * EFLAGS.IF 的中断返回路径(iret) → IF 保持中断入口的关闭状态,
         * 后续键盘 IRQ 被屏蔽 → 输入失效. 显式开中断.
         * V3.89-E35 修复: sti 必须放在 proc_lock_release 之后——irqrestore
         * 会把 IF 恢复为 acquire 时的 0(中断入口), 撤销 sti → 键盘失效. */

        /* V3.89-E32 修订: 释放"自己"——顺序: 页目录(先切回内核目录) →
         * 用户栈 → 内核栈放最后. TSS.ESP0 已在上方重置为 __stack_top,
         * 因此 PIT/键盘中断不再压到本进程栈; kfree(内核栈) 后到 longjmp
         * 之间不再触碰该栈(仅 state=UNUSED 的全局写 + longjmp call, 而
         * hepu_longjmp 内部只 mov buf→寄存器、不读栈、jmp 而非 ret)——
         * 彻底消除"已释放栈上继续执行"的窗口.
         * (此前尝试硬切 esp 的方案作废: C 编译器对 esp 的变化无感知,
         * 后续循环的局部变量 i 按旧 esp 寻址读到垃圾 → 乱 free 破坏堆.) */
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (proc_table[i].state == PROC_UNUSED) continue;
            if (proc_table[i].pid == 1) continue;
            if (proc_table[i].pid != cur_pid) continue;
            if (proc_table[i].page_dir_owned && proc_table[i].page_dir) {
                extern page_directory_t *paging_get_kernel_dir(void);
                extern void paging_switch_dir(page_directory_t *);
                extern void paging_free_dir(page_directory_t *);
                paging_switch_dir(paging_get_kernel_dir());
                paging_free_dir(proc_table[i].page_dir);
                proc_table[i].page_dir = NULL;
                proc_table[i].page_dir_owned = 0;
            }
            if (proc_table[i].user_stack_raw) {
                kfree((void *)proc_table[i].user_stack_raw);
                proc_table[i].user_stack_raw = 0;
            }
            if (proc_table[i].kernel_stack) {
                /* E-44: cli 已提前到第二循环入口(见上), 此处不再重复. */
                /* V6.67: 优先用kernel_stack_raw释放 */
                if (proc_table[i].kernel_stack_raw) {
                    kfree((void *)proc_table[i].kernel_stack_raw);
                    proc_table[i].kernel_stack_raw = 0;
                } else {
                    kfree((void *)(proc_table[i].kernel_stack - KERNEL_STACK_SIZE));
                }
                proc_table[i].kernel_stack = 0;
            }
            proc_table[i].state = PROC_UNUSED;
            sched_ready_clear(proc_table[i].priority);   /* V3.90-E36 */
        }
        proc_lock_release();   /* V3.89-E35: 回收完成、longjmp 前释放表锁 */
        /* V4.00-E45: 此处不再 sti——第二循环入口已 cli(IF=0), 保持到
         * hepu_longjmp 内部 sti 才恢复, 彻底消除"已释放栈上中断窗口". */

        /* 回到主线 Shell (hug_run 的 setjmp 点), 不返回 */
        extern uint32_t mainline_jmp_buf[6];
        extern void hepu_longjmp(uint32_t *, int);
        hepu_longjmp(mainline_jmp_buf, 1);
    }
    /* E-18: 理论不可达——首次 sys_exit 走 longjmp 不返回; 兜底停机防静默 */
    vga_puts("[SYS-EXIT] hlt path (unexpected)\n");
    for (;;) { __asm__ volatile ("hlt"); }
    return 0;
}

u32 sys_yield(u32 arg1, u32 arg2,
                          u32 arg3, u32 arg4)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4;
    /* V2.22: 强制请求调度——syscall 返回路径检查 need_resched 并切换进程 */
    extern volatile int need_resched;
    need_resched = 1;
    return 0;
}

u32 sys_getpid(u32 arg1, u32 arg2,
                           u32 arg3, u32 arg4)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4;
    return sched_current_pid();
}

u32 sys_kill(u32 pid, u32 sig,
                         u32 arg3, u32 arg4)
{
    (void)arg3; (void)arg4;
    return ipc_kill(pid, sig);
}

u32 sys_signal(u32 sig, u32 handler_ptr,
                           u32 arg3, u32 arg4)
{
    (void)arg3; (void)arg4;
    return ipc_signal(sig, (void (*)(int))handler_ptr);
}

u32 sys_clone(u32 flags, u32 stack_ptr, u32 ptid, u32 ctid)
{
    (void)flags; (void)stack_ptr; (void)ptid; (void)ctid;
    return (u32)-1;  /* 原型阶段不支持 */
}

/* V6.67: 真正的waitpid实现——等待子进程退出并获取退出码
 * 支持：pid>0等待指定PID子进程；pid=-1等待任意子进程
 * 子进程退出后状态为PROC_ZOMBIE，waitpid回收其资源并返回退出码 */
u32 sys_waitpid(u32 pid, u32 status_ptr, u32 options, u32 arg4)
{
    (void)arg4;
    /* V7.79安全修复: 用户指针校验（漏洞C全覆盖） */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (status_ptr && !user_ptr_valid((void *)status_ptr, sizeof(uint32_t))) {
        vga_printf("[PROC] sys_waitpid: invalid status ptr 0x%x\n", status_ptr);
        return (u32)-1;
    }
    extern process_t proc_table[MAX_PROCESSES];
    extern process_t *current;
    extern volatile int need_resched;
    extern void kfree(void *);
    extern page_directory_t *paging_get_kernel_dir(void);
    extern void paging_switch_dir(page_directory_t *);
    extern void paging_free_dir(page_directory_t *);

    uint32_t parent_pid = current->pid;

    /* 轮询等待子进程退出 */
    for (int wait_loop = 0; wait_loop < 10000; wait_loop++) {
        proc_lock_acquire();

        int found = 0;
        int zombie_idx = -1;

        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (proc_table[i].state == PROC_UNUSED) continue;
            if (proc_table[i].parent_pid != parent_pid) continue;

            /* pid>0: 只匹配指定PID；pid=-1(0xFFFFFFFF): 匹配任意子进程 */
            if (pid != 0xFFFFFFFF && proc_table[i].pid != pid) continue;

            found = 1;

            if (proc_table[i].state == PROC_ZOMBIE) {
                zombie_idx = i;
                break;
            }
        }

        if (zombie_idx >= 0) {
            /* 回收子进程资源 */
            process_t *child = &proc_table[zombie_idx];
            int exit_code = child->exit_code;

            /* 写入status_ptr（如果非空） */
            if (status_ptr) {
                uint32_t *status = (uint32_t *)status_ptr;
                *status = (uint32_t)exit_code;
            }

            /* 释放子进程资源
             * V6.67: 注意——子进程页目录与父进程浅共享页表，
             * paging_free_dir只释放页目录本身，不释放页表（页表可能被共享）。
             * 释放后必须切回父进程页目录，否则父进程用户空间映射消失导致崩溃。 */
            page_directory_t *parent_dir = current->page_dir;
            if (child->page_dir_owned && child->page_dir) {
                if (child->page_dir != parent_dir) {
                    paging_switch_dir(paging_get_kernel_dir());
                    paging_free_dir(child->page_dir);
                }
                child->page_dir = NULL;
                child->page_dir_owned = 0;
            }
            if (child->kernel_stack_raw) {
                kfree((void *)child->kernel_stack_raw);
                child->kernel_stack_raw = 0;
            } else if (child->kernel_stack) {
                kfree((void *)(child->kernel_stack - KERNEL_STACK_SIZE));
            }
            child->kernel_stack = 0;
            if (child->user_stack_raw) {
                kfree((void *)child->user_stack_raw);
                child->user_stack_raw = 0;
            }
            child->state = PROC_UNUSED;
            sched_ready_clear(child->priority);

            uint32_t child_pid = child->pid;
            proc_lock_release();

            /* V6.67: 切回父进程页目录（如果之前切换了） */
            if (parent_dir && parent_dir != paging_get_kernel_dir()) {
                paging_switch_dir(parent_dir);
            }

            return child_pid;  /* 返回子进程PID */
        }

        proc_lock_release();

        if (!found) {
            /* 没有找到子进程 */
            return 0xFFFFFFFF;  /* -1: 无此子进程 */
        }

        /* 子进程还在运行，让出CPU等待 */
        if (options & 1) {  /* WNOHANG */
            return 0;  /* 立即返回0 */
        }
        need_resched = 1;
        /* 中断返回路径会调度到其他进程 */
        return 0;  /* 临时返回，下次调用继续等待 */
    }

    return 0xFFFFFFFF;  /* 超时 */
}

u32 sys_execve(u32 path_ptr, u32 argv, u32 envp, u32 arg4)
{
    (void)argv; (void)envp; (void)arg4;
    /* V7.70安全修复: 校验用户指针，防止内核从任意地址读 */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (!user_ptr_valid((const void *)path_ptr, 4096)) {
        vga_printf("[PROC] sys_execve: invalid path ptr 0x%x\n", path_ptr);
        return (u32)-1;
    }
    extern int sys_execve_impl(const char *path, char *const argv[], char *const envp[]);
    return (u32)sys_execve_impl((const char *)path_ptr, NULL, NULL);
}

u32 sys_fork(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4;
    extern int sys_fork_impl(void);
    return (u32)sys_fork_impl();
}

