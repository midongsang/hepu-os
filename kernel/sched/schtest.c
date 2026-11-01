/* ============================================================================
 * 赫普OS - 调度器动态时间片测试 (V3.89-E27)
 *
 * 场景: 两个 NORM 用户态进程竞争——
 *   A (sch_a): CPU 密集, 持续计算不主动让出;
 *   B (sch_b): IO/交互型, 反复真睡眠(sys_sleep)让出 CPU.
 * 调度器按片内让出次数分化时间片: A→长片(30), B→短片(8).
 * 验证链: shell 执行 schtest → 进入 A, PIT 驱动 A/B 轮转 8 秒 →
 *         A/B 自动 SYS_EXIT 回 shell → schstat 显示动态调整统计.
 * ========================================================================== */

#include <stdint.h>
#include "vga.h"
#include "process.h"

/* 动态时间片调整统计(内核侧, schstat 读取) */
uint32_t slice_adj_count = 0;   /* 时间片被动态调整的次数 */

/* 用户态进程入口(ring3 汇编, 见 schtest_a.S / schtest_b.S) */
extern void schtest_a_start(void);
extern void schtest_b_start(void);

/* shell 挂接: schtest —— 创建 A/B 两个 NORM 进程并进入演示 */
void schtest_run(void)
{
    extern process_t *process_create(const char *, uint32_t, privilege_t, uint32_t);
    extern void process_enter_user_mode(process_t *);
    process_t *a = process_create("sch_a", (uint32_t)schtest_a_start, PRIV_USER, 1);
    process_t *b = process_create("sch_b", (uint32_t)schtest_b_start, PRIV_USER, 1);
    if (a && b) {
        slice_adj_count = 0;   /* 清统计再演示 */
        vga_printf("[SCHTEST] A=CPU pid=%d B=IO pid=%d, 8s demo then auto-exit\n",
                   a->pid, b->pid);
        process_enter_user_mode(a);   /* 进入 A; PIT 驱动 A/B 轮转 */
    } else {
        vga_puts("[SCHTEST] proc table full, skip\n");
    }
}

/* shell 挂接: schstat —— 显示动态时间片统计 (V3.89-E31 增强) */
void schstat_show(void)
{
    extern uint32_t sched_get_switches(void);
    extern uint32_t sched_get_ticks(void);
    uint32_t i, n_run = 0, n_rdy = 0, n_slp = 0, n_used = 0;
    uint32_t ts8 = 0, ts20 = 0, ts30 = 0;

    /* 遍历 proc_table 统计状态分布与当前时间片分布(纯读) */
    for (i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_UNUSED) continue;
        n_used++;
        switch (proc_table[i].state) {
        case PROC_RUNNING: n_run++; break;
        case PROC_READY:   n_rdy++; break;
        case PROC_SLEEPING:n_slp++; break;
        default: break;
        }
        if      (proc_table[i].time_slice == 8)  ts8++;
        else if (proc_table[i].time_slice == 20) ts20++;
        else if (proc_table[i].time_slice == 30) ts30++;
    }

    vga_printf("[SCHSTAT] slice adj=%d  dist: ts8=%d ts20=%d ts30=%d\n",
               slice_adj_count, ts8, ts20, ts30);
    vga_printf("          procs: run=%d rdy=%d slp=%d total=%d\n",
               n_run, n_rdy, n_slp, n_used);
    {
        uint32_t sw = sched_get_switches();
        uint32_t tk = sched_get_ticks();
        uint32_t rate = (tk >= 100) ? (sw / (tk / 100)) : 0;  /* 每秒切换次数 */
        vga_printf("          ctx=%d ticks=%d ~%d/s\n", sw, tk, rate);
    }
}
