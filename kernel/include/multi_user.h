#ifndef MULTI_USER_H
#define MULTI_USER_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 多用户/隐私空间
 * 双空间 / 访客模式 / 应用双开 / 工作资料
 * ========================================================================== */

#define MU_MAX_USERS 16
#define MU_MAX_APPS 256
#define MU_NAME_LEN 128

/* 用户类型 */
typedef enum {
    MU_USER_OWNER = 0,     /* 机主 */
    MU_USER_SECONDARY,     /* 次要用户 */
    MU_USER_GUEST,         /* 访客 */
    MU_USER_WORK,          /* 工作资料 */
    MU_USER_PRIVATE,       /* 隐私空间 */
    MU_USER_KIDS,          /* 儿童空间 */
} mu_user_type_t;

/* 用户状态 */
typedef enum {
    MU_USER_STATE_DISABLED = 0,
    MU_USER_STATE_ACTIVE,
    MU_USER_STATE_RUNNING,
    MU_USER_STATE_LOCKED,
} mu_user_state_t;

/* 用户信息 */
typedef struct {
    int used;
    int id;
    char name[MU_NAME_LEN];
    mu_user_type_t type;
    mu_user_state_t state;
    char avatar_path[256];
    uint64_t create_time;
    uint64_t last_login;
    /* 权限 */
    int can_install_apps;
    int can_uninstall_apps;
    int can_make_calls;
    int can_send_sms;
    int can_use_camera;
    int can_use_location;
    int can_access_storage;
    int can_change_settings;
    /* 限制 */
    int app_limit;        /* 应用数量限制 */
    int time_limit_min;   /* 使用时间限制 */
    int used_time_min;
    /* 存储 */
    uint64_t storage_used;
    uint64_t storage_limit;
} mu_user_t;

/* 应用双开实例 */
typedef struct {
    int used;
    int id;
    char app_name[MU_NAME_LEN];
    char package[MU_NAME_LEN];
    int user_id;        /* 所属用户 */
    int clone_id;       /* 克隆ID (0=主,1=双开) */
    int running;
    uint64_t data_size;
    int data_isolated;  /* 数据隔离 */
} mu_app_instance_t;

/* 隐私空间配置 */
typedef struct {
    int enabled;
    int hidden;         /* 隐藏入口 */
    int password_protected;
    int fingerprint_unlock;
    int separate_notifications;
    int app_clone_default;
    char entry_code[16];  /* 入口密码 */
} mu_private_config_t;

/* 工作资料配置 */
typedef struct {
    int enabled;
    int it_managed;
    int separate_apps;
    int separate_contacts;
    int vpn_required;
    int copy_paste_restricted;
    int screenshot_disabled;
} mu_work_config_t;

/* 访客模式配置 */
typedef struct {
    int enabled;
    int allow_calls;
    int allow_sms;
    int allow_camera;
    int allow_install;
    int wipe_on_exit;
} mu_guest_config_t;

/* 儿童模式配置 */
typedef struct {
    int enabled;
    int allowed_apps[64];
    int allowed_app_count;
    int time_limit_min;
    int content_rating;  /* 0=all,1=3+,2=7+,3=12+,4=16+,5=18+ */
    int in_app_purchases;
} mu_kids_config_t;

/* 多用户管理器状态 */
typedef struct {
    int initialized;
    int enabled;
    /* 用户 */
    mu_user_t users[MU_MAX_USERS];
    int user_count;
    int current_user;
    /* 应用实例 */
    mu_app_instance_t apps[MU_MAX_APPS];
    int app_count;
    /* 配置 */
    mu_private_config_t private_config;
    mu_work_config_t work_config;
    mu_guest_config_t guest_config;
    mu_kids_config_t kids_config;
    /* 统计 */
    uint64_t total_switches;
    uint64_t total_clones;
} multi_user_t;

/* 初始化 */
void multi_user_init(void);

/* 用户管理 */
int mu_user_create(const char *name, mu_user_type_t type);
int mu_user_remove(int user_id);
int mu_user_switch(int user_id);
int mu_user_get(int user_id, mu_user_t *user);
int mu_user_list(mu_user_t *users, int max);
int mu_user_get_current(mu_user_t *user);
int mu_user_set_name(int user_id, const char *name);
int mu_user_set_permission(int user_id, const char *perm, int value);
int mu_user_lock(int user_id);
int mu_user_unlock(int user_id);

/* 应用双开 */
int mu_app_clone(const char *package, int user_id);
int mu_app_unclone(int clone_id);
int mu_app_get_instance(int clone_id, mu_app_instance_t *inst);
int mu_app_list_instances(mu_app_instance_t *insts, int max);
int mu_app_launch(int clone_id);
int mu_app_close(int clone_id);

/* 隐私空间 */
int mu_private_enable(void);
int mu_private_disable(void);
int mu_private_get_config(mu_private_config_t *config);
int mu_private_set_config(const mu_private_config_t *config);
int mu_private_unlock(const char *password);
int mu_private_lock(void);
int mu_private_is_unlocked(int *unlocked);

/* 工作资料 */
int mu_work_enable(void);
int mu_work_disable(void);
int mu_work_get_config(mu_work_config_t *config);
int mu_work_set_config(const mu_work_config_t *config);

/* 访客模式 */
int mu_guest_enter(void);
int mu_guest_exit(void);
int mu_guest_get_config(mu_guest_config_t *config);
int mu_guest_set_config(const mu_guest_config_t *config);

/* 儿童模式 */
int mu_kids_enter(void);
int mu_kids_exit(void);
int mu_kids_get_config(mu_kids_config_t *config);
int mu_kids_set_config(const mu_kids_config_t *config);
int mu_kids_add_allowed_app(int app_id);
int mu_kids_remove_allowed_app(int app_id);

/* 使用时间 */
int mu_user_get_used_time(int user_id, int *minutes);
int mu_user_reset_time(int user_id);
int mu_user_set_time_limit(int user_id, int minutes);

/* 统计 */
int mu_get_stats(multi_user_t *stats);

#endif /* MULTI_USER_H */
