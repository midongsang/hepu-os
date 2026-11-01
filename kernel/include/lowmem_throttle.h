#ifndef LOWMEM_THROTTLE_H
#define LOWMEM_THROTTLE_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 旧设备低内存节流模式
 * 限制窗口数量、降低渲染开销、降级AI能力、自动清理后台
 * ========================================================================== */

#define LT_MAX_WHITELIST 32
#define LT_NAME_LEN 128

/* 节流等级 */
typedef enum {
    LT_LEVEL_OFF = 0,       /* 关闭 */
    LT_LEVEL_LIGHT,         /* 轻度（4GB以上） */
    LT_LEVEL_MODERATE,      /* 中度（2-4GB） */
    LT_LEVEL_AGGRESSIVE,    /* 重度（1-2GB） */
    LT_LEVEL_EXTREME,       /* 极限（1GB以下） */
} lt_level_t;

/* 节流策略 */
typedef struct {
    int max_windows;           /* 最大窗口数 */
    int max_background_apps;   /* 最大后台应用数 */
    int animation_quality;     /* 动画质量 0=关,1=低,2=中,3=高 */
    int render_resolution;     /* 渲染分辨率百分比 50-100 */
    int ai_enabled;            /* AI功能是否启用 */
    int ai_level;              /* AI等级 0=关,1=基础,2=完整 */
    int background_freeze;     /* 后台自动冻结 */
    int cache_limit_mb;        /* 缓存上限MB */
    int compositor_effects;    /* 合成器特效 0=关,1=基本,2=完整 */
    int wallpaper_quality;     /* 壁纸质量 0=纯色,1=低,2=中,3=高 */
    int auto_clean_interval_s; /* 自动清理间隔 */
    int oom_killer_threshold;  /* OOM阈值百分比 */
} lt_strategy_t;

/* 设备内存等级 */
typedef enum {
    LT_MEM_UNKNOWN = 0,
    LT_MEM_512MB,
    LT_MEM_1GB,
    LT_MEM_2GB,
    LT_MEM_3GB,
    LT_MEM_4GB,
    LT_MEM_6GB,
    LT_MEM_8GB,
    LT_MEM_12GB,
    LT_MEM_16GB_PLUS,
} lt_mem_level_t;

/* 节流事件 */
typedef enum {
    LT_EVENT_NONE = 0,
    LT_EVENT_ENTER_THROTTLE,
    LT_EVENT_EXIT_THROTTLE,
    LT_EVENT_LEVEL_CHANGE,
    LT_EVENT_AUTO_CLEAN,
    LT_EVENT_OOM_TRIGGERED,
    LT_EVENT_APP_KILLED,
} lt_event_t;

/* 节流记录 */
typedef struct {
    int used;
    int id;
    lt_event_t event;
    uint64_t timestamp;
    char detail[256];
    int memory_before_kb;
    int memory_after_kb;
} lt_record_t;

/* 白名单应用（不被节流/杀死） */
typedef struct {
    int used;
    char name[LT_NAME_LEN];
    int reason;  /* 0=用户,1=系统,2=前台 */
} lt_whitelist_t;

/* 节流配置 */
typedef struct {
    int enabled;
    int auto_detect;           /* 自动检测内存并调整等级 */
    lt_level_t forced_level;   /* 强制等级（auto_detect=0时生效） */
    int show_notification;     /* 节流时显示通知 */
    int allow_override;        /* 允许用户覆盖 */
    int aggressive_clean;      /* 激进清理 */
} lt_config_t;

/* 节流管理器状态 */
typedef struct {
    int initialized;
    int enabled;
    lt_level_t current_level;
    lt_mem_level_t mem_level;
    uint64_t total_memory_kb;
    uint64_t used_memory_kb;
    uint64_t free_memory_kb;
    lt_config_t config;
    lt_strategy_t strategy;
    /* 白名单 */
    lt_whitelist_t whitelist[LT_MAX_WHITELIST];
    int whitelist_count;
    /* 记录 */
    lt_record_t records[64];
    int record_count;
    /* 统计 */
    uint64_t total_throttle_time_s;
    uint64_t total_cleanups;
    uint64_t total_apps_killed;
    uint64_t memory_saved_kb;
    uint64_t oom_events;
} lowmem_throttle_t;

/* 初始化 */
void lowmem_throttle_init(void);

/* 内存检测与等级调整 */
int lowmem_throttle_detect_memory(void);
int lowmem_throttle_set_level(lt_level_t level);
int lowmem_throttle_get_level(lt_level_t *level);
int lowmem_throttle_auto_adjust(void);

/* 节流操作 */
int lowmem_throttle_apply_strategy(const lt_strategy_t *strategy);
int lowmem_throttle_get_strategy(lt_strategy_t *strategy);
int lowmem_throttle_auto_clean(void);
int lowmem_throttle_kill_background(int count);

/* 内存监控 */
int lowmem_throttle_update_memory(uint64_t total_kb, uint64_t used_kb, uint64_t free_kb);
int lowmem_throttle_get_memory(uint64_t *total_kb, uint64_t *used_kb, uint64_t *free_kb);
int lowmem_throttle_get_usage_percent(int *percent);
int lowmem_throttle_check_oom(int *triggered);

/* 白名单 */
int lowmem_throttle_whitelist_add(const char *name, int reason);
int lowmem_throttle_whitelist_remove(const char *name);
int lowmem_throttle_whitelist_check(const char *name, int *whitelisted);
int lowmem_throttle_whitelist_list(lt_whitelist_t *list, int max);

/* 配置 */
int lowmem_throttle_set_config(const lt_config_t *config);
int lowmem_throttle_get_config(lt_config_t *config);
int lowmem_throttle_set_enabled(int enable);

/* 记录与统计 */
int lowmem_throttle_get_records(lt_record_t *records, int max);
int lowmem_throttle_get_stats(lowmem_throttle_t *stats);
int lowmem_throttle_get_default_strategy(lt_level_t level, lt_strategy_t *strategy);

#endif /* LOWMEM_THROTTLE_H */
