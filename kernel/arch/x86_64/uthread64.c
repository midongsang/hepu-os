/* V0.57.4: 用户线程调度单元
 *
 * 背景（见 docs/status/FIXES_0_57_0.md §1.84、OPEN_BUGS SEC-20）：
 * 在此之前用户执行流不在任何调度表里，rip/rsp/rflags/GPR 没有保存槽位，
 * PIT 的 tick 只轮转 process64 的内核线程。本次把这个缺口补上：
 *
 *   1. 每条用户执行流对应一个 uthread_t，保存完整用户上下文；
 *   2. 用户态时钟中断（vector 0x40，帧里是原样的用户现场）直接读/写中断帧，
 *      完成"保存当前 → 选下一个 → 恢复"的轮转；
 *   3. 单线程进程永不被切换（只在同 owner 下 >=2 线程时轮转），
 *      所以现有用户的观感与行为不变。
 *
 * 上下文与中断帧一一对应。帧索引（entry_64.S IRQ_ENTRY 的 push 顺序）：
 *   0=r15 1=r14 2=r13 3=r12 4=r11 5=r10 6=r9 7=r8 8=rbp 9=rdi 10=rsi
 *   11=rdx 12=rcx 13=rbx 14=rax 15=ds 16=es 17=vector 18=error
 *   19=rip 20=cs 21=rflags 22=rsp 23=ss
 */
#include <stdint.h>

#define MAX_UTHREADS 16
#define UT_FREE    0
#define UT_READY   1

typedef struct {
    int used;
    int owner;          /* 所属 user_process 槽位（共享页表的边界） */
    int tid;
    int state;
    uint64_t raw[15];   /* 帧 0..14：r15..rax，原样保存 */
    uint64_t rip, rflags, rsp, fsbase;
    uint64_t kstack_top;      /* 本线程的 syscall 内核栈顶（g_syscall_kstack_slot 是全局单值，
                                 父子共用同一个栈会互相踩，实测表现为随机 #PF） */
} uthread_t;

static uthread_t g_ut[MAX_UTHREADS];
static int g_ut_cur = -1;
static int g_next_tid = 1000;

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t v);
extern int user_process_current_slot(void);
extern uint64_t pmm64_alloc_page(void);

/* 帧布局（entry_64.S IRQ_ENTRY）：ds/es 是在 15 个 GPR 之后压的，
 * 所以它们在**最低地址**：
 *   f[0]=es f[1]=ds f[2]=r15 f[3]=r14 f[4]=r13 f[5]=r12 f[6]=r11
 *   f[7]=r10 f[8]=r9 f[9]=r8 f[10]=rbp f[11]=rdi f[12]=rsi f[13]=rdx
 *   f[14]=rcx f[15]=rbx f[16]=rax
 *   f[17]=vector f[18]=error f[19]=rip f[20]=cs f[21]=rflags f[22]=rsp f[23]=ss
 *
 * V0.57.5 修正：之前按"f[0]=r15"算，整体差了 2 个槽 —— 写 rax 其实写进了 rbx，
 * 于是子线程的 rax 一直是父线程的返回值，clone 直接走进父分支（实测 rip 落在
 * 0x400000bb 的 g_parent_count 循环里）。 */
#define UT_FRAME_GPR_BASE 2

static void ut_save(uthread_t *t, uint64_t *f)
{
    for (int i = 0; i < 15; i++) t->raw[i] = f[UT_FRAME_GPR_BASE + i];
    t->rip = f[19];
    t->rflags = f[21];
    t->rsp = f[22];
}

static void ut_restore(uthread_t *t, uint64_t *f)
{
    for (int i = 0; i < 15; i++) f[UT_FRAME_GPR_BASE + i] = t->raw[i];
    if (t->kstack_top) {
        extern uint64_t g_syscall_kstack_slot;
        g_syscall_kstack_slot = t->kstack_top;
    }
    f[19] = t->rip;
    f[21] = t->rflags;
    f[22] = t->rsp;
    /* FS_BASE 不是 sysret/iret 恢复的寄存器，必须显式写 MSR */
    __asm__ volatile("wrmsr" : :
                     "c"(0xC0000100),
                     "a"((uint32_t)(t->fsbase & 0xFFFFFFFFULL)),
                     "d"((uint32_t)(t->fsbase >> 32)));
}

/* 进程启动时登记主线程（上下文留空，首次 tick 自然填充） */
int uthread_bind_main(int owner, int tid)
{
    for (int i = 0; i < MAX_UTHREADS; i++) {
        if (!g_ut[i].used) {
            g_ut[i].used = 1;
            g_ut[i].owner = owner;
            g_ut[i].tid = tid;
            g_ut[i].state = UT_READY;
            g_ut[i].fsbase = 0;
            if (!g_ut[i].kstack_top) {
                uint64_t kp = pmm64_alloc_page();
                g_ut[i].kstack_top = kp ? (kp + 4096) : 0;
            }
            for (int k = 0; k < 15; k++) g_ut[i].raw[k] = 0;
            g_ut[i].rip = 0; g_ut[i].rsp = 0; g_ut[i].rflags = 0;
            g_ut_cur = i;
            return i;
        }
    }
    return -1;
}

/* clone：建一条新执行流，从 rip/rsp 开始，rax=0 */
int uthread_create(int owner, uint64_t rip, uint64_t rsp, uint64_t fsbase)
{
    for (int i = 0; i < MAX_UTHREADS; i++) {
        if (!g_ut[i].used) {
            g_ut[i].used = 1;
            g_ut[i].owner = owner;
            g_ut[i].tid = g_next_tid++;
            g_ut[i].state = UT_READY;
            g_ut[i].fsbase = fsbase;
            if (!g_ut[i].kstack_top) {
                uint64_t kp = pmm64_alloc_page();
                g_ut[i].kstack_top = kp ? (kp + 4096) : 0;
            }
            for (int k = 0; k < 15; k++) g_ut[i].raw[k] = 0;
            g_ut[i].raw[14] = 0;                /* rax = 0：子线程 clone 返回 0 */
            g_ut[i].rip = rip;
            g_ut[i].rsp = rsp;
            g_ut[i].rflags = 0x202;             /* IF=1，否则收不到时钟 */
            vga_puts("[UTHREAD] created tid=");
            vga_put_hex((uint64_t)g_ut[i].tid);
            vga_puts(" owner=");
            vga_put_hex((uint64_t)owner);
            vga_puts("\n");
            return g_ut[i].tid;
        }
    }
    return -1;
}

int uthread_current_tid(void)
{
    if (g_ut_cur < 0 || !g_ut[g_ut_cur].used) return -1;
    return g_ut[g_ut_cur].tid;
}

/* 摘掉某个进程的全部用户线程。
 *
 * V0.57.6 修复：**不能在这里释放内核栈页**。
 * sys_exit 会先调本函数，而此刻 CPU 正踩在这条线程的内核栈上；
 * 把该页还给分配器后，后面的代码（含 syscall 返回路径）就写在已释放的页里，
 * 栈上的 rcx（sysretq 的 RIP）被踩成 0 —— 实测正是收尾阶段
 * `vector=0xE error=0x15 rip=0x0` 的来源。
 * 改为：只清 used、保留 kstack_top，下次 bind/create 复用同一块页。 */
void uthread_clear_owner(int owner)
{
    for (int i = 0; i < MAX_UTHREADS; i++) {
        if (g_ut[i].used && g_ut[i].owner == owner) {
            g_ut[i].used = 0;      /* kstack_top 保留，槽位复用时重新初始化 */
        }
    }
    if (g_ut_cur >= 0 && !g_ut[g_ut_cur].used) g_ut_cur = -1;
}

/* ---- V0.57.8：线程退出 ------------------------------------------------
 * 目标：一条用户线程结束自己时，不终止整进程，而是让 CPU 直接回到同进程的
 * 下一个就绪线程。
 *
 * 实现方式：syscall 返回走的是 sysretq，RIP/RFLAGS/RSP 分别来自
 * rcx / r11 / g_syscall_ustack_slot。所以只要能在这条 syscall 的内核栈上
 * 重建目标线程的整套返回帧，sysretq 就会"落到"目标线程且不回到当前线程。
 * 具体重建在 uthread_apply_redirect() 里做，由 entry_64.S 在 handler
 * 返回后调用。
 *
 * 找不到兄弟线程时返回 0 且不改变任何状态 —— 线程继续跑，
 * 绝不因为一次 thread_exit 把整个进程带走。 */
int g_thr_redirect_flag = 0;

int uthread_exit_current(void)
{
    if (g_ut_cur < 0 || !g_ut[g_ut_cur].used) return 0;

    int cur = g_ut_cur;
    int owner = g_ut[cur].owner;

    /* 先看有没有可切换的兄弟；没有就原地不动 */
    int next = -1;
    for (int i = 0; i < MAX_UTHREADS; i++) {
        if (i != cur && g_ut[i].used && g_ut[i].owner == owner &&
            g_ut[i].state == UT_READY) { next = i; break; }
    }
    if (next < 0) return 0;

    g_ut[cur].used = 0;        /* kstack_top 保留，槽位复用 */
    g_ut_cur = next;
    g_thr_redirect_flag = 1;

    vga_puts("[UTHREAD] thread exit tid=");
    vga_put_hex((uint64_t)g_ut[cur].tid);
    vga_puts(" -> switch to tid=");
    vga_put_hex((uint64_t)g_ut[next].tid);
    vga_puts("\n");
    return 1;
}

/* 在 syscall 返回帧上重建目标线程上下文。
 * f 指向 syscall 帧的 r15 槽（与 IRQ 帧不同：syscall 帧没有 ds/es 那两个槽）：
 *   f[0]=r15 f[1]=r14 f[2]=r13 f[3]=r12 f[4]=r11 f[5]=r10 f[6]=r9 f[7]=r8
 *   f[8]=rbp f[9]=rdi f[10]=rsi f[11]=rdx f[12]=rcx f[13]=rbx f[14]=rax
 * 注意：rcx/r11 在 sysretq 语义下分别是 RIP/RFLAGS，必须用 t->rip/t->rflags
 * 覆盖，不能直接搬 raw[12]/raw[4]（那两个是普通寄存器快照）。 */
uint64_t uthread_apply_redirect(uint64_t *f)
{
    if (g_ut_cur < 0 || !g_ut[g_ut_cur].used) return 0;
    uthread_t *t = &g_ut[g_ut_cur];

    for (int i = 0; i < 15; i++) f[i] = t->raw[i];
    f[12] = t->rip;             /* rcx = 目标 RIP */
    f[4]  = t->rflags;          /* r11 = 目标 RFLAGS */

    extern uint64_t g_syscall_ustack_slot, g_syscall_kstack_slot;
    g_syscall_ustack_slot = t->rsp;
    if (t->kstack_top) g_syscall_kstack_slot = t->kstack_top;

    return t->raw[14];          /* 作为返回值写进 rax 槽 */
}

/* 用户态时钟中断：保存当前线程，轮转同 owner 的下一个 */
void uthread_timer_tick(uint64_t *f)
{
    int owner = user_process_current_slot();
    if (owner < 0) return;

    /* 当前线程若不是本 owner 的（例如刚 exec 完），先认领 */
    if (g_ut_cur < 0 || !g_ut[g_ut_cur].used || g_ut[g_ut_cur].owner != owner) {
        g_ut_cur = -1;
        for (int i = 0; i < MAX_UTHREADS; i++) {
            if (g_ut[i].used && g_ut[i].owner == owner) { g_ut_cur = i; break; }
        }
        if (g_ut_cur < 0) return;
    }

    ut_save(&g_ut[g_ut_cur], f);

    /* 同 owner 的线程清单 */
    int idx[MAX_UTHREADS], n = 0, pos = -1;
    for (int i = 0; i < MAX_UTHREADS; i++) {
        if (!g_ut[i].used || g_ut[i].owner != owner) continue;
        if (i == g_ut_cur) pos = n;
        idx[n++] = i;
    }

    if (n < 2) return;                  /* 单线程：行为完全不变 */

    /* 时间片：每 10 个 tick（约 10ms）才真正切一次。
     * PIT 是 1000Hz，若每个 tick 都切，线程只能前进几条指令，
     * 实测表现为 rip 永远停在同一处。 */
    { static int slice = 0;
      if (++slice < 10) return;
      slice = 0; }

    int next_i = idx[(pos + 1) % n];
    g_ut_cur = next_i;
    ut_restore(&g_ut[next_i], f);
}
