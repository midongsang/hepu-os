#ifndef PM_H
#define PM_H

#include <stdint.h>

/* 电源状态 */
typedef enum {
    PM_STATE_ON = 0,      /* 正常运行 */
    PM_STATE_IDLE,        /* 空闲 (CPU暂停) */
    PM_STATE_STANDBY,     /* 待机 (内存自刷新) */
    PM_STATE_SUSPEND,     /* 挂起到内存 (S3) */
    PM_STATE_HIBERNATE,   /* 挂起到磁盘 (S4) */
    PM_STATE_OFF,         /* 关机 (S5) */
} pm_state_t;

/* 电源事件 */
typedef enum {
    PM_EVENT_WAKEUP = 0,
    PM_EVENT_SLEEP,
    PM_EVENT_BATTERY_LOW,
    PM_EVENT_BATTERY_CRITICAL,
    PM_EVENT_AC_CONNECT,
    PM_EVENT_AC_DISCONNECT,
} pm_event_t;

/* 电池状态 */
typedef struct {
    int present;
    int charging;
    int capacity;       /* 百分比 0-100 */
    int voltage;        /* mV */
    int current;        /* mA */
    int temperature;    /* 0.1°C */
    int design_capacity; /* mAh */
    int full_capacity;   /* mAh */
    int remaining_capacity; /* mAh */
} pm_battery_t;

/* 电源管理回调 */
typedef void (*pm_callback_t)(pm_event_t event);

/* 初始化电源管理 */
void pm_init(void);

/* 设置电源状态 */
int pm_set_state(pm_state_t state);

/* 获取当前电源状态 */
pm_state_t pm_get_state(void);

/* 注册电源事件回调 */
void pm_register_callback(pm_callback_t cb);

/* 唤醒系统 */
void pm_wakeup(void);

/* 进入休眠 */
void pm_sleep(pm_state_t state);

/* 获取电池状态 */
int pm_get_battery(pm_battery_t *battery);

/* 更新电池状态 (由驱动调用) */
void pm_update_battery(pm_battery_t *battery);

/* CPU空闲 (HLT) */
void pm_cpu_idle(void);

/* 检查是否应该进入省电模式 */
int pm_should_sleep(void);

/* 设置自动休眠超时 (秒) */
void pm_set_sleep_timeout(int seconds);

/* 获取自动休眠超时 */
int pm_get_sleep_timeout(void);

/* V2.61: 屏幕电源状态 (休眠灭屏标志) */
int pm_is_screen_blanked(void);

/* 重置空闲计时器 */
void pm_reset_idle_timer(void);

/* 电源管理周期 tick (V2.54: 由 PIT 每秒调用, 驱动空闲计时/电池放电) */
void pm_tick(void);

/* V2.54: 模拟设置电池电量 (调试/演示) */
void pm_batt_set(int pct);

/* V3.79: 省电模式 (开启→冻结后台+低电量策略) */
void pm_set_low_power(int on);
int pm_is_low_power(void);

/* V4.04-E49: 深度睡眠统计——pm_sleep 进入睡眠等待循环(键盘唤醒),
 * 统计睡眠/唤醒次数与累计睡眠 tick */
typedef struct {
    uint32_t sleep_count;      /* 进入深度睡眠次数 */
    uint32_t wake_count;       /* 唤醒次数 */
    uint32_t total_sleep_ticks;/* 累计睡眠 tick(1/100s) */
    uint32_t last_sleep_ticks; /* 最近一次睡眠时长(tick) */
    int      last_wake_reason; /* 0=键盘/输入唤醒 1=其他 */
} pm_wake_stats_t;
void pm_get_wake_stats(pm_wake_stats_t *st);

/* V4.07-E52: 低电量自动深度睡眠——pm_tick 检测电量<=5% 置 pending,
 * shell 输入等待循环消费后进入 pm_sleep(可 RTC 闹钟/按键唤醒). */
#define PM_LOWBATT_PCT 5
void pm_set_lowbatt_pending(void);
int  pm_consume_lowbatt_pending(void);   /* 返回并消费标志 */

/* V4.49-E94: 省电等级——0=性能 1=平衡 2=省电 (影响自动休眠超时/策略) */
#define PM_LEVEL_PERF     0
#define PM_LEVEL_BALANCED 1
#define PM_LEVEL_SAVER    2
int pm_set_level(int level);
int pm_get_level(void);

/* V4.49-E94: 唤醒原因——键盘输入 / 定时器 / 外部事件 */
#define PM_WAKE_KEYBOARD  1
#define PM_WAKE_TIMER     2
#define PM_WAKE_EXTERNAL  3
void pm_wakeup_reason(int reason);

/* V4.49-E94: 电池续航估算——剩余容量/放电电流 → 剩余分钟数 (0=不可估) */
int pm_battery_minutes(void);

#endif /* PM_H */
