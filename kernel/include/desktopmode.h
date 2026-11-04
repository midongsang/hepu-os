#ifndef DESKTOPMODE_H
#define DESKTOPMODE_H

#include <stdint.h>

#define DM_MAX_DISPLAYS 4
#define DM_MAX_WINDOWS 64
#define DM_MAX_TASKBAR_ITEMS 32
#define DM_MAX_DESKTOP_ICONS 64
#define DM_MAX_WORKSPACES 4

/* 显示模式 */
typedef enum {
    DISPLAY_MODE_PHONE = 0,    /* 手机模式 */
    DISPLAY_MODE_TABLET,       /* 平板模式 */
    DISPLAY_MODE_DESKTOP,      /* 桌面模式 */
    DISPLAY_MODE_TV,           /* 电视模式 */
    DISPLAY_MODE_CAR,          /* 车载模式 */
} dm_display_mode_t;

/* 显示设备 */
typedef struct {
    int used;
    int id;
    char name[64];
    int width, height;
    int dpi;
    int refresh_rate;
    int is_primary;
    int connected;
    int rotation;  /* 0,90,180,270 */
    int brightness;
} display_device_t;

/* 窗口状态 (桌面模式) */
typedef struct {
    int used;
    int id;
    char title[128];
    int x, y, width, height;
    int prev_x, prev_y, prev_w, prev_h;  /* 最大化前的位置 */
    int minimized;
    int maximized;
    int fullscreen;
    int always_on_top;
    int resizable;
    int focusable;
    int visible;
    int workspace;
    int z_order;
    int opacity;  /* 0-255 */
    char app_id[64];
} dm_window_t;

/* 任务栏项目 */
typedef struct {
    int used;
    int id;
    char title[128];
    int window_id;
    int pinned;
    int running;
    int attention;  /* 需要关注 (闪烁) */
} taskbar_item_t;

/* 桌面图标 */
typedef struct {
    int used;
    int id;
    char name[64];
    char app_id[64];
    int x, y;
    int icon_id;
    int selected;
} desktop_icon_t;

/* 工作区 */
typedef struct {
    int id;
    char name[32];
    int window_count;
    int active;
    char wallpaper[256];
} workspace_t;

/* 鼠标光标类型 */
typedef enum {
    CURSOR_ARROW = 0,
    CURSOR_IBEAM,
    CURSOR_WAIT,
    CURSOR_CROSSHAIR,
    CURSOR_HAND,
    CURSOR_RESIZE_EW,
    CURSOR_RESIZE_NS,
    CURSOR_RESIZE_NWSE,
    CURSOR_RESIZE_NESW,
    CURSOR_MOVE,
    CURSOR_FORBIDDEN,
} cursor_type_t;

/* 鼠标状态 */
typedef struct {
    int x, y;
    int prev_x, prev_y;
    int left_button, right_button, middle_button;
    int wheel_delta;
    cursor_type_t cursor;
    int cursor_visible;
    int double_click;
    uint64_t last_click_time;
} mouse_state_t;

/* 键盘状态 */
typedef struct {
    int keys[256];
    int modifiers;  /* Ctrl/Alt/Shift/Super */
    int caps_lock;
    int num_lock;
    int scroll_lock;
    int repeat_rate;
    int repeat_delay;
} keyboard_state_t;

/* 剪贴板 (桌面模式增强) */
typedef struct {
    char text[4096];
    int has_text;
    int has_image;
    uint8_t image_data[65536];
    int image_width, image_height;
    uint64_t timestamp;
} dm_clipboard_t;

/* 桌面模式状态 */
typedef struct {
    int initialized;
    dm_display_mode_t mode;
    display_device_t displays[DM_MAX_DISPLAYS];
    int display_count;
    int primary_display;
    /* 窗口管理 */
    dm_window_t windows[DM_MAX_WINDOWS];
    int window_count;
    int focused_window;
    int z_order_counter;
    /* 任务栏 */
    taskbar_item_t taskbar[DM_MAX_TASKBAR_ITEMS];
    int taskbar_count;
    int taskbar_height;
    int taskbar_position;  /* 0=底部,1=顶部,2=左,3=右 */
    int taskbar_autohide;
    /* 桌面 */
    desktop_icon_t desktop_icons[DM_MAX_DESKTOP_ICONS];
    int desktop_icon_count;
    /* 工作区 */
    workspace_t workspaces[DM_MAX_WORKSPACES];
    int current_workspace;
    /* 输入 */
    mouse_state_t mouse;
    keyboard_state_t keyboard;
    /* 剪贴板 */
    dm_clipboard_t clipboard;
    /* 设置 */
    int window_snapping;  /* 窗口吸附 */
    int window_animations;
    int show_desktop_icons;
    int dark_mode;
    int accent_color;
} desktop_mode_t;

/* 初始化桌面模式 */
void desktopmode_init(void);

/* 显示管理 */
int dm_add_display(const char *name, int width, int height, int dpi, int primary);
int dm_remove_display(int display_id);
int dm_set_primary(int display_id);
int dm_get_display(int display_id, display_device_t *display);
int dm_list_displays(display_device_t *displays, int max);
int dm_set_resolution(int display_id, int width, int height);
int dm_set_rotation(int display_id, int rotation);
int dm_set_brightness(int display_id, int brightness);

/* 模式切换 */
int dm_set_mode(dm_display_mode_t mode);
dm_display_mode_t dm_get_mode(void);
int dm_enter_desktop_mode(void);
int dm_exit_desktop_mode(void);

/* 窗口管理 */
int dm_window_create(const char *title, int x, int y, int w, int h);
int dm_window_destroy(int window_id);
int dm_window_move(int window_id, int x, int y);
int dm_window_resize(int window_id, int w, int h);
int dm_window_focus(int window_id);
int dm_window_minimize(int window_id);
int dm_window_maximize(int window_id);
int dm_window_restore(int window_id);
int dm_window_close(int window_id);
int dm_window_set_always_on_top(int window_id, int on_top);
int dm_window_set_opacity(int window_id, int opacity);
int dm_window_get(int window_id, dm_window_t *window);
int dm_list_windows(dm_window_t *windows, int max);
int dm_get_focused_window(void);

/* 任务栏 */
int dm_taskbar_add(const char *title, int window_id, int pinned);
int dm_taskbar_remove(int item_id);
int dm_taskbar_set_position(int position);
int dm_taskbar_set_autohide(int autohide);
int dm_taskbar_get(taskbar_item_t *items, int max);

/* 桌面图标 */
int dm_desktop_add_icon(const char *name, const char *app_id, int x, int y);
int dm_desktop_remove_icon(int icon_id);
int dm_desktop_get_icons(desktop_icon_t *icons, int max);

/* 工作区 */
int dm_workspace_switch(int workspace_id);
int dm_workspace_move_window(int window_id, int workspace_id);
int dm_get_current_workspace(void);
int dm_list_workspaces(workspace_t *workspaces, int max);

/* 输入处理 */
int dm_mouse_move(int x, int y);
int dm_mouse_button(int button, int pressed);
int dm_mouse_wheel(int delta);
int dm_key_press(int key, int pressed);
int dm_set_cursor(cursor_type_t cursor);
int dm_get_mouse(mouse_state_t *mouse);
int dm_get_keyboard(keyboard_state_t *keyboard);

/* 剪贴板 */
int dm_clipboard_set_text(const char *text);
int dm_clipboard_get_text(char *text, int len);
int dm_clipboard_set_image(const uint8_t *data, int w, int h);
int dm_clipboard_has_text(void);
int dm_clipboard_clear(void);

/* 窗口吸附 */
int dm_snap_window(int window_id, int edge);  /* 0=左,1=右,2=上,3=下 */

/* 渲染 */
int dm_render(void);
void dm_tick(void);

/* 统计 */
int dm_window_count(void);
int dm_display_count(void);

#endif /* DESKTOPMODE_H */
