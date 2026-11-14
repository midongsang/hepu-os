/* ============================================================================
 * 赫普OS - Hepu Desktop 桌面环境
 * 复现 Quickshell 核心功能：面板、小部件、启动器、窗口管理
 * V0.10.160
 * ========================================================================== */

#ifndef HPDESK_H
#define HPDESK_H

#include <stdint.h>
#include <stddef.h>

/* 最大配置 */
#define HS_MAX_WIDGETS     16
#define HS_MAX_WINDOWS     32
#define HS_MAX_WORKSPACES  9
#define HS_MAX_APPS        32
#define HS_PANEL_HEIGHT    28
#define HS_MAX_TRAY_ICONS  16
#define HS_MAX_NOTIFICATIONS 32

/* 主题类型 */
typedef enum {
    HS_THEME_LIGHT = 0,  /* 浅色主题（Quickshell风格） */
    HS_THEME_DARK = 1,   /* 深色主题 */
    HS_THEME_MONO = 2,   /* 极简黑白风格 */
} hs_theme_t;

/* 主题颜色 */
typedef struct {
    uint32_t bg;           /* 背景色 */
    uint32_t panel_bg;     /* 面板背景 */
    uint32_t panel_fg;     /* 面板文字 */
    uint32_t border;       /* 边框色 */
    uint32_t border_strong;/* 焦点/强调边框(比 border 深一档, 规格化对比度) */
    uint32_t accent;       /* 强调色 */
    uint32_t accent_pressed;/* 按下/选中态(深一档) */
    uint32_t window_bg;    /* 窗口背景(surface) */
    uint32_t window_fg;    /* 窗口文字(主文本) */
    uint32_t text_secondary;/* 次要文本 */
    uint32_t titlebar_bg;  /* 标题栏背景 */
    uint32_t workspace_active;   /* 活动工作区 */
    uint32_t workspace_inactive; /* 非活动工作区 */
} hs_theme_colors_t;

/* V0.13.8 S0: UI 设计令牌 — 间距(5档)/圆角(4档)/字号(7档) 全系统统一 */
/* 系统版本号唯一宏 —— 界面/终端/任务管理器都引用它, 别再多点硬编码 */
#define HEPU_OS_VERSION "0.59.0"

#define HS_SP_1 4
#define HS_SP_2 8
#define HS_SP_3 12
#define HS_SP_4 16
#define HS_SP_5 24
#define HS_RD_BTN   4   /* 按钮/勾选 */
#define HS_RD_INPUT 8   /* 输入框/滚动条滑块 */
#define HS_RD_WIN   10  /* 窗口/浮层(对齐 Clavis cornerRadius=10) */
#define HS_RD_CARD  12  /* 卡片/启动器 */
#define HS_FS_10 10     /* 角标 */
#define HS_FS_12 12     /* 次要文本 */
#define HS_FS_14 14     /* 正文 */
#define HS_FS_16 16     /* 标题 */
#define HS_FS_20 20     /* 窗口标题 */
#define HS_FS_28 28     /* 大时钟 */
#define HS_FS_48 48     /* 锁屏 */

/* 小部件类型 */
typedef enum {
    HS_WIDGET_CLOCK = 0,
    HS_WIDGET_CPU,
    HS_WIDGET_MEMORY,
    HS_WIDGET_WORKSPACES,
    HS_WIDGET_BATTERY,
    HS_WIDGET_VOLUME,
    HS_WIDGET_NETWORK,
    HS_WIDGET_CUSTOM,
} hs_widget_type_t;

/* 小部件 */
typedef struct {
    hs_widget_type_t type;
    int x;
    int y;
    int width;
    int height;
    char text[64];
    void (*render)(void *widget, int x, int y);
    void (*update)(void *widget);
} hs_widget_t;

/* 面板位置 */
typedef enum {
    HS_PANEL_TOP = 0,
    HS_PANEL_BOTTOM = 1,
} hs_panel_pos_t;

/* 面板 */
typedef struct {
    hs_panel_pos_t position;
    int height;
    int widget_count;
    hs_widget_t widgets[HS_MAX_WIDGETS];
} hs_panel_t;

/* 应用程序 */
typedef struct {
    char name[32];
    char command[64];
    char icon[8];  /* 简单字符图标 */
} hs_app_t;

/* 系统托盘图标 */
typedef struct {
    char name[32];
    char icon[8];
    int visible;
    int active;
    void (*on_click)(void);
} hs_tray_icon_t;

/* 通知 */
typedef struct {
    int id;
    char title[64];
    char body[128];
    int urgency;  /* 0=low, 1=normal, 2=critical */
    int timestamp;
    int visible;
    int timeout;  /* 0=persistent */
    uint32_t born_frame; /* 发送时的渲染帧号 (到期/滑入/进度条时基) */
} hs_notification_t;

/* 通知中心 */
typedef struct {
    hs_notification_t notifications[HS_MAX_NOTIFICATIONS];
    int count;
    int visible;
    int next_id;
} hs_notification_center_t;

/* 媒体播放器状态 */
typedef struct {
    int playing;
    int volume;
    int position;
    int duration;
    char title[64];
    char artist[64];
} hs_media_player_t;

/* 系统状态 */
typedef struct {
    int volume;         /* 0-100 */
    int brightness;     /* 0-100 */
    int battery_percent;
    int battery_charging;
    int network_connected;
    char network_type[16];  /* "ethernet"/"wifi"/"none" */
    int cpu_usage;
    int mem_usage;
} hs_system_status_t;

/* 窗口类型（对标Quickshell） */
typedef enum {
    HS_WINDOW_PANEL = 0,    /* 面板窗口：顶部/底部栏，固定位置 */
    HS_WINDOW_FLOATING,     /* 浮动窗口：可拖动/缩放 */
    HS_WINDOW_POPUP,        /* 弹出窗口：通知/菜单，临时显示 */
} hs_window_type_t;

/* 窗口状态 */
typedef enum {
    HS_WINDOW_STATE_NORMAL = 0,
    HS_WINDOW_STATE_MAXIMIZED,
    HS_WINDOW_STATE_MINIMIZED,
    HS_WINDOW_STATE_FULLSCREEN,
} hs_window_state_t;

/* 布局模式 */
typedef enum {
    HS_LAYOUT_TILE = 0,      /* 平铺布局 */
    HS_LAYOUT_FLOATING,      /* 浮动布局 */
    HS_LAYOUT_TAB,           /* 标签布局 */
    HS_LAYOUT_SINGLE,        /* 单窗布局 */
    HS_LAYOUT_BSP,           /* BSP二分布局 */
} hs_layout_mode_t;

/* 窗口规则 */
typedef struct {
    char app_name[32];
    hs_layout_mode_t layout;
    int workspace;
    int floating;
    int fullscreen;
} hs_window_rule_t;

/* 窗口 */
typedef struct hs_window {
    int id;
    hs_window_type_t type;
    hs_window_state_t state;
    char title[64];
    int x, y, width, height;
    int prev_x, prev_y, prev_w, prev_h;  /* 最大化前的位置 */
    int visible;
    int focus;
    int focus_seq;    /* P17.2: LRU 时间戳 (窗口切换器排序) */
    uint64_t anim_start;  /* 打开动画起始 tick (PIT, 1ms) */
    uint32_t anim_dur;    /* 打开动画时长(ms), 200=Clavis small */
    int workspace;
    int always_on_top;
    uint32_t bg_color;
    uint32_t border_color;
    /* 拖动状态 */
    int dragging;
    int drag_offset_x, drag_offset_y;
    /* Surface 雏形(V0.59.7): 拖动期间用的窗口内容快照(窗口矩形内像素, w*h*4)。
     * 拖动时窗口内容不变, 每帧重画(尤其文字栅格)纯属浪费; 有快照就只 blit。
     * 只在拖动期间存在, 拖动结束释放。分配失败时为 0, 自动退回重画路径。 */
    void *surface;
    int surface_w, surface_h;
    /* 内容渲染回调 */
    void (*render)(struct hs_window *win);
} hs_window_t;

/* 窗口管理器 */
typedef struct {
    int window_count;
    hs_window_t windows[HS_MAX_WINDOWS];
    int focus_window;
    int z_order[HS_MAX_WINDOWS];  /* 窗口层级，从底到顶 */
    int z_count;
    int cascade_index;            /* V0.13.13: 自动级联定位游标 */
} hs_window_manager_t;

/* 工作区 */
typedef struct {
    int id;
    char name[16];
    int window_count;
    hs_layout_mode_t layout;
} hs_workspace_t;

/* 面板ID */
typedef enum {
    HS_PANEL_TOP_BAR = 0,
    HS_PANEL_DOCK,
    HS_PANEL_SIDEBAR,
    HS_PANEL_CONTROL_CENTER,
    HS_PANEL_DESKTOP_CLOCK,
    HS_PANEL_WEATHER,
    HS_PANEL_DYNAMIC_ISLAND,
    HS_PANEL_COUNT
} hs_panel_id_t;

/* 面板注册表项 */
typedef struct {
    const char *name;
    int enabled;
    void (*render)(void);
} hs_panel_registry_t;

/* Hepu Desktop 主结构 */
typedef struct {
    uint32_t canary_start;  /* 头部canary，检测结构前越界写入 */
    int initialized;
    hs_theme_t theme;
    hs_theme_colors_t colors;
    uint32_t theme_source;  /* 当前取色器源色（matugen seed） */
    hs_panel_t top_panel;
    hs_panel_t bottom_panel;
    hs_workspace_t workspaces[HS_MAX_WORKSPACES];
    int current_workspace;
    hs_window_t windows[HS_MAX_WINDOWS];
    int window_count;
    hs_app_t apps[HS_MAX_APPS];
    int app_count;
    int launcher_visible;
    char launcher_input[64];
    int launcher_cursor;
    int selected_app;
    int screen_width;
    int screen_height;
    /* 自适应缩放因子（基准1024x768） */
    float scale_x;
    float scale_y;
    float scale;      /* min(scale_x, scale_y)，用于字体和正方形元素 */
    /* UI配置（从配置文件加载，基准1024x768坐标） */
    struct {
        int clock_x, clock_y;
        int clock_font_size;
        int date_font_size;
        int weather_margin_right, weather_margin_bottom;
        int weather_width, weather_height;
        int weather_temp_font, weather_desc_font;
        int sidebar_x, sidebar_y, sidebar_width, sidebar_height;
        int cc_margin_right, cc_y, cc_width, cc_height;
        int bar_height;
        int island_width, island_height, island_y, island_font_size;
        int dock_width, dock_height, dock_margin_bottom, dock_icon_size;
        int corner_radius;
        int window_shadow;   /* 窗口投影开关(Display 页可关) */
        int cursor_theme;    /* 0=经典(素材包 xcur), 1=Modern(内置) */
        int font_small, font_normal, font_large;
    } ui;
    /* 系统托盘 */
    hs_tray_icon_t tray_icons[HS_MAX_TRAY_ICONS];
    int tray_count;
    /* 通知中心 */
    hs_notification_center_t notifications;
    /* 媒体播放器 */
    hs_media_player_t media;
    /* 系统状态 */
    hs_system_status_t status;
    /* 面板注册表（放在结构内避免静态变量内存冲突） */
    hs_panel_registry_t panel_registry[HS_PANEL_COUNT];
    /* 鼠标位置（用于Dock悬停弹出等交互） */
    int mouse_x;
    int mouse_y;
    int mouse_wheel;  /* V0.13.5: 最近滚轮增量 (滚动/缩放预留) */
    /* 控制中心状态 */
    struct {
        int toggles[6];  /* WiFi, BT, Air, DND, Loc, Torch */
        int volume;      /* 0-100 */
        int brightness;  /* 0-100 */
    } cc;
    uint32_t canary_end;  /* 尾部canary，检测结构后越界写入 */
} hpdesk_t;

/* 全局实例 */
extern hpdesk_t g_shell;

/* 面板注册表API */
void hs_panel_registry_init(void);
int hs_panel_set_enabled(const char *name, int enabled);
int hs_panel_is_enabled(const char *name);
void hs_panel_render_all(void);

/* API */
int hpdesk_init(int width, int height);
void hpdesk_run(void);
void hpdesk_render(void);

/* P0: 标记内容层脏, 下一帧重绘(交互/动画/数据变化时调用) */
void hpdesk_mark_dirty(void);
void hpdesk_handle_key(int key);
void hpdesk_update_mouse(int x, int y);
void hpdesk_handle_click(int x, int y);
void hpdesk_handle_right_click(int x, int y);
void hpdesk_mouse_down(int x, int y);
void hpdesk_mouse_up(int x, int y);
void hpdesk_update_wheel(int delta);  /* V0.13.5: 滚轮累积 (滚动/缩放预留) */

/* P3 组合快捷键钩子 (hsinput 引擎调用) */
void hpdesk_lock(void);
void hpdesk_toggle_theme(void);
void hpdesk_switch_workspace_delta(int delta);
void hpdesk_cycle_window_focus(void);
void hpdesk_close_focused_window(void);

/* V0.13.7 P4: 消费应用启动请求 (Dock/启动器图标) */
void hpdesk_handle_pending_app(void);
void hpdesk_pinch_resize(int dir);  /* V0.13.8 P5: 触摸捏合缩放 */

/* 应用启动请求（Dock图标点击时设置，desktop64主循环检测并启动） */
#define HS_APP_NONE 0
#define HS_APP_TERMINAL 1
#define HS_APP_FILES 2
#define HS_APP_BROWSER 3
#define HS_APP_SETTINGS 4
#define HS_APP_EDITOR 5
#define HS_APP_CALCULATOR 6
#define HS_APP_NOTES 7
#define HS_APP_ABOUT 8
#define HS_APP_TASK 9    /* V0.13.9: 任务管理器 */
#define HS_APP_DEVMGR 10   /* V0.55: 设备管理器 */
#define HS_APP_DISKMGR 11  /* V0.55: 磁盘管理 */
#define HS_APP_LOGVIEW 12  /* V0.55: 日志查看器 */
extern int g_pending_app;
void hpdesk_set_theme(hs_theme_t theme);
void hpdesk_add_widget(hs_panel_pos_t panel, hs_widget_type_t type);
void hpdesk_add_app(const char *name, const char *command, const char *icon);
void hpdesk_toggle_launcher(void);
void hpdesk_show_desktop(void);
void hpdesk_switch_workspace(int ws);
int hpdesk_reload(void);  /* 重新加载配置 */

/* 主题 */
void hs_theme_init(hs_theme_colors_t *colors, hs_theme_t theme);

/* 小部件 */
void hs_widget_clock_render(hs_widget_t *w, int x, int y);
void hs_widget_cpu_render(hs_widget_t *w, int x, int y);
void hs_widget_memory_render(hs_widget_t *w, int x, int y);
void hs_widget_workspaces_render(hs_widget_t *w, int x, int y);
void hs_widget_update_all(void);

/* 面板 */
void hs_panel_render(hs_panel_t *panel);
void hs_panel_init(hs_panel_t *panel, hs_panel_pos_t pos, int height);

/* 启动器 */
void hs_launcher_render(void);
void hs_launcher_handle_key(int key);

/* 系统托盘 */
void hs_tray_init(void);
int hs_tray_add_icon(const char *name, const char *icon);
void hs_tray_remove_icon(const char *name);
void hs_tray_render(int x, int y, int max_width);
void hs_tray_handle_click(int x, int y);

/* 通知中心 */
void hs_notification_init(void);
int hs_notification_send(const char *title, const char *body, int urgency, int timeout);
void hs_notification_render(void);
void hs_notification_toggle_center(void);
void hs_notification_dismiss(int id);
void hs_notification_clear_all(void);

/* 媒体控制 */
void hs_media_init(void);
void hs_media_render(int x, int y);
void hs_media_play_pause(void);
void hs_media_next(void);
void hs_media_prev(void);
void hs_media_set_volume(int vol);

/* 系统状态小部件 */
void hs_status_init(void);
void hs_status_update(void);
void hs_volume_render(int x, int y, int width);
void hs_brightness_render(int x, int y, int width);
void hs_battery_render(int x, int y);
void hs_network_render(int x, int y);
void hs_tasklist_render(int x, int y, int width);
void hs_status_render_all(int panel_y);

/* 窗口管理 */
void hs_window_manager_init(void);
void hs_window_render_all(void);
void hs_window_tile(void);
hs_window_t *hs_window_create(hs_window_type_t type, const char *title,
                              int x, int y, int w, int h);
void hs_window_destroy(hs_window_t *win);
void hs_window_focus(hs_window_t *win);
void hs_window_show(hs_window_t *win);
void hs_window_hide(hs_window_t *win);
void hs_window_maximize(hs_window_t *win);
void hs_window_restore(hs_window_t *win);
void hs_window_raise(hs_window_t *win);
void hs_scrollbar_draw(int x, int track_y, int track_h, int offset, int max_scroll);
void hs_window_lower(hs_window_t *win);
void hs_window_move(hs_window_t *win, int x, int y);
void hs_window_resize(hs_window_t *win, int w, int h);
hs_window_t *hs_window_at(int x, int y);
hs_window_t *hs_window_get_focused(void);

/* 布局管理 */
void hs_layout_set(hs_layout_mode_t mode);
hs_layout_mode_t hs_layout_get(void);
void hs_layout_apply(void);
void hs_layout_cycle(void);
void hs_layout_tile(void);
void hs_layout_bsp(void);
void hs_layout_single(void);

/* 窗口拖动 */
void hs_window_drag_start(int x, int y);
void hs_window_drag_move(int x, int y);
void hs_window_drag_end(void);
int hs_window_is_dragging(void);

/* 窗口规则 */
void hs_window_rule_init(void);
int hs_window_rule_add(const char *app, hs_layout_mode_t layout,
                       int workspace, int floating);
void hs_window_rule_apply(hs_window_t *win);

/* 工作区完善 */
void hs_workspace_set_name(int ws, const char *name);
void hs_workspace_move_window(hs_window_t *win, int ws);
void hs_workspace_send_to_next(void);
void hs_workspace_send_to_prev(void);

#endif /* HPDESK_H */
