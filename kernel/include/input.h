#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

#define INPUT_MAX_DEVICES 16
#define INPUT_MAX_LISTENERS 16
#define INPUT_EVENT_QUEUE_SIZE 256

/* 输入设备类型 */
typedef enum {
    INPUT_DEV_KEYBOARD = 0,
    INPUT_DEV_MOUSE,
    INPUT_DEV_TOUCHSCREEN,
    INPUT_DEV_GAMEPAD,
    INPUT_DEV_JOYSTICK,
    INPUT_DEV_TRACKBALL,
    INPUT_DEV_PEN,
    INPUT_DEV_REMOTE,
} input_dev_type_t;

/* 事件类型 */
typedef enum {
    EV_SYN = 0,      /* 同步事件 */
    EV_KEY,          /* 按键 */
    EV_REL,          /* 相对轴 */
    EV_ABS,          /* 绝对轴 */
    EV_MSC,          /* 杂项 */
    EV_LED,          /* LED */
    EV_SND,          /* 声音 */
    EV_REP,          /* 重复 */
    EV_FF,           /* 力反馈 */
} input_event_type_t;

/* 按键码 (简化) */
#define KEY_RESERVED 0
#define KEY_ESC 1
#define KEY_ENTER 28
#define KEY_SPACE 57
#define KEY_BACK 158
#define KEY_HOME 102
#define KEY_MENU 139
#define KEY_VOLUMEUP 115
#define KEY_VOLUMEDOWN 114
#define KEY_POWER 116
#define KEY_CAMERA 212

/* 绝对轴码 */
#define ABS_X 0
#define ABS_Y 1
#define ABS_Z 2
#define ABS_RX 3
#define ABS_RY 4
#define ABS_RZ 5
#define ABS_MT_SLOT 47
#define ABS_MT_TOUCH_MAJOR 48
#define ABS_MT_POSITION_X 53
#define ABS_MT_POSITION_Y 54
#define ABS_MT_TRACKING_ID 57

/* 相对轴码 */
#define REL_X 0
#define REL_Y 1
#define REL_WHEEL 8

/* 输入事件 */
typedef struct {
    uint16_t type;
    uint16_t code;
    int32_t  value;
    uint64_t timestamp;
} input_event_t;

/* 多点触摸触点 */
typedef struct {
    int tracking_id;
    int x, y;
    int pressure;
    int active;
} input_touch_slot_t;

/* 输入设备 */
typedef struct {
    int used;
    int id;
    const char *name;
    input_dev_type_t type;
    int vendor_id;
    int product_id;
    /* 能力位 (简化) */
    int ev_types[8];
    int key_codes[32];
    int abs_count;
    int rel_count;
    /* 当前状态 */
    int key_state[32];
    int abs_value[16];
    int rel_value[8];
    /* 多点触摸 */
    input_touch_slot_t touch_slots[10];
    int slot_count;
    int current_slot;
    /* 回调 */
    void (*event_callback)(input_event_t *event);
} input_device_t;

/* 输入监听器 */
typedef struct {
    int used;
    int id;
    int device_filter;  /* -1 = 所有设备 */
    int type_filter;    /* -1 = 所有类型 */
    void (*callback)(input_event_t *event, void *user_data);
    void *user_data;
} input_listener_t;

/* 初始化输入子系统 */
void input_init(void);

/* 注册输入设备 */
int input_register_device(input_device_t *dev);

/* 注销输入设备 */
int input_unregister_device(int dev_id);

/* 注入事件 (驱动调用) */
int input_inject_event(int dev_id, uint16_t type, uint16_t code, int32_t value);

/* 读取事件 (阻塞/非阻塞) */
int input_read_event(input_event_t *event);

/* 注册事件监听器 */
int input_add_listener(int device_filter, int type_filter,
                       void (*callback)(input_event_t *, void *), void *user_data);

/* 移除监听器 */
int input_remove_listener(int listener_id);

/* 获取设备信息 */
int input_get_device_info(int dev_id, input_device_t *info);

/* 获取按键状态 */
int input_get_key_state(int dev_id, int key_code);

/* 获取绝对轴值 */
int input_get_abs_value(int dev_id, int axis);

/* 获取相对轴值 (并清零) */
int input_get_rel_value(int dev_id, int axis);

/* 获取触摸点 */
int input_get_touch(int dev_id, int slot, input_touch_slot_t *touch);

/* 触摸点数 */
int input_touch_count(int dev_id);

/* 手势识别 */
typedef enum {
    GESTURE_NONE = 0,
    GESTURE_TAP,
    GESTURE_LONG_PRESS,
    GESTURE_SWIPE_LEFT,
    GESTURE_SWIPE_RIGHT,
    GESTURE_SWIPE_UP,
    GESTURE_SWIPE_DOWN,
    GESTURE_PINCH,
    GESTURE_ZOOM,
    GESTURE_DOUBLE_TAP,
    GESTURE_TYPE_MAX
} gesture_type_t;

typedef struct {
    gesture_type_t type;
    int x, y;
    int dx, dy;
    float scale;
} gesture_event_t;

/* 手势识别回调 */
typedef void (*gesture_callback_t)(gesture_event_t *gesture);

/* V4.38-E83: 手势阈值配置 */
int input_gesture_config(int lp, int dt, int sw);
void input_gesture_cfg_get(int *lp, int *dt, int *sw);

/* V4.39-E84: 手势事件队列轮询 */
int input_gesture_poll(gesture_event_t *out);
void input_gesture_queue_clear(void);
void input_gesture_queue_stats(int *pending, int *dropped);

/* 启用手势识别 */
int input_enable_gestures(int enable);
uint32_t input_gesture_count(int type);

/* 注册手势回调 */
int input_set_gesture_callback(gesture_callback_t cb);

/* 输入法框架 */
typedef struct {
    int active;
    const char *name;
    int (*process_key)(int key_code, int *output_char);
    int (*get_candidates)(char *buf, int len);
} input_method_t;

/* 注册输入法 */
int input_register_method(input_method_t *method);

/* 切换输入法 */
int input_switch_method(const char *name);

/* 获取活跃设备数 */
int input_device_count(void);

/* 清空事件队列 */
void input_flush(void);

#endif /* INPUT_H */
