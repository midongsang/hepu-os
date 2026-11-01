/* ============================================================================
 * 赫普OS - IRQ 硬件中断处理框架
 * 功能: 管理16个硬件中断(IRQ0-15)，分发给注册的处理函数
 * ========================================================================== */

#include <stdint.h>
#include "irq.h"
#include "pic.h"
#include "pit.h"
#include "smp.h"
#include "vga.h"

#define IRQ_COUNT 16

static irq_handler_t irq_handlers[IRQ_COUNT] = {0};

/* V3.87-E1: 每IRQ中断次数统计(供 perf 监控) */
uint32_t irq_count[IRQ_COUNT] = {0};

void irq_init(void)
{
    for (int i = 0; i < IRQ_COUNT; i++) {
        irq_handlers[i] = 0;
    }
    vga_puts("[IRQ] HW IRQ framework init done \n");
}

void irq_register_handler(uint8_t irq, irq_handler_t handler)
{
    if (irq >= IRQ_COUNT) return;
    irq_handlers[irq] = handler;
    pic_unmask_irq(irq);
}

void irq_unregister_handler(uint8_t irq)
{
    if (irq >= IRQ_COUNT) return;
    irq_handlers[irq] = 0;
    pic_mask_irq(irq);
}

/* V3.89-E33: IRQ0(PIT 调度 tick) 处理延迟统计(rdtsc, CPU 周期)——衡量
 * 中断处理是否拖沓. 长尾 max 远大于 avg 说明某次中断路径有重活(如上下文
 * 切换). 32 位累加防溢出(内核无 libgcc __udivdi3), 溢出后停累加(max/n 继续). */
static uint32_t irq0_lat_acc = 0;
static uint32_t irq0_lat_max = 0;
static uint32_t irq0_lat_n = 0;
/* V3.89-E33: t0 用 static 而非局部变量——避免 irq_handler 栈帧增大.
 * (栈帧 +16B 两次复测均触发 schtest 退出路径的偶发 ISR 6, 疑似把
 * sys_exit 的内核栈嵌套深度推向临界溢出; 用 static 后帧零增长.) */
static uint64_t irq_t0;

static inline uint64_t rdtsc64(void)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void irq_lat_stats(uint32_t *max, uint32_t *avg, uint32_t *n)
{
    if (max) *max = irq0_lat_max;
    if (avg) *avg = irq0_lat_n ? (irq0_lat_acc / irq0_lat_n) : 0;
    if (n)   *n = irq0_lat_n;
}

/* 通用IRQ处理函数（由汇编入口调用）
 * irq_number 是 0-15 的IRQ号，不是中断号 */
void irq_handler(uint32_t irq_number)
{
    irq_t0 = rdtsc64();
    if (irq_number < IRQ_COUNT) smp_atomic_inc(&irq_count[irq_number]);  /* V3.87-E1/E3: 原子计数 */
    if (irq_number >= IRQ_COUNT) {
        vga_printf("[IRQ] unknown IRQ: %d\n", irq_number);
        pic_send_eoi(irq_number);
        return;
    }

    /* 调用注册的处理函数 */
    if (irq_handlers[irq_number]) {
        irq_handlers[irq_number]();
    }

    /* 发送EOI */
    pic_send_eoi(irq_number);

    /* V3.89-E33: 只统计 IRQ0(PIT 调度 tick) 的处理耗时 */
    if (irq_number == 0) {
        uint32_t dt = (uint32_t)(rdtsc64() - irq_t0);
        if (dt > irq0_lat_max) irq0_lat_max = dt;
        if (irq0_lat_acc < 0xFFFFFF00u) irq0_lat_acc += dt;  /* 防溢出 */
        irq0_lat_n++;

        /* V3.92-E38: 底半部演示触发——IRQ0 每 tick 必达(独立 noinline,
         * 仅 raise 入队, 处理由主循环 softirq_run 完成; 绝不在中断
         * 上下文 run: 键盘底半部含 pm_wakeup, E-19). */
        extern void softirq_demo_tick(void);
        softirq_demo_tick();

        /* V3.94-E40: 死锁检测扫描——IRQ0 每 tick 必达(noinline, 仅 +4B),
         * 持锁超时打印告警(中断里 vga_printf 已有先例: unknown IRQ 路径). */
        extern void lock_monitor_scan(void);
        lock_monitor_scan();
    }
}
