/* ============================================================================
 * 赫普OS - 示例应用：Hello World
 * 展示应用开发框架的基本使用方法
 * ========================================================================== */

#include "app_framework.h"
#include <string.h>
#include <stdio.h>

/* 应用配置 */
static app_config_t config = {
    .name = "Hello World",
    .version = "1.0.0",
    .author = "HepuOS Team",
    .description = "一个简单的示例应用，展示应用开发框架的使用",
    .window_size = {400, 300},
    .window_resizable = 1,
    .fullscreen = 0,
    .target_fps = 60,
    .user_data = NULL
};

/* 全局变量 */
static app_window_t *main_window = NULL;
static app_widget_t *label = NULL;
static app_widget_t *button = NULL;
static int click_count = 0;

/* 按钮点击回调 */
static void on_button_click(app_widget_t *widget, void *user_data)
{
    click_count++;
    char text[64];
    snprintf(text, sizeof(text), "点击次数: %d", click_count);
    app_set_widget_text(label, text);
    APP_LOGI("HelloWorld", "Button clicked, count=%d", click_count);
}

/* 初始化回调 */
static int on_init(void *user_data)
{
    APP_LOGI("HelloWorld", "Initializing...");

    /* 创建主窗口 */
    main_window = app_create_window("Hello World", 100, 100, 400, 300);
    if (!main_window) {
        APP_LOGE("HelloWorld", "Failed to create window");
        return -1;
    }

    /* 创建标签 */
    label = app_create_widget(WIDGET_LABEL, 50, 50, 300, 40, "欢迎使用赫普OS！");
    if (!label) {
        APP_LOGE("HelloWorld", "Failed to create label");
        return -1;
    }
    app_set_widget_colors(label, APP_COLOR(240, 240, 240), APP_COLOR(0, 0, 0));

    /* 创建按钮 */
    button = app_create_widget(WIDGET_BUTTON, 150, 150, 100, 40, "点击我");
    if (!button) {
        APP_LOGE("HelloWorld", "Failed to create button");
        return -1;
    }
    button->on_click = on_button_click;
    app_set_widget_colors(button, APP_COLOR(70, 130, 180), APP_COLOR(255, 255, 255));

    APP_LOGI("HelloWorld", "Initialized successfully");
    return 0;
}

/* 启动回调 */
static int on_start(void *user_data)
{
    APP_LOGI("HelloWorld", "App started");
    return 0;
}

/* 停止回调 */
static int on_stop(void *user_data)
{
    APP_LOGI("HelloWorld", "App stopped");
    return 0;
}

/* 事件回调 */
static int on_event(app_event_t *event, void *user_data)
{
    switch (event->type) {
        case EVENT_KEY_DOWN:
            APP_LOGD("HelloWorld", "Key down: %d", event->data.key.key_code);
            if (event->data.key.key_code == 27) {  /* ESC */
                app_stop();
            }
            break;
        case EVENT_WINDOW_CLOSE:
            app_stop();
            break;
        default:
            break;
    }
    return 0;
}

/* 更新回调 */
static int on_update(float delta_time, void *user_data)
{
    return 0;
}

/* 渲染回调 */
static int on_render(void *user_data)
{
    /* 清空背景 */
    app_draw_clear(APP_COLOR(240, 240, 240));

    /* 绘制标题 */
    app_draw_text(150, 20, "Hello World", APP_COLOR(0, 0, 0));

    /* 绘制FPS */
    char fps_text[32];
    snprintf(fps_text, sizeof(fps_text), "FPS: %.1f", app_get_fps());
    app_draw_text(320, 270, fps_text, APP_COLOR(128, 128, 128));

    return 0;
}

/* 应用回调 */
static app_callbacks_t callbacks = {
    .on_init = on_init,
    .on_start = on_start,
    .on_stop = on_stop,
    .on_pause = NULL,
    .on_resume = NULL,
    .on_event = on_event,
    .on_update = on_update,
    .on_render = on_render
};

/* 主函数 */
int main(int argc, char *argv[])
{
    APP_LOGI("HelloWorld", "Starting app...");

    /* 初始化应用 */
    if (app_init(&config, &callbacks) != 0) {
        APP_LOGE("HelloWorld", "Failed to initialize app");
        return 1;
    }

    /* 运行应用 */
    int result = app_run();

    APP_LOGI("HelloWorld", "App exited with code %d", result);
    return result;
}
