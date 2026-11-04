#ifndef TOUCH_H
#define TOUCH_H

#include <stdint.h>

#define TOUCH_MAX_POINTS 5

/* 触摸点状态 */
typedef struct {
    int active;
    int x, y;
    int pressure;
    int id;
} touch_point_t;

/* 触摸事件类型 */
typedef enum {
    TOUCH_DOWN = 0,
    TOUCH_UP,
    TOUCH_MOVE,
} touch_event_type_t;

/* 触摸事件 */
typedef struct {
    touch_event_type_t type;
    int point_id;
    int x, y;
    int pressure;
} touch_event_t;

/* 触摸回调 */
typedef void (*touch_callback_t)(touch_event_t *event);

/* 初始化触屏 */
void touch_init(void);

/* 读取触摸状态 */
int touch_get_points(touch_point_t *points, int max);

/* 注册触摸回调 */
void touch_register_callback(touch_callback_t cb);

/* 处理触屏中断 */
void touch_handle_irq(void);

/* 获取最后触摸位置 */
int touch_last_x(void);
int touch_last_y(void);
int touch_is_pressed(void);

/* 触屏校准 */
void touch_calibrate(void);

#endif /* TOUCH_H */
