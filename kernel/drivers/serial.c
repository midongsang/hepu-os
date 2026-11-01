/* ============================================================================
 * 赫普OS - 串口 UART 16550 驱动实现 (V2.23新增)
 * 标准 16550 UART 初始化/收发. 参考: 16550 datasheet / OSDev "Serial ports"
 * ========================================================================== */
#include <stdint.h>
#include "serial.h"
#include "vga.h"

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* 初始化 16550: 8N1, 禁用中断, 使能FIFO, 波特率=115200/divisor */
void serial_init(uint16_t base, uint16_t divisor)
{
    outb((uint16_t)(base + UART_IER), 0x00);      /* 禁用所有中断 */
    outb((uint16_t)(base + UART_LCR), UART_LCR_DLAB); /* 设 DLAB */
    outb((uint16_t)(base + UART_THR), (uint8_t)(divisor & 0xFF));      /* 分频低字节 */
    outb((uint16_t)(base + UART_IER), (uint8_t)((divisor >> 8) & 0xFF)); /* 分频高字节 */
    outb((uint16_t)(base + UART_LCR), UART_LCR_8N1); /* 8N1 */
    outb((uint16_t)(base + UART_FCR), 0x07);      /* 使能并清空 FIFO */
    outb((uint16_t)(base + UART_MCR), UART_MCR_DTR_RTS); /* DTR+RTS */
    /* 清残留中断 */
    (void)inb((uint16_t)(base + UART_IIR));
    (void)inb((uint16_t)(base + UART_MSR));
}

void serial_putc(uint16_t base, char c)
{
    /* 等待 THR 空 (LSR bit5) */
    for (int i = 0; i < 100000; i++) {
        if (inb((uint16_t)(base + UART_LSR)) & UART_LSR_THRE) {
            break;
        }
    }
    outb((uint16_t)(base + UART_THR), (uint8_t)c);
    /* 换行时补 \r, 否则终端只换行不回车 */
    if (c == '\n') {
        for (int i = 0; i < 100000; i++) {
            if (inb((uint16_t)(base + UART_LSR)) & UART_LSR_THRE) {
                break;
            }
        }
        outb((uint16_t)(base + UART_THR), (uint8_t)'\r');
    }
}

void serial_puts(uint16_t base, const char *s)
{
    while (s && *s) {
        serial_putc(base, *s);
        s++;
    }
}

int serial_rx_ready(uint16_t base)
{
    return (inb((uint16_t)(base + UART_LSR)) & UART_LSR_DR) != 0;
}

char serial_getc(uint16_t base)
{
    return (char)inb((uint16_t)(base + UART_RBR));
}

void serial_init_com1(void)
{
    serial_init(COM1_BASE, 1);   /* divisor=1 → 115200 baud */
    serial_puts(COM1_BASE, "[SERIAL] COM1 16550 UART ready (115200 8N1)\n");
    vga_puts("[OK] serial COM1 16550 UART (debug output)\n");
}

/* V3.81 调试辅助: 串口输出 32 位十六进制 */
void serial_puthex(uint16_t base, uint32_t val)
{
    const char *hex = "0123456789ABCDEF";
    serial_puts(base, "0x");
    for (int i = 7; i >= 0; i--) {
        serial_putc(base, hex[(val >> (i * 4)) & 0xF]);
    }
}
