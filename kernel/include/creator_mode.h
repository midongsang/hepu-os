#ifndef CREATOR_MODE_H
#define CREATOR_MODE_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 创变自由模式
 * 无需刷机/ROOT，一键开启系统级修改权限，自带快照回滚，不会变砖
 * ========================================================================== */

#define CM_MAX_SNAPSHOTS 8
#define CM_MAX_PROTECTED 64
#define CM_MAX_AUDIT 256
#define CM_NAME_LEN 128
#define CM_PATH_LEN 256

/* 系统模式 */
typedef enum {
    CM_MODE_NORMAL = 0,    /* 普通安全模式（默认） */
    CM_MODE_CREATOR,       /* 创变自由模式 */
    CM_MODE_LOCKDOWN,      /* 锁定模式（企业/家长） */
} cm_mode_t;

/* 快照状态 */
typedef enum {
    CM_SNAPSHOT_EMPTY = 0,
    CM_SNAPSHOT_CREATING,
    CM_SNAPSHOT_READY,
    CM_SNAPSHOT_RESTORING,
    CM_SNAPSHOT_FAILED,
} cm_snapshot_state_t;

/* 系统快照 */
typedef struct {
    int used;
    int id;
    char name[CM_NAME_LEN];
    uint64_t create_time;
    cm_snapshot_state_t state;
    uint64_t size;
    char description[256];
    int is_auto;           /* 自动创建（开启创变模式前） */
    int verified;
} cm_snapshot_t;

/* 受保护项（创变模式下也不可修改） */
typedef struct {
    int used;
    char path[CM_PATH_LEN];
    char reason[128];
    int level;  /* 0=核心不可变,1=需确认,2=可修改 */
} cm_protected_t;

/* 审计日志 */
typedef struct {
    int used;
    int id;
    uint64_t timestamp;
    cm_mode_t mode_before;
    cm_mode_t mode_after;
    char action[CM_NAME_LEN];
    char target[CM_PATH_LEN];
    int result;  /* 0=成功,1=拒绝,2=需确认 */
    char user[64];
} cm_audit_t;

/* 可修改的系统参数 */
typedef enum {
    CM_PARAM_ANIMATION_SPEED = 0,
    CM_PARAM_STATUSBAR_THEME,
    CM_PARAM_NAVBAR_STYLE,
    CM_PARAM_ICON_PACK,
    CM_PARAM_FONT,
    CM_PARAM_BOOT_ANIMATION,
    CM_PARAM_WALLPAPER,
    CM_PARAM_LOCKSCREEN,
    CM_PARAM_SYSTEM_FONT_SIZE,
    CM_PARAM_DPI,
    CM_PARAM_CPU_GOVERNOR,
    CM_PARAM_IO_SCHEDULER,
    CM_PARAM_VM_SWAPPINESS,
    CM_PARAM_MAX,
} cm_param_t;

/* 创变模式配置 */
typedef struct {
    int enabled;
    int require_auth;          /* 开启需要身份验证 */
    int auto_snapshot;         /* 开启前自动创建快照 */
    int allow_system_modify;   /* 允许修改系统文件 */
    int allow_kernel_param;    /* 允许修改内核参数 */
    int allow_replace_app;     /* 允许替换系统应用 */
    int allow_custom_rom;      /* 允许刷入自定义组件 */
    int audit_enabled;         /* 审计日志 */
    int auto_rollback_on_fail; /* 修改失败自动回滚 */
} cm_config_t;

/* 创变管理器状态 */
typedef struct {
    int initialized;
    cm_mode_t current_mode;
    cm_config_t config;
    /* 快照 */
    cm_snapshot_t snapshots[CM_MAX_SNAPSHOTS];
    int snapshot_count;
    /* 受保护项 */
    cm_protected_t protected[CM_MAX_PROTECTED];
    int protected_count;
    /* 审计 */
    cm_audit_t audit[CM_MAX_AUDIT];
    int audit_count;
    /* 统计 */
    uint64_t total_mode_switches;
    uint64_t total_snapshots;
    uint64_t total_rollbacks;
    uint64_t total_modifications;
    uint64_t total_blocked;
} creator_mode_t;

/* 初始化 */
void creator_mode_init(void);

/* 模式切换 */
int creator_mode_enter(void);
int creator_mode_exit(void);
int creator_mode_switch(cm_mode_t mode);
int creator_mode_get(cm_mode_t *mode);
int creator_mode_is_creator(int *is_creator);

/* 快照管理 */
int creator_mode_snapshot_create(const char *name, const char *desc);
int creator_mode_snapshot_restore(int snapshot_id);
int creator_mode_snapshot_delete(int snapshot_id);
int creator_mode_snapshot_list(cm_snapshot_t *snaps, int max);
int creator_mode_snapshot_get(int id, cm_snapshot_t *snap);
int creator_mode_snapshot_verify(int id);

/* 系统修改 */
int creator_mode_modify_param(cm_param_t param, const char *value);
int creator_mode_replace_system_app(const char *package, const char *new_path);
int creator_mode_modify_system_file(const char *path, const char *content);
int creator_mode_check_protected(const char *path, int *level);

/* 受保护项 */
int creator_mode_add_protected(const char *path, const char *reason, int level);
int creator_mode_remove_protected(const char *path);
int creator_mode_list_protected(cm_protected_t *items, int max);

/* 审计 */
int creator_mode_audit_log(const char *action, const char *target, int result);
int creator_mode_audit_list(cm_audit_t *logs, int max);

/* 配置 */
int creator_mode_set_config(const cm_config_t *config);
int creator_mode_get_config(cm_config_t *config);

/* 统计 */
int creator_mode_get_stats(creator_mode_t *stats);

#endif /* CREATOR_MODE_H */
