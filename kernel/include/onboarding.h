#ifndef ONBOARDING_H
#define ONBOARDING_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 系统新手引导
 * 帮助安卓/iOS用户迁移，交互教程，差异对比
 * ========================================================================== */

#define OB_MAX_STEPS 32
#define OB_MAX_TIPS 64
#define OB_TEXT_LEN 256
#define OB_TITLE_LEN 64

/* 引导类型 */
typedef enum {
    OB_TYPE_WELCOME = 0,     /* 欢迎页 */
    OB_TYPE_MIGRATION,       /* 数据迁移 */
    OB_TYPE_PRIVACY,         /* 隐私设置 */
    OB_TYPE_GESTURE,         /* 手势教学 */
    OB_TYPE_DESKTOP,         /* 桌面教学 */
    OB_TYPE_HUG_FORMAT,      /* .hug应用介绍 */
    OB_TYPE_CREATOR_MODE,    /* 创变模式介绍 */
    OB_TYPE_MULTI_USER,      /* 多用户/隐私空间 */
    OB_TYPE_AI_FEATURES,     /* AI功能介绍 */
    OB_TYPE_COMPLETE,        /* 完成页 */
} ob_type_t;

/* 引导步骤 */
typedef struct {
    int used;
    int id;
    ob_type_t type;
    char title[OB_TITLE_LEN];
    char description[OB_TEXT_LEN];
    char tip[OB_TEXT_LEN];
    int icon_id;
    int required;       /* 是否必须完成 */
    int completed;
    int skippable;
    int duration_ms;    /* 建议展示时长 */
} ob_step_t;

/* 使用技巧 */
typedef struct {
    int used;
    int id;
    char title[OB_TITLE_LEN];
    char content[OB_TEXT_LEN];
    char category[32];
    int shown_count;
    int useful_votes;
    int category_id;  /* 0=基础,1=进阶,2=效率,3=隐私,4=自定义 */
} ob_tip_t;

/* 迁移对比项 */
typedef struct {
    int used;
    int id;
    char android_way[OB_TEXT_LEN];
    char hepu_way[OB_TEXT_LEN];
    char category[32];
} ob_migration_item_t;

/* 引导状态 */
typedef enum {
    OB_STATE_NOT_STARTED = 0,
    OB_STATE_IN_PROGRESS,
    OB_STATE_COMPLETED,
    OB_STATE_SKIPPED,
} ob_state_t;

/* 引导配置 */
typedef struct {
    int enabled;
    int auto_start_on_first_boot;
    int show_tips;
    int show_migration_guide;
    int minimal_mode;     /* 极简模式（只显示关键步骤） */
    int dark_mode;
    int animation_enabled;
    int language;         /* 引导语言 */
} ob_config_t;

/* 引导管理器状态 */
typedef struct {
    int initialized;
    int enabled;
    ob_state_t state;
    int current_step;
    int total_steps;
    int completed_steps;
    ob_config_t config;
    /* 步骤 */
    ob_step_t steps[OB_MAX_STEPS];
    int step_count;
    /* 技巧 */
    ob_tip_t tips[OB_MAX_TIPS];
    int tip_count;
    /* 迁移对比 */
    ob_migration_item_t migration[32];
    int migration_count;
    /* 统计 */
    uint64_t total_starts;
    uint64_t total_completions;
    uint64_t total_skips;
    uint64_t total_tips_shown;
} onboarding_t;

/* 初始化 */
void onboarding_init(void);

/* 引导流程 */
int onboarding_start(void);
int onboarding_next(void);
int onboarding_prev(void);
int onboarding_skip(void);
int onboarding_complete_step(int step_id);
int onboarding_get_current(ob_step_t *step);
int onboarding_get_progress(int *current, int *total);
int onboarding_get_state(ob_state_t *state);
int onboarding_reset(void);

/* 步骤管理 */
int onboarding_add_step(ob_type_t type, const char *title, const char *desc, const char *tip);
int onboarding_list_steps(ob_step_t *steps, int max);
int onboarding_get_step(int id, ob_step_t *step);

/* 使用技巧 */
int onboarding_add_tip(const char *title, const char *content, int category);
int onboarding_get_random_tip(ob_tip_t *tip);
int onboarding_get_tips_by_category(int category, ob_tip_t *tips, int max);
int onboarding_list_tips(ob_tip_t *tips, int max);
int onboarding_vote_tip(int tip_id, int useful);

/* 迁移对比 */
int onboarding_add_migration(const char *android_way, const char *hepu_way, const char *category);
int onboarding_list_migration(ob_migration_item_t *items, int max);

/* 配置 */
int onboarding_set_config(const ob_config_t *config);
int onboarding_get_config(ob_config_t *config);
int onboarding_set_enabled(int enable);

/* 统计 */
int onboarding_get_stats(onboarding_t *stats);

#endif /* ONBOARDING_H */
