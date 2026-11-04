/* ============================================================================
 * 赫普OS - Shell：系统/进程命令
 * 从 shell.c 拆分（V6.46 代码重构，大文件拆分目标<500行）
 * 包含：cmd_ps/cmd_affinity/cmd_ktest/cmd_prio/cmd_meminfo/
 *       rtc_snooze_parse/cmd_rtc/cmd_sysinfo
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "vga.h"
#include "process.h"
#include "shell.h"
#include "serial.h"
#include "pit.h"
#include "hepu.h"
#include "dtb.h"
#include "i18n.h"

void cmd_ps(void)
{
    uint32_t total, running, ready;
    process_stats(&total, &running, &ready);
    vga_puts(i18n_get(I18N_PROC_TOTAL));
    vga_printf(": %d, running: %d, ready: %d\n", total, running, ready);
    vga_printf(" current PID: %d\n", sched_current_pid());
    /* V3.87-E7: 每进程资源账本 */
    extern process_t proc_table[];
    proc_lock_acquire();   /* V3.89-E35 */
    /* V3.89-E28 修复: 遍历全表——原 i<total 在表前部有 UNUSED 空位时
     * 会漏显/错位真实进程(残留显示与丢失并存). */
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        process_t *p = &proc_table[i];
        if (p->state == PROC_UNUSED) continue;
        if (!p->name[0]) continue;
        vga_printf("  pid=%d %-8s %-4s mem=%u/%uKB cpu=%u/%u viol=%u blk=%u aff=0x%x\n",
                   p->pid, p->name,
                   (p->priority == PRIO_RT) ? "RT" :
                   (p->priority == PRIO_BKG) ? "BKG" : "NORM",
                   p->mem_used * (PAGE_SIZE / 1024),
                   p->mem_limit ? p->mem_limit * (PAGE_SIZE / 1024) : 0,
                   p->ticks_total,
                   p->cpu_limit_ticks,
                   p->res_violations,
                   p->blocked_ticks,
                   p->cpu_affinity);
    }
    proc_lock_release();
}

/* V3.96-E42: 设置/查看进程 CPU 亲和性 */
void cmd_affinity(char *arg)
{
    extern int sched_set_affinity(uint32_t, uint32_t);
    extern uint32_t smp_online_cpus(void);
    if (!arg || !*arg) {
        vga_puts("usage: affinity <pid> <mask>\n");
        vga_printf("  online CPUs mask: 0x%x (bit i = CPU i)\n",
                   smp_online_cpus());
        vga_puts("  mask 必须与在线 CPU 有交集, 否则拒绝\n");
        return;
    }
    char *sp = 0;
    for (char *c = arg; *c; c++) if (*c == ' ') { *c = 0; sp = c + 1; break; }
    if (!sp) { vga_puts("usage: affinity <pid> <mask>\n"); return; }
    int pid = 0; { int i = 0; while (arg[i]) { pid = pid * 10 + (arg[i] - '0'); i++; } }
    uint32_t mask = 0; { int i = 0; while (sp[i]) { mask = mask * 10 + (sp[i] - '0'); i++; } }
    if (sched_set_affinity((uint32_t)pid, mask) == 0)
        vga_printf("[AFF] pid=%d affinity set to 0x%x\n", pid, mask);
    else
        vga_printf("[AFF] reject: pid=%d mask=0x%x (no online CPU in mask)\n",
                   pid, mask);
}

/* V3.97-E43: 一键运行全部 KUnit 套件 + 打印报告(压力回归) */
void cmd_ktest(void)
{
    extern int kunit_run_all(void);
    extern void kunit_print_report(void);
    vga_puts("[KTEST] running all suites...\n");
    int fail = kunit_run_all();
    kunit_print_report();
    vga_printf("[KTEST] done: %s\n", fail ? "FAIL" : "ALL PASS");
}

/* V3.90-E36: 运行时调整进程优先级(0=RT/1=NORM/2=BKG), 同步时间片 */
void cmd_prio(char *arg)
{
    extern process_t proc_table[];
    if (!arg || !*arg) {
        vga_puts("usage: prio <pid> <0|1|2>\n");
        vga_puts("  0=RT(实时,片2)  1=NORM(普通,片20动态)  2=BKG(后台,片5)\n");
        return;
    }
    char *sp = 0;
    for (char *c = arg; *c; c++) if (*c == ' ') { *c = 0; sp = c + 1; break; }
    if (!sp) return;
    int pid = 0, lvl = 0;
    { int i = 0; while (arg[i]) { pid = pid * 10 + (arg[i] - '0'); i++; } }
    { int i = 0; while (sp[i]) { lvl = lvl * 10 + (sp[i] - '0'); i++; } }
    if (lvl < 0 || lvl > 2) { vga_puts("level must be 0..2\n"); return; }
    extern void proc_lock_acquire(void);
    extern void proc_lock_release(void);
    extern void sched_ready_mark(uint32_t);
    extern void sched_ready_clear(uint32_t);
    proc_lock_acquire();
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        process_t *p = &proc_table[i];
        if (p->state == PROC_UNUSED || p->pid != (uint32_t)pid) continue;
        uint32_t oldp = p->priority;
        p->priority = lvl;
        p->time_slice = (lvl == 0) ? 2 : (lvl == 2) ? 5 : 20;
        p->time_used = 0;   /* 新片长立即生效 */
        if (p->state == PROC_READY) {
            sched_ready_clear(oldp);   /* 位图迁移: 旧级清位 */
            sched_ready_mark(lvl);     /* 新级置位 */
        }
        vga_printf(" pid %d -> prio %s (slice %d)\n", pid,
                   lvl==0?"RT":lvl==1?"NORM":"BKG", p->time_slice);
        proc_lock_release();
        return;
    }
    proc_lock_release();
    vga_printf(" pid %d not found\n", pid);
}

/* V3.89-E29: 内存报表——kheap 堆统计 + kmalloc 计数 + 进程状态/内存分布 */
void cmd_meminfo(void)
{
    extern process_t proc_table[MAX_PROCESSES];
    extern void kheap_stats(uint32_t *, uint32_t *, uint32_t *);
    extern void kheap_perf(uint32_t *, uint32_t *, uint32_t *, uint32_t *, uint32_t *);
    uint32_t used = 0, free = 0, total = 0;
    uint32_t calls = 0, frees = 0, bytes = 0, peak = 0, fails = 0;
    kheap_stats(&used, &free, &total);
    kheap_perf(&calls, &frees, &bytes, &peak, &fails);
    vga_printf("[MEM] kheap: used=%uB free=%uB total=%uB peak=%uB\n",
               used, free, total, peak);
    vga_printf("[MEM] kmalloc: calls=%u frees=%u bytes=%u fails=%u\n",
               calls, frees, bytes, fails);
    /* 进程状态分布 */
    uint32_t n_run = 0, n_rdy = 0, n_slp = 0, n_other = 0, n_unused = 0;
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        switch (proc_table[i].state) {
        case PROC_RUNNING: n_run++; break;
        case PROC_READY:   n_rdy++; break;
        case PROC_SLEEPING:n_slp++; break;
        case PROC_UNUSED:  n_unused++; break;
        default:           n_other++; break;
        }
    }
    vga_printf("[MEM] procs: running=%u ready=%u sleeping=%u other=%u unused=%u\n",
               n_run, n_rdy, n_slp, n_other, n_unused);
    /* 每进程内存: 内核栈 + 用户栈 */
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        process_t *p = &proc_table[i];
        if (p->state == PROC_UNUSED || !p->name[0]) continue;
        uint32_t kb = (p->kernel_stack ? KERNEL_STACK_SIZE : 0) +
                      (p->user_stack_raw ? USER_STACK_SIZE : 0);
        vga_printf("  pid=%u %-8s state=%d stack=%uKB ticks=%u\n",
                   p->pid, p->name, (int)p->state, kb / 1024, p->ticks_total);
    }
}

/* V3.87-E1: 性能指标——调度/内存/中断/每进程CPU占用 */
/* V4.01-E46: IDE DMA 批量读写自测.
 * 1) 显示 DMA 可用性/BMBA;
 * 2) 批量读 64 扇区(LBA 100): DMA 与 PIO 结果一致性校验;
 * 3) DMA 写 1 扇区(LBA 65000, 先备份后恢复——数据零破坏);
 * 4) PIO vs DMA 耗时对比(rdtsc). */
/* V4.14-E59: snooze 独立解析函数 (避免 gcc 优化删除分支体) */
void rtc_snooze_parse(const char *p6)
{
    extern int rtc_alarm_snooze(int);
    static int (*volatile snooze_impl)(int) = rtc_alarm_snooze;  /* volatile 防优化删除 */
    const char *q = p6 + 7;
    int min = 0;
    while (*q >= '0' && *q <= '9') { min = min * 10 + (*q - '0'); q++; }
    if (min < 1 || min > 60) {
        vga_puts("[RTC] usage: rtc alarm snooze <1-60 min>\n");
        return;
    }
    int r = snooze_impl(min);
    if (r == 0)      vga_puts("[RTC] no alarm fired recently\n");
    else if (r == -2) vga_puts("[RTC] alarm slots full!\n");
    else if (r == 1) vga_printf("[RTC] snooze +%d min (oneshot added)\n", min);
}

/* V4.14-E59: volatile 挂钩防 gcc DCE */
static void (*volatile rtc_snooze_hook)(const char *) = rtc_snooze_parse;

/* V7.45: 图形模式任务管理器命令 */
void cmd_taskmgr(void)
{
    extern void task_manager_gui_run(void);
    vga_puts("[TASKMGR] 启动图形模式任务管理器...\n");
    vga_puts("按键: W/S=选择, K=终止, R=刷新, Q/ESC=退出\n");
    task_manager_gui_run();
}

void cmd_rtc(char *args)
{
    /* V4.06-E51: RTC 时钟显示 + 闹钟唤醒设置 */
    extern int rtc_get_time(int *, int *, int *, int *, int *, int *);
    extern void rtc_alarm_sec(uint32_t);
    extern void rtc_alarm_off(void);
    extern int rtc_alarm_fired(void);
    if (args && args[0]) {
        /* rtc alarm <sec> | rtc alarm off */
        if (strncmp(args, "alarm ", 6) == 0) {
            const char *p6 = args + 6;
            if (strcmp(p6, "off") == 0) {
                rtc_alarm_off();
                vga_puts("[RTC] alarm disabled\n");
                return;
            }
            /* V4.13-E58: rtc alarm list */
            if (strcmp(p6, "list") == 0) {
                extern int rtc_alarm_get(int, int *, int *, int *, int *);
                int t, a, b, c;
                int any = 0;
                for (int i = 0; i < 8; i++) {
                    if (rtc_alarm_get(i, &t, &a, &b, &c)) {
                        any = 1;
                        if (t == 1)      vga_printf("  slot %d: one-shot %d s\n", i, a);
                        else if (t == 2) vga_printf("  slot %d: daily %02d:%02d\n", i, a, b);
                        else if (t == 3) vga_printf("  slot %d: weekly dow=%d %02d:%02d\n", i, a, c, b);
                    }
                }
                if (!any) vga_puts("[RTC] no alarms\n");
                return;
            }
            /* V4.13-E58: rtc alarm del <idx> */
            if (strncmp(p6, "del ", 4) == 0) {
                extern void rtc_alarm_del(int);
                const char *q = p6 + 4;
                int idx = 0;
                while (*q >= '0' && *q <= '9') idx = idx * 10 + (*q - '0');
                if (idx >= 0 && idx < 8) rtc_alarm_del(idx);
                else vga_puts("[RTC] slot index must be 0-7\n");
                return;
            }
            /* V4.14-E59: rtc alarm snooze <min> — 稍后提醒 */
            if (strncmp(p6, "snooze ", 7) == 0) {
                rtc_snooze_hook(p6);
                return;
            }
            /* V4.13-E58: "add " 前缀——显式追加槽位 (与旧形式等价) */
            if (strncmp(p6, "add ", 4) == 0) p6 += 4;
            /* V4.14-E59: rtc alarm add weekday <hh:mm> — 周一至五 */
            if (strncmp(p6, "weekday", 7) == 0) {
                extern void rtc_alarm_weekly(int, int, int);
                const char *q = p6 + 7;
                while (*q == ' ') q++;
                int hh = 0, mm = 0, nd = 0;
                while (*q >= '0' && *q <= '9') { q++; nd++; }
                if (nd == 4) {
                    const char *r = q - 4;
                    hh = (r[0] - '0') * 10 + (r[1] - '0');
                    mm = (r[2] - '0') * 10 + (r[3] - '0');
                } else {
                    q = p6 + 7;
                    while (*q >= '0' && *q <= '9') { hh = hh * 10 + (*q - '0'); q++; }
                    if (*q == ':') q++;
                    while (*q >= '0' && *q <= '9') { mm = mm * 10 + (*q - '0'); q++; }
                }
                if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59) {
                    for (int d = 1; d <= 5; d++) rtc_alarm_weekly(d, hh, mm);
                    vga_printf("[RTC] weekday alarms mon-fri %02d:%02d (use with pm sleep)\n",
                               hh, mm);
                } else {
                    vga_puts("[RTC] usage: rtc alarm add weekday <hh:mm>\n");
                }
                return;
            }
            /* V4.11-E56: rtc alarm weekly <dow> <hh:mm> */
            if (strncmp(p6, "weekly ", 7) == 0) {
                extern void rtc_alarm_weekly(int, int, int);
                const char *q = p6 + 7;
                int dow = -1;
                if (*q >= '0' && *q <= '6') {
                    dow = *q - '0';
                    q++;
                } else if (strncmp(q, "sun", 3) == 0) { dow = 0; q += 3; }
                else if (strncmp(q, "mon", 3) == 0) { dow = 1; q += 3; }
                else if (strncmp(q, "tue", 3) == 0) { dow = 2; q += 3; }
                else if (strncmp(q, "wed", 3) == 0) { dow = 3; q += 3; }
                else if (strncmp(q, "thu", 3) == 0) { dow = 4; q += 3; }
                else if (strncmp(q, "fri", 3) == 0) { dow = 5; q += 3; }
                else if (strncmp(q, "sat", 3) == 0) { dow = 6; q += 3; }
                else if (*q >= '7' && *q <= '9') {
                    vga_puts("[RTC] dow must be 0-6 (0=Sun) or mon..sun\n");
                    return;
                } else {
                    vga_puts("[RTC] usage: rtc alarm weekly <dow> <hh:mm>\n");
                    return;
                }
                while (*q == ' ') q++;
                int hh = 0, mm = 0, nd = 0;
                const char *d0 = q;
                while (*q >= '0' && *q <= '9') { q++; nd++; }
                if (nd == 4) {
                    const char *r = q - 4;
                    hh = (r[0] - '0') * 10 + (r[1] - '0');
                    mm = (r[2] - '0') * 10 + (r[3] - '0');
                } else if (nd == 2 || nd == 1) {
                    q = d0;
                    while (*q >= '0' && *q <= '9') { hh = hh * 10 + (*q - '0'); q++; }
                    if (*q == ':') q++;
                    while (*q >= '0' && *q <= '9') { mm = mm * 10 + (*q - '0'); q++; }
                } else {
                    vga_puts("[RTC] usage: rtc alarm weekly <dow> <hh:mm>\n");
                    return;
                }
                if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59) {
                    rtc_alarm_weekly(dow, hh, mm);
                    vga_printf("[RTC] weekly alarm dow=%d %02d:%02d (use with pm sleep)\n",
                               dow, hh, mm);
                } else {
                    vga_puts("[RTC] usage: rtc alarm weekly <dow> <hh:mm>\n");
                }
                return;
            }
            /* V4.09-E54: rtc alarm daily <hh:mm> */
            if (strncmp(p6, "daily ", 6) == 0) {
                extern void rtc_alarm_daily(int, int);
                const char *q = p6 + 6;
                int hh = 0, mm = 0, nd = 0;
                while (*q >= '0' && *q <= '9') { q++; nd++; }
                if (nd == 4) {
                    /* daily 1301 无冒号格式 (数字起点在 q-4) */
                    const char *r = q - 4;
                    hh = (r[0] - '0') * 10 + (r[1] - '0');
                    mm = (r[2] - '0') * 10 + (r[3] - '0');
                } else {
                    q = p6;
                    while (*q >= '0' && *q <= '9') { hh = hh * 10 + (*q - '0'); q++; }
                    if (*q == ':') q++;
                    while (*q >= '0' && *q <= '9') { mm = mm * 10 + (*q - '0'); q++; }
                }
                if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59) {
                    rtc_alarm_daily(hh, mm);
                    vga_printf("[RTC] daily alarm %02d:%02d (use with pm sleep)\n", hh, mm);
                } else {
                    vga_puts("[RTC] usage: rtc alarm daily <hh:mm>\n");
                }
                return;
            }
            int sec = 0;
            for (; *p6 >= '0' && *p6 <= '9'; p6++) sec = sec * 10 + (*p6 - '0');
            if (sec > 0) {
                rtc_alarm_sec((uint32_t)sec);
                vga_printf("[RTC] alarm in %d sec (use with pm sleep)\n", sec);
            } else {
                vga_puts("[RTC] usage: rtc alarm <sec> | daily <hh:mm> | weekly <dow> <hh:mm>\n");
                vga_puts("[RTC]        rtc alarm add <sec> | add daily .. | add weekly ..\n");
                vga_puts("[RTC]        rtc alarm list | del <idx> | snooze <min> | off\n");
            }
            return;
        }
        vga_puts("[RTC] usage: rtc | rtc alarm <sec> | rtc alarm daily <hh:mm> | rtc alarm off\n");
        return;
    }
    int h = 0, m = 0, s2 = 0, d = 0, mo = 0, y = 0;
    if (rtc_get_time(&h, &m, &s2, &d, &mo, &y) == 0) {
        vga_printf("[RTC] %04d-%02d-%02d %02d:%02d:%02d\n", y, mo, d, h, m, s2);
    } else {
        vga_puts("[RTC] CMOS read error\n");
    }
    vga_printf("[RTC] alarm: %s\n", rtc_alarm_fired() ? "fired" : "idle");
}

void cmd_sysinfo(void)
{
    /* V4.05-E50: 系统性能指标采集——tick/上下文切换/中断/kheap 汇总 */
    extern uint32_t sched_get_ticks(void);
    extern uint32_t sched_get_switches(void);
    extern uint32_t irq_count[];
    extern void sched_stats(uint32_t *, uint32_t *, uint32_t *);
    extern void kheap_stats(uint32_t *, uint32_t *, uint32_t *);
    uint32_t total = 0, running = 0, ready = 0;
    sched_stats(&total, &running, &ready);
    uint32_t ticks = sched_get_ticks();
    uint32_t sw = sched_get_switches();
    uint32_t hu = 0, hf = 0, ht = 0;
    kheap_stats(&hu, &hf, &ht);
    /* 中断汇总 */
    uint32_t irq_total = 0, irq_pit = 0, irq_kbd = 0, irq_mouse = 0;
    for (int i = 0; i < 16; i++) irq_total += irq_count[i];
    irq_pit   = irq_count[0];
    irq_kbd   = irq_count[1];
    irq_mouse = irq_count[12];
    vga_puts("=== HEPU sysinfo (V4.05-E50) ===\n");
    vga_puts(i18n_get(I18N_UPTIME));
    vga_printf(": %u.%02us (ticks=%u)\n", ticks / 100, ticks % 100, ticks);
    vga_printf(" procs: total=%u running=%u ready=%u\n", total, running, ready);
    vga_printf(" ctx-switches: %u (avg %u/s)\n", sw, ticks ? (sw * 100) / ticks : 0);
    vga_printf(" irq: PIT=%u kbd=%u mouse=%u total=%u\n",
               irq_pit, irq_kbd, irq_mouse, irq_total);
    uint32_t pct = ht ? (hu * 100) / ht : 0;  /* 内核无浮点, 整数百分比 */
    vga_puts(i18n_get(I18N_MEM_USED));
    vga_printf(": kheap used=%uKB free=%uKB total=%uKB (%u%%)\n",
               hu / 1024, hf / 1024, ht / 1024, pct);
    /* V4.21-E66: DTB 平台信息 */
    extern int dtb_node_count(void);
    extern dtb_node_t *dtb_find_node(const char *);
    extern uint32_t dtb_get_prop_u32(dtb_node_t *, const char *, uint32_t);
    dtb_node_t *cpu = dtb_find_node("/cpus/cpu@0");
    uint32_t cpu_mhz = cpu ? dtb_get_prop_u32(cpu, "clock-frequency", 0) / 1000000 : 0;
    vga_printf(" platform: hepu-x86 (dtb=%d nodes, cpu=%uMHz)\n",
               dtb_node_count(), cpu_mhz);
}
