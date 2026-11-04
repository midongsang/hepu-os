#ifndef APP_FREEZE_H
#define APP_FREEZE_H

#include <stdint.h>
#include "process.h"

/* ============================================================================
 * 赫普OS 内核级后台冻结
 * 后台应用完全挂起，零调度、零CPU、零后台刷新
 * ========================================================================== */

#define AF_MAX_WHITELIST 64
#define AF_MAX_FROZEN 64
#define AF_NAME_LEN 64

/* 冻结原因 */
typedef enum {
    AF_REASON_MANUAL = 0,     /* 用户手动冻结 */
    AF_REASON_SCREEN_OFF,     /* 屏幕关闭自动冻结 */
    AF_REASON_BACKGROUND,     /* 进入后台超时自动冻结 */
    AF_REASON_LOW_BATTERY,    /* 低电量强制冻结 */
    AF_REASON_POLICY,         /* 系统策略冻结 */
} af_reason_t;

/* 冻结记录 */
typedef struct {
    int used;
    uint32_t pid;
    char name[AF_NAME_LEN];
    af_reason_t reason;
    uint64_t freeze_time;
    uint64_t total_frozen_ms;
    int freeze_count;
    int is_frozen;
} af_record_t;

/* 白名单项 */
typedef struct {
    int used;
    char name[AF_NAME_LEN];
    int reason;  /* 0=用户,1=系统(音乐/导航/下载),2=厂商 */
} af_whitelist_t;

/* 冻结策略 */
typedef struct {
    int enabled;
    int auto_freeze_on_screen_off;
    int auto_freeze_background;
    uint32_t background_timeout_s;  /* 后台多久后冻结 */
    int low_battery_threshold;      /* 低电量百分比 */
    int low_battery_freeze;         /* 低电量时冻结非白名单 */
    int preserve_music;             /* 保留音乐播放 */
    int preserve_navigation;        /* 保留导航 */
    int preserve_download;          /* 保留下载 */
    int preserve_voip;              /* 保留VoIP通话 */
} af_policy_t;

/* 冻结统计 */
typedef struct {
    uint64_t total_freeze_count;
    uint64_t total_unfreeze_count;
    uint64_t total_frozen_time_s;
    uint64_t cpu_saved_ms;          /* 估算节省的CPU时间 */
    uint64_t battery_saved_pct;     /* 估算节省电量百分比 */
    int currently_frozen;
} af_stats_t;

/* 冻结管理器状态 */
typedef struct {
    int initialized;
    int enabled;
    int screen_on;
    int battery_level;
    af_policy_t policy;
    af_record_t records[AF_MAX_FROZEN];
    int record_count;
    af_whitelist_t whitelist[AF_MAX_WHITELIST];
    int whitelist_count;
    af_stats_t stats;
} app_freeze_t;

/* 初始化 */
void app_freeze_init(void);

/* 冻结/解冻 */
int app_freeze_process(uint32_t pid, af_reason_t reason);
int app_unfreeze_process(uint32_t pid);
int app_freeze_all_background(af_reason_t reason);
int app_unfreeze_all(void);
int app_is_frozen(uint32_t pid, int *frozen);

/* 白名单 */
int app_freeze_whitelist_add(const char *name, int reason);
int app_freeze_whitelist_remove(const char *name);
int app_freeze_whitelist_check(const char *name, int *whitelisted);
int app_freeze_whitelist_list(af_whitelist_t *list, int max);

/* 策略 */
int app_freeze_set_policy(const af_policy_t *policy);
int app_freeze_get_policy(af_policy_t *policy);
int app_freeze_set_enabled(int enable);
int app_freeze_screen_state(int on);
int app_freeze_battery_update(int level);

/* 事件触发 */
int app_freeze_on_screen_off(void);
int app_freeze_on_screen_on(void);
int app_freeze_on_app_background(uint32_t pid);
int app_freeze_on_low_battery(void);

/* 查询 */
int app_freeze_get_frozen_list(af_record_t *records, int max);
int app_freeze_get_stats(af_stats_t *stats);
int app_freeze_get_record(uint32_t pid, af_record_t *record);

#endif /* APP_FREEZE_H */
