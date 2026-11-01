#ifndef WINDOWSYSTEM_H
#define WINDOWSYSTEM_H

#include <stdint.h>

#define WS_MAX_WINDOWS 32
#define WS_MAX_ANIMATIONS 16
#define WS_TITLE_LEN 64

/* 窗口类型 */
typedef enum {
    WINDOW_TYPE_NORMAL = 0,
    WINDOW_TYPE_DIALOG,
    WINDOW_TYPE_POPUP,
    WINDOW_TYPE_TOAST,
    WINDOW_TYPE_SPLASH,
    WINDOW_TYPE_LOCKSCREEN,
    WINDOW_TYPE_STATUS_BAR,
    WINDOW_TYPE_NAV_BAR,
    WINDOW_TYPE_NOTIFICATION_SHADE,
    WINDOW_TYPE_PIP,  /* 画中画 */
} window_type_t;

/* 窗口状态 */
typedef enum {
    WINDOW_STATE_CREATED = 0,
    WINDOW_STATE_SHOWN,
    WINDOW_STATE_HIDDEN,
    WINDOW_STATE_MINIMIZED,
    WINDOW_STATE_MAXIMIZED,
    WINDOW_STATE_FULLSCREEN,
    WINDOW_STATE_DESTROYED,
} window_state_t;

/* 窗口标志 */
#define WINDOW_FLAG_FOCUSABLE      0x0001
#define WINDOW_FLAG_TOUCHABLE      0x0002
#define WINDOW_FLAG_DRAGGABLE      0x0004
#define WINDOW_FLAG_RESIZABLE      0x0008
#define WINDOW_FLAG_TITLE_BAR      0x0010
#define WINDOW_FLAG_BORDER         0x0020
#define WINDOW_FLAG_TRANSLUCENT    0x0040
#define WINDOW_FLAG_ALWAYS_ON_TOP  0x0080
#define WINDOW_FLAG_SYSTEM         0x0100

/* 分屏模式 */
typedef enum {
    SPLIT_NONE = 0,
    SPLIT_LEFT_RIGHT,
    SPLIT_TOP_BOTTOM,
} split_mode_t;

/* 动画类型 */
typedef enum {
    ANIM_NONE = 0,
    ANIM_FADE_IN,
    ANIM_FADE_OUT,
    ANIM_SLIDE_LEFT,
    ANIM_SLIDE_RIGHT,
    ANIM_SLIDE_UP,
    ANIM_SLIDE_DOWN,
    ANIM_SCALE_IN,
    ANIM_SCALE_OUT,
    ANIM_ROTATE,
    ANIM_CUSTOM,
} anim_type_t;

/* 动画插值器 */
typedef enum {
    INTERP_LINEAR = 0,
    INTERP_ACCELERATE,
    INTERP_DECELERATE,
    INTERP_ACCELERATE_DECELERATE,
    INTERP_BOUNCE,
    INTERP_OVERSHOOT,
} anim_interpolator_t;

/* 动画 */
typedef struct {
    int used;
    int id;
    int window_id;
    anim_type_t type;
    anim_interpolator_t interpolator;
    int duration;      /* ms */
    int start_time;
    int current_time;
    float from_x, from_y, from_w, from_h, from_alpha;
    float to_x, to_y, to_w, to_h, to_alpha;
    int finished;
    void (*on_complete)(int window_id);
} window_animation_t;

/* 窗口 */
typedef struct {
    int used;
    int id;
    char title[WS_TITLE_LEN];
    window_type_t type;
    window_state_t state;
    uint32_t flags;
    /* 位置和大小 */
    int x, y;
    int width, height;
    int saved_x, saved_y, saved_w, saved_h;  /* 用于最大化恢复 */
    /* Z序 */
    int z_order;
    /* 透明度 */
    float alpha;
    /* 旋转角度 */
    float rotation;
    /* 缩放 */
    float scale_x, scale_y;
    /* 内容缓冲区 */
    void *buffer;
    uint32_t buffer_size;
    /* 父窗口 */
    int parent_id;
    /* 子窗口列表 */
    int children[16];
    int child_count;
    /* 输入焦点 */
    int has_focus;
    /* 拖动状态 */
    int dragging;
    int drag_offset_x, drag_offset_y;
    /* 调整大小状态 */
    int resizing;
    int resize_edge;
    /* 动画 */
    int anim_id;
    /* 回调 */
    void (*on_focus)(int win_id, int focused);
    void (*on_resize)(int win_id, int w, int h);
    void (*on_close)(int win_id);
    void (*on_touch)(int win_id, int x, int y, int action);
} window_t;

/* 窗口管理器状态 */
typedef struct {
    int initialized;
    window_t windows[WS_MAX_WINDOWS];
    window_animation_t animations[WS_MAX_ANIMATIONS];
    int window_count;
    int animation_count;
    int focus_window;
    int desktop_window;
    split_mode_t split_mode;
    int split_window_a;
    int split_window_b;
    int screen_width;
    int screen_height;
} window_manager_t;

/* 初始化窗口系统 */
void windowsystem_init(void);

/* 窗口管理 */
int ws_create_window(const char *title, int x, int y, int w, int h, uint32_t flags);
int ws_destroy_window(int win_id);
int ws_show_window(int win_id);
int ws_hide_window(int win_id);
int ws_minimize_window(int win_id);
int ws_maximize_window(int win_id);
int ws_restore_window(int win_id);
int ws_fullscreen_window(int win_id);
int ws_close_window(int win_id);

/* 窗口操作 */
int ws_move_window(int win_id, int x, int y);
int ws_resize_window(int win_id, int w, int h);
int ws_set_focus(int win_id);
int ws_raise_window(int win_id);
int ws_lower_window(int win_id);
int ws_set_alpha(int win_id, float alpha);
int ws_set_rotation(int win_id, float angle);
int ws_set_scale(int win_id, float sx, float sy);
int ws_set_title(int win_id, const char *title);
int ws_set_buffer(int win_id, void *buffer, uint32_t size);

/* 分屏 */
int ws_enter_split_mode(int win_a, int win_b, split_mode_t mode);
int ws_exit_split_mode(void);
int ws_get_split_mode(void);

/* 画中画 */
int ws_enter_pip(int win_id, int x, int y, int w, int h);
int ws_exit_pip(int win_id);

/* 动画 */
int ws_start_animation(int win_id, anim_type_t type, int duration,
                       anim_interpolator_t interp, void (*on_complete)(int));
int ws_cancel_animation(int win_id);
int ws_animate_window(int win_id, float to_x, float to_y, float to_w, float to_h,
                      float to_alpha, int duration, anim_interpolator_t interp);

/* 转场动画 */
int ws_transition(int from_win, int to_win, anim_type_t type, int duration);

/* 查询 */
int ws_get_window(int win_id, window_t *win);
int ws_get_focus_window(void);
int ws_list_windows(window_t *list, int max);
int ws_window_at(int x, int y);

/* 渲染 */
int ws_render(void);
void ws_tick(void);

/* 获取窗口数量 */
int ws_window_count(void);

#endif /* WINDOWSYSTEM_H */
