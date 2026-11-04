#ifndef SOFTIRQ_H
#define SOFTIRQ_H

#include <stdint.h>

/* V3.92-E38: 中断底半部(softirq)框架(融合 V3.87-E8/E13 旧接口)
 *
 * 原则:
 * - 顶半部(ISR)只做最小处理(读硬件入环形缓冲) + softirq_raise 入队;
 * - 底半部 fn 在"主循环/进程上下文"由 softirq_run 执行——绝不在中断
 *   上下文执行(键盘底半部含 pm_wakeup, 中断里调用会死锁, E-19);
 * - 队列满丢弃并计数, 中断上下文绝不阻塞.
 *
 * 接口:
 * - softirq_register(type, fn): 注册类型处理器(键盘=0/鼠标=1);
 * - softirq_raise(type): 按类型查表入队(旧接口);
 * - softirq_raise_fn(fn, arg): 通用入队(演示/新驱动用);
 * - softirq_run(): 处理全部 pending(主循环调用). */

#define SOFTIRQ_KBD    0   /* 键盘底半部(扫描码→ASCII 状态机) */
#define SOFTIRQ_MOUSE  1   /* 鼠标底半部(数据包解析/位置计算) */
#define SOFTIRQ_MAX_TYPES 8
#define SOFTIRQ_QUEUE  16

typedef void (*softirq_fn_t)(void);

void softirq_register(int type, softirq_fn_t fn);
void softirq_raise(int type);
void softirq_raise_fn(softirq_fn_t fn, void *arg);
void softirq_run(void);
void softirq_stats(uint32_t *raised, uint32_t *handled, uint32_t *dropped,
                   uint32_t *max_defer, uint32_t *pending);

/* 演示: softirqtest 开启后每 25 ticks 模拟外设事件 raise 一次 */
void softirq_demo_tick(void);   /* sched_tick noinline 钩子(仅 raise) */
void softirq_demo_ctl(int on);
uint32_t softirq_demo_count(void);
uint32_t softirq_demo_seen_count(void);
uint32_t softirq_demo_enabled_get(void);
uint32_t softirq_demo_last_get(void);
uint32_t softirq_tick_now(void);

#endif /* SOFTIRQ_H */
