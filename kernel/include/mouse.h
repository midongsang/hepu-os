#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

/* 鼠标按钮 */
#define MOUSE_LEFT   0x01
#define MOUSE_RIGHT  0x02
#define MOUSE_MIDDLE 0x04

/* 鼠标状态 */
typedef struct {
    int x, y;           /* 当前位置 */
    int dx, dy;         /* 最后一次移动增量 */
    uint8_t buttons;    /* 按钮状态 */
    int initialized;    /* 是否初始化 */
} mouse_state_t;

/* 初始化PS/2鼠标 */
void mouse_init(void);

/* 鼠标中断处理 (IRQ12) */
void mouse_handler(void);

/* 获取鼠标状态 */
mouse_state_t *mouse_get_state(void);

/* 获取鼠标X坐标 */
int mouse_get_x(void);

/* 获取鼠标Y坐标 */
int mouse_get_y(void);

/* 获取按钮状态 */
uint8_t mouse_get_buttons(void);

/* 检查按钮是否按下 */
int mouse_button_pressed(uint8_t button);

#endif /* MOUSE_H */
