#ifndef HAL_CLOCK_H
#define HAL_CLOCK_H

#include <stdint.h>

/* 定时器回调 */
typedef void (*timer_callback_t)(uint32_t timer_id, void *context);

/* 初始化时钟子系统 */
void hal_clock_init(uint32_t frequency_hz);

/* 时钟tick（硬件定时器中断调用） */
void hal_clock_tick(void);

/* 时间获取 */
uint64_t hal_clock_uptime_ms(void);
uint32_t hal_clock_ticks(void);

/* 延时 */
void hal_clock_delay_ms(uint32_t ms);
void hal_clock_delay_us(uint32_t us);

/* 定时器管理 */
int  hal_timer_create(const char *name, uint32_t interval_ms,
                      uint8_t periodic, timer_callback_t callback, void *context);
int  hal_timer_start(int timer_id);
int  hal_timer_stop(int timer_id);
int  hal_timer_destroy(int timer_id);

/* 统计 */
void hal_timer_stats(uint32_t *total, uint32_t *running);

#endif
