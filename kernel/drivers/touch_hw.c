/* ============================================================================
 * 赫普OS - 触屏硬件驱动 (V2.50)
 *
 * 统一触屏控制器驱动框架：
 *  1. 硬件探测（I2C 触屏控制器，Goodix GT9xx / FocalTech FT5x06 类）
 *  2. 触点读取 → 注入 input 子系统 (ABS_MT 事件)
 *  3. 无硬件时回退软件模拟（touch_sim_inject），QEMU 可验证事件链路
 *
 * 真机适配点（标注 TODO_HW）：
 *  - i2c_base / i2c_addr / rst_gpio / int_gpio 需按真实主板配置
 *  - 坐标转换（分辨率/反向）按真实面板调整
 * ========================================================================== */

#include <stdint.h>
#include "touch_hw.h"
#include "input.h"
#include "vga.h"
#include "string.h"

static touch_hw_state_t touch_state;

/* ----------------------------------------------------------------------------
 * 底层 I/O 抽象 (真机需映射到真实 I2C/GPIO 控制器)
 * 以下端口读写为框架占位——真机由板级 I2C 驱动实现，见 TODO_HW。
 * -------------------------------------------------------------------------- */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* I2C 寄存器读写占位 (真机由 I2C 控制器驱动实现) */
static int touch_i2c_read_reg(uint32_t reg, uint8_t *buf, uint32_t len)
{
    (void)reg; (void)buf; (void)len;
    /* TODO_HW: 真实 I2C 读取——outb(i2c_base+CTRL, ...); 按 GT9xx/FT5x06 时序 */
    return -1;  /* 无硬件时失败 */
}

/* 触屏控制器复位 (真机 GPIO 拉低复位) */
static void touch_reset(void)
{
    /* TODO_HW: 复位 GPIO 时序 (rst_gpio) */
    outb(0x80, 0);
    outb(0x80, 0);
}

/* ----------------------------------------------------------------------------
 * 触屏硬件探测
 * 有硬件则初始化真机控制器；无硬件回退软件模拟。
 * -------------------------------------------------------------------------- */
static int touch_probe_hw(void)
{
    uint8_t id[4] = {0};
    /* GT9xx 类: 读取芯片 ID (寄存器 0x8140) */
    if (touch_i2c_read_reg(0x8140, id, 4) == 0) {
        if (id[0] == 0 && id[1] == 0) {
            return 0;  /* 芯片应答 */
        }
    }
    return -1;  /* 未检测到触屏芯片 */
}

int touch_hw_init(void)
{
    memset(&touch_state, 0, sizeof(touch_state));
    touch_state.cfg.if_type = TOUCH_IF_NONE;
    touch_state.cfg.max_x = 1024;
    touch_state.cfg.max_y = 600;
    touch_state.cfg.max_points = 5;

    /* 尝试硬件探测 */
    if (touch_probe_hw() == 0) {
        touch_state.cfg.if_type = TOUCH_IF_I2C;
        touch_state.cfg.i2c_addr = 0x5D;  /* GT911 默认地址 (真机可改) */
        touch_reset();
        vga_puts("[TOUCH] hardware touchscreen detected (I2C)\n");
    } else {
        touch_state.cfg.if_type = TOUCH_IF_SIM;
        vga_puts("[TOUCH] no hardware touchscreen, using software sim\n");
    }

    /* 注册到 input 子系统 (触屏设备, 多点触摸) */
    input_device_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.name = "HEPU Touchscreen";
    dev.type = INPUT_DEV_TOUCHSCREEN;
    dev.vendor_id = 0x0048;  /* 赫普厂商 id */
    dev.product_id = 0x0001;
    dev.ev_types[0] = 1;  /* EV_SYN */
    dev.ev_types[1] = 1;  /* EV_KEY */
    dev.ev_types[3] = 1;  /* EV_ABS */
    dev.abs_count = 4;    /* X/Y/TOUCH_MAJOR/TRACKING_ID */
    dev.slot_count = touch_state.cfg.max_points;
    dev.current_slot = 0;
    touch_state.dev_id = input_register_device(&dev);

    touch_state.initialized = 1;
    vga_printf("[TOUCH] touchscreen registered (dev %d, %d points, %dx%d)\n",
               touch_state.dev_id,
               (int)touch_state.cfg.max_points,
               (int)touch_state.cfg.max_x,
               (int)touch_state.cfg.max_y);
    return 0;
}

/* ----------------------------------------------------------------------------
 * 触点注入 input 子系统 (ABS_MT 协议)
 * slot 槽位 0 → ABS_MT_SLOT, TRACKING_ID(-1 抬起), POSITION_X/Y
 * -------------------------------------------------------------------------- */
static void touch_inject_point(int slot, int tracking_id, int x, int y)
{
    int did = touch_state.dev_id;
    if (did < 0) return;
    /* 坐标归一化到面板分辨率 */
    if (touch_state.cfg.inverted_x) x = (int)touch_state.cfg.max_x - x;
    if (touch_state.cfg.inverted_y) y = (int)touch_state.cfg.max_y - y;

    input_inject_event(did, EV_ABS, ABS_MT_SLOT, slot);
    input_inject_event(did, EV_ABS, ABS_MT_TRACKING_ID, tracking_id);
    if (tracking_id >= 0) {
        input_inject_event(did, EV_ABS, ABS_MT_POSITION_X, x);
        input_inject_event(did, EV_ABS, ABS_MT_POSITION_Y, y);
        input_inject_event(did, EV_ABS, ABS_MT_TOUCH_MAJOR, 8);
    }
    input_inject_event(did, EV_SYN, 0, 0);
}

/* ----------------------------------------------------------------------------
 * 轮询硬件触点 (由主循环或触屏中断调用)
 * 无硬件时返回 0。
 * -------------------------------------------------------------------------- */
int touch_hw_poll(void)
{
    if (!touch_state.initialized) return 0;
    if (touch_state.cfg.if_type == TOUCH_IF_SIM) return 0;  /* 模拟由 touch_sim_inject 驱动 */

    /* TODO_HW: 读取 GT9xx/FT5x06 坐标寄存器 (最多 max_points 个触点) */
    uint8_t buf[40];
    if (touch_i2c_read_reg(0x814E, buf, 40) != 0) return 0;
    int pts = buf[0] & 0x0F;  /* 触点计数 (GT9xx) */
    if (pts > (int)touch_state.cfg.max_points) pts = touch_state.cfg.max_points;
    for (int i = 0; i < pts; i++) {
        int x = (buf[2 + i * 6] & 0x0F) << 8 | buf[3 + i * 6];
        int y = (buf[4 + i * 6] & 0x0F) << 8 | buf[5 + i * 6];
        touch_inject_point(i, i, x, y);
        touch_state.points = i + 1;
    }
    /* 释放消失的触点 */
    for (int i = pts; i < touch_state.points; i++) {
        touch_inject_point(i, -1, 0, 0);
    }
    touch_state.points = pts;
    return pts;
}

/* ----------------------------------------------------------------------------
 * 软件模拟注入 (QEMU/测试)
 * 向 input 子系统注入单点触摸：down=1 按下, down=0 抬起。
 * 配合 input 子系统手势识别（TAP/SWIPE/LONG_PRESS）与 mobile 手势处理。
 * -------------------------------------------------------------------------- */
int touch_sim_inject(int x, int y, int down)
{
    if (!touch_state.initialized) return -1;
    if (down == 2) {
        /* V4.35-E80: 移动模式——只更新坐标, 不重复注入 TRACKING_ID (避免误判双指) */
        input_inject_event(touch_state.dev_id, EV_ABS, ABS_MT_POSITION_X, x);
        input_inject_event(touch_state.dev_id, EV_ABS, ABS_MT_POSITION_Y, y);
    } else {
        touch_inject_point(0, down ? 0 : -1, x, y);
    }
    touch_state.points = down ? 1 : 0;
    return 0;
}

/* V4.35-E80: 多触点注入 (slot 0-4)——pinch/zoom 手势验证 */
int touch_sim_inject_slot(int slot, int x, int y, int down)
{
    if (!touch_state.initialized) return -1;
    if (slot < 0 || slot > 4) return -1;
    touch_inject_point(slot, down ? slot : -1, x, y);
    return 0;
}

touch_hw_state_t *touch_hw_get_state(void)
{
    return &touch_state;
}
