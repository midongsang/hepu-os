/* ============================================================================
 * 赫普OS - 电源管理
 * 休眠/唤醒/省电模式/电池管理
 * ========================================================================== */

#include <stdint.h>
#include "pm.h"
#include "vga.h"
#include "pit.h"
#include "serial.h"
#include "string.h"
#include "app_freeze.h"   /* V3.79: AF_REASON_POLICY 冻结原因 */

static pm_state_t current_state = PM_STATE_ON;
/* V2.61: 屏幕电源状态 (休眠时灭屏, 唤醒后恢复) */
static int screen_blanked = 0;
/* V3.79: 省电模式标志 (开启→冻结后台+低电量策略联动) */
static int low_power_mode = 0;

int pm_is_screen_blanked(void)
{
    return screen_blanked;
}

static pm_battery_t battery;
static pm_callback_t callbacks[8];
extern uint32_t sched_get_ticks(void);   /* V4.04-E49: 睡眠时长统计(与 softirq 同源) */
static int callback_count = 0;
static int sleep_timeout = 300;  /* 5分钟 */
static volatile uint32_t idle_seconds = 0;
static int pm_initialized = 0;

/* V4.49-E94: 省电等级 (0=性能 1=平衡 2=省电) */
static int power_level = PM_LEVEL_BALANCED;

/* V4.04-E49: 深度睡眠统计 */
static uint32_t pm_sleep_count = 0;
static uint32_t pm_wake_count = 0;
static uint32_t pm_total_sleep_ticks = 0;
static uint32_t pm_last_sleep_ticks = 0;
static uint32_t pm_sleep_start_tick = 0;
static int pm_last_wake_reason = 0;
static volatile int pm_lowbatt_pending = 0;   /* V4.07-E52 */
static volatile uint32_t pm_lowbatt_suppress_until = 0; /* 唤醒后抑制窗口(tick) */
#define PM_LOWBATT_SUPPRESS_TICKS 3000  /* 唤醒后 30s 内不重复自动睡 */

void pm_init(void)
{
    if (pm_initialized) return;   /* V2.54: 幂等——避免重复调用重置状态 */
    current_state = PM_STATE_ON;
    callback_count = 0;
    idle_seconds = 0;

    /* 初始化电池状态 (模拟) */
    battery.present = 1;
    battery.charging = 0;
    battery.capacity = 85;
    battery.voltage = 3800;
    battery.current = -500;
    battery.temperature = 250;
    battery.design_capacity = 3000;
    battery.full_capacity = 2850;
    battery.remaining_capacity = 2420;

    pm_initialized = 1;
    vga_puts("[PM] power mgmt init done \n");
    vga_printf(" battery: %d%% %s\n", battery.capacity,
               battery.charging ? " 充电 in " : " 放电 in ");
}

int pm_set_state(pm_state_t state)
{
    if (state == current_state) return 0;

    pm_state_t old_state = current_state;
    current_state = state;

    /* V2.61: 维护屏幕电源状态 (休眠->灭屏, 唤醒->恢复)
     * V3.79: 联动后台应用冻结 (灭屏冻结后台, 亮屏恢复) */
    if (state == PM_STATE_SUSPEND || state == PM_STATE_HIBERNATE) {
        screen_blanked = 1;
        extern int app_freeze_screen_state(int);
        app_freeze_screen_state(0);
    } else if (state == PM_STATE_ON) {
        screen_blanked = 0;
        extern int app_freeze_screen_state(int);
        app_freeze_screen_state(1);
    }

    /* 通知回调 */
    pm_event_t event = (state < old_state) ? PM_EVENT_SLEEP : PM_EVENT_WAKEUP;
    for (int i = 0; i < callback_count; i++) {
        if (callbacks[i]) callbacks[i](event);
    }

    const char *state_name = "Unknown";
    switch (state) {
        case PM_STATE_ON: state_name = "ON"; break;
        case PM_STATE_IDLE: state_name = "IDLE"; break;
        case PM_STATE_STANDBY: state_name = "STANDBY"; break;
        case PM_STATE_SUSPEND: state_name = "SUSPEND(S3)"; break;
        case PM_STATE_HIBERNATE: state_name = "HIBERNATE(S4)"; break;
        case PM_STATE_OFF: state_name = "OFF(S5)"; break;
    }
    vga_printf("[PM] power state: %s -> %s\n",
               old_state == PM_STATE_ON ? "ON" : "...", state_name);
    /* V2.60: 串口镜像(诊断/自动化验证用) */
    serial_puts(0x3F8, "[PM] state -> ");
    switch (state) {
        case PM_STATE_ON: serial_puts(0x3F8, "ON\n"); break;
        case PM_STATE_IDLE: serial_puts(0x3F8, "IDLE\n"); break;
        case PM_STATE_STANDBY: serial_puts(0x3F8, "STANDBY\n"); break;
        case PM_STATE_SUSPEND: serial_puts(0x3F8, "SUSPEND\n"); break;
        case PM_STATE_HIBERNATE: serial_puts(0x3F8, "HIBERNATE\n"); break;
        default: serial_puts(0x3F8, "OFF\n"); break;
    }

    /* 原型阶段: 实际硬件需要ACPI/APM调用 */
    if (state == PM_STATE_SUSPEND || state == PM_STATE_HIBERNATE) {
        vga_puts("[PM] enter low power ...\n");
        /* 实际: 保存CPU状态, 关闭外设, 进入休眠 */
    }

    return 0;
}

pm_state_t pm_get_state(void)
{
    return current_state;
}

void pm_register_callback(pm_callback_t cb)
{
    if (cb && callback_count < 8) {
        callbacks[callback_count++] = cb;
    }
}

void pm_wakeup(void)
{
    if (current_state != PM_STATE_ON) {
        /* V4.04-E49: 唤醒冷却——进入深度睡眠后 10 ticks(0.1s)内的
         * 扫描码视为"睡眠前残留"(8042 延迟注入的释放码等), 不唤醒.
         * 冷却期后真实按键才触发唤醒+时长统计. */
        if (pm_sleep_start_tick != 0) {
            uint32_t _now = sched_get_ticks();
            if ((_now - pm_sleep_start_tick) < 10) return;
        }
        serial_puts(0x3F8, "[PM] wakeup system\n");
        vga_puts("[PM] wakeup system ...\n");
        pm_set_state(PM_STATE_ON);
        idle_seconds = 0;
        /* V4.07-E52: 唤醒后抑制低电量自动睡 (防止睡-醒死循环) */
        pm_lowbatt_suppress_until = sched_get_ticks() + PM_LOWBATT_SUPPRESS_TICKS;
        /* V4.04-E49: 唤醒统计——键盘底半部/输入路径调用 */
        pm_wake_count++;
        /* V4.49-E94: 唤醒原因由 pm_wakeup_reason 设置, 默认键盘(1) */
        if (pm_last_wake_reason == 0) pm_last_wake_reason = PM_WAKE_KEYBOARD;
        if (pm_sleep_start_tick != 0) {
            uint32_t now = sched_get_ticks();
            uint32_t dur = (now >= pm_sleep_start_tick)
                           ? (now - pm_sleep_start_tick) : 0;
            pm_last_sleep_ticks = dur;
            pm_total_sleep_ticks += dur;
            pm_sleep_start_tick = 0;
        }
    }
}

/* V4.49-E94: 带原因的唤醒 (键盘底半部/定时器/外部事件) */
void pm_wakeup_reason(int reason)
{
    pm_last_wake_reason = reason;
    pm_wakeup();
}

/* V4.49-E94: 省电等级——0=性能 1=平衡 2=省电 (调整自动休眠超时) */
int pm_set_level(int level)
{
    if (level < PM_LEVEL_PERF || level > PM_LEVEL_SAVER) return -1;
    power_level = level;
    switch (level) {
        case PM_LEVEL_PERF:     sleep_timeout = 600; break;  /* 10 分钟 */
        case PM_LEVEL_BALANCED: sleep_timeout = 300; break;  /* 5 分钟 */
        case PM_LEVEL_SAVER:    sleep_timeout = 60;  break;  /* 1 分钟 */
    }
    vga_printf("[PM] power level: %d (%s), auto-sleep %d s\n", level,
               level == 0 ? "performance" :
               level == 1 ? "balanced" : "power-saver",
               sleep_timeout);
    return 0;
}

int pm_get_level(void)
{
    return power_level;
}

/* V4.49-E94: 电池续航估算——剩余容量 / 放电电流 → 剩余分钟 */
int pm_battery_minutes(void)
{
    if (battery.present && battery.current < 0 && battery.remaining_capacity > 0) {
        return (battery.remaining_capacity * 60) / (-battery.current);
    }
    return 0;
}

void pm_sleep(pm_state_t state)
{
    if (state <= PM_STATE_ON || state > PM_STATE_OFF) return;
    /* V4.04-E49: 深度睡眠——置状态(SUSPEND: 灭屏+冻结后台)后进入
     * 睡眠等待循环: 执行底半部(键盘扫描→pm_wakeup 唤醒) + hlt 停机.
     * PIT 每 tick 唤醒复查, 直到输入唤醒(状态回 ON). 唤醒后统计. */
    pm_sleep_count++;
    pm_sleep_start_tick = sched_get_ticks();
    pm_set_state(state);
    extern void keyboard_flush_raw(void);
    keyboard_flush_raw();      /* V4.04-E49: 清残留扫描码, 防立即唤醒 */
    extern void softirq_run(void);
    uint32_t guard = 0;
    while (pm_get_state() != PM_STATE_ON) {
        softirq_run();              /* 键盘底半部→pm_wakeup→状态回 ON */
        __asm__ volatile ("hlt");   /* 停机省电, PIT 中断唤醒复查 */
        if (++guard > 200000000) break;  /* 超时保护(约数小时) */
    }
}

int pm_get_battery(pm_battery_t *out)
{
    if (!out) return -1;
    memcpy(out, &battery, sizeof(pm_battery_t));
    return 0;
}

void pm_update_battery(pm_battery_t *new_battery)
{
    if (!new_battery) return;
    memcpy(&battery, new_battery, sizeof(pm_battery_t));

    /* 检查低电量 */
    if (battery.capacity <= 5 && !battery.charging) {
        vga_puts("[PM] warn: battery critical, about to sleep !\n");
        for (int i = 0; i < callback_count; i++) {
            if (callbacks[i]) callbacks[i](PM_EVENT_BATTERY_CRITICAL);
        }
        pm_set_state(PM_STATE_SUSPEND);
    }
    else if (battery.capacity <= 20 && !battery.charging) {
        for (int i = 0; i < callback_count; i++) {
            if (callbacks[i]) callbacks[i](PM_EVENT_BATTERY_LOW);
        }
    }
}

void pm_cpu_idle(void)
{
    /* CPU进入空闲状态, 等待中断 */
    __asm__ volatile ("hlt");
}

/* V2.54: 模拟设置电池电量百分比 (调试/演示用) */
void pm_batt_set(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    battery.capacity = pct;
    battery.remaining_capacity = (battery.full_capacity * pct) / 100;
    battery.charging = (pct == 100) ? 1 : 0;
    vga_printf("[PM] battery set to %d%% (charging=%d)\n",
               battery.capacity, battery.charging);
    /* V3.79: 低电量联动后台冻结 (app_freeze 低电量策略) */
    extern int app_freeze_battery_update(int);
    app_freeze_battery_update(battery.capacity);
    /* V4.07-E52: 低电量自动休眠统一由 pm_tick 自动路径接管——
     * 此处不再直接置 SUSPEND (旧逻辑绕过唤醒抑制窗口, 造成
     * 睡-醒-睡死循环); 仅提示, pm_tick 置 pending 后由输入等待
     * 循环消费进入 pm_sleep(深度睡眠), 唤醒后 30s 抑制窗口内不重复. */
    if (battery.capacity <= 5 && !battery.charging) {
        vga_puts("[PM] warn: battery critical, auto suspend in idle\n");
    }
}

/* V3.79: 省电模式——开启时冻结全部后台应用 + 低电量策略联动
 * (app_freeze API 经 app_freeze.h 声明, 勿重复 extern 以免类型冲突) */
void pm_set_low_power(int on)
{
    low_power_mode = on ? 1 : 0;
    if (low_power_mode) {
        app_freeze_set_enabled(1);
        app_freeze_all_background(AF_REASON_POLICY);
        app_freeze_battery_update(battery.capacity);
    } else {
        app_freeze_set_enabled(0);   /* 关闭时解冻全部 */
    }
    serial_puts(0x3F8, low_power_mode ? "[PM] low power ON\n" : "[PM] low power OFF\n");
    vga_printf("[PM] low power mode %s\n", on ? "ON" : "OFF");
}

int pm_is_low_power(void)
{
    return low_power_mode;
}

int pm_should_sleep(void)
{
    if (current_state != PM_STATE_ON) return 0;
    if (sleep_timeout <= 0) return 0;
    return (idle_seconds >= (uint32_t)sleep_timeout);
}

void pm_set_sleep_timeout(int seconds)
{
    sleep_timeout = seconds;
}

int pm_get_sleep_timeout(void)
{
    return sleep_timeout;
}

void pm_reset_idle_timer(void)
{
    idle_seconds = 0;
}

/* 电源管理计时器 (由PIT中断调用) */
void pm_tick(void)
{
    if (current_state == PM_STATE_ON) {
        idle_seconds++;
        if (pm_should_sleep()) {
            serial_puts(0x3F8, "[PM] idle timeout, enter sleep\n");
            vga_puts("[PM] idle timeout, enter sleep \n");
            pm_set_state(PM_STATE_SUSPEND);
        }
    }

    /* 模拟电池放电 */
    if (!battery.charging && battery.remaining_capacity > 0) {
        battery.remaining_capacity--;
        battery.capacity = (battery.remaining_capacity * 100) / battery.full_capacity;
    }

    /* V4.07-E52: 低电量自动深度睡眠——电量<=5% 且系统空闲(ON)时置
     * pending, 由输入等待循环消费进入 pm_sleep (仅置标志, 不在中断
     * 上下文睡眠——中断里阻塞是死锁源). */
    if (current_state == PM_STATE_ON &&
        battery.capacity <= PM_LOWBATT_PCT && !battery.charging) {
        /* V4.07-E52: 唤醒后抑制窗口——刚被唤醒(30s内)不重复自动睡,
         * 给用户操作/充电时间, 防"睡-醒-睡"死循环 */
        uint32_t _now = sched_get_ticks();
        if (_now >= pm_lowbatt_suppress_until) {
            if (!pm_lowbatt_pending) {
                serial_puts(0x3F8, "[PM] low battery, auto suspend pending\n");
            }
            pm_lowbatt_pending = 1;
        }
    }
}

/* V4.07-E52: 低电量自动睡眠标志 */
void pm_set_lowbatt_pending(void)
{
    pm_lowbatt_pending = 1;
}

int pm_consume_lowbatt_pending(void)
{
    if (pm_lowbatt_pending) {
        pm_lowbatt_pending = 0;
        return 1;
    }
    return 0;
}

/* V4.04-E49: 深度睡眠统计查询 */
void pm_get_wake_stats(pm_wake_stats_t *st)
{
    if (!st) return;
    st->sleep_count = pm_sleep_count;
    st->wake_count = pm_wake_count;
    st->total_sleep_ticks = pm_total_sleep_ticks;
    st->last_sleep_ticks = pm_last_sleep_ticks;
    st->last_wake_reason = pm_last_wake_reason;
}
