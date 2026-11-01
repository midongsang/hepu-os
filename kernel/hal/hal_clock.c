/* ============================================================================
 * 赫普OS - HAL时钟与定时器 (V0.97新增)
 * 实现：系统时钟源抽象、定时器管理、延时、时间戳
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "hal_clock.h"
#include "vga.h"
#include "string.h"

#define TIMER_MAX 8
#define TIMER_NAME_MAX 16

/* 定时器回调 */
typedef void (*timer_callback_t)(uint32_t timer_id, void *context);

/* 定时器描述符 */
typedef struct {
    uint8_t           used;
    uint8_t           periodic;    /* 1=周期, 0=一次性 */
    uint8_t           running;
    char              name[TIMER_NAME_MAX];
    uint32_t          interval_ms; /* 间隔（毫秒） */
    uint32_t          remaining;   /* 剩余计数 */
    timer_callback_t  callback;
    void             *context;
    uint32_t          fire_count;
} timer_desc_t;

/* 时钟源 */
typedef struct {
    uint32_t frequency_hz;
    uint32_t tick_count;
    uint64_t uptime_ms;
    uint8_t  initialized;
} clock_source_t;

static clock_source_t sys_clock;
static timer_desc_t timers[TIMER_MAX];
static uint32_t timer_count = 0;

void hal_clock_init(uint32_t frequency_hz)
{
    sys_clock.frequency_hz = frequency_hz ? frequency_hz : 1000;
    sys_clock.tick_count = 0;
    sys_clock.uptime_ms = 0;
    sys_clock.initialized = 1;

    memset(timers, 0, sizeof(timers));
    timer_count = 0;

    vga_printf("[HAL-CLK] clock subsystem init: %d Hz\n", sys_clock.frequency_hz);
}

/* 时钟tick（由硬件定时器中断调用） */
void hal_clock_tick(void)
{
    if (!sys_clock.initialized) return;
    sys_clock.tick_count++;
    /* 假设1ms per tick（1000Hz） */
    uint32_t ms_per_tick = 1000 / sys_clock.frequency_hz;
    if (ms_per_tick == 0) ms_per_tick = 1;
    sys_clock.uptime_ms += ms_per_tick;

    /* 检查定时器 */
    for (int i = 0; i < TIMER_MAX; i++) {
        if (timers[i].used && timers[i].running) {
            if (timers[i].remaining > ms_per_tick) {
                timers[i].remaining -= ms_per_tick;
            } else {
                timers[i].remaining = 0;
                timers[i].fire_count++;
                if (timers[i].callback) {
                    timers[i].callback(i, timers[i].context);
                }
                if (timers[i].periodic) {
                    timers[i].remaining = timers[i].interval_ms;
                } else {
                    timers[i].running = 0;
                }
            }
        }
    }
}

/* 获取系统运行时间（毫秒） */
uint64_t hal_clock_uptime_ms(void)
{
    return sys_clock.uptime_ms;
}

/* 获取tick计数 */
uint32_t hal_clock_ticks(void)
{
    return sys_clock.tick_count;
}

/* 忙等待延时（毫秒） */
void hal_clock_delay_ms(uint32_t ms)
{
    if (!sys_clock.initialized) return;
    uint64_t start = sys_clock.uptime_ms;
    while (sys_clock.uptime_ms - start < ms) {
        __asm__ volatile ("nop");
    }
}

/* 忙等待延时（微秒，简化实现） */
void hal_clock_delay_us(uint32_t us)
{
    /* 简化：用循环延时，实际应基于硬件定时器 */
    for (uint32_t i = 0; i < us * 10; i++) {
        __asm__ volatile ("nop");
    }
}

/* 创建定时器 */
int hal_timer_create(const char *name, uint32_t interval_ms,
                     uint8_t periodic, timer_callback_t callback, void *context)
{
    for (int i = 0; i < TIMER_MAX; i++) {
        if (!timers[i].used) {
            timers[i].used = 1;
            timers[i].periodic = periodic ? 1 : 0;
            timers[i].running = 0;
            if (name) strncpy(timers[i].name, name, TIMER_NAME_MAX - 1);
            timers[i].interval_ms = interval_ms;
            timers[i].remaining = interval_ms;
            timers[i].callback = callback;
            timers[i].context = context;
            timers[i].fire_count = 0;
            timer_count++;
            return i;
        }
    }
    return -1;
}

/* 启动定时器 */
int hal_timer_start(int timer_id)
{
    if (timer_id < 0 || timer_id >= TIMER_MAX || !timers[timer_id].used) return -1;
    timers[timer_id].remaining = timers[timer_id].interval_ms;
    timers[timer_id].running = 1;
    return 0;
}

/* 停止定时器 */
int hal_timer_stop(int timer_id)
{
    if (timer_id < 0 || timer_id >= TIMER_MAX || !timers[timer_id].used) return -1;
    timers[timer_id].running = 0;
    return 0;
}

/* 销毁定时器 */
int hal_timer_destroy(int timer_id)
{
    if (timer_id < 0 || timer_id >= TIMER_MAX || !timers[timer_id].used) return -1;
    timers[timer_id].used = 0;
    timers[timer_id].running = 0;
    timers[timer_id].callback = NULL;
    timer_count--;
    return 0;
}

/* 定时器统计 */
void hal_timer_stats(uint32_t *total, uint32_t *running)
{
    uint32_t run = 0;
    for (int i = 0; i < TIMER_MAX; i++) {
        if (timers[i].used && timers[i].running) run++;
    }
    if (total) *total = timer_count;
    if (running) *running = run;
}
