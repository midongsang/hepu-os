#ifndef HEPU_PS2MOUSE_H
#define HEPU_PS2MOUSE_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS - PS/2 鼠标驱动 (V2.23新增)
 * 8042 辅助设备, 端口 0x60(数据)/0x64(状态/命令).
 * 真实标准: 8042 PS/2 规范 / OSDev "Mouse Input"
 * ========================================================================== */

#define PS2_DATA_PORT 0x60
#define PS2_STATUS_PORT 0x64
#define PS2_CMD_PORT   0x64

/* 8042 命令 */
#define PS2_CMD_ENABLE_AUX    0xA8  /* 启用辅助设备 */
#define PS2_CMD_READ_CONFIG   0x20  /* 读命令字节 */
#define PS2_CMD_WRITE_CONFIG  0x60  /* 写命令字节 */
#define PS2_CMD_WRITE_AUX     0xD4  /* 写字节到辅助设备 */

/* 鼠标命令 (经 0xD4 发送) */
#define MOUSE_CMD_RESET       0xFF
#define MOUSE_CMD_ENABLE      0xF4  /* 启用数据报告 */
#define MOUSE_CMD_DISABLE     0xF5
#define MOUSE_CMD_READ_ID     0xF2

#define MOUSE_ACK             0xFA

typedef struct {
    int      dx;       /* X 移动量 (带符号) */
    int      dy;       /* Y 移动量 (带符号) */
    uint8_t  left;     /* 左键 */
    uint8_t  right;    /* 右键 */
    uint8_t  middle;   /* 中键 */
    uint8_t  valid;    /* 1=有效包 */
} ps2_mouse_event_t;

/* 初始化 PS/2 鼠标 (启用辅助设备 + 启用数据报告) */
int ps2mouse_init(void);

/* 向鼠标发送命令, 返回 0=ACK 成功 */
int ps2mouse_command(uint8_t cmd);

/* 轮询读取一个鼠标包; 返回 1=有有效包, 0=无 */
int ps2mouse_poll(ps2_mouse_event_t *ev);

#endif /* HEPU_PS2MOUSE_H */
