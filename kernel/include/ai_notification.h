#ifndef AI_NOTIFICATION_H
#define AI_NOTIFICATION_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS AI通知分级
 * 智能分类、垃圾过滤、优先级排序、静默收纳
 * ========================================================================== */

#define AN_MAX_RULES 128
#define AN_MAX_HISTORY 256
#define AN_MAX_KEYWORDS 64
#define AN_TEXT_LEN 512
#define AN_APP_LEN 128

/* 通知分类 */
typedef enum {
    AN_CAT_IMPORTANT = 0,   /* 重要（人/通话/短信/日程） */
    AN_CAT_NORMAL,          /* 普通（应用通知） */
    AN_CAT_PROMOTION,       /* 推广/营销 */
    AN_CAT_SPAM,            /* 垃圾/骚扰 */
    AN_CAT_SYSTEM,          /* 系统通知 */
    AN_CAT_SOCIAL,          /* 社交（消息/评论/点赞） */
    AN_CAT_NEWS,            /* 新闻/资讯 */
    AN_CAT_OTHER,           /* 其他 */
} an_category_t;

/* 通知优先级 */
typedef enum {
    AN_PRIORITY_URGENT = 0,  /* 紧急（立即提醒） */
    AN_PRIORITY_HIGH,        /* 高（醒目标记） */
    AN_PRIORITY_NORMAL,      /* 普通 */
    AN_PRIORITY_LOW,         /* 低（静默收纳） */
    AN_PRIORITY_SPAM,        /* 垃圾（自动过滤） */
} an_priority_t;

/* 通知处理动作 */
typedef enum {
    AN_ACTION_SHOW = 0,      /* 正常显示 */
    AN_ACTION_SILENT,        /* 静默显示（无声音/震动） */
    AN_ACTION_COLLAPSE,      /* 折叠收纳 */
    AN_ACTION_FILTER,        /* 自动过滤（不显示） */
    AN_ACTION_DELAY,         /* 延迟显示（集中时段） */
} an_action_t;

/* 分类规则 */
typedef struct {
    int used;
    int id;
    char app[AN_APP_LEN];
    char keyword[64];
    an_category_t category;
    an_priority_t priority;
    an_action_t action;
    int hit_count;
    int enabled;
} an_rule_t;

/* 通知记录 */
typedef struct {
    int used;
    int id;
    char app[AN_APP_LEN];
    char title[128];
    char content[AN_TEXT_LEN];
    an_category_t category;
    an_priority_t priority;
    an_action_t action;
    uint64_t timestamp;
    int user_clicked;
    int user_dismissed;
    float confidence;  /* 分类置信度 */
} an_notification_t;

/* 应用通知配置 */
typedef struct {
    int used;
    char app[AN_APP_LEN];
    int allowed;
    int silent_mode;
    int priority_override;  /* -1=自动, 0-4=强制优先级 */
    int show_on_lock_screen;
    int badge_enabled;
    uint64_t total_received;
    uint64_t total_filtered;
} an_app_config_t;

/* AI分类器配置 */
typedef struct {
    int enabled;
    int auto_learn;               /* 自动学习用户行为 */
    int spam_filter;              /* 垃圾过滤 */
    int promotion_filter;         /* 推广过滤 */
    int smart_silence;            /* 智能静默 */
    int focus_mode;               /* 专注模式 */
    int sleep_mode;               /* 睡眠模式（23:00-7:00静默） */
    int quiet_hours_start;        /* 静默开始小时 */
    int quiet_hours_end;          /* 静默结束小时 */
    int min_spam_confidence;      /* 垃圾判定最低置信度(0-100) */
} an_config_t;

/* 统计 */
typedef struct {
    uint64_t total_notifications;
    uint64_t total_important;
    uint64_t total_promotions;
    uint64_t total_spam;
    uint64_t total_filtered;
    uint64_t total_silenced;
    uint64_t total_user_clicks;
    uint64_t total_user_dismisses;
    float accuracy_estimate;  /* 估算准确率 */
} an_stats_t;

/* AI通知管理器状态 */
typedef struct {
    int initialized;
    int enabled;
    an_config_t config;
    /* 规则 */
    an_rule_t rules[AN_MAX_RULES];
    int rule_count;
    /* 历史 */
    an_notification_t history[AN_MAX_HISTORY];
    int history_count;
    /* 应用配置 */
    an_app_config_t app_configs[64];
    int app_config_count;
    /* 统计 */
    an_stats_t stats;
} ai_notification_t;

/* 初始化 */
void ai_notification_init(void);

/* 核心：分类通知 */
int ai_notification_classify(const char *app, const char *title, const char *content,
                             an_category_t *category, an_priority_t *priority,
                             an_action_t *action, float *confidence);

/* 通知处理 */
int ai_notification_process(const char *app, const char *title, const char *content);
int ai_notification_on_click(int notification_id);
int ai_notification_on_dismiss(int notification_id);

/* 规则管理 */
int ai_notification_rule_add(const char *app, const char *keyword,
                             an_category_t category, an_priority_t priority,
                             an_action_t action);
int ai_notification_rule_remove(int rule_id);
int ai_notification_rule_list(an_rule_t *rules, int max);
int ai_notification_rule_enable(int rule_id, int enable);

/* 应用配置 */
int ai_notification_app_config_set(const char *app, const an_app_config_t *config);
int ai_notification_app_config_get(const char *app, an_app_config_t *config);
int ai_notification_app_allow(const char *app, int allow);
int ai_notification_app_silence(const char *app, int silent);

/* 配置 */
int ai_notification_set_config(const an_config_t *config);
int ai_notification_get_config(an_config_t *config);
int ai_notification_set_enabled(int enable);
int ai_notification_set_focus_mode(int enable);
int ai_notification_set_sleep_mode(int enable);

/* 查询 */
int ai_notification_get_history(an_notification_t *notifs, int max);
int ai_notification_get_by_category(an_category_t cat, an_notification_t *notifs, int max);
int ai_notification_get_stats(an_stats_t *stats);
int ai_notification_get_pending_count(int *count);

/* 学习 */
int ai_notification_learn_feedback(int notification_id, int is_important);
int ai_notification_reset_learning(void);

#endif /* AI_NOTIFICATION_H */
