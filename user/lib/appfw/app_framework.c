/* ============================================================================
 * 赫普OS - 应用开发框架实现
 * V8.87: 初始实现
 * ========================================================================== */

#include "app_framework.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

/* 简单字符串工具 */
static int af_strlen(const char *s) {
    int n = 0; while (s && s[n]) n++; return n;
}
static int af_strcmp(const char *a, const char *b) {
    if (!a || !b) return -1;
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static void af_strncpy(char *dst, const char *src, int n) {
    if (!dst || !src || n <= 0) return;
    int i = 0; while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* 全局应用上下文 */
static app_context_t g_app;
static int g_initialized = 0;

/* 事件队列 */
#define MAX_EVENTS 64
static app_event_t g_event_queue[MAX_EVENTS];
static int g_event_head = 0;
static int g_event_tail = 0;

/* 定时器 */
#define MAX_TIMERS 16
typedef struct {
    int active;
    int interval_ms;
    uint32_t last_trigger;
    void (*callback)(int, void *);
    void *user_data;
} app_timer_t;
static app_timer_t g_timers[MAX_TIMERS];

/* ============================================================================
 * 应用生命周期
 * ========================================================================== */

int app_init(app_config_t *config, app_callbacks_t *callbacks)
{
    if (g_initialized) return -1;
    if (!config) return -1;

    memset(&g_app, 0, sizeof(app_context_t));
    g_app.config = *config;
    if (callbacks) {
        g_app.callbacks = *callbacks;
    }
    g_app.user_data = config->user_data;
    g_app.state = APP_STATE_CREATED;
    g_app.frame_count = 0;
    g_app.fps = 0;
    g_app.start_time = 0;

    /* 初始化事件队列 */
    g_event_head = g_event_tail = 0;

    /* 初始化定时器 */
    memset(g_timers, 0, sizeof(g_timers));

    g_initialized = 1;

    /* 调用初始化回调 */
    if (g_app.callbacks.on_init) {
        g_app.state = APP_STATE_INITIALIZING;
        if (g_app.callbacks.on_init(g_app.user_data) != 0) {
            g_app.state = APP_STATE_ERROR;
            return -1;
        }
    }

    return 0;
}

int app_run(void)
{
    if (!g_initialized) return -1;
    if (g_app.state != APP_STATE_CREATED &&
        g_app.state != APP_STATE_INITIALIZING) {
        return -1;
    }

    g_app.state = APP_STATE_RUNNING;
    g_app.start_time = app_get_ticks();

    /* 调用启动回调 */
    if (g_app.callbacks.on_start) {
        g_app.callbacks.on_start(g_app.user_data);
    }

    /* 主循环 */
    uint32_t last_time = app_get_ticks();
    while (g_app.state == APP_STATE_RUNNING) {
        uint32_t current_time = app_get_ticks();
        float delta_time = (current_time - last_time) / 1000.0f;
        last_time = current_time;

        /* 处理事件 */
        app_event_t event;
        while (app_poll_event(&event)) {
            if (g_app.callbacks.on_event) {
                g_app.callbacks.on_event(&event, g_app.user_data);
            }
        }

        /* 检查定时器 */
        for (int i = 0; i < MAX_TIMERS; i++) {
            if (g_timers[i].active &&
                current_time - g_timers[i].last_trigger >=
                (uint32_t)g_timers[i].interval_ms) {
                g_timers[i].last_trigger = current_time;
                if (g_timers[i].callback) {
                    g_timers[i].callback(i, g_timers[i].user_data);
                }
            }
        }

        /* 更新 */
        if (g_app.callbacks.on_update) {
            g_app.callbacks.on_update(delta_time, g_app.user_data);
        }

        /* 渲染 */
        if (g_app.callbacks.on_render) {
            g_app.callbacks.on_render(g_app.user_data);
        }

        g_app.frame_count++;

        /* 计算FPS */
        if (current_time - g_app.start_time > 0) {
            g_app.fps = g_app.frame_count * 1000.0f /
                        (current_time - g_app.start_time);
        }

        /* 简单的帧率控制 */
        if (g_app.config.target_fps > 0) {
            uint32_t frame_time = 1000 / g_app.config.target_fps;
            uint32_t elapsed = app_get_ticks() - current_time;
            if (elapsed < frame_time) {
                app_delay(frame_time - elapsed);
            }
        }
    }

    /* 调用停止回调 */
    if (g_app.callbacks.on_stop) {
        g_app.callbacks.on_stop(g_app.user_data);
    }

    g_app.state = APP_STATE_STOPPED;
    return 0;
}

int app_stop(void)
{
    if (!g_initialized) return -1;
    if (g_app.state == APP_STATE_RUNNING) {
        g_app.state = APP_STATE_STOPPING;
    }
    return 0;
}

int app_pause(void)
{
    if (!g_initialized || g_app.state != APP_STATE_RUNNING) return -1;
    g_app.state = APP_STATE_PAUSED;
    if (g_app.callbacks.on_pause) {
        g_app.callbacks.on_pause(g_app.user_data);
    }
    return 0;
}

int app_resume(void)
{
    if (!g_initialized || g_app.state != APP_STATE_PAUSED) return -1;
    g_app.state = APP_STATE_RUNNING;
    if (g_app.callbacks.on_resume) {
        g_app.callbacks.on_resume(g_app.user_data);
    }
    return 0;
}

app_state_t app_get_state(void)
{
    return g_initialized ? g_app.state : APP_STATE_ERROR;
}

app_context_t *app_get_context(void)
{
    return &g_app;
}

/* ============================================================================
 * 窗口管理
 * ========================================================================== */

app_window_t *app_create_window(const char *title, int x, int y,
                                int width, int height)
{
    if (!g_initialized) return NULL;

    app_window_t *window = (app_window_t *)malloc(sizeof(app_window_t));
    if (!window) return NULL;

    memset(window, 0, sizeof(app_window_t));
    window->id = 1;  /* 简化：单窗口 */
    af_strncpy(window->title, title ? title : "HepuOS App", 256);
    window->rect.x = x;
    window->rect.y = y;
    window->rect.width = width;
    window->rect.height = height;
    window->visible = 1;
    window->focused = 1;
    window->background_color = APP_COLOR(240, 240, 240);

    if (!g_app.main_window) {
        g_app.main_window = window;
    }

    return window;
}

int app_destroy_window(app_window_t *window)
{
    if (!window) return -1;
    if (g_app.main_window == window) {
        g_app.main_window = NULL;
    }
    free(window);
    return 0;
}

int app_show_window(app_window_t *window)
{
    if (!window) return -1;
    window->visible = 1;
    return 0;
}

int app_hide_window(app_window_t *window)
{
    if (!window) return -1;
    window->visible = 0;
    return 0;
}

int app_resize_window(app_window_t *window, int width, int height)
{
    if (!window) return -1;
    window->rect.width = width;
    window->rect.height = height;
    return 0;
}

int app_move_window(app_window_t *window, int x, int y)
{
    if (!window) return -1;
    window->rect.x = x;
    window->rect.y = y;
    return 0;
}

int app_set_window_title(app_window_t *window, const char *title)
{
    if (!window || !title) return -1;
    af_strncpy(window->title, title, 256);
    return 0;
}

int app_set_window_background(app_window_t *window, app_color_t color)
{
    if (!window) return -1;
    window->background_color = color;
    return 0;
}

/* ============================================================================
 * 事件处理
 * ============================================================================ */

int app_poll_event(app_event_t *event)
{
    if (!event || g_event_head == g_event_tail) return 0;
    *event = g_event_queue[g_event_head];
    g_event_head = (g_event_head + 1) % MAX_EVENTS;
    return 1;
}

int app_wait_event(app_event_t *event)
{
    while (!app_poll_event(event)) {
        app_delay(10);
    }
    return 1;
}

int app_post_event(app_event_t *event)
{
    if (!event) return -1;
    int next = (g_event_tail + 1) % MAX_EVENTS;
    if (next == g_event_head) return -1;  /* 队列满 */
    g_event_queue[g_event_tail] = *event;
    g_event_tail = next;
    return 0;
}

int app_register_timer(int interval_ms,
                       void (*callback)(int timer_id, void *),
                       void *user_data)
{
    if (!callback || interval_ms <= 0) return -1;
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (!g_timers[i].active) {
            g_timers[i].active = 1;
            g_timers[i].interval_ms = interval_ms;
            g_timers[i].last_trigger = app_get_ticks();
            g_timers[i].callback = callback;
            g_timers[i].user_data = user_data;
            return i;
        }
    }
    return -1;
}

int app_unregister_timer(int timer_id)
{
    if (timer_id < 0 || timer_id >= MAX_TIMERS) return -1;
    g_timers[timer_id].active = 0;
    return 0;
}

/* ============================================================================
 * UI组件
 * ============================================================================ */

app_widget_t *app_create_widget(widget_type_t type, int x, int y,
                                int width, int height, const char *text)
{
    if (!g_initialized || g_app.widget_count >= 256) return NULL;

    app_widget_t *widget = (app_widget_t *)malloc(sizeof(app_widget_t));
    if (!widget) return NULL;

    memset(widget, 0, sizeof(app_widget_t));
    widget->id = g_app.widget_count;
    widget->type = type;
    widget->rect.x = x;
    widget->rect.y = y;
    widget->rect.width = width;
    widget->rect.height = height;
    af_strncpy(widget->text, text ? text : "", 256);
    widget->visible = 1;
    widget->enabled = 1;
    widget->bg_color = APP_COLOR(255, 255, 255);
    widget->fg_color = APP_COLOR(0, 0, 0);

    g_app.widgets[g_app.widget_count++] = widget;
    return widget;
}

int app_destroy_widget(app_widget_t *widget)
{
    if (!widget) return -1;
    for (int i = 0; i < g_app.widget_count; i++) {
        if (g_app.widgets[i] == widget) {
            for (int j = i; j < g_app.widget_count - 1; j++) {
                g_app.widgets[j] = g_app.widgets[j + 1];
            }
            g_app.widget_count--;
            free(widget);
            return 0;
        }
    }
    return -1;
}

int app_add_widget(app_window_t *window, app_widget_t *widget)
{
    if (!window || !widget) return -1;
    /* 简化：组件全局管理 */
    return 0;
}

int app_remove_widget(app_window_t *window, app_widget_t *widget)
{
    if (!window || !widget) return -1;
    return 0;
}

int app_set_widget_text(app_widget_t *widget, const char *text)
{
    if (!widget || !text) return -1;
    af_strncpy(widget->text, text, 256);
    return 0;
}

int app_set_widget_position(app_widget_t *widget, int x, int y)
{
    if (!widget) return -1;
    widget->rect.x = x;
    widget->rect.y = y;
    return 0;
}

int app_set_widget_size(app_widget_t *widget, int width, int height)
{
    if (!widget) return -1;
    widget->rect.width = width;
    widget->rect.height = height;
    return 0;
}

int app_set_widget_visible(app_widget_t *widget, int visible)
{
    if (!widget) return -1;
    widget->visible = visible;
    return 0;
}

int app_set_widget_enabled(app_widget_t *widget, int enabled)
{
    if (!widget) return -1;
    widget->enabled = enabled;
    return 0;
}

int app_set_widget_colors(app_widget_t *widget,
                          app_color_t bg, app_color_t fg)
{
    if (!widget) return -1;
    widget->bg_color = bg;
    widget->fg_color = fg;
    return 0;
}

/* ============================================================================
 * 绘图API（简化实现）
 * ========================================================================== */

int app_draw_clear(app_color_t color)
{
    /* 简化：实际应该调用系统绘图API */
    return 0;
}

int app_draw_pixel(int x, int y, app_color_t color)
{
    return 0;
}

int app_draw_line(int x1, int y1, int x2, int y2, app_color_t color)
{
    return 0;
}

int app_draw_rect(int x, int y, int w, int h, app_color_t color)
{
    return 0;
}

int app_draw_fill_rect(int x, int y, int w, int h, app_color_t color)
{
    return 0;
}

int app_draw_circle(int cx, int cy, int radius, app_color_t color)
{
    return 0;
}

int app_draw_fill_circle(int cx, int cy, int radius, app_color_t color)
{
    return 0;
}

int app_draw_text(int x, int y, const char *text, app_color_t color)
{
    return 0;
}

int app_draw_text_size(const char *text, int *width, int *height)
{
    if (!text) return -1;
    if (width) *width = af_strlen(text) * 8;  /* 简化：8像素宽 */
    if (height) *height = 16;  /* 简化：16像素高 */
    return 0;
}

/* ============================================================================
 * 工具函数
 * ========================================================================== */

uint32_t app_get_ticks(void)
{
    /* 简化：实际应该读取系统定时器 */
    static uint32_t ticks = 0;
    return ticks++;
}

void app_delay(uint32_t ms)
{
    /* 简化：实际应该调用系统延时 */
}

float app_get_fps(void)
{
    return g_initialized ? g_app.fps : 0;
}

int app_get_version(char *buf, int buf_size)
{
    if (!buf || buf_size <= 0) return -1;
    af_strncpy(buf, APPFW_VERSION, buf_size);
    return 0;
}

void *app_alloc(size_t size)
{
    return malloc(size);
}

void app_free(void *ptr)
{
    free(ptr);
}

/* ============================================================================
 * 日志
 * ========================================================================== */

int app_log(log_level_t level, const char *tag, const char *format, ...)
{
    const char *level_str[] = {"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
    if (level < 0 || level > LOG_FATAL) level = LOG_DEBUG;

    fprintf(stderr, "[%s] %s: ", level_str[level], tag ? tag : "APP");

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);

    fprintf(stderr, "\n");
    return 0;
}
