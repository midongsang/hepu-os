#ifndef APPECO_H
#define APPECO_H

#include <stdint.h>

#define APPECO_MAX_APPS 256
#define APPECO_MAX_CATEGORIES 16
#define APPECO_MAX_REVIEWS 64
#define APPECO_MAX_PERMISSIONS 32
#define APPECO_MAX_SCREENSHOTS 8
#define APPECO_MAX_SDK_MODULES 32

/* 应用类别 */
typedef enum {
    APP_CAT_SYSTEM = 0,
    APP_CAT_TOOLS,
    APP_CAT_COMMUNICATION,
    APP_CAT_SOCIAL,
    APP_CAT_PRODUCTIVITY,
    APP_CAT_ENTERTAINMENT,
    APP_CAT_GAMES,
    APP_CAT_MUSIC,
    APP_CAT_VIDEO,
    APP_CAT_PHOTOGRAPHY,
    APP_CAT_NEWS,
    APP_CAT_FINANCE,
    APP_CAT_HEALTH,
    APP_CAT_EDUCATION,
    APP_CAT_TRAVEL,
    APP_CAT_SHOPPING,
} app_category_t;

/* 应用状态 */
typedef enum {
    APP_STATE_NOT_INSTALLED = 0,
    APP_STATE_INSTALLED,
    APP_STATE_RUNNING,
    APP_STATE_PAUSED,
    APP_STATE_STOPPED,
    APP_STATE_DISABLED,
    APP_STATE_UPDATING,
    APP_STATE_UNINSTALLING,
} app_state_t;

/* 应用权限 */
typedef struct {
    char name[64];
    char description[128];
    int granted;
    int dangerous;  /* 0=normal,1=dangerous,2=special */
    int group;  /* 权限组 */
} app_permission_t;

/* 应用信息 */
typedef struct {
    int used;
    int id;
    char name[64];
    char package_name[128];  /* 如 com.hepu.calculator */
    char version[32];
    int version_code;
    app_category_t category;
    app_state_t state;
    char developer[64];
    char description[512];
    uint64_t install_size;
    uint64_t data_size;
    uint64_t cache_size;
    int installed;
    uint64_t install_time;
    uint64_t update_time;
    int launch_count;
    uint64_t last_launch;
    int rating;  /* 0-5 */
    int rating_count;
    int downloads;
    int min_sdk;
    int target_sdk;
    /* 权限 */
    app_permission_t permissions[APPECO_MAX_PERMISSIONS];
    int permission_count;
    /* 图标 */
    int icon_id;
    /* 截图 */
    int screenshots[APPECO_MAX_SCREENSHOTS];
    int screenshot_count;
    /* 包路径 */
    char package_path[256];
    char data_dir[256];
    char cache_dir[256];
    /* 签名 */
    char signature[256];
    int signature_verified;
    /* 系统应用 */
    int system_app;
    int removable;
    int default_app;
} app_info_t;

/* 应用商店条目 */
typedef struct {
    int used;
    int id;
    char name[64];
    char package_name[128];
    char version[32];
    int version_code;
    app_category_t category;
    char developer[64];
    char description[512];
    uint64_t size;
    int rating;
    int rating_count;
    int downloads;
    int price;  /* 0=免费, >0=价格(分) */
    int has_ads;
    int has_iap;
    int content_rating;  /* 0=all,1=3+,2=7+,3=12+,4=16+,5=18+ */
    int installed;
    int has_update;
    char latest_version[32];
    int latest_version_code;
    char changelog[512];
} store_app_t;

/* 应用评论 */
typedef struct {
    int used;
    int id;
    int app_id;
    char user[64];
    int rating;
    char title[128];
    char content[512];
    uint64_t date;
    int helpful;
} app_review_t;

/* SDK模块 */
typedef struct {
    int used;
    int id;
    char name[64];
    char description[256];
    int version;
    int api_count;
    int header_files;
    int library_size;
    int installed;
    char dependencies[256];
} sdk_module_t;

/* 应用生态状态 */
typedef struct {
    int initialized;
    /* 已安装应用 */
    app_info_t apps[APPECO_MAX_APPS];
    int app_count;
    int running_apps;
    /* 应用商店 */
    store_app_t store_apps[APPECO_MAX_APPS];
    int store_count;
    /* 评论 */
    app_review_t reviews[APPECO_MAX_REVIEWS];
    int review_count;
    /* SDK */
    sdk_module_t sdk_modules[APPECO_MAX_SDK_MODULES];
    int sdk_count;
    /* 应用商店设置 */
    int auto_update;
    int update_over_wifi_only;
    int parental_control;
    int content_rating_limit;
    /* 统计 */
    uint64_t total_installs;
    uint64_t total_uninstalls;
    uint64_t total_updates;
    uint64_t total_launches;
} appeco_t;

/* 初始化应用生态 */
void appeco_init(void);

/* 应用管理 */
int appeco_install(const char *package_path);
int appeco_uninstall(int app_id);
int appeco_update(int app_id);
int appeco_launch(int app_id);
int appeco_close(int app_id);
int appeco_get_app(int app_id, app_info_t *app);
int appeco_list_apps(app_info_t *apps, int max);
int appeco_list_by_category(app_category_t category, app_info_t *apps, int max);
int appeco_get_by_package(const char *package, app_info_t *app);
int appeco_set_default(int app_id, int is_default);
int appeco_disable(int app_id);
int appeco_enable(int app_id);
int appeco_clear_data(int app_id);
int appeco_clear_cache(int app_id);
int appeco_grant_permission(int app_id, const char *permission);
int appeco_revoke_permission(int app_id, const char *permission);

/* 应用商店 */
int appeco_store_search(const char *query, store_app_t *results, int max);
int appeco_store_list(store_app_t *apps, int max);
int appeco_store_list_by_category(app_category_t category, store_app_t *apps, int max);
int appeco_store_get_app(int store_id, store_app_t *app);
int appeco_store_install(int store_id);
int appeco_store_get_updates(store_app_t *apps, int max);
int appeco_store_update_all(void);

/* 评论 */
int appeco_review_add(int app_id, int rating, const char *title, const char *content);
int appeco_review_list(int app_id, app_review_t *reviews, int max);
int appeco_review_get(int review_id, app_review_t *review);

/* SDK */
int appeco_sdk_install(int module_id);
int appeco_sdk_uninstall(int module_id);
int appeco_sdk_list(sdk_module_t *modules, int max);
int appeco_sdk_get(int module_id, sdk_module_t *module);

/* 应用商店设置 */
int appeco_set_auto_update(int enable);
int appeco_set_update_wifi_only(int enable);
int appeco_set_parental_control(int enable, int content_rating);

/* 统计 */
int appeco_get_stats(int *installed, int *running, int *store_apps, uint64_t *launches);

#endif /* APPECO_H */
