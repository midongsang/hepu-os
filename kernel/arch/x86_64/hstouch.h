/* ============================================================================
 * 赫普OS - 多点触控/手势抽象层 (V0.13.6 P3)
 * 参考 Android MotionEvent / Linux evdev 多点触控抽象:
 * 触摸点状态机 + 手势识别 (单指点击/拖动、双指捏合缩放、双指滑动、长按)
 * 本层为纯逻辑(无内核IO依赖), 触摸硬件(USB触摸屏/触摸板)由驱动填充点集;
 * host 侧可用纯输入序列做单元验证
 * ========================================================================== */
#ifndef HSTOUCH_H
#define HSTOUCH_H

#include <stdint.h>

#define HS_TOUCH_MAX_POINTS 5

/* 触摸点 */
typedef struct {
    int active;      /* 1=按下中 */
    int id;          /* 触摸点ID (多指) */
    int x, y;        /* 当前坐标 */
    int px, py;      /* 上一帧坐标 */
    int start_x, start_y;  /* 按下位置 (长按/抖动判定) */
    uint64_t down_ticks;  /* 按下时刻 (用于长按判定, 由调用方提供时钟) */
} hs_touch_point_t;

/* 手势结果 */
typedef struct {
    int gesture;         /* HS_GESTURE_* */
    int pinch_scale;     /* 捏合: 256=不变, >256放大 <256缩小 (定点) */
    int scroll_delta;    /* 滑动: 垂直增量 (双指) */
    int x, y;            /* 手势中心/触点位置 */
} hs_gesture_t;

#define HS_GESTURE_NONE      0
#define HS_GESTURE_TAP       1   /* 单指点击 (≈鼠标左键) */
#define HS_GESTURE_DRAG      2   /* 单指拖动 (≈窗口拖拽/滚动画布) */
#define HS_GESTURE_LONG_PRESS 3  /* 长按 (≈鼠标右键) */
#define HS_GESTURE_PINCH     4   /* 双指捏合缩放 */
#define HS_GESTURE_SWIPE     5   /* 双指滑动 (≈滚动) */

/* 触摸事件 (由驱动填充) */
typedef struct {
    int type;          /* 0=down 1=move 2=up */
    int id;
    int x, y;
    uint64_t ticks;    /* 时间戳 */
} hs_touch_event_t;

/* 状态 (调用方持有, 可 static) */
typedef struct {
    hs_touch_point_t pts[HS_TOUCH_MAX_POINTS];
    int active_count;
    int last_gesture;
    int pinch_start_dist;   /* 捏合起始距离 */
    int pinch_last_dist;
    int px_center, py_center;  /* 双指中心上一帧 */
    int moved;              /* 触点是否移动过 (区分tap/drag/long) */
    int prev_x, prev_y;     /* 单指上一帧位置 */
} hs_touch_state_t;

/* 处理一个触摸事件, 返回手势 (NONE=无手势/继续跟踪) */
hs_gesture_t hs_touch_process(hs_touch_state_t *st, const hs_touch_event_t *ev);

/* 复位状态 */
void hs_touch_reset(hs_touch_state_t *st);

#endif /* HSTOUCH_H */
