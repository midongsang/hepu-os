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
} uthread_t;

static uthread_t g_ut[MAX_UTHREADS];
static int g_ut_cur = -1;
static int g_next_tid = 1000;

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t v);
extern int user_process_current_slot(void);

static void ut_save(uthread_t *t, uint64_t *f)
{
    for (int i = 0; i < 15; i++) t->raw[i] = f[i];
    t->rip = f[19];
    t->rflags = f[21];
    t->rsp = f[22];
}

static void ut_restore(uthread_t *t, uint64_t *f)
{
    for (int i = 0; i < 15; i++) f[i] = t->raw[i];
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

void uthread_clear_owner(int owner)
{
    for (int i = 0; i < MAX_UTHREADS; i++) {
        if (g_ut[i].used && g_ut[i].owner == owner) g_ut[i].used = 0;
    }
    if (g_ut_cur >= 0 && !g_ut[g_ut_cur].used) g_ut_cur = -1;
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
