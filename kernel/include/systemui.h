#ifndef SYSTEMUI_H
#define SYSTEMUI_H

#include <stdint.h>

#define SUI_MAX_ICONS 16
#define SUI_MAX_TOGGLES 12
#define SUI_MAX_NOTIFICATIONS 16

/* UI元素状态 */
typedef enum {
    SUI_HIDDEN = 0,
    SUI_VISIBLE,
    SUI_EXPANDING,
    SUI_EXPANDED,
    SUI_COLLAPSING,
} sui_state_t;

/* 状态栏图标 */
typedef struct {
    int used;
    int id;
    const char *name;
    int icon;
    int visible;
    int priority;
    char text[32];
} status_icon_t;

/* 控制中心开关 */
typedef struct {
    int used;
    int id;
    const char *name;
    int icon;
    int enabled;
    int available;
    void (*on_toggle)(int enabled);
} control_toggle_t;

/* 锁屏状态 */
typedef enum {
    LOCK_SCREEN_OFF = 0,
    LOCK_SCREEN_LOCKED,
    LOCK_SCREEN_UNLOCKING,
    LOCK_SCREEN_UNLOCKED,
} lock_state_t;

/* 锁屏方式 */
typedef enum {
    LOCK_TYPE_NONE = 0,
    LOCK_TYPE_SWIPE,
    LOCK_TYPE_PIN,
    LOCK_TYPE_PATTERN,
    LOCK_TYPE_PASSWORD,
    LOCK_TYPE_FINGERPRINT,
    LOCK_TYPE_FACE,
} lock_type_t;

/* 状态栏 */
typedef struct {
    sui_state_t state;
    int height;
    int background_color;
    int text_color;
    int transparent;
    status_icon_t icons[SUI_MAX_ICONS];
    int icon_count;
    int show_clock;
    int show_battery;
    int show_signal;
    int show_wifi;
    int show_bluetooth;
    int show_notification_count;
} status_bar_t;

/* 导航栏 */
typedef struct {
    sui_state_t state;
    int height;
    int background_color;
    int icon_color;
    int show_back;
    int show_home;
    int show_recent;
    int show_menu;
    int immersive_mode;
} nav_bar_t;

/* 通知中心 */
typedef struct {
    sui_state_t state;
    int height;
    int background_color;
    int notification_count;
    int clear_all_enabled;
    int quick_settings_visible;
} notification_shade_t;

/* 控制中心 */
typedef struct {
    sui_state_t state;
    int height;
    int background_color;
    control_toggle_t toggles[SUI_MAX_TOGGLES];
    int toggle_count;
    int brightness;
    int volume;
    int rotation_locked;
    int flashlight_on;
} control_center_t;

/* 锁屏 */
typedef struct {
    lock_state_t state;
    lock_type_t lock_type;
    int background_color;
    int show_clock;
    int show_date;
    int show_notifications;
    int show_camera_shortcut;
    int show_emergency;
    int pin_length;
    int pattern_length;
    int failed_attempts;
    int max_attempts;
    uint64_t lockout_until;
} lock_screen_t;

/* 系统UI管理器 */
typedef struct {
    int initialized;
    status_bar_t status_bar;
    nav_bar_t nav_bar;
    notification_shade_t notification_shade;
    control_center_t control_center;
    lock_screen_t lock_screen;
    int screen_width;
    int screen_height;
    int dpi;
    int font_scale;
    int dark_mode;
} system_ui_t;

/* 初始化系统UI */
void systemui_init(void);

/* 状态栏 */
int statusbar_show(void);
int statusbar_hide(void);
int statusbar_add_icon(const char *name, int icon, int priority);
int statusbar_remove_icon(int icon_id);
int statusbar_set_icon_text(int icon_id, const char *text);
int statusbar_set_transparent(int transparent);
int statusbar_get_height(void);

/* 导航栏 */
int navbar_show(void);
int navbar_hide(void);
int navbar_set_immersive(int immersive);
int navbar_get_height(void);

/* 通知中心 */
int notificationshade_expand(void);
int notificationshade_collapse(void);
int notificationshade_clear_all(void);
int notificationshade_get_state(void);

/* 控制中心 */
int controlcenter_expand(void);
int controlcenter_collapse(void);
int controlcenter_toggle(int toggle_id);
int controlcenter_set_brightness(int value);
int controlcenter_set_volume(int value);
int controlcenter_add_toggle(const char *name, int icon, void (*on_toggle)(int));

/* 锁屏 */
int lockscreen_lock(void);
int lockscreen_unlock(void);
int lockscreen_set_type(lock_type_t type);
int lockscreen_verify_pin(const char *pin);
int lockscreen_verify_pattern(const int *pattern, int length);
int lockscreen_get_state(void);
int lockscreen_is_locked(void);
int lockscreen_show_emergency(void);

/* 主题 */
int systemui_set_dark_mode(int enable);
int systemui_get_dark_mode(void);
int systemui_set_font_scale(float scale);
int systemui_get_font_scale(void);

/* 屏幕 */
int systemui_set_screen_brightness(int value);
int systemui_get_screen_brightness(void);
int systemui_sleep(void);
int systemui_wake(void);

/* 渲染 */
int systemui_render(void);
void systemui_tick(void);

/* 获取系统UI状态 */
int systemui_get_status(system_ui_t *status);

#endif /* SYSTEMUI_H */
