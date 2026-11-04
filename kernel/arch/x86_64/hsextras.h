/* ============================================================================
 * 赫普OS - HepuShell 高级功能
 * 锁屏、动画系统、主题引擎、插件系统
 * V0.10.170
 * ========================================================================== */

#ifndef HSEXTRAS_H
#define HSEXTRAS_H

#include <stdint.h>
#include <stddef.h>

/* 最大配置 */
#define HS_MAX_ANIMATIONS   32
#define HS_MAX_THEMES       8
#define HS_MAX_PLUGINS      16
#define HS_MAX_PASSWORD_LEN 64

/* 动画类型 */
typedef enum {
    HS_ANIM_FADE = 0,      /* 淡入淡出 */
    HS_ANIM_SLIDE_LEFT,    /* 左滑 */
    HS_ANIM_SLIDE_RIGHT,   /* 右滑 */
    HS_ANIM_SLIDE_UP,      /* 上滑 */
    HS_ANIM_SLIDE_DOWN,    /* 下滑 */
    HS_ANIM_SCALE,         /* 缩放 */
} hs_anim_type_t;

/* 动画状态 */
typedef enum {
    HS_ANIM_IDLE = 0,
    HS_ANIM_RUNNING,
    HS_ANIM_DONE,
} hs_anim_state_t;

/* 动画 */
typedef struct {
    int id;
    hs_anim_type_t type;
    hs_anim_state_t state;
    int x, y, width, height;
    int start_x, start_y, start_w, start_h;
    int end_x, end_y, end_w, end_h;
    int duration;     /* 毫秒 */
    int elapsed;      /* 已过时间 */
    int alpha;        /* 0-255 */
    void *target;     /* 动画目标 */
    void (*on_complete)(void *target);
} hs_animation_t;

/* 主题定义 */
typedef struct {
    char name[32];
    uint32_t bg;
    uint32_t panel_bg;
    uint32_t panel_fg;
    uint32_t border;
    uint32_t accent;
    uint32_t window_bg;
    uint32_t window_fg;
    uint32_t titlebar_bg;
    uint32_t workspace_active;
    uint32_t workspace_inactive;
} hs_theme_def_t;

/* 插件类型 */
typedef enum {
    HS_PLUGIN_WIDGET = 0,   /* 小部件插件 */
    HS_PLUGIN_APP,          /* 应用插件 */
    HS_PLUGIN_THEME,        /* 主题插件 */
} hs_plugin_type_t;

/* 插件 */
typedef struct {
    int id;
    hs_plugin_type_t type;
    char name[32];
    char version[16];
    int enabled;
    int loaded;
    void (*init)(void);
    void (*render)(int x, int y);
    void (*update)(void);
    void (*shutdown)(void);
} hs_plugin_t;

/* 锁屏状态 */
typedef struct {
    int locked;
    char password[HS_MAX_PASSWORD_LEN];
    char input[HS_MAX_PASSWORD_LEN];
    int input_len;
    int attempts;
    int lock_time;
} hs_lock_screen_t;

/* API - 锁屏 */
int hs_lock_init(const char *password);
void hs_lock_lock(void);
void hs_lock_unlock(void);
int hs_lock_is_locked(void);
void hs_lock_handle_key(int key);
void hs_lock_render(void);

/* API - 动画系统 */
int hs_animation_init(void);
int hs_animation_create(hs_anim_type_t type, int x, int y, int w, int h,
                        int duration, void *target,
                        void (*on_complete)(void *));
void hs_animation_start(int id);
void hs_animation_stop(int id);
void hs_animation_update(int delta_ms);
void hs_animation_render(int id);
int hs_animation_is_done(int id);
void hs_animation_cancel_all(void);

/* API - 主题引擎 */
int hs_theme_engine_init(void);
int hs_theme_register(hs_theme_def_t *theme);
int hs_theme_load(const char *name);
hs_theme_def_t *hs_theme_get_current(void);
hs_theme_def_t *hs_theme_get_list(int *count);
void hs_theme_apply(hs_theme_def_t *theme);

/* API - 插件系统 */
int hs_plugin_init(void);
int hs_plugin_register(hs_plugin_type_t type, const char *name,
                       const char *version,
                       void (*init)(void),
                       void (*render)(int x, int y),
                       void (*update)(void),
                       void (*shutdown)(void));
void hs_plugin_load_all(void);
void hs_plugin_update_all(void);
void hs_plugin_render_all(int x, int y);
hs_plugin_t *hs_plugin_get(int id);
int hs_plugin_count(void);

#endif /* HSEXTRAS_H */
