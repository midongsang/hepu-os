/* ============================================================================
 * 赫普OS - 手势识别实现
 * V8.60: 初始实现
 * 支持点击/双击/长按/滑动/滚动/捏合/旋转/拖拽
 * ========================================================================== */

#include "gesture.h"
#include "../include/vga.h"
#include <string.h>

/* 默认配置 */
static gesture_config_t default_config = {
    .tap_max_distance = 10,
    .tap_max_duration = 200,
    .double_tap_max_interval = 300,
    .long_press_min_duration = 500,
    .swipe_min_distance = 50,
    .swipe_max_duration = 500,
    .scroll_min_distance = 20,
    .pinch_min_scale_diff = 0.1f,
    .rotate_min_angle = 10.0f,
    .drag_min_distance = 10,
};

/* 全局状态 */
static gesture_config_t config;
static gesture_callback_t callback = NULL;
static gesture_type_t last_gesture = GESTURE_NONE;
static gesture_state_t current_state = GESTURE_STATE_IDLE;
static int initialized = 0;

/* 跟踪状态 */
static struct {
    int down_x, down_y;
    int last_x, last_y;
    uint32_t down_time;
    uint32_t last_time;
    int finger_count;
    int moved;
    int total_dx, total_dy;

    /* 双击检测 */
    int last_tap_x, last_tap_y;
    uint32_t last_tap_time;
    int last_tap_valid;

    /* 双指手势 */
    int second_down_x, second_down_y;
    int second_last_x, second_last_y;
    float initial_distance;
    float initial_angle;

    /* 长按定时器 */
    int long_press_triggered;
} gs;

/* 简单数学函数 */
static int abs_int(int x) { return x < 0 ? -x : x; }

static float sqrt_float(float x) {
    if (x <= 0) return 0;
    float result = x;
    for (int i = 0; i < 10; i++) {
        result = (result + x / result) / 2.0f;
    }
    return result;
}

static float distance(int x1, int y1, int x2, int y2) {
    int dx = x2 - x1;
    int dy = y2 - y1;
    return sqrt_float((float)(dx * dx + dy * dy));
}

static float atan2_float(int y, int x) {
    /* 简化的atan2实现，返回角度（度） */
    float angle = 0;
    if (x == 0 && y == 0) return 0;
    if (x > 0) {
        angle = (float)y / (float)x;
        angle = angle * 45.0f;  /* 近似 */
    } else if (x < 0) {
        if (y >= 0) angle = (float)y / (float)(-x) * 45.0f + 90.0f;
        else angle = (float)(-y) / (float)(-x) * 45.0f - 90.0f;
    } else {
        angle = y > 0 ? 90.0f : -90.0f;
    }
    return angle;
}

/* 发送手势事件 */
static void emit_gesture(gesture_type_t type, gesture_state_t state,
                          int x, int y, int dx, int dy,
                          float scale, float rotation)
{
    gesture_event_t event;
    memset(&event, 0, sizeof(event));

    event.type = type;
    event.state = state;
    event.x = x;
    event.y = y;
    event.dx = dx;
    event.dy = dy;
    event.scale = scale;
    event.rotation = rotation;
    event.finger_count = gs.finger_count;
    event.timestamp = gs.last_time;

    last_gesture = type;
    current_state = state;

    if (callback) {
        callback(&event);
    }
}

/* 初始化手势识别 */
int gesture_init(void)
{
    if (initialized) return 0;

    memcpy(&config, &default_config, sizeof(gesture_config_t));
    callback = NULL;
    last_gesture = GESTURE_NONE;
    current_state = GESTURE_STATE_IDLE;

    memset(&gs, 0, sizeof(gs));
    gs.initial_distance = 0;
    gs.initial_angle = 0;

    initialized = 1;
    vga_puts("[GESTURE] Gesture recognition initialized\n");
    return 0;
}

/* 注册回调 */
void gesture_register_callback(gesture_callback_t cb)
{
    callback = cb;
}

/* 配置 */
void gesture_set_config(gesture_config_t *cfg)
{
    if (cfg) memcpy(&config, cfg, sizeof(gesture_config_t));
}

void gesture_get_config(gesture_config_t *cfg)
{
    if (cfg) memcpy(cfg, &config, sizeof(gesture_config_t));
}

/* 处理单指触摸 */
static void process_single_touch(touch_point_t *p, uint32_t timestamp)
{
    if (!p || !p->active) {
        /* 手指抬起 */
        if (gs.finger_count == 1) {
            int dx = p->x - gs.down_x;
            int dy = p->y - gs.down_y;
            int total_dist = abs_int(dx) + abs_int(dy);
            uint32_t duration = timestamp - gs.down_time;

            if (!gs.moved && total_dist < config.tap_max_distance &&
                duration < config.tap_max_duration) {
                /* 点击 */
                emit_gesture(GESTURE_TAP, GESTURE_STATE_COMPLETED,
                            p->x, p->y, 0, 0, 1.0f, 0);

                /* 双击检测 */
                if (gs.last_tap_valid &&
                    (timestamp - gs.last_tap_time) < config.double_tap_max_interval &&
                    abs_int(p->x - gs.last_tap_x) < config.tap_max_distance &&
                    abs_int(p->y - gs.last_tap_y) < config.tap_max_distance) {
                    emit_gesture(GESTURE_DOUBLE_TAP, GESTURE_STATE_COMPLETED,
                                p->x, p->y, 0, 0, 1.0f, 0);
                    gs.last_tap_valid = 0;
                } else {
                    gs.last_tap_x = p->x;
                    gs.last_tap_y = p->y;
                    gs.last_tap_time = timestamp;
                    gs.last_tap_valid = 1;
                }
            } else if (gs.moved && total_dist > config.swipe_min_distance &&
                       duration < config.swipe_max_duration) {
                /* 滑动 */
                gesture_type_t swipe_type;
                if (abs_int(dx) > abs_int(dy)) {
                    swipe_type = (dx > 0) ? GESTURE_SWIPE_RIGHT : GESTURE_SWIPE_LEFT;
                } else {
                    swipe_type = (dy > 0) ? GESTURE_SWIPE_DOWN : GESTURE_SWIPE_UP;
                }
                emit_gesture(swipe_type, GESTURE_STATE_COMPLETED,
                            p->x, p->y, dx, dy, 1.0f, 0);
            } else if (gs.moved) {
                /* 拖拽结束 */
                emit_gesture(GESTURE_DRAG, GESTURE_STATE_COMPLETED,
                            p->x, p->y, dx, dy, 1.0f, 0);
            }

            gs.finger_count = 0;
            gs.long_press_triggered = 0;
        }
        return;
    }

    /* 手指按下 */
    if (gs.finger_count == 0) {
        gs.down_x = p->x;
        gs.down_y = p->y;
        gs.down_time = timestamp;
        gs.finger_count = 1;
        gs.moved = 0;
        gs.total_dx = 0;
        gs.total_dy = 0;
        gs.long_press_triggered = 0;
        emit_gesture(GESTURE_NONE, GESTURE_STATE_POSSIBLE, p->x, p->y, 0, 0, 1.0f, 0);
    }

    /* 手指移动 */
    int dx = p->x - gs.last_x;
    int dy = p->y - gs.last_y;
    gs.total_dx += dx;
    gs.total_dy += dy;

    int total_dist = abs_int(gs.total_dx) + abs_int(gs.total_dy);
    if (total_dist > config.drag_min_distance) {
        if (!gs.moved) {
            gs.moved = 1;
            emit_gesture(GESTURE_DRAG, GESTURE_STATE_IN_PROGRESS,
                        p->x, p->y, dx, dy, 1.0f, 0);
        } else {
            emit_gesture(GESTURE_DRAG, GESTURE_STATE_IN_PROGRESS,
                        p->x, p->y, dx, dy, 1.0f, 0);
        }
    }

    /* 长按检测 */
    if (!gs.moved && !gs.long_press_triggered &&
        (timestamp - gs.down_time) > config.long_press_min_duration) {
        gs.long_press_triggered = 1;
        emit_gesture(GESTURE_LONG_PRESS, GESTURE_STATE_COMPLETED,
                    p->x, p->y, 0, 0, 1.0f, 0);
    }

    gs.last_x = p->x;
    gs.last_y = p->y;
    gs.last_time = timestamp;
}

/* 处理双指触摸 */
static void process_multi_touch(touch_point_t *points, int num_points,
                                 uint32_t timestamp)
{
    if (num_points < 2) return;

    touch_point_t *p1 = &points[0];
    touch_point_t *p2 = &points[1];

    if (!p1->active || !p2->active) return;

    if (gs.finger_count < 2) {
        /* 第二指按下 */
        gs.second_down_x = p2->x;
        gs.second_down_y = p2->y;
        gs.second_last_x = p2->x;
        gs.second_last_y = p2->y;
        gs.initial_distance = distance(p1->x, p1->y, p2->x, p2->y);
        gs.initial_angle = atan2_float(p2->y - p1->y, p2->x - p1->x);
        gs.finger_count = 2;
        gs.moved = 0;
        return;
    }

    /* 计算当前距离和角度 */
    float current_distance = distance(p1->x, p1->y, p2->x, p2->y);
    float current_angle = atan2_float(p2->y - p1->y, p2->x - p1->x);

    /* 中心点 */
    int cx = (p1->x + p2->x) / 2;
    int cy = (p1->y + p2->y) / 2;

    /* 捏合检测 */
    if (gs.initial_distance > 0) {
        float scale = current_distance / gs.initial_distance;
        if (scale < (1.0f - config.pinch_min_scale_diff)) {
            emit_gesture(GESTURE_PINCH_IN, GESTURE_STATE_IN_PROGRESS,
                        cx, cy, 0, 0, scale, 0);
            gs.moved = 1;
        } else if (scale > (1.0f + config.pinch_min_scale_diff)) {
            emit_gesture(GESTURE_PINCH_OUT, GESTURE_STATE_IN_PROGRESS,
                        cx, cy, 0, 0, scale, 0);
            gs.moved = 1;
        }
    }

    /* 旋转检测 */
    float angle_diff = current_angle - gs.initial_angle;
    if (angle_diff > 180) angle_diff -= 360;
    if (angle_diff < -180) angle_diff += 360;

    if (abs_int((int)angle_diff) > (int)config.rotate_min_angle) {
        gesture_type_t rot_type = (angle_diff > 0) ?
            GESTURE_ROTATE_CW : GESTURE_ROTATE_CCW;
        emit_gesture(rot_type, GESTURE_STATE_IN_PROGRESS,
                    cx, cy, 0, 0, 1.0f, angle_diff);
        gs.moved = 1;
    }

    /* 双指滑动（滚动） */
    int dx1 = p1->x - gs.last_x;
    int dy1 = p1->y - gs.last_y;
    int dx2 = p2->x - gs.second_last_x;
    int dy2 = p2->y - gs.second_last_y;

    if ((abs_int(dx1) > config.scroll_min_distance ||
         abs_int(dy1) > config.scroll_min_distance) &&
        abs_int(dx1 - dx2) < config.scroll_min_distance &&
        abs_int(dy1 - dy2) < config.scroll_min_distance) {
        emit_gesture(GESTURE_SCROLL, GESTURE_STATE_IN_PROGRESS,
                    cx, cy, (dx1 + dx2) / 2, (dy1 + dy2) / 2, 1.0f, 0);
        gs.moved = 1;
    }

    gs.last_x = p1->x;
    gs.last_y = p1->y;
    gs.second_last_x = p2->x;
    gs.second_last_y = p2->y;
    gs.last_time = timestamp;
}

/* 处理触摸点更新 */
void gesture_update(touch_point_t *points, int num_points, uint32_t timestamp)
{
    if (!initialized || !points || num_points <= 0) return;

    gs.last_time = timestamp;

    if (num_points == 1) {
        process_single_touch(&points[0], timestamp);
    } else if (num_points >= 2) {
        process_multi_touch(points, num_points, timestamp);
    }
}

/* 处理触摸事件 */
void gesture_handle_event(touch_event_t *event)
{
    if (!initialized || !event) return;

    touch_point_t pt;
    memset(&pt, 0, sizeof(pt));
    pt.active = (event->type != TOUCH_UP);
    pt.x = event->x;
    pt.y = event->y;
    pt.pressure = event->pressure;
    pt.id = event->point_id;

    /* 使用简单的时间计数器（内核tick） */
    static uint32_t event_time = 0;
    event_time += 16;  /* 假设60fps，每帧约16ms */

    gesture_update(&pt, 1, event_time);
}

/* 获取最后手势 */
gesture_type_t gesture_get_last(void)
{
    return last_gesture;
}

/* 获取手势状态 */
gesture_state_t gesture_get_state(void)
{
    return current_state;
}

/* 重置 */
void gesture_reset(void)
{
    memset(&gs, 0, sizeof(gs));
    gs.initial_distance = 0;
    gs.initial_angle = 0;
    last_gesture = GESTURE_NONE;
    current_state = GESTURE_STATE_IDLE;
}

/* 手势类型名称 */
const char *gesture_type_name(gesture_type_t type)
{
    switch (type) {
        case GESTURE_NONE: return "None";
        case GESTURE_TAP: return "Tap";
        case GESTURE_DOUBLE_TAP: return "Double Tap";
        case GESTURE_LONG_PRESS: return "Long Press";
        case GESTURE_SWIPE_LEFT: return "Swipe Left";
        case GESTURE_SWIPE_RIGHT: return "Swipe Right";
        case GESTURE_SWIPE_UP: return "Swipe Up";
        case GESTURE_SWIPE_DOWN: return "Swipe Down";
        case GESTURE_SCROLL: return "Scroll";
        case GESTURE_PINCH_IN: return "Pinch In";
        case GESTURE_PINCH_OUT: return "Pinch Out";
        case GESTURE_ROTATE_CW: return "Rotate CW";
        case GESTURE_ROTATE_CCW: return "Rotate CCW";
        case GESTURE_DRAG: return "Drag";
        default: return "Unknown";
    }
}
