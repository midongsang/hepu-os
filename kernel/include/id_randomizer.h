#ifndef ID_RANDOMIZER_H
#define ID_RANDOMIZER_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 设备标识随机化
 * 设备ID/MAC/广告ID/Android ID/序列号全局随机化，APP无法追踪用户
 * ========================================================================== */

#define IR_MAX_APP_IDS 256
#define IR_MAX_HISTORY 64
#define IR_ID_LEN 64
#define IR_MAC_LEN 6
#define IR_PACKAGE_LEN 128

/* 标识类型 */
typedef enum {
    IR_ID_DEVICE = 0,     /* 设备唯一标识 */
    IR_ID_ANDROID,        /* Android ID */
    IR_ID_ADVERTISING,    /* 广告ID (AAID) */
    IR_ID_SERIAL,         /* 序列号 */
    IR_ID_IMEI,           /* IMEI (受保护，返回随机值) */
    IR_ID_MEID,           /* MEID (受保护) */
    IR_ID_MAC_WIFI,       /* WiFi MAC */
    IR_ID_MAC_BT,         /* 蓝牙MAC */
    IR_ID_MAC_ETHERNET,   /* 以太网MAC */
    IR_ID_INSTALL,        /* 应用安装ID */
    IR_ID_PUSH,           /* 推送令牌 */
} ir_id_type_t;

/* 随机化策略 */
typedef enum {
    IR_POLICY_GLOBAL = 0,    /* 全局随机，所有APP同一个随机值 */
    IR_POLICY_PER_APP,       /* 每个APP不同的随机值 */
    IR_POLICY_PER_SESSION,   /* 每次启动重新随机 */
    IR_POLICY_REAL,          /* 返回真实值（仅系统应用） */
} ir_policy_t;

/* 应用标识映射 */
typedef struct {
    int used;
    char package[IR_PACKAGE_LEN];
    char device_id[IR_ID_LEN];
    char android_id[IR_ID_LEN];
    char advertising_id[IR_ID_LEN];
    char serial[IR_ID_LEN];
    uint8_t wifi_mac[IR_MAC_LEN];
    uint8_t bt_mac[IR_MAC_LEN];
    uint64_t create_time;
    uint64_t last_access;
    int access_count;
} ir_app_id_t;

/* 标识历史 */
typedef struct {
    int used;
    ir_id_type_t type;
    char old_value[IR_ID_LEN];
    char new_value[IR_ID_LEN];
    uint64_t change_time;
    char reason[64];
} ir_history_t;

/* 随机化配置 */
typedef struct {
    int enabled;
    ir_policy_t default_policy;
    int randomize_device_id;
    int randomize_android_id;
    int randomize_advertising_id;
    int randomize_serial;
    int protect_imei;       /* IMEI返回随机值 */
    int randomize_wifi_mac;
    int randomize_bt_mac;
    int mac_randomization_interval_s;  /* MAC定期重新随机 */
    int ad_id_reset_on_demand;         /* 用户可手动重置广告ID */
    int system_apps_get_real;          /* 系统应用获取真实值 */
} ir_config_t;

/* 随机化管理器状态 */
typedef struct {
    int initialized;
    int enabled;
    ir_config_t config;
    /* 全局随机标识 */
    char global_device_id[IR_ID_LEN];
    char global_android_id[IR_ID_LEN];
    char global_advertising_id[IR_ID_LEN];
    char global_serial[IR_ID_LEN];
    uint8_t global_wifi_mac[IR_MAC_LEN];
    uint8_t global_bt_mac[IR_MAC_LEN];
    /* 应用级标识 */
    ir_app_id_t app_ids[IR_MAX_APP_IDS];
    int app_id_count;
    /* 历史 */
    ir_history_t history[IR_MAX_HISTORY];
    int history_count;
    /* 统计 */
    uint64_t total_id_requests;
    uint64_t total_randomizations;
    uint64_t total_tracking_blocked;
} id_randomizer_t;

/* 初始化 */
void id_randomizer_init(void);

/* 获取标识（核心API） */
int id_randomizer_get(ir_id_type_t type, const char *package, char *buf, int size);
int id_randomizer_get_mac(ir_id_type_t type, const char *package, uint8_t *mac, int len);

/* 重置标识 */
int id_randomizer_reset(ir_id_type_t type);
int id_randomizer_reset_all(void);
int id_randomizer_reset_advertising_id(void);
int id_randomizer_reset_for_app(const char *package);

/* 应用管理 */
int id_randomizer_register_app(const char *package);
int id_randomizer_unregister_app(const char *package);
int id_randomizer_get_app_ids(const char *package, ir_app_id_t *ids);

/* 策略 */
int id_randomizer_set_policy(ir_policy_t policy);
int id_randomizer_get_policy(ir_policy_t *policy);
int id_randomizer_set_config(const ir_config_t *config);
int id_randomizer_get_config(ir_config_t *config);
int id_randomizer_set_enabled(int enable);

/* MAC随机化 */
int id_randomizer_randomize_mac(ir_id_type_t type);
int id_randomizer_get_current_mac(ir_id_type_t type, uint8_t *mac);

/* 历史与统计 */
int id_randomizer_get_history(ir_history_t *history, int max);
int id_randomizer_get_stats(id_randomizer_t *stats);
int id_randomizer_is_tracking_blocked(const char *package, int *blocked);

#endif /* ID_RANDOMIZER_H */
