#ifndef SETTINGS_APP_H
#define SETTINGS_APP_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 系统设置应用
 * 设置项框架 / 分类 / 值存储 / 搜索
 * ========================================================================== */

#define SETTINGS_MAX_CATEGORIES 16
#define SETTINGS_MAX_ITEMS 256
#define SETTINGS_NAME_LEN 64
#define SETTINGS_DESC_LEN 128
#define SETTINGS_VALUE_LEN 256

/* 设置项类型 */
typedef enum {
    SETTING_TYPE_BOOL = 0,
    SETTING_TYPE_INT,
    SETTING_TYPE_STRING,
    SETTING_TYPE_ENUM,
    SETTING_TYPE_SLIDER,
    SETTING_TYPE_ACTION,
    SETTING_TYPE_GROUP,
} setting_type_t;

/* 设置分类 */
typedef enum {
    SETTING_CAT_NETWORK = 0,   /* 网络与连接 */
    SETTING_CAT_DEVICE,        /* 设备 */
    SETTING_CAT_APPS,          /* 应用 */
    SETTING_CAT_DISPLAY,       /* 显示 */
    SETTING_CAT_SOUND,         /* 声音 */
    SETTING_CAT_BATTERY,       /* 电池 */
    SETTING_CAT_STORAGE,       /* 存储 */
    SETTING_CAT_SECURITY,      /* 安全与隐私 */
    SETTING_CAT_ACCOUNT,       /* 账户 */
    SETTING_CAT_SYSTEM,        /* 系统 */
    SETTING_CAT_ABOUT,         /* 关于 */
    SETTING_CAT_ACCESSIBILITY, /* 无障碍 */
    SETTING_CAT_AI,            /* AI */
    SETTING_CAT_DEVELOPER,     /* 开发者选项 */
} setting_category_t;

/* 设置项 */
typedef struct {
    int used;
    int id;
    char name[SETTINGS_NAME_LEN];
    char description[SETTINGS_DESC_LEN];
    setting_type_t type;
    setting_category_t category;
    /* 值 */
    int value_int;
    char value_str[SETTINGS_VALUE_LEN];
    int value_bool;
    /* 范围 */
    int min_val;
    int max_val;
    int step;
    /* 枚举选项 */
    char enum_options[8][32];
    int enum_count;
    /* 标志 */
    int visible;
    int enabled;
    int modified;
    int requires_reboot;
    /* 权限 */
    int protected;  /* 受保护设置, 需要root */
    /* 回调 */
    int (*on_change)(int id, const char *new_value);
} setting_item_t;

/* 设置分类描述 */
typedef struct {
    setting_category_t id;
    char name[SETTINGS_NAME_LEN];
    char icon[32];
    int item_count;
    int visible;
} setting_category_info_t;

/* 设置应用状态 */
typedef struct {
    int initialized;
    int enabled;
    setting_item_t items[SETTINGS_MAX_ITEMS];
    int item_count;
    setting_category_info_t categories[SETTINGS_MAX_CATEGORIES];
    int category_count;
    /* 当前页面 */
    setting_category_t current_category;
    int current_item;
    /* 搜索 */
    char search_query[64];
    int search_results[64];
    int search_result_count;
    /* 统计 */
    uint64_t total_changes;
    uint64_t reboots_required;
} settings_app_t;

/* 初始化 */
void settings_init(void);

/* 设置项管理 */
int settings_register(const char *name, setting_category_t category, setting_type_t type,
                      const char *desc, int protected);
int settings_set_value(int id, const char *value);
int settings_get_value(int id, char *buf, int size);
int settings_set_bool(int id, int value);
int settings_get_bool(int id);
int settings_set_int(int id, int value);
int settings_get_int(int id);
int settings_reset(int id);
int settings_reset_category(setting_category_t category);
int settings_reset_all(void);

/* 查询 */
int settings_get_item(int id, setting_item_t *item);
int settings_list_by_category(setting_category_t category, setting_item_t *items, int max);
int settings_list_all(setting_item_t *items, int max);
int settings_get_category_info(setting_category_t category, setting_category_info_t *info);
int settings_list_categories(setting_category_info_t *categories, int max);

/* 搜索 */
int settings_search(const char *query, int *results, int max);

/* 导入导出 */
int settings_export(char *buf, int size);
int settings_import(const char *data);

/* 导航 */
int settings_navigate(setting_category_t category);
int settings_get_current_category(setting_category_t *category);

/* 统计 */
int settings_get_stats(settings_app_t *stats);

/* V7.15: 字体大小设置 */
int settings_apply_font_size(void);
int settings_get_font_size_px(void);

/* V8.43: 深色模式与主题 */
int settings_apply_dark_mode(void);
int settings_get_dark_mode(void);
int settings_set_theme(int theme_id);

/* V8.43: 亮度调节 */
int settings_apply_brightness(void);
int settings_get_brightness(void);

/* V8.43: 应用所有显示设置 */
int settings_apply_display(void);

/* V8.44: 声音设置 */
int settings_apply_media_volume(void);
int settings_get_media_volume(void);
int settings_apply_ring_volume(void);
int settings_apply_sound(void);
int settings_set_mute(int mute);

/* V8.45: 电池/电源设置 */
int settings_get_battery_info(int *percentage, int *charging, int *remaining_s);
int settings_set_low_power_mode(int enable);
int settings_get_low_power_mode(void);
int settings_apply_battery(void);

/* V8.45: 系统信息 */
int settings_get_storage_info(uint64_t *total, uint64_t *used, uint64_t *free);
int settings_get_system_info(char *buf, int size);

#endif /* SETTINGS_APP_H */
