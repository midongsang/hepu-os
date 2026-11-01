#ifndef PERMISSION_MANAGER_H
#define PERMISSION_MANAGER_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 应用权限精细化管理
 * 一次性权限 / 模糊定位 / 权限使用记录 / 空信息授权 / 权限组
 * ========================================================================== */

#define PM_MAX_APPS 256
#define PM_MAX_PERMISSIONS 64
#define PM_MAX_LOGS 1024
#define PM_NAME_LEN 128

/* 权限ID */
typedef enum {
    PM_PERM_CAMERA = 0,
    PM_PERM_MICROPHONE,
    PM_PERM_LOCATION,
    PM_PERM_STORAGE,
    PM_PERM_CONTACTS,
    PM_PERM_SMS,
    PM_PERM_PHONE,
    PM_PERM_CALL_LOG,
    PM_PERM_CALENDAR,
    PM_PERM_SENSORS,
    PM_PERM_BLUETOOTH,
    PM_PERM_NEARBY_WIFI,
    PM_PERM_NOTIFICATION,
    PM_PERM_CLIPBOARD,
    PM_PERM_BODY_SENSORS,
    PM_PERM_ACTIVITY_RECOGNITION,
    PM_PERM_INSTALL_PACKAGES,
    PM_PERM_SYSTEM_ALERT,
    PM_PERM_ACCESSIBILITY,
    PM_PERM_DEVICE_ADMIN,
} prm_permission_t;

/* 授权状态 */
typedef enum {
    PM_GRANT_DENIED = 0,
    PM_GRANT_ALLOWED,
    PM_GRANT_ONETIME,     /* 一次性 */
    PM_GRANT_WHILE_USING, /* 使用时 */
    PM_GRANT_FUZZY,       /* 模糊授权（如模糊定位） */
    PM_GRANT_EMPTY,       /* 空信息授权（返回空数据） */
} prm_grant_t;

/* 权限使用记录 */
typedef struct {
    int used;
    int id;
    int app_id;
    prm_permission_t permission;
    uint64_t timestamp;
    int duration_ms;
    int access_count;
    char detail[256];
} prm_log_entry_t;

/* 应用权限状态 */
typedef struct {
    int used;
    int app_id;
    char app_name[PM_NAME_LEN];
    char package[PM_NAME_LEN];
    /* 权限状态 */
    prm_grant_t grants[PM_MAX_PERMISSIONS];
    /* 一次性权限剩余时间 */
    uint64_t onetime_expiry[PM_MAX_PERMISSIONS];
    /* 模糊定位精度 */
    float fuzzy_location_radius;  /* 米 */
    /* 权限使用统计 */
    uint64_t usage_count[PM_MAX_PERMISSIONS];
    uint64_t total_usage_time[PM_MAX_PERMISSIONS];
    /* 上次使用 */
    uint64_t last_used[PM_MAX_PERMISSIONS];
    /* 危险标志 */
    int suspicious;  /* 可疑行为 */
    int restricted;  /* 被系统限制 */
} prm_app_perms_t;

/* 权限组 */
typedef struct {
    prm_permission_t permissions[16];
    int count;
    char name[PM_NAME_LEN];
    char description[256];
    int dangerous;  /* 危险权限组 */
} prm_group_t;

/* 权限管理器配置 */
typedef struct {
    int enabled;
    int onetime_permissions;    /* 一次性权限 */
    int fuzzy_location;         /* 模糊定位 */
    int empty_info_auth;        /* 空信息授权 */
    int permission_logging;     /* 权限使用记录 */
    int auto_revoke_unused;     /* 自动撤销未使用权限 */
    int auto_revoke_days;       /* 自动撤销天数 */
    int clipboard_warning;      /* 剪贴板读取提醒 */
    int camera_mic_indicator;   /* 相机麦克风使用指示器 */
    int approximate_location_default;  /* 默认模糊定位 */
} prm_config_t;

/* 权限管理器状态 */
typedef struct {
    int initialized;
    int enabled;
    prm_config_t config;
    /* 应用 */
    prm_app_perms_t apps[PM_MAX_APPS];
    int app_count;
    /* 日志 */
    prm_log_entry_t logs[PM_MAX_LOGS];
    int log_count;
    int log_pos;
    /* 统计 */
    uint64_t total_permission_checks;
    uint64_t total_permission_denials;
    uint64_t total_onetime_grants;
    uint64_t total_auto_revokes;
    /* 当前使用相机/麦克风的应用 */
    int camera_in_use_by;
    int mic_in_use_by;
    int location_in_use_by;
} permission_manager_t;

/* 初始化 */
void permission_manager_init(void);

/* V2.70: 轻量应用条目 (供 UI 列表, 避免栈上拷贝大结构) */
typedef struct {
    int used;
    int app_id;
    char app_name[PM_NAME_LEN];
    char package[PM_NAME_LEN];
    int grant_count;   /* 已授权权限数 */
} prm_app_brief_t;

/* 应用注册 */
int prm_app_register(const char *name, const char *package);
int prm_app_unregister(int app_id);
int prm_app_get(int app_id, prm_app_perms_t *perms);
int prm_app_list(prm_app_perms_t *apps, int max);
int prm_app_list_brief(prm_app_brief_t *apps, int max);

/* 权限请求/授权 */
int prm_request_permission(int app_id, prm_permission_t perm);
int prm_grant_permission(int app_id, prm_permission_t perm, prm_grant_t grant);
int prm_revoke_permission(int app_id, prm_permission_t perm);
int prm_check_permission(int app_id, prm_permission_t perm, int *granted);
int prm_get_permission_status(int app_id, prm_permission_t perm, prm_grant_t *grant);

/* 一次性权限 */
int prm_grant_onetime(int app_id, prm_permission_t perm, uint64_t duration_ms);
int prm_onetime_expired(int app_id, prm_permission_t perm, int *expired);
int prm_extend_onetime(int app_id, prm_permission_t perm, uint64_t duration_ms);

/* 模糊定位 */
int prm_set_fuzzy_location(int app_id, float radius_m);
int prm_get_fuzzy_location(int app_id, float *radius_m);
int prm_fuzzy_location_coords(float lat, float lon, float *fuzzy_lat, float *fuzzy_lon, float radius);

/* 空信息授权 */
int prm_set_empty_auth(int app_id, prm_permission_t perm, int enable);
int prm_get_empty_data(prm_permission_t perm, void *buf, int size);

/* 权限使用记录 */
int prm_log_access(int app_id, prm_permission_t perm, int duration_ms, const char *detail);
int prm_get_logs(prm_log_entry_t *logs, int max);
int prm_get_app_logs(int app_id, prm_log_entry_t *logs, int max);
int prm_clear_logs(void);

/* V2.71: 权限使用聚合统计 (app x perm -> count), 供隐私看板 */
typedef struct {
    int used;
    int app_id;
    int permission;
    int count;
} prm_app_perm_stat_t;
int prm_log_stats(prm_app_perm_stat_t *stats, int max);

/* 权限组 */
int prm_group_get(prm_permission_t perm, prm_group_t *group);
int prm_group_list(prm_group_t *groups, int max);

/* 自动管理 */
int prm_auto_revoke_check(void);
int prm_auto_revoke_app(int app_id);
int prm_get_suspicious_apps(prm_app_perms_t *apps, int max);

/* 配置 */
int prm_set_config(const prm_config_t *config);
int prm_get_config(prm_config_t *config);

/* 使用指示器 */
int prm_get_camera_in_use(int *app_id);
int prm_get_mic_in_use(int *app_id);
int prm_get_location_in_use(int *app_id);
int prm_release_all_in_use(void);
void prm_seed_data_dirs(void);

/* 统计 */
int prm_get_stats(permission_manager_t *stats);

/* ---- V2.66: 权限名映射/危险标记/自测 ---- */
const char *prm_perm_name(int perm);
int prm_perm_dangerous(int perm);
void prm_selftest(void);

/* ---- V2.69: 权限持久化 ---- */
int prm_save_state(const char *path);
int prm_load_state(const char *path);

#endif /* PERMISSION_MANAGER_H */
