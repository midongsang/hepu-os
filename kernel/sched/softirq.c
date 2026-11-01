#include "softirq.h"
#include "vga.h"
#include <stdint.h>

/* ============================================================================
 * V3.92-E38: 中断底半部(softirq)——类型表 + 环形队列 + 延迟统计
 *
 * 处理时机: softirq_run 只在主循环(keyboard_getchar 等输入时的 hlt
 * 复查循环 / shell 命令间隙)执行——kbd_poll 含 pm_wakeup(VGA 写/回调),
 * 中断上下文调用会死锁(E-19), 故本框架拒绝在 IRQ 内 run.
 * 演示 raise 钩子 softirq_demo_tick 挂在 sched_tick(noinline, 仅 +4B),
 * 只入队不处理——处理由随后主循环的 softirq_run 完成.
 * ========================================================================== */

typedef struct {
    softirq_fn_t fn;
    void *arg;
} softirq_entry_t;

static softirq_fn_t   softirq_handlers[SOFTIRQ_MAX_TYPES];
static softirq_entry_t softirq_queue[SOFTIRQ_QUEUE];
static volatile uint32_t softirq_head = 0;   /* 取 */
static volatile uint32_t softirq_tail = 0;   /* 放 */
static uint32_t softirq_raised   = 0;
static uint32_t softirq_handled  = 0;
static uint32_t softirq_dropped  = 0;
static uint32_t softirq_max_defer = 0;
static uint32_t softirq_last_raise_tick = 0;
/* V4.16-E61: 延迟直方图桶: 0-1, 2-5, 6-10, 11-20, 21-50, >50 (tick) */
#define SOFTIRQ_HIST_BUCKETS 6
static uint32_t softirq_hist[SOFTIRQ_HIST_BUCKETS];
static uint32_t softirq_defer_sum = 0;

extern uint32_t sched_get_ticks(void);

static inline uint32_t softirq_eflags_save(void)
{
    uint32_t f;
    __asm__ volatile ("pushfl; popl %0" : "=r"(f));
    return f;
}
static inline void softirq_eflags_restore(uint32_t f)
{
    __asm__ volatile ("pushl %0; popfl" :: "r"(f));
}

static void softirq_enqueue(softirq_fn_t fn, void *arg)
{
    uint32_t next = (softirq_tail + 1) % SOFTIRQ_QUEUE;
    if (next == softirq_head) {
        softirq_dropped++;          /* 满: 丢弃并计数, 绝不阻塞 */
        return;
    }
    softirq_queue[softirq_tail].fn  = fn;
    softirq_queue[softirq_tail].arg = arg;
    softirq_tail = next;
    softirq_raised++;
    softirq_last_raise_tick = sched_get_ticks();
}

void softirq_register(int type, softirq_fn_t fn)
{
    if (type < 0 || type >= SOFTIRQ_MAX_TYPES) return;
    uint32_t flags = softirq_eflags_save();
    __asm__ volatile ("cli");
    softirq_handlers[type] = fn;
    softirq_eflags_restore(flags);
}

void softirq_raise(int type)
{
    if (type < 0 || type >= SOFTIRQ_MAX_TYPES) return;
    softirq_fn_t fn = softirq_handlers[type];
    if (!fn) return;
    uint32_t flags = softirq_eflags_save();
    __asm__ volatile ("cli");
    softirq_enqueue(fn, 0);
    softirq_eflags_restore(flags);
}

void softirq_raise_fn(softirq_fn_t fn, void *arg)
{
    if (!fn) return;
    uint32_t flags = softirq_eflags_save();
    __asm__ volatile ("cli");
    softirq_enqueue(fn, arg);
    softirq_eflags_restore(flags);
}

void softirq_run(void)
{
    /* V4.06-E51: RTC 闹钟到点唤醒——顶半部只置 pending,
     * 底半部在此消费并唤醒 (与键盘唤醒同链, 避免中断里做复杂操作). */
    extern int rtc_consume_wake_pending(void);
    if (rtc_consume_wake_pending()) {
        extern void pm_wakeup(void);
        pm_wakeup();
    }
    while (softirq_head != softirq_tail) {
        softirq_entry_t e = softirq_queue[softirq_head];
        softirq_head = (softirq_head + 1) % SOFTIRQ_QUEUE;
        if (e.fn) {
            uint32_t defer = sched_get_ticks() - softirq_last_raise_tick;
            if (defer > softirq_max_defer) softirq_max_defer = defer;
            /* V4.16-E61: 延迟直方图分布 */
            if (defer <= 1)      softirq_hist[0]++;
            else if (defer <= 5) softirq_hist[1]++;
            else if (defer <= 10) softirq_hist[2]++;
            else if (defer <= 20) softirq_hist[3]++;
            else if (defer <= 50) softirq_hist[4]++;
            else                  softirq_hist[5]++;
            softirq_defer_sum += defer;
            (void)e.arg;
            e.fn();
            softirq_handled++;
        }
    }
}

void softirq_stats(uint32_t *raised, uint32_t *handled, uint32_t *dropped,
                   uint32_t *max_defer, uint32_t *pending)
{
    uint32_t flags = softirq_eflags_save();
    __asm__ volatile ("cli");
    if (raised)   *raised    = softirq_raised;
    if (handled)  *handled   = softirq_handled;
    if (dropped)  *dropped   = softirq_dropped;
    if (max_defer)*max_defer = softirq_max_defer;
    if (pending)  *pending   = (softirq_tail >= softirq_head)
                                 ? (softirq_tail - softirq_head)
                                 : (SOFTIRQ_QUEUE - softirq_head + softirq_tail);
    softirq_eflags_restore(flags);
}

/* V4.16-E61: 扩展统计——直方图桶 + 延迟总和 (sysinfo 用) */
void softirq_stats_hist(uint32_t *h, uint32_t *sum)
{
    uint32_t flags = softirq_eflags_save();
    __asm__ volatile ("cli");
    for (int i = 0; i < SOFTIRQ_HIST_BUCKETS; i++) h[i] = softirq_hist[i];
    if (sum) *sum = softirq_defer_sum;
    softirq_eflags_restore(flags);
}

/* ---- 演示: 模拟外设事件(网络包/磁盘完成/传感器) ---- */
static uint32_t softirq_demo_enabled = 0;
static uint32_t softirq_demo_cnt     = 0;
static uint32_t softirq_demo_last    = 0;
static uint32_t softirq_demo_seen    = 0;   /* demo_tick 被调用次数(诊断) */

static void softirq_demo_fn(void)
{
    softirq_demo_cnt++;
}

void softirq_demo_ctl(int on)
{
    softirq_demo_enabled = (on ? 1 : 0);
    softirq_demo_cnt = 0;
    softirq_demo_last = 0;
}

uint32_t softirq_demo_count(void)
{
    return softirq_demo_cnt;
}

/* sched_tick 每 tick 调用: 每 25 ticks raise 一次(仅入队, 处理在主循环) */
__attribute__((noinline))
void softirq_demo_tick(void)
{
    extern uint32_t sched_get_ticks(void);
    softirq_demo_seen++;
    if (!softirq_demo_enabled) return;
    uint32_t t = sched_get_ticks();
    if (t - softirq_demo_last >= 25) {
        softirq_demo_last = t;
        softirq_raise_fn(softirq_demo_fn, 0);
    }
}

uint32_t softirq_demo_seen_count(void) { return softirq_demo_seen; }
uint32_t softirq_demo_enabled_get(void) { return softirq_demo_enabled; }
uint32_t softirq_demo_last_get(void) { return softirq_demo_last; }
uint32_t softirq_tick_now(void) { extern uint32_t sched_get_ticks(void); return sched_get_ticks(); }
