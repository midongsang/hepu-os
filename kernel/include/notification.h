#ifndef NOTIFICATION_H
#define NOTIFICATION_H

#include <stdint.h>

#define NOTIF_MAX_NOTIFICATIONS 64
#define NOTIF_MAX_CHANNELS 16
#define NOTIF_MAX_LISTENERS 8
#define NOTIF_MAX_ACTIONS 3
#define NOTIF_TEXT_LEN 256
#define NOTIF_CHANNEL_NAME_LEN 64

/* 通知重要性 */
typedef enum {
    NOTIF_IMPORTANCE_NONE = 0,
    NOTIF_IMPORTANCE_MIN,
    NOTIF_IMPORTANCE_LOW,
    NOTIF_IMPORTANCE_DEFAULT,
    NOTIF_IMPORTANCE_HIGH,
    NOTIF_IMPORTANCE_MAX,
} notif_importance_t;

/* 通知优先级 (兼容旧版) */
typedef enum {
    NOTIF_PRIORITY_MIN = -2,
    NOTIF_PRIORITY_LOW = -1,
    NOTIF_PRIORITY_DEFAULT = 0,
    NOTIF_PRIORITY_HIGH = 1,
    NOTIF_PRIORITY_MAX = 2,
} notif_priority_t;

/* 通知类别 */
typedef enum {
    NOTIF_CATEGORY_ALARM = 0,
    NOTIF_CATEGORY_CALL,
    NOTIF_CATEGORY_EMAIL,
    NOTIF_CATEGORY_MESSAGE,
    NOTIF_CATEGORY_NAVIGATION,
    NOTIF_CATEGORY_PROGRESS,
    NOTIF_CATEGORY_PROMO,
    NOTIF_CATEGORY_RECOMMENDATION,
    NOTIF_CATEGORY_REMINDER,
    NOTIF_CATEGORY_SERVICE,
    NOTIF_CATEGORY_SOCIAL,
    NOTIF_CATEGORY_SYSTEM,
    NOTIF_CATEGORY_TRANSPORT,
} notif_category_t;

/* 通知可见性 */
typedef enum {
    NOTIF_VISIBILITY_PUBLIC = 0,
    NOTIF_VISIBILITY_PRIVATE,
    NOTIF_VISIBILITY_SECRET,
} notif_visibility_t;

/* 通知动作 */
typedef struct {
    char label[64];
    int icon;
    int action_id;
} notif_action_t;

/* 通知渠道 */
typedef struct {
    int used;
    int id;
    char name[NOTIF_CHANNEL_NAME_LEN];
    char description[128];
    notif_importance_t importance;
    int enable_lights;
    int enable_vibration;
    int enable_sound;
    int show_badge;
    int bypass_dnd;  /* 勿扰模式绕过 */
    int locked;      /* 用户不可修改 */
} notif_channel_t;

/* 通知 */
typedef struct {
    int used;
    int id;
    char package_name[64];
    char channel_id[64];
    char title[NOTIF_TEXT_LEN];
    char text[NOTIF_TEXT_LEN];
    char big_text[NOTIF_TEXT_LEN * 4];
    char ticker[NOTIF_TEXT_LEN];
    int icon;
    int large_icon;
    notif_priority_t priority;
    notif_category_t category;
    notif_visibility_t visibility;
    int ongoing;       /* 持续通知 */
    int auto_cancel;   /* 点击后自动取消 */
    int show_when;
    uint64_t when;
    uint64_t post_time;
    /* 进度条 */
    int has_progress;
    int progress_max;
    int progress_current;
    int progress_indeterminate;
    /* 动作 */
    notif_action_t actions[NOTIF_MAX_ACTIONS];
    int action_count;
    /* 状态 */
    int read;
    int shown;
} notification_t;

/* 通知监听器 */
typedef struct {
    int used;
    int id;
    char package_name[64];
    void (*on_notification_posted)(notification_t *notif);
    void (*on_notification_removed)(notification_t *notif);
} notif_listener_t;

/* 初始化通知系统 */
void notification_init(void);

/* 通知渠道管理 */
int notif_create_channel(notif_channel_t *channel);
int notif_delete_channel(const char *channel_id);
int notif_get_channel(const char *channel_id, notif_channel_t *channel);
int notif_list_channels(notif_channel_t *channels, int max);

/* 通知管理 */
int notif_post(const char *package, const char *channel_id, notification_t *notif);
int notif_cancel(const char *package, int notif_id);
int notif_cancel_all(const char *package);
int notif_get_notification(int id, notification_t *notif);
int notif_list_notifications(notification_t *list, int max);
int notif_list_by_package(const char *package, notification_t *list, int max);

/* 通知操作 */
int notif_mark_read(int id);
int notif_tap(int id, int action_index);

/* 锁屏通知 */
int notif_get_lockscreen_notifications(notification_t *list, int max);

/* 通知监听 */
int notif_register_listener(notif_listener_t *listener);
int notif_unregister_listener(int listener_id);

/* 勿扰模式 */
int notif_set_dnd(int enable);
int notif_get_dnd(void);

/* 统计 */
int notif_count(void);
int notif_unread_count(void);
int notif_channel_count(void);

/* 通知灯控制 */
int notif_set_light(int color, int on_ms, int off_ms);
int notif_cancel_light(void);

/* 震动控制 */
int notif_vibrate(int *pattern, int length, int repeat);
int notif_cancel_vibrate(void);

#endif /* NOTIFICATION_H */
