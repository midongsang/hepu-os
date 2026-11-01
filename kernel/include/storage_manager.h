#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 存储管理深化
 * 大文件清理 / 重复文件 / 应用缓存管理 / 存储分析
 * ========================================================================== */

#define SM_MAX_FILES 1024
#define SM_MAX_APPS 256
#define SM_MAX_CATEGORIES 16
#define SM_NAME_LEN 128

/* 文件类别 */
typedef enum {
    SM_CAT_PHOTO = 0,
    SM_CAT_VIDEO,
    SM_CAT_AUDIO,
    SM_CAT_DOCUMENT,
    SM_CAT_APK,
    SM_CAT_ARCHIVE,
    SM_CAT_CACHE,
    SM_CAT_TEMP,
    SM_CAT_DOWNLOAD,
    SM_CAT_SYSTEM,
    SM_CAT_OTHER,
} sm_category_t;

/* 文件信息 */
typedef struct {
    int used;
    int id;
    char path[512];
    char name[256];
    sm_category_t category;
    uint64_t size;
    uint64_t last_access;
    uint64_t last_modify;
    int is_duplicate;
    int duplicate_group;
    int is_large;
    int is_old;  /* 超过30天未访问 */
    int is_cache;
    int selected;
    uint8_t md5[16];
} sm_file_t;

/* 应用存储信息 */
typedef struct {
    int used;
    int id;
    char app_name[SM_NAME_LEN];
    char package[SM_NAME_LEN];
    uint64_t app_size;
    uint64_t data_size;
    uint64_t cache_size;
    uint64_t total_size;
    int cache_clearable;
    int data_clearable;
} sm_app_storage_t;

/* 重复文件组 */
typedef struct {
    int used;
    int id;
    uint8_t md5[16];
    uint64_t file_size;
    int file_count;
    int file_ids[64];
    uint64_t wasted_space;
} sm_duplicate_group_t;

/* 存储统计 */
typedef struct {
    uint64_t total_space;
    uint64_t used_space;
    uint64_t free_space;
    uint64_t system_space;
    uint64_t app_space;
    uint64_t photo_space;
    uint64_t video_space;
    uint64_t audio_space;
    uint64_t document_space;
    uint64_t cache_space;
    uint64_t other_space;
    int usage_percent;
} sm_stats_t;

/* 清理配置 */
typedef struct {
    int auto_clean;
    int auto_clean_days;
    int clean_cache_when_low;
    int low_storage_threshold_percent;
    int notify_large_files;
    int large_file_threshold_mb;
    int duplicate_detection;
    int old_file_days;
} sm_config_t;

/* 存储管理器状态 */
typedef struct {
    int initialized;
    int enabled;
    sm_config_t config;
    /* 文件 */
    sm_file_t files[SM_MAX_FILES];
    int file_count;
    /* 应用 */
    sm_app_storage_t apps[SM_MAX_APPS];
    int app_count;
    /* 重复 */
    sm_duplicate_group_t duplicates[SM_MAX_FILES / 2];
    int duplicate_count;
    /* 统计 */
    sm_stats_t stats;
    /* 清理 */
    uint64_t total_cleaned;
    uint64_t total_cache_cleared;
    int clean_count;
} storage_manager_t;

/* 初始化 */
void storage_manager_init(void);

/* 存储分析 */
int sm_analyze(void);
int sm_get_stats(sm_stats_t *stats);
int sm_get_usage_percent(int *percent);
int sm_scan_directory(const char *path);

/* 文件管理 */
int sm_file_list(sm_file_t *files, int max);
int sm_file_list_by_category(sm_category_t cat, sm_file_t *files, int max);
int sm_file_get(int id, sm_file_t *file);
int sm_file_delete(int id);
int sm_file_get_large(sm_file_t *files, int max, uint64_t threshold);
int sm_file_get_old(sm_file_t *files, int max, int days);

/* 重复文件 */
int sm_detect_duplicates(void);
int sm_get_duplicates(sm_duplicate_group_t *groups, int max);
int sm_get_duplicate_files(int group_id, sm_file_t *files, int max);
int sm_delete_duplicate(int group_id, int keep_index);

/* 应用存储 */
int sm_app_register(const char *name, const char *package);
int sm_app_update(int app_id, uint64_t app_size, uint64_t data_size, uint64_t cache_size);
int sm_app_get(int app_id, sm_app_storage_t *app);
int sm_app_list(sm_app_storage_t *apps, int max);
int sm_app_clear_cache(int app_id);
int sm_app_clear_data(int app_id);
int sm_app_get_top(sm_app_storage_t *apps, int max);

/* 缓存管理 */
int sm_clear_all_cache(void);
int sm_clear_app_cache(int app_id);
int sm_clear_temp_files(void);
int sm_clear_downloads(int older_than_days);
int sm_get_cache_size(uint64_t *size);

/* 大文件 */
int sm_find_large_files(uint64_t threshold_mb);
int sm_get_large_files(sm_file_t *files, int max);

/* 配置 */
int sm_set_config(const sm_config_t *config);
int sm_get_config(sm_config_t *config);
int sm_set_auto_clean(int enable);
int sm_set_low_storage_threshold(int percent);

/* 清理操作 */
int sm_clean_selected(int *file_ids, int count);
int sm_clean_recommended(uint64_t *freed);
int sm_get_recommended_clean(sm_file_t *files, int max);

/* 统计 */
int sm_get_full_stats(storage_manager_t *stats);

#endif /* STORAGE_MANAGER_H */
