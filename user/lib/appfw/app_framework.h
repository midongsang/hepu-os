/* ============================================================================
 * 赫普OS - 应用开发框架 (Application Framework)
 * V8.87: 初始实现
 * 提供应用生命周期、窗口管理、事件处理、UI组件
 * ========================================================================== */

#ifndef _APP_FRAMEWORK_H
#define _APP_FRAMEWORK_H

#include <stdint.h>
#include <stddef.h>

/* 版本信息 */
#define APPFW_VERSION "1.0.0"
#define APPFW_VERSION_MAJOR 1
#define APPFW_VERSION_MINOR 0
#define APPFW_VERSION_PATCH 0

/* 应用状态 */
typedef enum {
    APP_STATE_CREATED = 0,
    APP_STATE_INITIALIZING,
    APP_STATE_RUNNING,
    APP_STATE_PAUSED,
    APP_STATE_STOPPING,
    APP_STATE_STOPPED,
    APP_STATE_ERROR
} app_state_t;

/* 事件类型 */
typedef enum {
    EVENT_NONE = 0,
    EVENT_KEY_DOWN,
    EVENT_KEY_UP,
    EVENT_MOUSE_MOVE,
    EVENT_MOUSE_DOWN,
    EVENT_MOUSE_UP,
    EVENT_MOUSE_WHEEL,
    EVENT_WINDOW_RESIZE,
    EVENT_WINDOW_CLOSE,
    EVENT_WINDOW_FOCUS,
    EVENT_WINDOW_BLUR,
    EVENT_TIMER,
    EVENT_NETWORK,
    EVENT_CUSTOM
} event_type_t;

/* 键盘事件 */
typedef struct {
    int key_code;
    int modifiers;  /* Ctrl/Alt/Shift */
    char character;
} key_event_t;

/* 鼠标事件 */
typedef struct {
    int x, y;
    int button;
    int wheel_delta;
} mouse_event_t;

/* 窗口事件 */
typedef struct {
    int width, height;
} window_event_t;

/* 通用事件 */
typedef struct {
    event_type_t type;
    uint32_t timestamp;
    union {
        key_event_t key;
        mouse_event_t mouse;
        window_event_t window;
        struct {
            int timer_id;
        } timer;
        struct {
            int event_id;
            void *data;
            size_t data_size;
        } custom;
    } data;
} app_event_t;

/* 颜色 */
typedef uint32_t app_color_t;
#define APP_COLOR(r,g,b) (((r)<<16)|((g)<<8)|(b))
#define APP_COLOR_R(c) ((c>>16)&0xFF)
#define APP_COLOR_G(c) ((c>>8)&0xFF)
#define APP_COLOR_B(c) (c&0xFF)

/* 矩形 */
typedef struct {
    int x, y;
    int width, height;
} app_rect_t;

/* 点 */
typedef struct {
    int x, y;
} app_point_t;

/* 大小 */
typedef struct {
    int width, height;
} app_size_t;

/* 窗口 */
typedef struct {
    int id;
    char title[256];
    app_rect_t rect;
    int visible;
    int focused;
    int minimized;
    int maximized;
    app_color_t background_color;
    void *user_data;
} app_window_t;

/* UI组件类型 */
typedef enum {
    WIDGET_BUTTON = 0,
    WIDGET_LABEL,
    WIDGET_TEXTBOX,
    WIDGET_CHECKBOX,
    WIDGET_RADIO,
    WIDGET_SLIDER,
    WIDGET_PROGRESS,
    WIDGET_LIST,
    WIDGET_COMBOBOX,
    WIDGET_MENU,
    WIDGET_TAB,
    WIDGET_IMAGE,
    WIDGET_CUSTOM
} widget_type_t;

/* 前向声明 */
struct app_widget_t;

/* UI组件 */
typedef struct app_widget_t {
    int id;
    widget_type_t type;
    app_rect_t rect;
    char text[256];
    int visible;
    int enabled;
    int focused;
    app_color_t bg_color;
    app_color_t fg_color;
    void *user_data;
    /* 回调 */
    void (*on_click)(struct app_widget_t *widget, void *user_data);
    void (*on_change)(struct app_widget_t *widget, void *user_data);
} app_widget_t;

/* 应用回调 */
typedef struct {
    int (*on_init)(void *user_data);
    int (*on_start)(void *user_data);
    int (*on_stop)(void *user_data);
    int (*on_pause)(void *user_data);
    int (*on_resume)(void *user_data);
    int (*on_event)(app_event_t *event, void *user_data);
    int (*on_update)(float delta_time, void *user_data);
    int (*on_render)(void *user_data);
} app_callbacks_t;

/* 应用配置 */
typedef struct {
    char name[128];
    char version[32];
    char author[128];
    char description[512];
    app_size_t window_size;
    int window_resizable;
    int fullscreen;
    int target_fps;
    void *user_data;
} app_config_t;

/* 应用上下文 */
typedef struct {
    int initialized;
    app_state_t state;
    app_config_t config;
    app_callbacks_t callbacks;
    app_window_t *main_window;
    app_widget_t *widgets[256];
    int widget_count;
    void *user_data;
    uint32_t frame_count;
    float fps;
    uint64_t start_time;
} app_context_t;

/* 函数声明 */

/* 应用生命周期 */
int app_init(app_config_t *config, app_callbacks_t *callbacks);
int app_run(void);
int app_stop(void);
int app_pause(void);
int app_resume(void);
app_state_t app_get_state(void);
app_context_t *app_get_context(void);

/* 窗口管理 */
app_window_t *app_create_window(const char *title, int x, int y,
                                int width, int height);
int app_destroy_window(app_window_t *window);
int app_show_window(app_window_t *window);
int app_hide_window(app_window_t *window);
int app_resize_window(app_window_t *window, int width, int height);
int app_move_window(app_window_t *window, int x, int y);
int app_set_window_title(app_window_t *window, const char *title);
int app_set_window_background(app_window_t *window, app_color_t color);

/* 事件处理 */
int app_poll_event(app_event_t *event);
int app_wait_event(app_event_t *event);
int app_post_event(app_event_t *event);
int app_register_timer(int interval_ms, void (*callback)(int timer_id, void *),
                       void *user_data);
int app_unregister_timer(int timer_id);

/* UI组件 */
app_widget_t *app_create_widget(widget_type_t type, int x, int y,
                                int width, int height, const char *text);
int app_destroy_widget(app_widget_t *widget);
int app_add_widget(app_window_t *window, app_widget_t *widget);
int app_remove_widget(app_window_t *window, app_widget_t *widget);
int app_set_widget_text(app_widget_t *widget, const char *text);
int app_set_widget_position(app_widget_t *widget, int x, int y);
int app_set_widget_size(app_widget_t *widget, int width, int height);
int app_set_widget_visible(app_widget_t *widget, int visible);
int app_set_widget_enabled(app_widget_t *widget, int enabled);
int app_set_widget_colors(app_widget_t *widget,
                          app_color_t bg, app_color_t fg);

/* 绘图API */
int app_draw_clear(app_color_t color);
int app_draw_pixel(int x, int y, app_color_t color);
int app_draw_line(int x1, int y1, int x2, int y2, app_color_t color);
int app_draw_rect(int x, int y, int w, int h, app_color_t color);
int app_draw_fill_rect(int x, int y, int w, int h, app_color_t color);
int app_draw_circle(int cx, int cy, int radius, app_color_t color);
int app_draw_fill_circle(int cx, int cy, int radius, app_color_t color);
int app_draw_text(int x, int y, const char *text, app_color_t color);
int app_draw_text_size(const char *text, int *width, int *height);

/* 工具函数 */
uint32_t app_get_ticks(void);
void app_delay(uint32_t ms);
float app_get_fps(void);
int app_get_version(char *buf, int buf_size);
void *app_alloc(size_t size);
void app_free(void *ptr);

/* 日志 */
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_FATAL
} log_level_t;

int app_log(log_level_t level, const char *tag, const char *format, ...);

#define APP_LOGD(tag, ...) app_log(LOG_DEBUG, tag, __VA_ARGS__)
#define APP_LOGI(tag, ...) app_log(LOG_INFO, tag, __VA_ARGS__)
#define APP_LOGW(tag, ...) app_log(LOG_WARNING, tag, __VA_ARGS__)
#define APP_LOGE(tag, ...) app_log(LOG_ERROR, tag, __VA_ARGS__)
#define APP_LOGF(tag, ...) app_log(LOG_FATAL, tag, __VA_ARGS__)

#endif /* _APP_FRAMEWORK_H */
