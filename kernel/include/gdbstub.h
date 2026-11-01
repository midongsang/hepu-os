/*
 * 赫普OS - GDB远程调试桩（gdbstub）
 * 通过串口实现GDB远程调试协议，支持断点、单步、内存/寄存器读写
 *
 * GDB远程协议基础：
 * - 数据包格式：$<data>#<checksum>
 * - checksum = data所有字节和的低8位，十六进制
 * - 响应：+（ACK）/ -（NAK）
 *
 * 支持命令：
 * - ?  查询停止原因
 * - g  读取所有寄存器
 * - G  写入所有寄存器
 * - m  读取内存 (m addr,length)
 * - M  写入内存 (M addr,length:data)
 * - c  继续执行 (c addr)
 * - s  单步执行 (s addr)
 * - Z0 插入软件断点 (Z0 addr,kind)
 * - z0 移除软件断点 (z0 addr,kind)
 * - D  断开连接
 * - k  杀死程序
 *
 * V6.84: 初始实现
 */

#ifndef GDBSTUB_H
#define GDBSTUB_H

#include <stdint.h>

/* GDBstub状态 */
typedef enum {
    GDBSTUB_STATE_DISCONNECTED = 0,
    GDBSTUB_STATE_CONNECTED,
    GDBSTUB_STATE_RUNNING,
    GDBSTUB_STATE_STOPPED,
} gdbstub_state_t;

/* 初始化GDBstub */
void gdbstub_init(uint16_t serial_base);

/* 处理GDB命令（在断点/异常中断中调用） */
void gdbstub_handle_exception(uint32_t exception_type, uint32_t *regs);

/* 检查是否有GDB连接 */
int gdbstub_is_connected(void);

/* 手动中断到GDB */
void gdbstub_break(void);

/* 发送O包（控制台输出） */
void gdbstub_output(const char *msg);

#endif /* GDBSTUB_H */
