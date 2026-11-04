#ifndef WM_H
#define WM_H

#include <stdint.h>
#include "input.h"

#define WM_MAX_WINDOWS 8
#define WM_MENU_ITEMS 5
#define WM_ICON_COUNT 4
#define WM_ICON_X0  10
#define WM_ICON_Y0  20
#define WM_ICON_W   32
#define WM_ICON_H   32
#define WM_ICON_GAP 18
#define WM_MAX_WIDGETS 16
#define WM_MAX_TITLE 32

/* 事件类型 */
typedef enum {
    WM_EVENT_MOUSE_DOWN = 0,
    WM_EVENT_MOUSE_UP,
    WM_EVENT_MOUSE_MOVE,
    WM_EVENT_KEY_DOWN,
    WM_EVENT_KEY_UP,
    WM_EVENT_REDRAW,
    WM_EVENT_RESIZE,
    WM_EVENT_CLOSE
} wm_event_type_t;

/* 鼠标按钮 */
#define WM_MOUSE_LEFT   0x01
#define WM_MOUSE_RIGHT  0x02
#define WM_MOUSE_MIDDLE 0x04

/* 事件结构 */
typedef struct {
    wm_event_type_t type;
    int x, y;           /* 鼠标位置 */
    uint8_t button;     /* 鼠标按钮 */
    char key;           /* 键盘字符 */
    int keycode;        /* 键盘扫描码 */
} wm_event_t;

/* 控件类型 (V2.35 新增列表/复选/进度/滚动条) */
typedef enum {
    WM_WIDGET_BUTTON = 0,
    WM_WIDGET_LABEL,
    WM_WIDGET_INPUT,
    WM_WIDGET_LIST,
    WM_WIDGET_CHECKBOX,
    WM_WIDGET_PROGRESS,
    WM_WIDGET_SCROLLBAR
} wm_widget_type_t;

/* 列表控件条目数据 (最多 16 条, 条目为 const char* 字符串) */
#define WM_LIST_MAX_ITEMS 16
typedef struct {
    const char *items[WM_LIST_MAX_ITEMS];
    int count;
    int selected;   /* 选中索引, -1 无 */
    int scroll;     /* 滚动偏移 (条目数) */
    int item_h;     /* 每条高 (默认 10) */
} wm_list_data_t;

/* 前向声明 */
struct wm_window;
typedef struct wm_window wm_window_t;

/* 控件结构 */
typedef struct wm_widget {
    wm_widget_type_t type;
    int x, y, w, h;
    char text[32];
    int pressed;        /* 按钮按下状态 */
    int focused;        /* 输入框焦点 */
    /* 回调 */
    void (*on_click)(wm_window_t *win, struct wm_widget *widget);
    void *user_data;
    /* V2.35 扩展: 新控件数据 */
    union {
        wm_list_data_t list;  /* LIST */
        int checked;          /* CHECKBOX: 0/1 */
        int progress;         /* PROGRESS: 0-100 */
        int scroll_pos;       /* SCROLLBAR: 滑块位置 */
    } u;
} wm_widget_t;

/* 窗口事件回调 */
typedef void (*wm_event_cb_t)(wm_window_t *win, wm_event_t *event);

/* 窗口结构 */
struct wm_window {
    int used;
    int x, y, w, h;
    char title[WM_MAX_TITLE];
    uint8_t bg_color;
    uint8_t title_color;
    uint8_t title_text_color;

    /* 控件 */
    wm_widget_t widgets[WM_MAX_WIDGETS];
    int widget_count;

    /* 事件回调 */
    wm_event_cb_t event_cb;
    void (*draw_cb)(wm_window_t *win);  /* V2.38: 窗口自定义内容绘制 */

    /* 拖动状态 */
    int dragging;
    int drag_offset_x, drag_offset_y;

    /* Z-order (越大越靠前) */
    int z_order;

    /* 窗口状态 */
    int visible;
    int minimized;
    /* V2.35: 最大化与模态 */
    int maximized;
    int saved_x, saved_y, saved_w, saved_h;
    int modal;          /* 模态窗口: 阻止底层窗口事件 */

    void *user_data;
};

/* 初始化窗口管理器 */
void wm_init(void);

/* 创建窗口 */
wm_window_t *wm_create_window(const char *title, int x, int y, int w, int h);

/* 销毁窗口 */
void wm_destroy_window(wm_window_t *win);

/* 添加控件 */
wm_widget_t *wm_add_button(wm_window_t *win, int x, int y, int w, int h,
                            const char *label, void (*on_click)(wm_window_t*, wm_widget_t*));
wm_widget_t *wm_add_label(wm_window_t *win, int x, int y, const char *text);
wm_widget_t *wm_add_input(wm_window_t *win, int x, int y, int w, const char *text);
/* V2.35: 新控件 */
wm_widget_t *wm_add_list(wm_window_t *win, int x, int y, int w, int h, int item_h);
wm_widget_t *wm_add_checkbox(wm_window_t *win, int x, int y, const char *text, int checked);
wm_widget_t *wm_add_progress(wm_window_t *win, int x, int y, int w, int h, int percent);
wm_widget_t *wm_add_scrollbar(wm_window_t *win, int x, int y, int w, int h, int max, int pos);

/* V2.35: 最大化切换 + 模态 */
void wm_maximize_toggle(wm_window_t *win);
void wm_set_modal(wm_window_t *win, int modal);

/* 获取当前模态窗口, 无则 NULL */
wm_window_t *wm_get_modal(void);

/* 绘制所有窗口 */
void wm_draw_all(void);

/* 绘制单个窗口 */
void wm_draw_window(wm_window_t *win);

/* 处理事件 */
void wm_handle_event(wm_event_t *event);

/* 查找指定位置的窗口 */
wm_window_t *wm_window_at(int x, int y);

/* 将窗口置顶 */
void wm_bring_to_front(wm_window_t *win);

/* 主循环 */
void wm_main_loop(void);

/* 获取桌面窗口 */
wm_window_t *wm_get_desktop(void);

#endif /* WM_H */

/* V4.40-E85: 手势事件处理 (wm 主循环 + shell 文本验证) */
void wm_handle_gesture(gesture_event_t *ge);

/* V4.41-E86: 窗口缩放 (PINCH/ZOOM 手势驱动) */
void wm_resize_window(wm_window_t *win, int dw, int dh);
