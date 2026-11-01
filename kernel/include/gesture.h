/* ============================================================================
 * 赫普OS - 手势识别头文件
 * V8.60: 初始实现
 * 支持触摸屏/触摸板的手势识别
 * ========================================================================== */

#ifndef _GESTURE_H
#define _GESTURE_H

#include <stdint.h>
#include "touch.h"

/* 手势类型 */
typedef enum {
    GESTURE_NONE = 0,
    GESTURE_TAP,           /* 点击 */
    GESTURE_DOUBLE_TAP,    /* 双击 */
    GESTURE_LONG_PRESS,    /* 长按 */
    GESTURE_SWIPE_LEFT,    /* 左滑 */
    GESTURE_SWIPE_RIGHT,   /* 右滑 */
    GESTURE_SWIPE_UP,      /* 上滑 */
    GESTURE_SWIPE_DOWN,    /* 下滑 */
    GESTURE_SCROLL,        /* 滚动（双指滑动） */
    GESTURE_PINCH_IN,      /* 捏合缩小 */
    GESTURE_PINCH_OUT,     /* 捏合放大 */
    GESTURE_ROTATE_CW,     /* 顺时针旋转 */
    GESTURE_ROTATE_CCW,    /* 逆时针旋转 */
    GESTURE_DRAG,          /* 拖拽 */
    GESTURE_MAX
} gesture_type_t;

/* 手势状态 */
typedef enum {
    GESTURE_STATE_IDLE = 0,
    GESTURE_STATE_POSSIBLE,
    GESTURE_STATE_IN_PROGRESS,
    GESTURE_STATE_COMPLETED,
    GESTURE_STATE_CANCELLED
} gesture_state_t;

/* 手势事件 */
typedef struct {
    gesture_type_t type;
    gesture_state_t state;
    int x, y;               /* 中心点坐标 */
    int dx, dy;             /* 位移 */
    float scale;            /* 缩放比例（捏合） */
    float rotation;         /* 旋转角度（度） */
    int velocity_x;         /* X方向速度 */
    int velocity_y;         /* Y方向速度 */
    int finger_count;       /* 手指数量 */
    uint32_t timestamp;     /* 时间戳（毫秒） */
} gesture_event_t;

/* 手势回调 */
typedef void (*gesture_callback_t)(gesture_event_t *event);

/* 手势配置 */
typedef struct {
    int tap_max_distance;       /* 点击最大移动距离（像素） */
    int tap_max_duration;       /* 点击最大持续时间（毫秒） */
    int double_tap_max_interval;/* 双击最大间隔（毫秒） */
    int long_press_min_duration;/* 长按最小持续时间（毫秒） */
    int swipe_min_distance;     /* 滑动最小距离（像素） */
    int swipe_max_duration;     /* 滑动最大持续时间（毫秒） */
    int scroll_min_distance;    /* 滚动最小距离 */
    float pinch_min_scale_diff; /* 捏合最小缩放变化 */
    float rotate_min_angle;     /* 旋转最小角度（度） */
    int drag_min_distance;      /* 拖拽最小距离 */
} gesture_config_t;

/* 函数声明 */

/* 初始化手势识别 */
int gesture_init(void);

/* 注册手势回调 */
void gesture_register_callback(gesture_callback_t cb);

/* 配置手势参数 */
void gesture_set_config(gesture_config_t *config);

/* 获取当前配置 */
void gesture_get_config(gesture_config_t *config);

/* 处理触摸点更新（由触摸驱动调用） */
void gesture_update(touch_point_t *points, int num_points, uint32_t timestamp);

/* 处理触摸事件（由触摸驱动调用） */
void gesture_handle_event(touch_event_t *event);

/* 获取最后识别的手势 */
gesture_type_t gesture_get_last(void);

/* 获取手势状态 */
gesture_state_t gesture_get_state(void);

/* 重置手势识别状态 */
void gesture_reset(void);

/* 手势类型名称 */
const char *gesture_type_name(gesture_type_t type);

#endif /* _GESTURE_H */
