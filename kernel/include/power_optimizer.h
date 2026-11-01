#ifndef POWER_OPTIMIZER_H
#define POWER_OPTIMIZER_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 功耗/续航优化
 * 应用级功耗统计 / 待机优化 / 温控调频 / 电池管理
 * ========================================================================== */

#define PO_MAX_APPS 128
#define PO_MAX_WAKELOCKS 64
#define PO_NAME_LEN 128

/* 电源状态 */
typedef enum {
    PO_STATE_ACTIVE = 0,     /* 活跃 */
    PO_STATE_IDLE,           /* 空闲 */
    PO_STATE_SUSPEND,        /* 挂起 */
    PO_STATE_DEEP_SLEEP,     /* 深度睡眠 */
    PO_STATE_POWER_OFF,      /* 关机 */
} po_power_state_t;

/* CPU调频策略 */
typedef enum {
    PO_CPU_PERFORMANCE = 0,  /* 性能优先 */
    PO_CPU_BALANCED,         /* 平衡 */
    PO_CPU_POWERSAVE,        /* 省电 */
    PO_CPU_AUTO,             /* 自动 */
} po_cpu_governor_t;

/* 唤醒锁类型 */
typedef enum {
    PO_WAKE_CPU = 0,         /* CPU保持唤醒 */
    PO_WAKE_SCREEN,          /* 屏幕保持亮 */
    PO_WAKE_KEYPAD,          /* 键盘灯 */
    PO_WAKE_NETWORK,         /* 网络保持 */
    PO_WAKE_GPS,             /* GPS保持 */
} po_wakelock_type_t;

/* 应用功耗统计 */
typedef struct {
    int used;
    int id;
    char app_name[PO_NAME_LEN];
    char package[PO_NAME_LEN];
    /* 功耗 */
    uint64_t cpu_time_ms;
    uint64_t wake_time_ms;
    uint64_t network_rx_bytes;
    uint64_t network_tx_bytes;
    uint64_t gps_time_ms;
    uint64_t camera_time_ms;
    uint64_t audio_time_ms;
    uint64_t sensor_time_ms;
    /* 估算功耗 */
    float estimated_energy_mah;
    float percent_of_total;
    /* 唤醒锁 */
    int wakelock_count;
    uint64_t wakelock_time_ms;
    /* 后台活动 */
    int background_starts;
    uint64_t background_time_ms;
    /* 排名 */
    int rank;
} po_app_power_t;

/* 唤醒锁 */
typedef struct {
    int used;
    int id;
    char name[PO_NAME_LEN];
    po_wakelock_type_t type;
    int app_id;
    int held;
    uint64_t acquire_time;
    uint64_t total_held_time;
    int acquire_count;
} po_wakelock_t;

/* 电池信息 */
typedef struct {
    int present;
    int level;           /* 0-100 */
    int health;          /* 0-100 */
    int status;          /* 0=未知,1=充电,2=放电,3=未充电,4=充满 */
    int plugged;         /* 0=电池,1=AC,2=USB,3=无线 */
    int technology;      /* 0=未知,1=Li-ion,2=Li-poly,3=NiMH */
    int temperature;     /* 0.1摄氏度 */
    int voltage;         /* mV */
    int current_ma;      /* mA */
    uint64_t capacity_mah;
    uint64_t full_capacity_mah;
    uint64_t cycle_count;
    int estimated_time_min;  /* 剩余时间 */
} po_battery_t;

/* 温控 */
typedef struct {
    int cpu_temp;        /* 0.1C */
    int gpu_temp;
    int battery_temp;
    int skin_temp;       /* 机身温度 */
    int thermal_level;   /* 0=正常,1=警告,2=降频,3=紧急 */
    int throttling;      /* 0-100降频比例 */
} po_thermal_t;

/* 功耗配置 */
typedef struct {
    int enabled;
    po_cpu_governor_t governor;
    int power_save_mode;      /* 省电模式 */
    int ultra_power_save;     /* 超级省电 */
    int adaptive_brightness;  /* 自适应亮度 */
    int auto_screen_off;      /* 自动息屏秒数 */
    int background_limit;     /* 后台限制 */
    int wake_lock_control;    /* 唤醒锁管控 */
    int app_standby;          /* 应用待机分组 */
    int battery_saver_trigger; /* 省电触发百分比 */
} po_config_t;

/* 功耗优化器状态 */
typedef struct {
    int initialized;
    int enabled;
    po_config_t config;
    po_power_state_t state;
    /* 电池 */
    po_battery_t battery;
    /* 温控 */
    po_thermal_t thermal;
    /* 应用 */
    po_app_power_t apps[PO_MAX_APPS];
    int app_count;
    /* 唤醒锁 */
    po_wakelock_t wakelocks[PO_MAX_WAKELOCKS];
    int wakelock_count;
    /* 统计 */
    uint64_t total_screen_on_time;
    uint64_t total_standby_time;
    uint64_t total_charge_cycles;
    uint64_t last_charge_time;
    float total_energy_consumed_mah;
} power_optimizer_t;

/* 初始化 */
void power_optimizer_init(void);

/* 电池 */
int po_battery_get(po_battery_t *battery);
int po_battery_update(int level, int status, int plugged, int temp, int voltage);
int po_battery_get_time_remaining(int *minutes);

/* 电源状态 */
int po_set_state(po_power_state_t state);
int po_get_state(po_power_state_t *state);
int po_suspend(void);
int po_resume(void);
int po_deep_sleep(void);

/* CPU调频 */
int po_set_governor(po_cpu_governor_t governor);
int po_get_governor(po_cpu_governor_t *governor);
int po_set_cpu_freq(int cpu, uint32_t freq_khz);
int po_get_cpu_freq(int cpu, uint32_t *freq_khz);

/* 应用功耗 */
int po_app_register(const char *name, const char *package);
int po_app_unregister(int app_id);
int po_app_update_cpu(int app_id, uint64_t time_ms);
int po_app_update_network(int app_id, uint64_t rx, uint64_t tx);
int po_app_update_sensor(int app_id, uint64_t time_ms);
int po_app_get(int app_id, po_app_power_t *app);
int po_app_list(po_app_power_t *apps, int max);
int po_app_get_top(po_app_power_t *apps, int max);
int po_app_calculate_energy(int app_id, float *energy_mah);

/* 唤醒锁 */
int po_wakelock_acquire(const char *name, po_wakelock_type_t type, int app_id);
int po_wakelock_release(int wakelock_id);
int po_wakelock_get(int id, po_wakelock_t *wl);
int po_wakelock_list(po_wakelock_t *wls, int max);
int po_wakelock_force_release(int app_id);

/* 温控 */
int po_thermal_update(int cpu, int gpu, int battery, int skin);
int po_thermal_get(po_thermal_t *thermal);
int po_thermal_throttle(int level);

/* 配置 */
int po_set_config(const po_config_t *config);
int po_get_config(po_config_t *config);
int po_set_power_save(int enable);
int po_set_ultra_power_save(int enable);

/* 统计 */
int po_get_stats(power_optimizer_t *stats);
int po_get_remaining_battery_life(int *minutes);

#endif /* POWER_OPTIMIZER_H */
