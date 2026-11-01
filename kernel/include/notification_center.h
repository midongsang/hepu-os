#ifndef NOTIFICATION_CENTER_H
#define NOTIFICATION_CENTER_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 通知/控制中心
 * 通知分组 / 快捷开关 / 媒体控制 / 通知管理
 * ========================================================================== */

#define NC_MAX_NOTIFICATIONS 256
#define NC_MAX_GROUPS 64
#define NC_MAX_QUICK_TOGGLES 32
#define NC_MAX_MEDIA_SESSIONS 16
#define NC_NAME_LEN 128

/* 通知重要性 */
typedef enum {
    NC_IMPORTANCE_NONE = 0,   /* 无（不显示） */
    NC_IMPORTANCE_MIN,        /* 最小（无声） */
    NC_IMPORTANCE_LOW,        /* 低（无声无震动） */
    NC_IMPORTANCE_DEFAULT,    /* 默认（有声） */
    NC_IMPORTANCE_HIGH,       /* 高（ heads-up ） */
    NC_IMPORTANCE_URGENT,     /* 紧急（全屏） */
} nc_importance_t;

/* 通知类别 */
typedef enum {
    NC_CAT_CALL = 0,
    NC_CAT_MESSAGE,
    NC_CAT_EMAIL,
    NC_CAT_SOCIAL,
    NC_CAT_SYSTEM,
    NC_CAT_MEDIA,
    NC_CAT_ALARM,
    NC_CAT_REMINDER,
    NC_CAT_PROMO,
    NC_CAT_SERVICE,
} nc_category_t;

/* 通知 */
typedef struct {
    int used;
    int id;
    char app_name[NC_NAME_LEN];
    char package[NC_NAME_LEN];
    char title[256];
    char content[512];
    char big_text[1024];
    nc_importance_t importance;
    nc_category_t category;
    uint64_t post_time;
    uint64_t update_time;
    int group_id;
    int is_group_summary;
    int is_clearable;
    int is_ongoing;
    int is_read;
    int is_shown;
    /* 操作按钮 */
    char action1[64];
    char action2[64];
    char action3[64];
    int action_count;
    /* 图标 */
    int icon_id;
    int color;
    /* 进度 */
    int has_progress;
    int progress;
    int progress_max;
} nc_notification_t;

/* 通知组 */
typedef struct {
    int used;
    int id;
    char key[NC_NAME_LEN];
    char app_name[NC_NAME_LEN];
    int notification_count;
    int unread_count;
    uint64_t latest_time;
    int is_silenced;
} nc_group_t;

/* 快捷开关 */
typedef enum {
    NC_TOGGLE_WIFI = 0,
    NC_TOGGLE_BLUETOOTH,
    NC_TOGGLE_AIRPLANE,
    NC_TOGGLE_MOBILE_DATA,
    NC_TOGGLE_GPS,
    NC_TOGGLE_FLASHLIGHT,
    NC_TOGGLE_ROTATION,
    NC_TOGGLE_BLUETOOTH_TETHER,
    NC_TOGGLE_HOTSPOT,
    NC_TOGGLE_NFC,
    NC_TOGGLE_DND,
    NC_TOGGLE_VIBRATE,
    NC_TOGGLE_SILENT,
    NC_TOGGLE_BATTERY_SAVER,
    NC_TOGGLE_DARK_MODE,
    NC_TOGGLE_EYE_PROTECTION,
    NC_TOGGLE_SCREEN_RECORD,
    NC_TOGGLE_SCREENSHOT,
    NC_TOGGLE_CAST,
    NC_TOGGLE_AUTOROTATE,
} nc_toggle_t;

/* 快捷开关状态 */
typedef struct {
    nc_toggle_t toggle;
    int enabled;
    int available;
    char label[64];
    int icon_id;
} nc_quick_toggle_t;

/* 媒体会话 */
typedef struct {
    int used;
    int id;
    char app_name[NC_NAME_LEN];
    char title[256];
    char artist[256];
    char album[256];
    int is_playing;
    int position_ms;
    int duration_ms;
    int volume;
    int supports_play_pause;
    int supports_skip;
    int supports_seek;
    uint64_t last_active;
} nc_media_session_t;

/* 勿扰模式配置 */
typedef struct {
    int enabled;
    int allow_calls;
    int allow_messages;
    int allow_repeat_callers;
    int allow_favorites;
    int schedule_enabled;
    int start_hour;
    int start_minute;
    int end_hour;
    int end_minute;
} nc_dnd_config_t;

/* 通知中心配置 */
typedef struct {
    int enabled;
    int notification_history;
    int notification_history_days;
    int heads_up_enabled;
    int lock_screen_notifications;
    int notification_light;
    int notification_vibrate;
    int notification_sound;
    int badge_enabled;
    int snooze_enabled;
} nc_config_t;

/* 通知中心状态 */
typedef struct {
    int initialized;
    int enabled;
    nc_config_t config;
    /* 通知 */
    nc_notification_t notifications[NC_MAX_NOTIFICATIONS];
    int notification_count;
    int unread_count;
    /* 分组 */
    nc_group_t groups[NC_MAX_GROUPS];
    int group_count;
    /* 快捷开关 */
    nc_quick_toggle_t toggles[NC_MAX_QUICK_TOGGLES];
    int toggle_count;
    /* 媒体 */
    nc_media_session_t media[NC_MAX_MEDIA_SESSIONS];
    int media_count;
    int active_media;
    /* DND */
    nc_dnd_config_t dnd;
    /* 统计 */
    uint64_t total_notifications;
    uint64_t total_dismissed;
    uint64_t total_actions;
} notification_center_t;

/* 初始化 */
void notification_center_init(void);

/* 通知管理 */
int nc_notify(const char *app, const char *package, const char *title,
              const char *content, nc_importance_t importance, nc_category_t category);
int nc_notify_full(nc_notification_t *notif);
int nc_cancel(int notification_id);
int nc_cancel_all(const char *package);
int nc_get(int notification_id, nc_notification_t *notif);
int nc_list(nc_notification_t *notifs, int max);
int nc_list_by_app(const char *package, nc_notification_t *notifs, int max);
int nc_mark_read(int notification_id);
int nc_mark_all_read(void);
int nc_snooze(int notification_id, int minutes);

/* 通知分组 */
int nc_group_get(int group_id, nc_group_t *group);
int nc_group_list(nc_group_t *groups, int max);
int nc_group_dismiss(int group_id);
int nc_group_silence(int group_id, int silence);

/* 快捷开关 */
int nc_toggle_set(nc_toggle_t toggle, int enabled);
int nc_toggle_get(nc_toggle_t toggle, int *enabled);
int nc_toggle_list(nc_quick_toggle_t *toggles, int max);
int nc_toggle_add(nc_toggle_t toggle, const char *label);
int nc_toggle_remove(nc_toggle_t toggle);

/* 媒体控制 */
int nc_media_register(const char *app, const char *title, const char *artist);
int nc_media_unregister(int session_id);
int nc_media_update(int session_id, int playing, int position, int duration);
int nc_media_play_pause(int session_id);
int nc_media_next(int session_id);
int nc_media_prev(int session_id);
int nc_media_seek(int session_id, int position_ms);
int nc_media_get(int session_id, nc_media_session_t *session);
int nc_media_list(nc_media_session_t *sessions, int max);
int nc_media_get_active(nc_media_session_t *session);

/* 勿扰模式 */
int nc_dnd_set(int enabled);
int nc_dnd_get(int *enabled);
int nc_dnd_set_config(const nc_dnd_config_t *config);
int nc_dnd_get_config(nc_dnd_config_t *config);

/* 配置 */
int nc_set_config(const nc_config_t *config);
int nc_get_config(nc_config_t *config);

/* 统计 */
int nc_get_stats(notification_center_t *stats);
int nc_get_unread_count(int *count);

#endif /* NOTIFICATION_CENTER_H */
