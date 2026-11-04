/* ============================================================================
 * 赫普OS - 输入子系统 (evdev式)
 * 事件总线 + 多点触摸 + 手势识别 + 输入法框架
 * ========================================================================== */

#include <stdint.h>
#include "input.h"
#include "vga.h"
#include "string.h"

static int input_abs(int x) { return x < 0 ? -x : x; }

static input_device_t devices[INPUT_MAX_DEVICES];
static input_listener_t listeners[INPUT_MAX_LISTENERS];
static input_event_t event_queue[INPUT_EVENT_QUEUE_SIZE];
static int queue_head = 0;
static int queue_tail = 0;
static int device_count = 0;
static int listener_count = 0;
static int input_initialized = 0;

/* 手势识别状态 */
static int gestures_enabled = 0;
static int touch2_active = 0;
static int touch2_start_x = 0, touch2_start_y = 0;
static int touch2_last_x = 0, touch2_last_y = 0;
static uint32_t last_tap_ticks = 0;
static uint32_t gesture_counts[GESTURE_TYPE_MAX] = {0};
static gesture_callback_t gesture_cb = NULL;
static int touch_down = 0;
static int touch_start_x_valid = 0;
static int touch_start_y_valid = 0;
static int touch_start_x = 0, touch_start_y = 0;
static int touch_last_x = 0, touch_last_y = 0;
static uint64_t touch_start_time = 0;

/* V4.38-E83: 手势阈值可配置 (默认值, 可用 gesture threshold 命令调整) */
static int cfg_long_press_ticks = 50;   /* 500ms @10ms/tick */
static int cfg_double_tap_window = 30;  /* 300ms 双击窗口 */
static int cfg_swipe_min = 50;          /* 滑动判定最小位移 */

/* V4.39-E84: 手势事件环形队列——UI 层轮询事件源 (wm/mobile 联动用) */
#define GESTURE_QUEUE_SIZE 16
static gesture_event_t gesture_queue[GESTURE_QUEUE_SIZE];
static int gq_head = 0, gq_tail = 0, gq_count = 0;
static uint32_t gq_dropped = 0;

static void gesture_queue_push(const gesture_event_t *ge)
{
    if (!ge) return;
    if (gq_count >= GESTURE_QUEUE_SIZE) { gq_dropped++; return; }
    gesture_queue[gq_tail] = *ge;
    gq_tail = (gq_tail + 1) % GESTURE_QUEUE_SIZE;
    gq_count++;
}

/* 输入法 */
static input_method_t current_method;
static int method_registered = 0;

void input_init(void)
{
    for (int i = 0; i < INPUT_MAX_DEVICES; i++) devices[i].used = 0;
    for (int i = 0; i < INPUT_MAX_LISTENERS; i++) listeners[i].used = 0;
    queue_head = queue_tail = 0;
    device_count = 0;
    listener_count = 0;
    input_initialized = 1;

    /* 注册默认键盘 */
    input_device_t kb;
    memset(&kb, 0, sizeof(kb));
    kb.name = "PS/2 Keyboard";
    kb.type = INPUT_DEV_KEYBOARD;
    kb.vendor_id = 0x0001;
    kb.product_id = 0x0001;
    kb.ev_types[0] = 1;  /* EV_SYN */
    kb.ev_types[1] = 1;  /* EV_KEY */
    kb.ev_types[7] = 1;  /* EV_REP */
    input_register_device(&kb);

    /* 注册默认鼠标 */
    input_device_t mouse;
    memset(&mouse, 0, sizeof(mouse));
    mouse.name = "PS/2 Mouse";
    mouse.type = INPUT_DEV_MOUSE;
    mouse.vendor_id = 0x0002;
    mouse.product_id = 0x0001;
    mouse.ev_types[0] = 1;
    mouse.ev_types[1] = 1;
    mouse.ev_types[2] = 1;  /* EV_REL */
    input_register_device(&mouse);

    /* 注册触屏 */
    input_device_t touch;
    memset(&touch, 0, sizeof(touch));
    touch.name = "Touchscreen";
    touch.type = INPUT_DEV_TOUCHSCREEN;
    touch.vendor_id = 0x0003;
    touch.product_id = 0x0001;
    touch.ev_types[0] = 1;
    touch.ev_types[1] = 1;
    touch.ev_types[3] = 1;  /* EV_ABS */
    touch.slot_count = 5;
    input_register_device(&touch);

    vga_puts("[INPUT] input subsystem init done \n");
    vga_printf(" registered %d input devices \n", device_count);
}

int input_register_device(input_device_t *dev)
{
    if (!dev || !input_initialized) return -1;
    for (int i = 0; i < INPUT_MAX_DEVICES; i++) {
        if (!devices[i].used) {
            memcpy(&devices[i], dev, sizeof(input_device_t));
            devices[i].used = 1;
            devices[i].id = i;
            device_count++;
            return i;
        }
    }
    return -1;
}

int input_unregister_device(int dev_id)
{
    if (dev_id < 0 || dev_id >= INPUT_MAX_DEVICES || !devices[dev_id].used) return -1;
    devices[dev_id].used = 0;
    device_count--;
    return 0;
}

static void dispatch_event(input_event_t *event)
{
    /* 通知所有监听器 */
    for (int i = 0; i < INPUT_MAX_LISTENERS; i++) {
        if (listeners[i].used) {
            if (listeners[i].type_filter == -1 ||
                listeners[i].type_filter == event->type) {
                if (listeners[i].callback) {
                    listeners[i].callback(event, listeners[i].user_data);
                }
            }
        }
    }
}

static void detect_gesture(input_event_t *event)
{
    if (!gestures_enabled) return;

    if (event->type == EV_ABS && event->code == ABS_MT_TRACKING_ID) {
        if (event->value >= 0) {
            /* V4.35-E80: 双触点跟踪 (pinch/zoom) */
            if (!touch_down) {
                touch2_active = 0;
            } else {
                touch2_active = 1;
                touch2_start_x = touch_last_x;
                touch2_start_y = touch_last_y;
            }
            /* 触摸按下 (V4.35-E80: 起点坐标延迟到首个 POS 注入, 否则用旧坐标) */
            touch_down = 1;
            touch_start_x_valid = 0;
            touch_start_y_valid = 0;
            extern uint32_t pit_get_ticks(void);
            touch_start_time = pit_get_ticks();
        } else {
            /* 触摸抬起 */
            if (touch_down) {
                int dx = touch_last_x - touch_start_x;
                int dy = touch_last_y - touch_start_y;
                int dist = dx * dx + dy * dy;

                gesture_event_t ge;
                memset(&ge, 0, sizeof(ge));
                ge.x = touch_start_x;
                ge.y = touch_start_y;
                ge.dx = dx;
                ge.dy = dy;

                if (touch2_active) {
                    /* V4.35-E80: 双指捏合/放大 (两指距离变化) */
                    int d1 = touch_start_x - touch2_start_x;
                    int d2 = touch_last_x - touch2_last_x;
                    ge.type = (d2 * d2 > d1 * d1) ? GESTURE_ZOOM : GESTURE_PINCH;
                    touch2_active = 0;
                    if (ge.type > 0 && ge.type < GESTURE_TYPE_MAX)
                        gesture_counts[ge.type]++;
                    if (gesture_cb) gesture_cb(&ge);
                    gesture_queue_push(&ge);
                } else if (dist < 100) {
                    /* V4.37-E82: 轻点/长按——按按下时长区分 (LONG_PRESS_TICKS=50 即 500ms) */
                    extern uint32_t pit_get_ticks(void);
                    uint32_t now = pit_get_ticks();
                    if (now - touch_start_time >= (uint32_t)cfg_long_press_ticks) {
                        ge.type = GESTURE_LONG_PRESS;
                        gesture_counts[GESTURE_LONG_PRESS]++;
                        if (gesture_cb) gesture_cb(&ge);
                        gesture_queue_push(&ge);
                        last_tap_ticks = 0;   /* 长按不算 tap, 打断双击窗口 */
                    } else {
                        /* 轻点 */
                        ge.type = GESTURE_TAP;
                        if (gesture_cb) gesture_cb(&ge);
                        gesture_queue_push(&ge);
                        /* V4.35-E80: 双击检测——两次 tap 间隔 < 30 ticks(300ms) */
                        if (last_tap_ticks != 0 && (now - last_tap_ticks) < (uint32_t)cfg_double_tap_window) {
                            gesture_counts[GESTURE_DOUBLE_TAP]++;
                            /* V4.36-E81: 双击也推送事件回调 */
                            if (gesture_cb) {
                                gesture_event_t ge2 = ge;
                                ge2.type = GESTURE_DOUBLE_TAP;
                                gesture_cb(&ge2);
                            }
                            {
                                gesture_event_t ge2 = ge;
                                ge2.type = GESTURE_DOUBLE_TAP;
                                gesture_queue_push(&ge2);
                            }
                        }
                        gesture_counts[GESTURE_TAP]++;
                        last_tap_ticks = now;
                    }
                } else if (dx > cfg_swipe_min && input_abs(dy) < cfg_swipe_min) {
                    ge.type = GESTURE_SWIPE_RIGHT;
                    gesture_counts[ge.type]++;
                    if (gesture_cb) gesture_cb(&ge);
                    gesture_queue_push(&ge);
                } else if (dx < -cfg_swipe_min && input_abs(dy) < cfg_swipe_min) {
                    ge.type = GESTURE_SWIPE_LEFT;
                    gesture_counts[ge.type]++;
                    if (gesture_cb) gesture_cb(&ge);
                    gesture_queue_push(&ge);
                } else if (dy > cfg_swipe_min && input_abs(dx) < cfg_swipe_min) {
                    ge.type = GESTURE_SWIPE_DOWN;
                    gesture_counts[ge.type]++;
                    if (gesture_cb) gesture_cb(&ge);
                    gesture_queue_push(&ge);
                } else if (dy < -cfg_swipe_min && input_abs(dx) < cfg_swipe_min) {
                    ge.type = GESTURE_SWIPE_UP;
                    gesture_counts[ge.type]++;
                    if (gesture_cb) gesture_cb(&ge);
                    gesture_queue_push(&ge);
                }
            }
            touch_down = 0;
            touch_start_x_valid = 0;
            touch_start_y_valid = 0;
        }
    } else if (event->type == EV_ABS && event->code == ABS_MT_POSITION_X) {
        if (!touch_start_x_valid) { touch_start_x = event->value; touch_start_x_valid = 1; }
        touch_last_x = event->value;
        if (touch_down && touch2_active) touch2_last_x = touch_last_x;
    } else if (event->type == EV_ABS && event->code == ABS_MT_POSITION_Y) {
        if (!touch_start_y_valid) { touch_start_y = event->value; touch_start_y_valid = 1; }
        touch_last_y = event->value;
        if (touch_down && touch2_active) touch2_last_y = touch_last_y;
    }
}

int input_inject_event(int dev_id, uint16_t type, uint16_t code, int32_t value)
{
    if (dev_id < 0 || dev_id >= INPUT_MAX_DEVICES || !devices[dev_id].used) return -1;

    input_event_t event;
    event.type = type;
    event.code = code;
    event.value = value;
    event.timestamp = 0;

    /* 更新设备状态 */
    if (type == EV_KEY) {
        if (code < 256) {
            devices[dev_id].key_state[code / 8] = value ?
                (devices[dev_id].key_state[code / 8] | (1 << (code % 8))) :
                (devices[dev_id].key_state[code / 8] & ~(1 << (code % 8)));
        }
    } else if (type == EV_ABS) {
        if (code < 16) devices[dev_id].abs_value[code] = value;
    } else if (type == EV_REL) {
        if (code < 8) devices[dev_id].rel_value[code] += value;
    }

    /* 加入队列 */
    event_queue[queue_tail] = event;
    queue_tail = (queue_tail + 1) % INPUT_EVENT_QUEUE_SIZE;
    if (queue_tail == queue_head) {
        queue_head = (queue_head + 1) % INPUT_EVENT_QUEUE_SIZE;  /* 覆盖最旧 */
    }

    /* 分发 */
    dispatch_event(&event);

    /* 手势识别 */
    detect_gesture(&event);

    return 0;
}

int input_read_event(input_event_t *event)
{
    if (!event) return -1;
    if (queue_head == queue_tail) return 0;  /* 无事件 */
    *event = event_queue[queue_head];
    queue_head = (queue_head + 1) % INPUT_EVENT_QUEUE_SIZE;
    return 1;
}

int input_add_listener(int device_filter, int type_filter,
                       void (*callback)(input_event_t *, void *), void *user_data)
{
    if (!callback || !input_initialized) return -1;
    for (int i = 0; i < INPUT_MAX_LISTENERS; i++) {
        if (!listeners[i].used) {
            listeners[i].used = 1;
            listeners[i].id = i;
            listeners[i].device_filter = device_filter;
            listeners[i].type_filter = type_filter;
            listeners[i].callback = callback;
            listeners[i].user_data = user_data;
            listener_count++;
            return i;
        }
    }
    return -1;
}

int input_remove_listener(int listener_id)
{
    if (listener_id < 0 || listener_id >= INPUT_MAX_LISTENERS || !listeners[listener_id].used) return -1;
    listeners[listener_id].used = 0;
    listener_count--;
    return 0;
}

int input_get_device_info(int dev_id, input_device_t *info)
{
    if (dev_id < 0 || dev_id >= INPUT_MAX_DEVICES || !devices[dev_id].used) return -1;
    if (info) memcpy(info, &devices[dev_id], sizeof(input_device_t));
    return 0;
}

int input_get_key_state(int dev_id, int key_code)
{
    if (dev_id < 0 || dev_id >= INPUT_MAX_DEVICES || !devices[dev_id].used) return 0;
    if (key_code < 0 || key_code >= 256) return 0;
    return (devices[dev_id].key_state[key_code / 8] >> (key_code % 8)) & 1;
}

int input_get_abs_value(int dev_id, int axis)
{
    if (dev_id < 0 || dev_id >= INPUT_MAX_DEVICES || !devices[dev_id].used) return 0;
    if (axis < 0 || axis >= 16) return 0;
    return devices[dev_id].abs_value[axis];
}

int input_get_rel_value(int dev_id, int axis)
{
    if (dev_id < 0 || dev_id >= INPUT_MAX_DEVICES || !devices[dev_id].used) return 0;
    if (axis < 0 || axis >= 8) return 0;
    int val = devices[dev_id].rel_value[axis];
    devices[dev_id].rel_value[axis] = 0;
    return val;
}

int input_get_touch(int dev_id, int slot, input_touch_slot_t *touch)
{
    if (dev_id < 0 || dev_id >= INPUT_MAX_DEVICES || !devices[dev_id].used) return -1;
    if (slot < 0 || slot >= 10 || !touch) return -1;
    memcpy(touch, &devices[dev_id].touch_slots[slot], sizeof(input_touch_slot_t));
    return 0;
}

int input_touch_count(int dev_id)
{
    if (dev_id < 0 || dev_id >= INPUT_MAX_DEVICES || !devices[dev_id].used) return 0;
    int count = 0;
    for (int i = 0; i < 10; i++) {
        if (devices[dev_id].touch_slots[i].active) count++;
    }
    return count;
}

uint32_t input_gesture_count(int type)
{
    if (type <= 0 || type >= GESTURE_TYPE_MAX) return 0;
    return gesture_counts[type];
}

int input_enable_gestures(int enable)
{
    gestures_enabled = enable ? 1 : 0;
    vga_printf("[INPUT] gesture %s\n", enable ? " enabled " : " disabled ");
    return 0;
}

int input_set_gesture_callback(gesture_callback_t cb)
{
    gesture_cb = cb;
    return 0;
}

/* V4.38-E83: 手势阈值配置 (lp=长按 ticks, dt=双击窗口 ticks, sw=滑动最小位移) */
int input_gesture_config(int lp, int dt, int sw)
{
    if (lp > 0) cfg_long_press_ticks = lp;
    if (dt > 0) cfg_double_tap_window = dt;
    if (sw > 0) cfg_swipe_min = sw;
    return 0;
}

void input_gesture_cfg_get(int *lp, int *dt, int *sw)
{
    if (lp) *lp = cfg_long_press_ticks;
    if (dt) *dt = cfg_double_tap_window;
    if (sw) *sw = cfg_swipe_min;
}

/* V4.39-E84: 手势事件轮询 (UI 主循环调用; 返回 1 取到事件, 0 空) */
int input_gesture_poll(gesture_event_t *out)
{
    if (!out || gq_count == 0) return 0;
    *out = gesture_queue[gq_head];
    gq_head = (gq_head + 1) % GESTURE_QUEUE_SIZE;
    gq_count--;
    return 1;
}

void input_gesture_queue_clear(void)
{
    gq_head = gq_tail = gq_count = 0;
}

void input_gesture_queue_stats(int *pending, int *dropped)
{
    if (pending) *pending = gq_count;
    if (dropped) *dropped = (int)gq_dropped;
}

/* 默认英文输入法 */
static int default_method_process_key(int key_code, int *output_char)
{
    if (!output_char) return 0;
    /* 简化: 直接映射 */
    if (key_code >= 2 && key_code <= 11) {
        *output_char = '0' + (key_code == 11 ? 0 : key_code - 1);
        return 1;
    }
    if (key_code >= 16 && key_code <= 25) {
        *output_char = 'a' + (key_code - 16);
        return 1;
    }
    if (key_code == KEY_SPACE) { *output_char = ' '; return 1; }
    if (key_code == KEY_ENTER) { *output_char = '\n'; return 1; }
    return 0;
}

static int default_method_get_candidates(char *buf, int len)
{
    if (buf && len > 0) buf[0] = '\0';
    return 0;
}

int input_register_method(input_method_t *method)
{
    if (!method) return -1;
    memcpy(&current_method, method, sizeof(input_method_t));
    method_registered = 1;
    vga_printf("[INPUT] register IME: %s\n", method->name);
    return 0;
}

int input_switch_method(const char *name)
{
    if (!name) return -1;
    /* 原型阶段: 只有默认输入法 */
    if (strcmp(name, "default") == 0) {
        current_method.active = 1;
        current_method.name = "Default Keyboard";
        current_method.process_key = default_method_process_key;
        current_method.get_candidates = default_method_get_candidates;
        method_registered = 1;
        return 0;
    }
    return -1;
}

int input_device_count(void) { return device_count; }

void input_flush(void)
{
    queue_head = queue_tail = 0;
}
