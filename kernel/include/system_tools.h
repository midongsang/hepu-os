#ifndef SYSTEM_TOOLS_H
#define SYSTEM_TOOLS_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 系统工具集
 * 计算器 / 日历 / 时钟 / 备忘录
 * ========================================================================== */

/* ===== 计算器 ===== */
#define CALC_MAX_HISTORY 64
#define CALC_MAX_EXPR 256

typedef enum {
    CALC_MODE_BASIC = 0,
    CALC_MODE_SCIENTIFIC,
    CALC_MODE_PROGRAMMER,
} calc_mode_t;

typedef struct {
    char expression[CALC_MAX_EXPR];
    char result[64];
    uint64_t timestamp;
} calc_history_t;

typedef struct {
    int initialized;
    calc_mode_t mode;
    char current_expr[CALC_MAX_EXPR];
    int expr_len;
    double memory;
    int memory_set;
    calc_history_t history[CALC_MAX_HISTORY];
    int history_count;
    int angle_mode;  /* 0=度,1=弧度 */
    int base;  /* 2=二进制,8=八进制,10=十进制,16=十六进制 */
} calculator_t;

void calculator_init(void);
int calculator_input(char key);
int calculator_evaluate(void);
int calculator_clear(void);
int calculator_backspace(void);
int calculator_set_mode(calc_mode_t mode);
int calculator_memory_store(double value);
int calculator_memory_recall(double *value);
int calculator_memory_clear(void);
int calculator_get_history(calc_history_t *history, int max);
int calculator_get_expression(char *buf, int size);
int calculator_get_result(char *buf, int size);

/* ===== 日历 ===== */
#define CAL_MAX_EVENTS 256
#define CAL_TITLE_LEN 128
#define CAL_LOCATION_LEN 128

typedef struct {
    int used;
    int id;
    char title[CAL_TITLE_LEN];
    char location[CAL_LOCATION_LEN];
    char description[256];
    int year, month, day;
    int start_hour, start_minute;
    int end_hour, end_minute;
    int all_day;
    int repeat;  /* 0=无,1=每天,2=每周,3=每月,4=每年 */
    int reminder;  /* 分钟前提醒 */
    int calendar_id;
    int color;
} cal_event_t;

typedef struct {
    int initialized;
    cal_event_t events[CAL_MAX_EVENTS];
    int event_count;
    int current_year, current_month, current_day;
    int first_day_of_week;  /* 0=周日,1=周一 */
    int calendars[8];  /* 启用的日历 */
    int calendar_count;
} calendar_t;

void calendar_init(void);
int calendar_add_event(const char *title, int year, int month, int day, int start_h, int start_m, int end_h, int end_m);
int calendar_remove_event(int id);
int calendar_get_event(int id, cal_event_t *event);
int calendar_list_events(int year, int month, int day, cal_event_t *events, int max);
int calendar_list_month_events(int year, int month, cal_event_t *events, int max);
int calendar_navigate(int year, int month);
int calendar_get_current(int *year, int *month, int *day);
int calendar_days_in_month(int year, int month);
int calendar_day_of_week(int year, int month, int day);

/* ===== 时钟 ===== */
#define CLOCK_MAX_ALARMS 32
#define CLOCK_MAX_TIMERS 16
#define CLOCK_MAX_STOPWATCH_LAPS 64
#define CLOCK_MAX_CITIES 32

typedef struct {
    int used;
    int id;
    int hour, minute;
    int days[7];  /* 0=周日...6=周六, -1=一次性 */
    int enabled;
    char label[64];
    char sound[64];
    int vibrate;
    int snooze;  /* 分钟 */
} sysclock_alarm_t;

typedef struct {
    int used;
    int id;
    uint64_t duration;  /* 毫秒 */
    uint64_t remaining;
    int running;
    char label[64];
} timer_t;

typedef struct {
    int running;
    uint64_t elapsed;
    uint64_t start_time;
    uint64_t laps[CLOCK_MAX_STOPWATCH_LAPS];
    int lap_count;
} stopwatch_t;

typedef struct {
    char name[64];
    char timezone[32];
    int offset;  /* 小时偏移 */
} world_clock_t;

typedef struct {
    int initialized;
    sysclock_alarm_t alarms[CLOCK_MAX_ALARMS];
    int alarm_count;
    timer_t timers[CLOCK_MAX_TIMERS];
    int timer_count;
    stopwatch_t stopwatch;
    world_clock_t cities[CLOCK_MAX_CITIES];
    int city_count;
    int current_hour, current_minute, current_second;
} clock_app_t;

void clock_init(void);
int clock_alarm_add(int hour, int minute, const char *label);
int clock_alarm_remove(int id);
int clock_sysclock_alarm_toggle(int id);
int clock_alarm_get(int id, sysclock_alarm_t *alarm);
int clock_alarm_list(sysclock_alarm_t *alarms, int max);
int clock_timer_start(uint64_t duration, const char *label);
int clock_timer_pause(int id);
int clock_timer_resume(int id);
int clock_timer_cancel(int id);
int clock_timer_get(int id, timer_t *timer);
int clock_stopwatch_start(void);
int clock_stopwatch_stop(void);
int clock_stopwatch_lap(void);
int clock_stopwatch_reset(void);
int clock_stopwatch_get(stopwatch_t *sw);
int clock_world_clock_add(const char *name, const char *timezone, int offset);
int clock_world_clock_list(world_clock_t *cities, int max);
int clock_get_time(int *hour, int *minute, int *second);

/* ===== 备忘录 ===== */
#define NOTE_MAX_NOTES 256
#define NOTE_TITLE_LEN 128
#define NOTE_CONTENT_LEN 4096
#define NOTE_MAX_TAGS 16

typedef struct {
    int used;
    int id;
    char title[NOTE_TITLE_LEN];
    char content[NOTE_CONTENT_LEN];
    char tags[NOTE_MAX_TAGS][32];
    int tag_count;
    int color;
    int pinned;
    int archived;
    int locked;
    uint64_t created;
    uint64_t modified;
    int checklist;  /* 是否为清单 */
    int checklist_items[32];
    int checklist_count;
} note_t;

typedef struct {
    int initialized;
    note_t notes[NOTE_MAX_NOTES];
    int note_count;
    int current_note;
    int sort_mode;  /* 0=修改日期,1=创建日期,2=标题,3=颜色 */
    int view_mode;  /* 0=网格,1=列表 */
} notes_app_t;

void notes_init(void);
int note_create(const char *title, const char *content);
int note_delete(int id);
int note_update(int id, const char *title, const char *content);
int note_get(int id, note_t *note);
int note_list(note_t *notes, int max);
int note_search(const char *query, note_t *results, int max);

/* 任务管理器 */
void task_manager_init(void);
void task_manager_run(void);
void task_manager_selftest(void);
int note_toggle_pin(int id);
int note_toggle_archive(int id);
int note_toggle_lock(int id);
int note_set_color(int id, int color);
int note_add_tag(int id, const char *tag);
int note_remove_tag(int id, const char *tag);
int note_list_by_tag(const char *tag, note_t *notes, int max);
int notes_get_stats(notes_app_t *stats);

#endif /* SYSTEM_TOOLS_H */
void system_tools_init(void);
