/* ============================================================================
 * 赫普OS - 进程管理
 * 实现：进程创建、用户态进入、上下文切换框架
 * ========================================================================== */

#include "types.h"
#include <stdint.h>
#include <stddef.h>
#include "process.h"
#include "smp.h"
#include "shared/domain/hepu_domain.h"
#include "serial.h"
#include "assert.h"  /* V3.87-E11 */
#include "paging.h"
#include "kheap.h"
#include "string.h"
#include "vga.h"
#include "sched.h"
#include "constants.h"

/* V3.89-E29: 缓存行对齐(64B)——防多核伪共享: 相邻 PCB 不共享同一缓存行 */
process_t proc_table[MAX_PROCESSES] __attribute__((aligned(64)));  /* 全局，供app_freeze等模块访问 */

/* V3.89-E35: proc_table 表级自旋锁——保护结构性操作(找槽/回收/遍历),
 * 中断安全(irqsave). 当前单核+关中断路径下为形式互斥, SMP 启用后即
 * 真实互斥. 锁内禁止再获取本锁(无嵌套), 禁止睡眠. */
spinlock_t proc_lock = SPINLOCK_INIT;
uint32_t proc_lock_flags;   /* 单核下无并发 acquire, static 保存安全 */

void proc_lock_acquire(void)
{
    spin_lock_irqsave(&proc_lock, &proc_lock_flags);
}

void proc_lock_release(void)
{
    spin_unlock_irqrestore(&proc_lock, proc_lock_flags);
}
process_t *current = NULL;  /* 全局，汇编可访问 */
uint32_t next_pid = 1;

/* 调度标志 */
volatile int need_resched = 0;

/* V3.87-E1: 调度性能指标(供 perf 命令/监控) */
uint32_t sched_switches = 0;    /* 上下文切换次数 */
uint32_t sched_idle_ticks = 0;  /* V4.51-E96: idle tick 计数 */
uint32_t sched_ticks_total = 0; /* 系统总tick数(100Hz) */

/* V3.90-E36: 就绪位图——bit<prio> 置位表示该优先级至少有一个就绪进程.
 * 仅作调度器加速提示(ctz 定位最高优先级级), 正确性不依赖位图一致性:
 * sched_pick_next 级内扫描实际 state 验证, 位图滞后时回退全表扫描兜底.
 * 调用约束: 在持 proc_lock 或中断上下文(已 cli)内调用, 与 sched_tick
 * 的 irqsave 持锁窗口互斥, 无并发写竞争. */
uint32_t sched_ready_bitmap = 0;
void sched_ready_mark(uint32_t prio)
{
    if (prio <= PRIO_BKG) sched_ready_bitmap |= (1u << prio);
}
void sched_ready_clear(uint32_t prio)
{
    if (prio > PRIO_BKG) return;
    /* V6.68: 只有当同一优先级没有其他就绪进程时才清除位，
     * 否则会导致调度器跳过该优先级，总是选择当前进程 */
    extern process_t proc_table[];
    for (int i = 0; i < 64; i++) {  /* MAX_PROCESSES=64 */
        if (proc_table[i].state == PROC_READY &&
            proc_table[i].priority == prio) {
            return;  /* 还有其他就绪进程，不清除位 */
        }
    }
    sched_ready_bitmap &= ~(1u << prio);
}

uint32_t sched_get_switches(void) { return sched_switches; }
uint32_t sched_get_ticks(void)    { return sched_ticks_total; }
uint32_t sched_switch_count(void) { return sched_switches; }
uint32_t sched_get_idle_ticks(void) { return sched_idle_ticks; }  /* V4.51-E96 */

/* V3.96-E42: 设置进程 CPU 亲和性.
 * 校验: pid 存在 / mask 非 0 / mask 与在线 CPU 位图有交集(否则无法调度).
 * 返回 0 成功, -1 失败. */
int sched_set_affinity(uint32_t pid, uint32_t mask)
{
    extern uint32_t smp_online_cpus(void);
    if (mask == 0) return -1;
    if ((mask & smp_online_cpus()) == 0) return -1;
    proc_lock_acquire();
    int rc = -1;
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_UNUSED) continue;
        if (!proc_table[i].name[0]) continue;
        if (proc_table[i].pid == pid) {
            proc_table[i].cpu_affinity = mask;
            rc = 0;
            break;
        }
    }
    proc_lock_release();
    return rc;
}

void process_init(void)
{
    /* V3.94-E40: 注册 proc_lock 到死锁监控 */
    extern int lock_monitor_register(spinlock_t *, const char *);
    lock_monitor_register(&proc_lock, "proc_lock");

    for (int i = 0; i < MAX_PROCESSES; i++) {
        proc_table[i].state = PROC_UNUSED;
        proc_table[i].pid = 0;
    }
    vga_puts("[PROC] process mgmt init done \n");
}

/* 创建进程 */
process_t *process_create(const char *name, uint32_t entry,
                          privilege_t priv, uint32_t priority)
{
    process_t *proc = NULL;

    /* V3.89-E35: 表级锁——创建全程持锁(找槽/初始化/栈与页目录分配),
     * 无嵌套调用, SMP 下保护表结构一致性. */
    proc_lock_acquire();

    /* 找空闲PCB */
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_UNUSED) {
            proc = &proc_table[i];
            break;
        }
    }
    if (!proc) {
        proc_lock_release();
        vga_puts("[PROC] error: process table full \n");
        return NULL;
    }

    memset(proc, 0, sizeof(process_t));
    proc->pid = next_pid++;
    proc->state = PROC_READY;
    proc->privilege = priv;
    proc->entry_point = entry;
    /* V3.87-E1: 优先级归一化 + 动态时间片(CPU型长片/交互短片) */
    proc->priority = (priority <= 0) ? PRIO_RT : (priority >= 3 ? PRIO_BKG : priority);
    proc->time_slice = (proc->priority == PRIO_RT) ? 2
                     : (proc->priority == PRIO_BKG) ? 5 : 20;
    sched_ready_mark(proc->priority);   /* V3.90-E36: READY 入位图 */
    proc->sandbox_id = proc - proc_table;
    proc->domain_id = DOMAIN_ID_INVALID;  /* P2-2: 默认未挂靠，加载器随后设置 */

    /* 复制名称 */
    for (int i = 0; i < PROC_NAME_LEN - 1 && name[i]; i++) {
        proc->name[i] = name[i];
    }

    /* 分配内核栈 */
    proc->kernel_stack = (uint32_t)kmalloc(KERNEL_STACK_SIZE);
    if (!proc->kernel_stack) {
        vga_puts("[PROC] error: kernel stack alloc fail \n");
        proc->state = PROC_UNUSED;  /* V3.83: 归还PCB——state此前已置READY,
                                     * 失败不清理会在ps留下无栈幽灵进程 */
        sched_ready_clear(proc->priority);   /* V3.90-E36 */
        proc_lock_release();
        return NULL;
    }
    proc->kernel_stack += KERNEL_STACK_SIZE; /* 栈顶 */
    /* V3.89-E34: 栈底写 canary 哨兵——栈向下越界最先踩到它,
     * 销毁/检查时比对, 主动暴露溢出(而非偶发 ISR 6 式崩溃). */
    *(volatile uint32_t *)(proc->kernel_stack - KERNEL_STACK_SIZE)
        = KERNEL_STACK_CANARY;

    /* 用户态进程分配用户栈和页目录 */
    if (priv == PRIV_USER) {
        /* V3.91-E37: 全局堆水位保护——空闲低于 512KB 拒绝新建用户进程,
         * 防止并发进程耗尽 kheap 导致系统级 OOM(内核分配全面失败). */
        extern void kheap_stats(uint32_t *, uint32_t *, uint32_t *);
        uint32_t hf = 0;
        { uint32_t hu = 0, ht = 0; kheap_stats(&hu, &hf, &ht); }
        if (hf < KHEAP_LOW_WATER) {
            vga_printf("[PROC] kheap low-water: free=%d < %d, refuse create %s\n",
                       hf, (int)KHEAP_LOW_WATER, name);
            proc->state = PROC_UNUSED;
            sched_ready_clear(proc->priority);   /* V3.90-E36 */
            proc_lock_release();
            return NULL;
        }
        proc->page_dir = paging_create_dir();
        if (!proc->page_dir) {
            vga_puts("[PROC] error: pagedir create fail \n");
            kfree((void *)(proc->kernel_stack - KERNEL_STACK_SIZE));
            proc->state = PROC_UNUSED;  /* V3.83: 同上, 归还PCB */
            sched_ready_clear(proc->priority);   /* V3.90-E36 */
            proc_lock_release();
            return NULL;
        }
        proc->page_dir_owned = 1;   /* V3.78: 独占页目录, 退出时归还 */

        /* 分配用户栈 (V3.77: 保存 kheap 原始地址, 供进程退出时释放) */
        proc->user_stack_raw = (uint32_t)kmalloc(USER_STACK_SIZE);
        if (!proc->user_stack_raw) {
            vga_puts("[PROC] error: user stack alloc fail \n");
            paging_free_dir(proc->page_dir);
            kfree((void *)(proc->kernel_stack - KERNEL_STACK_SIZE));
            proc->state = PROC_UNUSED;  /* V3.83: 同上, 归还PCB */
            sched_ready_clear(proc->priority);   /* V3.90-E36 */
            proc_lock_release();
            return NULL;
        }
        proc->user_stack = proc->user_stack_raw + USER_STACK_SIZE;

        /* 映射用户栈到用户空间 (USER_STACK_ADDR) */
        uint32_t user_stack_virt = USER_STACK_ADDR;
        for (uint32_t i = 0; i < USER_STACK_SIZE; i += PAGE_SIZE) {
            paging_map(proc->page_dir,
                       user_stack_virt + i,
                       proc->user_stack_raw + i,
                       PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
        }
        proc->user_stack = user_stack_virt + USER_STACK_SIZE;
    } else {
        proc->page_dir = paging_get_current_dir();
        proc->page_dir_owned = 0;   /* V3.78: 共享内核目录, 不释放 */
        proc->user_stack_raw = 0;   /* 内核态进程无独立用户栈 */
    }

    /* 设置初始CPU状态（在栈顶构造中断返回帧）
     * 完整中断帧从低到高:
     *   ds, pusha(edi,esi,ebp,esp,ebx,edx,ecx,eax), irq_num, error_code,
     *   eip, cs, eflags, [user_esp, ss]
     * kernel_esp 指向 ds (中断恢复时第一个pop的位置)
     */
    uint32_t *stack = (uint32_t *)proc->kernel_stack;

    /* iret帧 */
    stack--;
    if (priv == PRIV_USER) {
        *stack-- = GDT_USER_DATA;       /* SS (用户数据段, RPL=3) */
        *stack-- = proc->user_stack; /* 用户 ESP */
    }
    *stack-- = EFLAGS_IF;           /* EFLAGS (IF=1, IOPL=0) */
    *stack-- = (priv == PRIV_USER) ? GDT_USER_CODE : GDT_KERNEL_CODE; /* CS */
    *stack-- = entry;           /* EIP */
    *stack-- = 0;               /* error_code (dummy) */
    *stack-- = 0;               /* irq_number */

    /* pusha 寄存器: edi,esi,ebp,esp,ebx,edx,ecx,eax */
    *stack-- = 0; /* eax */
    *stack-- = 0; /* ecx */
    *stack-- = 0; /* edx */
    *stack-- = 0; /* ebx */
    *stack-- = 0; /* esp (会被iret覆盖) */
    *stack-- = 0; /* ebp */
    *stack-- = 0; /* esi */
    *stack-- = 0; /* edi */

    /* ds (中断恢复时第一个pop) */
    *stack = (priv == PRIV_USER) ? GDT_USER_DATA : GDT_KERNEL_DATA;

    proc->kernel_esp = (uint32_t)stack;
    proc->cpu_state = (cpu_state_t *)((uint32_t)stack + 4);
    proc->time_used = 0;

    /* V3.87-E7: 创建即记账——内核栈+用户栈计入进程内存限额
     * P2-1: 单位改为页（与64位统一） */
    proc->mem_used = KERNEL_STACK_SIZE / PAGE_SIZE;
    if (priv == PRIV_USER) proc->mem_used += USER_STACK_SIZE / PAGE_SIZE;
    proc->mem_limit = 0;          /* 默认无限制, 加载器随后设置 */
    proc->cpu_limit_ticks = 0;
    proc->res_violations = 0;
    proc->cpu_affinity = 0x1;   /* V3.96-E42: 默认只允许 CPU0(当前唯一在线) */

    /* V2.22: 用户页目录显式映射内核栈页.
     * paging_create_dir 在帧构造前调用, 当时内核栈页(kmalloc)未触发
     * demand paging, 用户页目录 PDE 未设置; 之后帧构造才在 kernel_dir
     * 映射内核栈. 不补映射则 process_enter_user_asm 切 CR3 后读内核栈
     * 触发错误 demand, 读到页表垃圾 → mov %ds #GP. */
    if (priv == PRIV_USER) {
        page_directory_t *kdir = paging_get_kernel_dir();
        uint32_t kbot = proc->kernel_stack - KERNEL_STACK_SIZE;
        for (uint32_t a = kbot; a < proc->kernel_stack; a += 0x1000) {
            uint32_t phys = paging_get_phys(kdir, a);
            if (phys) {
                paging_map(proc->page_dir, a, phys,
                           PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
            }
        }
        /* V3.85: 用户页目录映射 VGA 文本显存(0xB8000 起 32KB).
         * 否则用户态应用 vga 输出访问 0xB8000 → 页错误, 应用崩溃/
         * 静默丢失显示(clock 等应用看不到输出). VGA 显存物理映射,
         * 用户空间同址直映(与内核目录一致). */
        for (uint32_t a = 0xB8000; a < 0xB8000 + 0x8000; a += 0x1000) {
            paging_map(proc->page_dir, a, a,
                       PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
        }
    }

    vga_printf("[PROC] create proc: %s(PID=%d, %s)\n",
               proc->name, proc->pid,
               priv == PRIV_USER ? " user mode " : " kernel 态 ");

    proc_lock_release();
    return proc;
}

/* 进入用户模式（切换到第一个用户进程） */
void process_enter_user_mode(process_t *proc)
{
    if (!proc || proc->privilege != PRIV_USER) {
        vga_puts("[PROC] error: cannot enter user mode \n");
        return;
    }

    current = proc;
    proc->state = PROC_RUNNING;
    sched_ready_clear(proc->priority);   /* V3.90-E36: 离开就绪位图 */

    vga_printf("[PROC] enter user proc: %s(PID=%d)\n",
               proc->name, proc->pid);
    /* V2.22 debug: 打印进入用户态关键状态 */
    vga_printf("[PROC] dbg: kernel_stack=0x%x kernel_esp=0x%x ds_slot=0x%x page_dir=0x%x\n",
               proc->kernel_stack, proc->kernel_esp,
               *(uint32_t *)proc->kernel_esp, (uint32_t)proc->page_dir);
    /* V6.47: 打印初始帧关键值，排查iret跳转错误 */
    {
        extern void serial_puts(uint16_t, const char *);
        extern void serial_puthex(uint16_t, uint32_t);
        uint32_t *frame = (uint32_t *)proc->kernel_esp;
        serial_puts(0x3F8, "[FRAME] ds=0x");
        serial_puthex(0x3F8, frame[0]);
        serial_puts(0x3F8, " edi=0x");
        serial_puthex(0x3F8, frame[1]);
        serial_puts(0x3F8, " esi=0x");
        serial_puthex(0x3F8, frame[2]);
        serial_puts(0x3F8, " ebp=0x");
        serial_puthex(0x3F8, frame[3]);
        serial_puts(0x3F8, " esp=0x");
        serial_puthex(0x3F8, frame[4]);
        serial_puts(0x3F8, " ebx=0x");
        serial_puthex(0x3F8, frame[5]);
        serial_puts(0x3F8, " edx=0x");
        serial_puthex(0x3F8, frame[6]);
        serial_puts(0x3F8, " ecx=0x");
        serial_puthex(0x3F8, frame[7]);
        serial_puts(0x3F8, " eax=0x");
        serial_puthex(0x3F8, frame[8]);
        serial_puts(0x3F8, " irq=0x");
        serial_puthex(0x3F8, frame[9]);
        serial_puts(0x3F8, " err=0x");
        serial_puthex(0x3F8, frame[10]);
        serial_puts(0x3F8, " eip=0x");
        serial_puthex(0x3F8, frame[11]);
        serial_puts(0x3F8, " cs=0x");
        serial_puthex(0x3F8, frame[12]);
        serial_puts(0x3F8, " eflags=0x");
        serial_puthex(0x3F8, frame[13]);
        serial_puts(0x3F8, " useresp=0x");
        serial_puthex(0x3F8, frame[14]);
        serial_puts(0x3F8, " ss=0x");
        serial_puthex(0x3F8, frame[15]);
        serial_puts(0x3F8, "\n");
    }

    /* 切换页目录 */
    paging_switch_dir(proc->page_dir);

    /* 汇编：加载进程状态并 iret 进入用户态 */
    extern void process_enter_user_asm(process_t *proc);
    process_enter_user_asm(proc);
}

process_t *process_current(void)
{
    return current;
}

void process_stats(uint32_t *total, uint32_t *running, uint32_t *ready)
{
    uint32_t t = 0, r = 0, rdy = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state != PROC_UNUSED) t++;
        if (proc_table[i].state == PROC_RUNNING) r++;
        if (proc_table[i].state == PROC_READY) rdy++;
    }
    if (total)   *total = t;
    if (running) *running = r;
    if (ready)   *ready = rdy;
}

/* ============================================================================
 * 抢占式调度器
 * 由时钟中断触发，在中断返回前执行上下文切换
 * ========================================================================== */

/* ---- V3.87-E7: 资源限额 ---- */

void process_set_limits(process_t *p, uint32_t mem_limit,
                        uint32_t cpu_limit_ticks)
{
    if (!p) return;
    p->mem_limit = mem_limit;
    p->cpu_limit_ticks = cpu_limit_ticks;
}

int process_mem_charge(process_t *p, uint32_t pages)
{
    if (!p) return -1;               /* V3.89-E22: 防御性检查前置 */
    ASSERT(p->mem_used <= p->mem_limit || p->mem_limit == 0);  /* 账本不变量 */
    if (p->mem_limit && p->mem_used + pages > p->mem_limit) {
        p->res_violations++;
        return -1;   /* 超限: 拒绝本次分配 */
    }
    p->mem_used += pages;
    return 0;
}

void process_mem_refund(process_t *p, uint32_t pages)
{
    if (!p) return;
    if (p->mem_used >= pages) p->mem_used -= pages;
    else p->mem_used = 0;
}

/* 时钟中断tick：递减当前进程时间片，到期则请求调度 */
/* V3.89-E34: current 有效性验证——回收(longjmp)后 current 若残留指向
 * 已释放 PCB(偶发), 此处立即识别并重置 NULL, 杜绝 sched_tick/schedule
 * 解引用悬垂指针(曾现 timeout kill 打印垃圾 pid=-268370093). */
int proc_current_valid(void)
{
    extern process_t *current;
    if (!current) return 0;
    uintptr_t p = (uintptr_t)current;
    uintptr_t base = (uintptr_t)proc_table;
    uintptr_t end = base + (uintptr_t)MAX_PROCESSES * sizeof(process_t);
    if (p < base || p >= end) return 0;          /* 不在表内=悬垂 */
    if (current->state == PROC_UNUSED) return 0; /* 槽已回收=悬垂 */
    return 1;
}


/* 调度器功能已拆分到 sched.c */

/* ============================================================================
 * V0.54.6: 进程快照 hepu_process 统一接口（32 位实现）
 * proc_table[] 已是全局，这里遍历拷贝到 hepu_process_stat_t 视图层
 * ========================================================================== */
#include "process/hepu_process.h"

int process_snapshot(hepu_process_stat_t *out, int max)
{
    if (!out || max <= 0) return -1;
    int n = 0;
    for (int i = 0; i < MAX_PROCESSES && n < max; i++) {
        process_t *p = &proc_table[i];
        if (p->state == PROC_UNUSED) continue;
        hepu_process_stat_t *s = &out[n];
        s->pid        = (int)p->pid;
        s->parent_pid = (int)p->parent_pid;
        s->state      = (int)p->state;
        s->priority   = (int)p->priority;
        s->cpu_id     = 0;                                    /* 32 位单 CPU */
        s->env_type   = (int)p->compat_abi;                   /* 0=原生, 1=Linux i386 */
        s->domain_id  = p->domain_id;
        s->runtime    = (uint64_t)p->ticks_total;             /* tick@100Hz */
        s->mem_pages  = (uint64_t)p->mem_used;                /* 已是页单位 */
        s->mem_limit  = (uint64_t)p->mem_limit;               /* 已是页单位 */
        for (int j = 0; j < 32 && p->name[j]; j++) s->name[j] = p->name[j];
        s->name[31] = 0;
        n++;
    }
    return n;
}

int process_active_count(void)
{
    int t = 0;
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (proc_table[i].state != PROC_UNUSED) t++;
    return t;
}

int process_current_pid(void)
{
    extern process_t *current;
    return current ? (int)current->pid : 0;
}
