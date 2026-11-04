#ifndef HEPU_SERIAL_H
#define HEPU_SERIAL_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS - 串口 UART 16550 驱动 (V2.23新增)
 * 标准 16550 UART, COM1 = 0x3F8. 用于调试输出/串口控制台.
 * 真实标准: National Semiconductor 16550 UART
 * ========================================================================== */

#define COM1_BASE 0x3F8
#define COM2_BASE 0x2F8
#define COM3_BASE 0x3E8
#define COM4_BASE 0x2E8

/* UART 寄存器偏移 (相对基址) */
#define UART_RBR  0  /* 接收缓冲 (读) */
#define UART_THR  0  /* 发送保持 (写) */
#define UART_IER  1  /* 中断使能 */
#define UART_IIR  2  /* 中断标识 (读) */
#define UART_FCR  2  /* FIFO 控制 (写) */
#define UART_LCR  3  /* 线路控制 */
#define UART_MCR  4  /* 调制解调器控制 */
#define UART_LSR  5  /* 线路状态 */
#define UART_MSR  6  /* 调制解调器状态 */

/* LSR 位 */
#define UART_LSR_DR   0x01  /* 数据就绪 */
#define UART_LSR_THRE 0x20  /* 发送保持空 */

/* LCR 位 */
#define UART_LCR_DLAB 0x80  /* 除数锁存访问 */
#define UART_LCR_8N1  0x03  /* 8位, 无校验, 1停止位 */

/* MCR 位 */
#define UART_MCR_DTR_RTS 0x03

/* 初始化指定串口 (base), 波特率由 divisor 决定 (115200/divisor) */
void serial_init(uint16_t base, uint16_t divisor);

/* 发送单字符 */
void serial_putc(uint16_t base, char c);

/* 发送字符串 */
void serial_puts(uint16_t base, const char *s);

/* 是否有接收数据 */
int serial_rx_ready(uint16_t base);

/* 读取一字节 (无等待) */
char serial_getc(uint16_t base);

/* 初始化 COM1 并打印 */
void serial_init_com1(void);

#endif /* HEPU_SERIAL_H */
void serial_puthex(uint16_t base, uint32_t val);
