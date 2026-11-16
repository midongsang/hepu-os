/* ============================================================================
 * 赫普OS - 8254 PIT 可编程间隔定时器驱动
 * 功能: 产生100Hz时钟中断，用于系统计时和抢占式调度
 * ========================================================================== */

#include <stdint.h>
#include "pit.h"
#include "pic.h"
#include "irq.h"
#include "vga.h"
#include "sched.h"

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static volatile uint32_t pit_ticks = 0;

/* 初始化PIT为100Hz */
void pit_init(void)
{
    /* 计算分频值: 1193182 / 100 = 11931 */
    uint32_t divisor = PIT_FREQUENCY / PIT_HZ;

    /* 设置通道0: 模式3(方波), 先低后高 */
    outb(PIT_CMD, PIT_SC0 | PIT_RL_BOTH | PIT_MODE3);

    /* 写入分频值 (先低后高) */
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);

    /* 开启IRQ0 (时钟) */
    pic_unmask_irq(IRQ_TIMER);

    /* 注册PIT中断处理程序 (V3.88修复: 此前只开IRQ未注册handler,
     * 导致pit_ticks恒为0, pit_sleep永久等待, 系统卡在Shell自检) */
    extern void irq_register_handler(uint8_t, void (*)(void));
    irq_register_handler(IRQ_TIMER, pit_handler);

    pit_ticks = 0;

    vga_printf("[PIT] PIT init: %d Hz(divisor=%d)\n",
               PIT_HZ, divisor);
}

/* 时钟中断处理 */
void pit_handler(void)
{
    pit_ticks++;
    /* V3.87-E6: 软看门狗检查——中断上下文独立于卡死的主循环 */
    extern int wd_check(void);
    if (wd_check()) {
        extern void wd_trigger(void);
        wd_trigger();
    }

    /* 每100次中断(1秒)可以做一些周期性工作 */
    if (pit_ticks % 100 == 0) {
        /* V2.54: 电源管理每秒 tick (空闲计时/电池放电) */
        extern void pm_tick(void);
        pm_tick();
    }

    /* 触发调度器 */
    sched_tick();
}

uint32_t pit_get_ticks(void)
{
    return pit_ticks;
}

uint32_t pit_get_seconds(void)
{
    return pit_ticks / PIT_HZ;
}

/* 睡 ms 毫秒。
 *
 * 这一版加了兜底, 因为在 QEMU 里实测到一处死锁: 自检阶段中断是关的
 * 串口有 [NET-TEST] diag tick=0 if=0(interrupts OFF), 而这里的 hlt 靠 PIT
 * 中断唤醒 —— IF=0 时 hlt 永远醒不过来, pit_ticks 不动, 于是 while 死循环。
 * 表现出来就是 shell 打完 "[SHELL] main started" 之后再没有任何动作:
 * 后面 HEPU_BOOT_DEBUG 的 pit_sleep(3000) 一直卡着。
 *
 * 真机上 IF 通常是开的, 不会碰到; 但"依赖中断才能推进的睡眠"不该在没有
 * 超时保护的前提下存在 —— 睡过头是小事, 卡死是大事。
 * 兜底用有限次 hlt: 每次 hlt 至少睡到下一个被屏蔽的唤醒机会, 足够保守。 */
void pit_sleep(uint32_t ms)
{
    uint32_t target = pit_ticks + (ms * PIT_HZ / 1000);
    uint32_t guard = (ms * PIT_HZ / 1000) * 64u + 100000u;
    while (pit_ticks < target && guard--) {
        extern void wd_feed(void);
        wd_feed();  /* V3.87-E6: 睡眠等待=系统活跃, 喂狗防误触发 */
        /* IF=1 才 hlt: 否则 hlt 会让 CPU 永久挂起(要等 NMI/SMI 才醒),
         * 下面的 guard 连一次都减不下去 —— 上一版就是这么假兜底的。
         * 中断关着的时候只能忙等, 虽然费电, 但至少会往前走。 */
        /* 完全不 hlt。原因: QEMU 里实测 IF=1 但 PIT 中断没投递(自检阶段
         * PIC 状态被前面的测试改过), hlt 一样会永久挂起。任何"靠中断才醒"
         * 的等待在这里都是赌, 而赌输的代价是整机卡死。
         * 纯 pause 忙等虽然费电, 但保证往前走 —— 这是自检/睡眠分隔路径上的
         * 正确取舍: 宁可多花 CPU, 不可卡住。 */
        __asm__ volatile ("pause");
    }
}
