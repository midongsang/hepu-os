#ifndef TOUCH_HW_H
#define TOUCH_HW_H

#include <stdint.h>
#include "input.h"

/* ============================================================================
 * 赫普OS - 触屏硬件驱动抽象 (V2.50)
 *
 * 目标：为真机触屏控制器（I2C/SPI 电容触屏，如 Goodix GT9xx /
 * FocalTech FT5x06 类）提供统一驱动框架，并把触点数据注入 input 子系统
 * （ABS_MT 事件），使 mobile 手机模式手势直接消费触屏事件。
 *
 * 当前真机硬件未就绪：驱动以框架 + 软件模拟模式交付，
 * 硬件探测失败时自动回退软件模拟（QEMU 可验证整条事件链路）。
 * ========================================================================== */

/* 触屏硬件接口类型 */
typedef enum {
    TOUCH_IF_NONE = 0,
    TOUCH_IF_I2C,     /* I2C 总线 (如 GT911: 0x5D/0x14) */
    TOUCH_IF_SPI,     /* SPI 总线 */
    TOUCH_IF_SIM,     /* 软件模拟 (无硬件/QEMU) */
} touch_if_type_t;

/* 触屏控制器描述 */
typedef struct {
    touch_if_type_t if_type;
    uint32_t i2c_addr;      /* I2C 设备地址 (真机配置) */
    uint32_t i2c_base;      /* I2C 控制器基地址 (真机配置) */
    uint32_t rst_gpio;      /* 复位 GPIO (真机配置) */
    uint32_t int_gpio;      /* 中断 GPIO (真机配置) */
    uint32_t max_x;         /* 最大 X (如 1024) */
    uint32_t max_y;         /* 最大 Y (如 600) */
    uint32_t max_points;    /* 最大触点数 (如 5) */
    int      inverted_x;    /* X 反向 (真机按面板配置) */
    int      inverted_y;
} touch_hw_config_t;

/* 触屏驱动状态 */
typedef struct {
    int initialized;
    int dev_id;             /* input 子系统设备 id */
    touch_hw_config_t cfg;
    int points;             /* 当前活动触点数 */
} touch_hw_state_t;

/* 初始化触屏驱动 (探测硬件；无硬件回退软件模拟) */
int touch_hw_init(void);

/* 轮询读取硬件触点并注入 input 子系统 (由主循环/定时器调用) */
int touch_hw_poll(void);

/* 软件模拟注入触点 (QEMU/测试)：down=1 按下 / 0 抬起 */
int touch_sim_inject(int x, int y, int down);
int touch_sim_inject_slot(int slot, int x, int y, int down);

/* 获取驱动状态 */
touch_hw_state_t *touch_hw_get_state(void);

#endif /* TOUCH_HW_H */
