#ifndef HUG_TEMPLATE_H
#define HUG_TEMPLATE_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS .hug 应用模板生成器
 * 模板管理 / 代码生成 / 项目脚手架
 * ========================================================================== */

#define HTPL_MAX_TEMPLATES 32
#define HTPL_MAX_FILES 64
#define HTPL_NAME_LEN 128
#define HTPL_PATH_LEN 256

/* 模板类型 */
typedef enum {
    HTPL_TYPE_BASIC = 0,       /* 基础应用 */
    HTPL_TYPE_TAB,             /* 标签页应用 */
    HTPL_TYPE_DRAWER,          /* 抽屉导航 */
    HTPL_TYPE_MASTER_DETAIL,   /* 主从详情 */
    HTPL_TYPE_GAME_2D,         /* 2D游戏 */
    HTPL_TYPE_GAME_3D,         /* 3D游戏 */
    HTPL_TYPE_SERVICE,         /* 后台服务 */
    HTPL_TYPE_WIDGET,          /* 桌面小部件 */
    HTPL_TYPE_THEME,           /* 主题包 */
    HTPL_TYPE_PLUGIN,          /* 插件 */
    HTPL_TYPE_LOGIN,           /* 登录注册 */
    HTPL_TYPE_SETTINGS,        /* 设置页 */
    HTPL_TYPE_LIST_DETAIL,     /* 列表详情 */
    HTPL_TYPE_MEDIA,           /* 媒体播放 */
    HTPL_TYPE_MAP,             /* 地图应用 */
    HTPL_TYPE_CHAT,            /* 聊天应用 */
} htpl_type_t;

/* 模板文件 */
typedef struct {
    int used;
    char path[HTPL_PATH_LEN];
    char name[HTPL_NAME_LEN];
    char content[4096];
    int is_binary;
    uint64_t size;
} htpl_file_t;

/* 模板定义 */
typedef struct {
    int used;
    int id;
    char name[HTPL_NAME_LEN];
    char description[512];
    htpl_type_t type;
    char category[64];
    char author[128];
    char version[32];
    int min_sdk;
    int file_count;
    htpl_file_t files[HTPL_MAX_FILES];
    int is_system;
    uint64_t usage_count;
} htpl_template_t;

/* 生成配置 */
typedef struct {
    char app_name[HTPL_NAME_LEN];
    char package_name[128];
    char version[32];
    int version_code;
    htpl_type_t template_type;
    int template_id;
    char output_dir[HTPL_PATH_LEN];
    int include_tests;
    int include_ci;
    int include_docs;
    int dark_mode;
    int localization;
    char languages[16][8];
    int language_count;
} htpl_config_t;

/* 生成结果 */
typedef struct {
    int success;
    char project_path[HTPL_PATH_LEN];
    int file_count;
    uint64_t total_size;
    int errors;
    char error_log[2048];
} htpl_result_t;

/* 模板管理器状态 */
typedef struct {
    int initialized;
    htpl_template_t templates[HTPL_MAX_TEMPLATES];
    int template_count;
    uint64_t total_generated;
} hug_template_t;

/* 初始化 */
void hug_template_init(void);

/* 模板管理 */
int htpl_register(const char *name, const char *desc, htpl_type_t type);
int htpl_unregister(int id);
int htpl_get(int id, htpl_template_t *tpl);
int htpl_list(htpl_template_t *templates, int max);
int htpl_list_by_type(htpl_type_t type, htpl_template_t *templates, int max);
int htpl_search(const char *query, htpl_template_t *results, int max);

/* 模板文件 */
int htpl_add_file(int template_id, const char *path, const char *content);
int htpl_remove_file(int template_id, const char *path);
int htpl_get_file(int template_id, const char *path, htpl_file_t *file);
int htpl_list_files(int template_id, htpl_file_t *files, int max);

/* 代码生成 */
int htpl_generate(const htpl_config_t *config, htpl_result_t *result);
int htpl_generate_file(int template_id, const char *file_path,
                       const char *app_name, const char *package, char *output, int max);

/* 内置模板 */
int htpl_init_builtin_templates(void);
int htpl_get_template_by_type(htpl_type_t type, htpl_template_t *tpl);

/* 统计 */
int htpl_get_stats(hug_template_t *stats);

#endif /* HUG_TEMPLATE_H */
