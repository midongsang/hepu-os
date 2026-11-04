#ifndef BACKUP_RESTORE_H
#define BACKUP_RESTORE_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 数据备份与迁移
 * 本地备份 / 云备份 / 一键换机 / 应用数据迁移
 * ========================================================================== */

#define BR_MAX_BACKUPS 32
#define BR_MAX_ITEMS 256
#define BR_NAME_LEN 128

/* 备份类型 */
typedef enum {
    BR_TYPE_FULL = 0,       /* 全量备份 */
    BR_TYPE_INCREMENTAL,    /* 增量备份 */
    BR_TYPE_SYSTEM,         /* 系统设置 */
    BR_TYPE_APP_DATA,       /* 应用数据 */
    BR_TYPE_CONTACTS,       /* 联系人 */
    BR_TYPE_SMS,            /* 短信 */
    BR_TYPE_CALL_LOG,       /* 通话记录 */
    BR_TYPE_PHOTOS,         /* 照片 */
    BR_TYPE_MEDIA,          /* 媒体文件 */
    BR_TYPE_WIFI,           /* WiFi密码 */
    BR_TYPE_DESKTOP,        /* 桌面布局 */
} br_backup_type_t;

/* 备份位置 */
typedef enum {
    BR_LOC_LOCAL = 0,       /* 本地存储 */
    BR_LOC_SD_CARD,         /* SD卡 */
    BR_LOC_USB,             /* USB OTG */
    BR_LOC_CLOUD,           /* 云端 */
    BR_LOC_PC,              /* 电脑 */
    BR_LOC_OTHER_DEVICE,    /* 其他设备（换机） */
} br_location_t;

/* 备份状态 */
typedef enum {
    BR_STATE_IDLE = 0,
    BR_STATE_PREPARING,
    BR_STATE_BACKING_UP,
    BR_STATE_RESTORING,
    BR_STATE_COMPRESSING,
    BR_STATE_ENCRYPTING,
    BR_STATE_UPLOADING,
    BR_STATE_COMPLETED,
    BR_STATE_FAILED,
    BR_STATE_CANCELLED,
} br_state_t;

/* 备份项 */
typedef struct {
    int used;
    int id;
    br_backup_type_t type;
    char name[BR_NAME_LEN];
    char path[256];
    uint64_t size;
    uint64_t compressed_size;
    int selected;
    int backed_up;
    int progress;
} br_backup_item_t;

/* 备份记录 */
typedef struct {
    int used;
    int id;
    char name[BR_NAME_LEN];
    br_backup_type_t type;
    br_location_t location;
    uint64_t create_time;
    uint64_t size;
    uint64_t compressed_size;
    int item_count;
    int encrypted;
    char device_model[64];
    char os_version[32];
    br_state_t state;
    int verified;
} br_backup_t;

/* 迁移会话 */
typedef struct {
    int active;
    int session_id;
    br_location_t source;
    br_location_t target;
    char source_device[64];
    char target_device[64];
    int total_items;
    int completed_items;
    uint64_t total_size;
    uint64_t transferred_size;
    int progress;
    int speed_kbps;
    int estimated_minutes;
    br_state_t state;
} br_migration_t;

/* 备份配置 */
typedef struct {
    int enabled;
    int auto_backup;
    int auto_backup_wifi_only;
    int auto_backup_charging;
    uint32_t auto_backup_interval_hours;
    int encrypt_backups;
    int compress_backups;
    int include_photos;
    int include_videos;
    int include_app_data;
    int include_system_settings;
    int cloud_upload;
    int max_backups;
    br_location_t default_location;
} br_config_t;

/* 备份管理器状态 */
typedef struct {
    int initialized;
    int enabled;
    br_config_t config;
    /* 备份 */
    br_backup_t backups[BR_MAX_BACKUPS];
    int backup_count;
    /* 当前操作 */
    br_state_t current_state;
    int current_backup_id;
    int progress;
    /* 迁移 */
    br_migration_t migration;
    /* 统计 */
    uint64_t total_backups;
    uint64_t total_restores;
    uint64_t total_data_backed_up;
    uint64_t total_data_restored;
} backup_restore_t;

/* 初始化 */
void backup_restore_init(void);

/* 备份 */
int br_backup_create(const char *name, br_backup_type_t type, br_location_t location);
int br_backup_add_item(int backup_id, br_backup_type_t type, const char *path);
int br_backup_start(int backup_id);
int br_backup_cancel(int backup_id);
int br_backup_delete(int backup_id);
int br_backup_get(int backup_id, br_backup_t *backup);
int br_backup_list(br_backup_t *backups, int max);
int br_backup_get_progress(int backup_id, int *progress, br_state_t *state);
int br_backup_verify(int backup_id);

/* 恢复 */
int br_restore_start(int backup_id);
int br_restore_cancel(void);
int br_restore_get_progress(int *progress, br_state_t *state);
int br_restore_select_items(int backup_id, int *item_ids, int count);

/* 一键换机 */
int br_migration_start(br_location_t source, br_location_t target);
int br_migration_scan_source(void);
int br_migration_select_items(int *item_ids, int count);
int br_migration_begin(void);
int br_migration_cancel(void);
int br_migration_get_status(br_migration_t *status);
int br_migration_complete(void);

/* 云备份 */
int br_cloud_upload(int backup_id);
int br_cloud_download(int backup_id);
int br_cloud_list(br_backup_t *backups, int max);
int br_cloud_delete(int backup_id);

/* 配置 */
int br_set_config(const br_config_t *config);
int br_get_config(br_config_t *config);
int br_set_auto_backup(int enable);
int br_set_encryption(int enable);

/* 统计 */
int br_get_stats(backup_restore_t *stats);

#endif /* BACKUP_RESTORE_H */
