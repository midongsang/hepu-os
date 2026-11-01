#ifndef APP_STORE_H
#define APP_STORE_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 应用商店
 * 浏览 / 搜索 / 安装 / 更新 / 评论
 * ========================================================================== */

#define STORE_MAX_APPS 512
#define STORE_MAX_CATEGORIES 32
#define STORE_MAX_REVIEWS 1024
#define STORE_MAX_PREINSTALLED 64
#define STORE_MAX_DEVELOPERS 256
#define STORE_NAME_LEN 128
#define STORE_DESC_LEN 512
#define STORE_AUTHOR_LEN 64

/* 应用来源 */
typedef enum {
    APP_SOURCE_PREINSTALLED = 0,  /* 系统预装，不可卸载 */
    APP_SOURCE_THIRD_PARTY,       /* 第三方开发者上传 */
    APP_SOURCE_SYSTEM_UPDATE,     /* 系统更新 */
} app_source_t;

/* 应用类别 */
typedef enum {
    STORE_CAT_TOOLS = 0,
    STORE_CAT_SOCIAL,
    STORE_CAT_PRODUCTIVITY,
    STORE_CAT_GAMES,
    STORE_CAT_ENTERTAINMENT,
    STORE_CAT_NEWS,
    STORE_CAT_FINANCE,
    STORE_CAT_HEALTH,
    STORE_CAT_EDUCATION,
    STORE_CAT_SHOPPING,
    STORE_CAT_TRAVEL,
    STORE_CAT_PHOTO,
    STORE_CAT_MUSIC,
    STORE_CAT_VIDEO,
    STORE_CAT_BOOKS,
    STORE_CAT_BUSINESS,
    STORE_CAT_WEATHER,
    STORE_CAT_UTILITIES,
    STORE_CAT_COMMUNICATION,
    STORE_CAT_OTHER,
} store_category_t;

/* 应用安装状态 */
typedef enum {
    APP_STATUS_NOT_INSTALLED = 0,
    APP_STATUS_INSTALLING,
    APP_STATUS_INSTALLED,
    APP_STATUS_UPDATING,
    APP_STATUS_UNINSTALLING,
    APP_STATUS_ERROR,
} app_status_t;

/* 应用信息 */
typedef struct {
    int used;
    int id;
    char name[STORE_NAME_LEN];
    char description[STORE_DESC_LEN];
    char author[STORE_AUTHOR_LEN];
    char package_name[128];
    char version[32];
    uint64_t size;
    store_category_t category;
    float rating;  /* 0-5 */
    int rating_count;
    int download_count;
    char icon_path[256];
    char screenshot_paths[8][256];
    int screenshot_count;
    int installed;
    app_status_t status;
    int has_update;
    char new_version[32];
    uint64_t updated;
    int free;
    float price;
    int age_rating;  /* 0=全年龄,3,7,12,16,18 */
    int permissions[32];
    int permission_count;
    app_source_t source;  /* 应用来源 */
    int removable;        /* 是否可卸载（预装应用不可卸载） */
} appstore_app_t;

/* 预装应用（系统软件，由赫普OS团队开发） */
typedef struct {
    int used;
    char name[STORE_NAME_LEN];
    char description[STORE_DESC_LEN];
    char package_name[128];
    char version[32];
    store_category_t category;
    char icon_path[256];
    int removable;  /* 0=不可卸载，1=可卸载 */
    int system_app; /* 是否为系统核心应用 */
} preinstalled_app_t;

/* 第三方开发者 */
typedef struct {
    int used;
    int id;
    char name[STORE_AUTHOR_LEN];
    char email[128];
    char website[256];
    int verified;  /* 已认证开发者 */
    int app_count;
    uint64_t total_downloads;
    float rating;
} store_developer_t;

/* 应用审核状态 */
typedef enum {
    APP_REVIEW_PENDING = 0,   /* 待审核 */
    APP_REVIEW_APPROVED,      /* 已通过 */
    APP_REVIEW_REJECTED,      /* 已拒绝 */
    APP_REVIEW_REMOVED,       /* 已下架 */
} app_review_status_t;

/* 评论 */
typedef struct {
    int used;
    int id;
    int app_id;
    char author[STORE_AUTHOR_LEN];
    float rating;
    char title[128];
    char content[512];
    uint64_t timestamp;
    int helpful;
} store_review_t;

/* 分类 */
typedef struct {
    store_category_t id;
    char name[64];
    int app_count;
    char icon[32];
} store_category_info_t;

/* 应用商店状态 */
typedef struct {
    int initialized;
    int enabled;
    /* 第三方应用库（由开发者上传） */
    appstore_app_t apps[STORE_MAX_APPS];
    int app_count;
    /* 预装应用（系统软件，不可在商店安装/卸载） */
    preinstalled_app_t preinstalled[STORE_MAX_PREINSTALLED];
    int preinstalled_count;
    /* 开发者 */
    store_developer_t developers[STORE_MAX_DEVELOPERS];
    int developer_count;
    /* 评论 */
    store_review_t reviews[STORE_MAX_REVIEWS];
    int review_count;
    /* 分类 */
    store_category_info_t categories[STORE_MAX_CATEGORIES];
    int category_count;
    /* 已安装 */
    int installed_count;
    int updates_available;
    /* 设置 */
    int auto_update;
    int update_on_wifi_only;
    int install_unknown_sources;
    int parental_control;
    /* 统计 */
    uint64_t total_downloads;
    uint64_t total_installs;
    uint64_t total_uninstalls;
    uint64_t total_updates;
} app_store_t;

/* 初始化 */
void app_store_init(void);

/* 应用浏览 */
int app_store_get_app(int id, appstore_app_t *app);
int app_store_list_apps(appstore_app_t *apps, int max);
int app_store_list_by_category(store_category_t cat, appstore_app_t *apps, int max);
int app_store_list_installed(appstore_app_t *apps, int max);
int app_store_list_updates(appstore_app_t *apps, int max);
int app_store_list_featured(appstore_app_t *apps, int max);
int app_store_search(const char *query, appstore_app_t *results, int max);

/* 安装/卸载 */
int app_store_install(int app_id);
int app_store_uninstall(int app_id);
int app_store_update(int app_id);
int app_store_update_all(void);
int app_store_cancel_install(int app_id);
int app_store_get_install_progress(int app_id, int *progress);

/* 评论 */
int app_store_add_review(int app_id, float rating, const char *title, const char *content);
int app_store_list_reviews(int app_id, store_review_t *reviews, int max);
int app_store_get_review(int id, store_review_t *review);

/* 分类 */
int app_store_list_categories(store_category_info_t *categories, int max);
int app_store_get_category(store_category_t id, store_category_info_t *cat);

/* 设置 */
int app_store_set_auto_update(int enable);
int app_store_set_wifi_only(int enable);
int app_store_set_unknown_sources(int allow);
int app_store_set_parental_control(int enable, int age_limit);

/* 统计 */
int app_store_get_stats(app_store_t *stats);

/* 预装应用管理（系统软件） */
int app_store_list_preinstalled(preinstalled_app_t *apps, int max);
int app_store_get_preinstalled(const char *package_name, preinstalled_app_t *app);
int app_store_is_preinstalled(const char *package_name);

/* 第三方开发者平台 */
int app_store_register_developer(const char *name, const char *email, const char *website);
int app_store_list_developers(store_developer_t *devs, int max);
int app_store_get_developer(int id, store_developer_t *dev);
int app_store_upload_app(int developer_id, const char *name, const char *desc,
                          const char *version, uint64_t size,
                          store_category_t category);
int app_store_review_app(int app_id, int approved, const char *reason);

#endif /* APP_STORE_H */
