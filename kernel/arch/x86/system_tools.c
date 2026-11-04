/* ============================================================================
 * 赫普OS - 系统工具集 (计算器/日历/时钟/备忘录)
 * ========================================================================== */

#include <stdint.h>
#include "system_tools.h"
#include "vga.h"
#include "string.h"

/* ===== 计算器 ===== */
static calculator_t calc;

void calculator_init(void)
{
    memset(&calc, 0, sizeof(calculator_t));
    calc.initialized = 1;
    calc.mode = CALC_MODE_BASIC;
    calc.angle_mode = 0;
    calc.base = 10;
    strncpy(calc.current_expr, "0", 255);
}

int calculator_input(char key)
{
    if (calc.expr_len < CALC_MAX_EXPR - 1) {
        calc.current_expr[calc.expr_len++] = key;
        calc.current_expr[calc.expr_len] = 0;
    }
    return 0;
}

int calculator_evaluate(void)
{
    /* 简化: 直接返回表达式作为结果 */
    strncpy(calc.history[calc.history_count % CALC_MAX_HISTORY].expression, calc.current_expr, 255);
    strncpy(calc.history[calc.history_count % CALC_MAX_HISTORY].result, "0", 63);
    calc.history[calc.history_count % CALC_MAX_HISTORY].timestamp = 1;
    calc.history_count++;
    return 0;
}

int calculator_clear(void)
{
    calc.current_expr[0] = '0';
    calc.current_expr[1] = 0;
    calc.expr_len = 1;
    return 0;
}

int calculator_backspace(void)
{
    if (calc.expr_len > 0) {
        calc.expr_len--;
        calc.current_expr[calc.expr_len] = 0;
    }
    return 0;
}

int calculator_set_mode(calc_mode_t mode)
{
    calc.mode = mode;
    return 0;
}

int calculator_memory_store(double value)
{
    calc.memory = value;
    calc.memory_set = 1;
    return 0;
}

int calculator_memory_recall(double *value)
{
    if (!calc.memory_set || !value) return -1;
    *value = calc.memory;
    return 0;
}

int calculator_memory_clear(void)
{
    calc.memory = 0;
    calc.memory_set = 0;
    return 0;
}

int calculator_get_history(calc_history_t *history, int max)
{
    if (!history || max <= 0) return 0;
    int count = calc.history_count < max ? calc.history_count : max;
    for (int i = 0; i < count; i++)
        memcpy(&history[i], &calc.history[i], sizeof(calc_history_t));
    return count;
}

int calculator_get_expression(char *buf, int size)
{
    if (!buf || size <= 0) return -1;
    strncpy(buf, calc.current_expr, size - 1);
    return 0;
}

int calculator_get_result(char *buf, int size)
{
    if (!buf || size <= 0) return -1;
    strncpy(buf, "0", size - 1);
    return 0;
}

/* ===== 日历 ===== */
static calendar_t cal;

static int is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

void calendar_init(void)
{
    memset(&cal, 0, sizeof(calendar_t));
    cal.initialized = 1;
    cal.current_year = 2026;
    cal.current_month = 8;
    cal.current_day = 22;
    cal.first_day_of_week = 1;  /* 周一 */
    cal.calendar_count = 1;

    /* 测试事件 */
    calendar_add_event(" 团队会议 ", 2026, 8, 25, 10, 0, 11, 30);
    calendar_add_event(" 产品 publish ", 2026, 9, 1, 9, 0, 18, 0);
}

int calendar_add_event(const char *title, int year, int month, int day, int start_h, int start_m, int end_h, int end_m)
{
    for (int i = 0; i < CAL_MAX_EVENTS; i++) {
        if (!cal.events[i].used) {
            memset(&cal.events[i], 0, sizeof(cal_event_t));
            cal.events[i].used = 1;
            cal.events[i].id = i;
            if (title) strncpy(cal.events[i].title, title, 127);
            cal.events[i].year = year;
            cal.events[i].month = month;
            cal.events[i].day = day;
            cal.events[i].start_hour = start_h;
            cal.events[i].start_minute = start_m;
            cal.events[i].end_hour = end_h;
            cal.events[i].end_minute = end_m;
            cal.events[i].reminder = 15;
            cal.events[i].color = i % 8;
            cal.event_count++;
            return i;
        }
    }
    return -1;
}

int calendar_remove_event(int id)
{
    if (id < 0 || id >= CAL_MAX_EVENTS || !cal.events[id].used) return -1;
    cal.events[id].used = 0;
    cal.event_count--;
    return 0;
}

int calendar_get_event(int id, cal_event_t *event)
{
    if (id < 0 || id >= CAL_MAX_EVENTS || !cal.events[id].used || !event) return -1;
    memcpy(event, &cal.events[id], sizeof(cal_event_t));
    return 0;
}

int calendar_list_events(int year, int month, int day, cal_event_t *events, int max)
{
    if (!events || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < CAL_MAX_EVENTS && count < max; i++) {
        if (cal.events[i].used && cal.events[i].year == year &&
            cal.events[i].month == month && cal.events[i].day == day)
            memcpy(&events[count++], &cal.events[i], sizeof(cal_event_t));
    }
    return count;
}

int calendar_list_month_events(int year, int month, cal_event_t *events, int max)
{
    if (!events || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < CAL_MAX_EVENTS && count < max; i++) {
        if (cal.events[i].used && cal.events[i].year == year && cal.events[i].month == month)
            memcpy(&events[count++], &cal.events[i], sizeof(cal_event_t));
    }
    return count;
}

int calendar_navigate(int year, int month)
{
    cal.current_year = year;
    cal.current_month = month;
    return 0;
}

int calendar_get_current(int *year, int *month, int *day)
{
    if (year) *year = cal.current_year;
    if (month) *month = cal.current_month;
    if (day) *day = cal.current_day;
    return 0;
}

int calendar_days_in_month(int year, int month)
{
    int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month == 2 && is_leap_year(year)) return 29;
    return days[month - 1];
}

int calendar_day_of_week(int year, int month, int day)
{
    /* 蔡勒公式简化 */
    if (month < 3) { month += 12; year--; }
    int k = year % 100;
    int j = year / 100;
    int h = (day + 13 * (month + 1) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    return (h + 6) % 7;  /* 0=周日 */
}

/* ===== 时钟 ===== */
static clock_app_t clk;

void clock_init(void)
{
    memset(&clk, 0, sizeof(clock_app_t));
    clk.initialized = 1;
    clk.current_hour = 10;
    clk.current_minute = 30;
    clk.current_second = 0;

    /* 测试闹钟 */
    clock_alarm_add(7, 0, " 起床 ");
    clock_alarm_add(8, 30, " 班 ");

    /* 世界时钟 */
    clock_world_clock_add(" 北京 ", "CST", 8);
    clock_world_clock_add(" 东京 ", "JST", 9);
    clock_world_clock_add(" 伦敦 ", "GMT", 0);
    clock_world_clock_add(" 纽约 ", "EST", -5);
}

int clock_alarm_add(int hour, int minute, const char *label)
{
    for (int i = 0; i < CLOCK_MAX_ALARMS; i++) {
        if (!clk.alarms[i].used) {
            memset(&clk.alarms[i], 0, sizeof(sysclock_alarm_t));
            clk.alarms[i].used = 1;
            clk.alarms[i].id = i;
            clk.alarms[i].hour = hour;
            clk.alarms[i].minute = minute;
            if (label) strncpy(clk.alarms[i].label, label, 63);
            clk.alarms[i].enabled = 1;
            clk.alarms[i].vibrate = 1;
            clk.alarms[i].snooze = 5;
            clk.alarm_count++;
            return i;
        }
    }
    return -1;
}

int clock_alarm_remove(int id)
{
    if (id < 0 || id >= CLOCK_MAX_ALARMS || !clk.alarms[id].used) return -1;
    clk.alarms[id].used = 0;
    clk.alarm_count--;
    return 0;
}

int clock_sysclock_alarm_toggle(int id)
{
    if (id < 0 || id >= CLOCK_MAX_ALARMS || !clk.alarms[id].used) return -1;
    clk.alarms[id].enabled = !clk.alarms[id].enabled;
    return clk.alarms[id].enabled;
}

int clock_alarm_get(int id, sysclock_alarm_t *alarm)
{
    if (id < 0 || id >= CLOCK_MAX_ALARMS || !clk.alarms[id].used || !alarm) return -1;
    memcpy(alarm, &clk.alarms[id], sizeof(sysclock_alarm_t));
    return 0;
}

int clock_alarm_list(sysclock_alarm_t *alarms, int max)
{
    if (!alarms || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < CLOCK_MAX_ALARMS && count < max; i++) {
        if (clk.alarms[i].used) memcpy(&alarms[count++], &clk.alarms[i], sizeof(sysclock_alarm_t));
    }
    return count;
}

int clock_timer_start(uint64_t duration, const char *label)
{
    for (int i = 0; i < CLOCK_MAX_TIMERS; i++) {
        if (!clk.timers[i].used) {
            memset(&clk.timers[i], 0, sizeof(timer_t));
            clk.timers[i].used = 1;
            clk.timers[i].id = i;
            clk.timers[i].duration = duration;
            clk.timers[i].remaining = duration;
            clk.timers[i].running = 1;
            if (label) strncpy(clk.timers[i].label, label, 63);
            clk.timer_count++;
            return i;
        }
    }
    return -1;
}

int clock_timer_pause(int id)
{
    if (id < 0 || id >= CLOCK_MAX_TIMERS || !clk.timers[id].used) return -1;
    clk.timers[id].running = 0;
    return 0;
}

int clock_timer_resume(int id)
{
    if (id < 0 || id >= CLOCK_MAX_TIMERS || !clk.timers[id].used) return -1;
    clk.timers[id].running = 1;
    return 0;
}

int clock_timer_cancel(int id)
{
    if (id < 0 || id >= CLOCK_MAX_TIMERS || !clk.timers[id].used) return -1;
    clk.timers[id].used = 0;
    clk.timer_count--;
    return 0;
}

int clock_timer_get(int id, timer_t *timer)
{
    if (id < 0 || id >= CLOCK_MAX_TIMERS || !clk.timers[id].used || !timer) return -1;
    memcpy(timer, &clk.timers[id], sizeof(timer_t));
    return 0;
}

int clock_stopwatch_start(void)
{
    clk.stopwatch.running = 1;
    clk.stopwatch.start_time = 1;
    return 0;
}

int clock_stopwatch_stop(void)
{
    clk.stopwatch.running = 0;
    return 0;
}

int clock_stopwatch_lap(void)
{
    if (!clk.stopwatch.running || clk.stopwatch.lap_count >= CLOCK_MAX_STOPWATCH_LAPS) return -1;
    clk.stopwatch.laps[clk.stopwatch.lap_count++] = clk.stopwatch.elapsed;
    return clk.stopwatch.lap_count;
}

int clock_stopwatch_reset(void)
{
    clk.stopwatch.running = 0;
    clk.stopwatch.elapsed = 0;
    clk.stopwatch.lap_count = 0;
    return 0;
}

int clock_stopwatch_get(stopwatch_t *sw)
{
    if (!sw) return -1;
    memcpy(sw, &clk.stopwatch, sizeof(stopwatch_t));
    return 0;
}

int clock_world_clock_add(const char *name, const char *timezone, int offset)
{
    if (clk.city_count >= CLOCK_MAX_CITIES) return -1;
    strncpy(clk.cities[clk.city_count].name, name, 63);
    strncpy(clk.cities[clk.city_count].timezone, timezone, 31);
    clk.cities[clk.city_count].offset = offset;
    clk.city_count++;
    return clk.city_count - 1;
}

int clock_world_clock_list(world_clock_t *cities, int max)
{
    if (!cities || max <= 0) return 0;
    int count = clk.city_count < max ? clk.city_count : max;
    for (int i = 0; i < count; i++)
        memcpy(&cities[i], &clk.cities[i], sizeof(world_clock_t));
    return count;
}

int clock_get_time(int *hour, int *minute, int *second)
{
    if (hour) *hour = clk.current_hour;
    if (minute) *minute = clk.current_minute;
    if (second) *second = clk.current_second;
    return 0;
}

/* ===== 备忘录 ===== */
void system_tools_init(void)
{
    calculator_init();
    calendar_init();
    clock_init();
    notes_init();
    task_manager_init();
    vga_puts("[TOOLS] system tools init done \n");
    vga_printf(" calculator / calendar / clock / notes / taskmgr ready \n");
}

