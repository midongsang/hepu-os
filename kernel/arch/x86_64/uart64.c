/* ============================================================================
 * 赫普OS 64位 - 16550 UART串口驱动
 * 支持COM1-COM4，波特率设置，发送/接收
 * V0.10.157
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 16550 UART寄存器偏移 */
#define UART_RX       0  /* 接收缓冲（读） */
#define UART_TX       0  /* 发送保持（写） */
#define UART_IER      1  /* 中断使能 */
#define UART_IIR      2  /* 中断识别（读） */
#define UART_FCR      2  /* FIFO控制（写） */
#define UART_LCR      3  /* 线路控制 */
#define UART_MCR      4  /* 调制解调器控制 */
#define UART_LSR      5  /* 线路状态 */
#define UART_MSR      6  /* 调制解调器状态 */
#define UART_SCR      7  /* 暂存寄存器 */

/* LCR位定义 */
#define UART_LCR_DLAB 0x80  /* 除数锁存访问位 */
#define UART_LCR_8N1  0x03  /* 8数据位，无校验，1停止位 */

/* LSR位定义 */
#define UART_LSR_DR   0x01  /* 数据就绪 */
#define UART_LSR_THRE 0x20  /* 发送保持寄存器空 */
#define UART_LSR_TEMT 0x40  /* 发送器空 */

/* IER位定义 */
#define UART_IER_RDI  0x01  /* 接收数据中断 */
#define UART_IER_THRI 0x02  /* 发送保持寄存器空中断 */

/* FCR位定义 */
#define UART_FCR_ENABLE 0x01  /* 使能FIFO */
#define UART_FCR_CLEAR_RX 0x02  /* 清除接收FIFO */
#define UART_FCR_CLEAR_TX 0x04  /* 清除发送FIFO */
#define UART_FCR_TRIGGER_14 0xC0  /* 14字节触发 */

/* COM端口基地址 */
#define COM1_PORT 0x3F8
#define COM2_PORT 0x2F8
#define COM3_PORT 0x3E8
#define COM4_PORT 0x2E8

/* 串口设备结构 */
typedef struct {
    uint16_t port;       /* IO端口基地址 */
    uint8_t  irq;        /* 中断号 */
    uint8_t  initialized;/* 是否已初始化 */
    uint32_t baud_rate;  /* 波特率 */
} uart_device_t;

static uart_device_t uart_devices[4] = {
    {COM1_PORT, 4, 0, 115200},
    {COM2_PORT, 3, 0, 115200},
    {COM3_PORT, 4, 0, 115200},
    {COM4_PORT, 3, 0, 115200},
};

/* 函数原型声明 */
int uart64_putc(int com, char c);
int uart64_puts(int com, const char *str);

/* IO端口读写 */
static inline uint8_t uart_inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void uart_outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* 短暂延迟 */
static inline void uart_delay(void)
{
    for (volatile int i = 0; i < 1000; i++) { __asm__ volatile("nop"); }
}

/* 初始化串口 */
int uart64_init(int com)
{
    if (com < 0 || com >= 4) return -1;

    uart_device_t *dev = &uart_devices[com];
    uint16_t port = dev->port;

    /* 禁用中断 */
    uart_outb(port + UART_IER, 0x00);

    /* 设置除数锁存（DLAB=1） */
    uart_outb(port + UART_LCR, UART_LCR_DLAB);

    /* 设置波特率除数（115200 = 115200 / 115200 = 1） */
    uint16_t divisor = 115200 / dev->baud_rate;
    if (divisor == 0) divisor = 1;
    uart_outb(port + 0, divisor & 0xFF);         /* 低字节 */
    uart_outb(port + 1, (divisor >> 8) & 0xFF);  /* 高字节 */

    /* 设置8N1，清除DLAB */
    uart_outb(port + UART_LCR, UART_LCR_8N1);

    /* 使能FIFO，清除收发FIFO，设置14字节触发 */
    uart_outb(port + UART_FCR,
              UART_FCR_ENABLE | UART_FCR_CLEAR_RX |
              UART_FCR_CLEAR_TX | UART_FCR_TRIGGER_14);

    /* 使能接收中断，禁用发送中断 */
    uart_outb(port + UART_IER, UART_IER_RDI);

    /* 设置DTR和RTS */
    uart_outb(port + UART_MCR, 0x03);

    dev->initialized = 1;

    /* 测试：发送初始化完成标记 */
    uart64_puts(com, "[UART] COM");
    uart64_putc(com, '1' + com);
    uart64_puts(com, " initialized at ");
    char buf[16];
    int n = dev->baud_rate, i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
    uart64_puts(com, buf);
    uart64_puts(com, " baud\n");

    return 0;
}

/* 发送一个字符 */
int uart64_putc(int com, char c)
{
    if (com < 0 || com >= 4 || !uart_devices[com].initialized) return -1;

    uint16_t port = uart_devices[com].port;

    /* 等待发送保持寄存器空 */
    int timeout = 100000;
    while (!(uart_inb(port + UART_LSR) & UART_LSR_THRE)) {
        if (--timeout == 0) return -1;
    }

    /* 发送字符 */
    uart_outb(port + UART_TX, (uint8_t)c);

    return 0;
}

/* 发送字符串 */
int uart64_puts(int com, const char *str)
{
    if (!str) return -1;

    int count = 0;
    while (*str) {
        if (uart64_putc(com, *str) == 0) {
            count++;
        }
        str++;
    }
    return count;
}

/* 接收一个字符（非阻塞） */
int uart64_getc_nonblock(int com)
{
    if (com < 0 || com >= 4 || !uart_devices[com].initialized) return -1;

    uint16_t port = uart_devices[com].port;

    /* 检查数据是否就绪 */
    if (!(uart_inb(port + UART_LSR) & UART_LSR_DR)) {
        return -1;  /* 无数据 */
    }

    /* 读取数据 */
    return uart_inb(port + UART_RX);
}

/* 接收一个字符（阻塞） */
char uart64_getc(int com)
{
    if (com < 0 || com >= 4 || !uart_devices[com].initialized) return 0;

    uint16_t port = uart_devices[com].port;

    /* 等待数据就绪 */
    while (!(uart_inb(port + UART_LSR) & UART_LSR_DR)) {
        __asm__ volatile("pause");
    }

    /* 读取数据 */
    return uart_inb(port + UART_RX);
}

/* 检查是否有数据可读 */
int uart64_data_ready(int com)
{
    if (com < 0 || com >= 4 || !uart_devices[com].initialized) return 0;

    uint16_t port = uart_devices[com].port;
    return (uart_inb(port + UART_LSR) & UART_LSR_DR) ? 1 : 0;
}

/* 刷新发送缓冲区 */
void uart64_flush(int com)
{
    if (com < 0 || com >= 4 || !uart_devices[com].initialized) return;

    uint16_t port = uart_devices[com].port;

    /* 等待发送器空 */
    int timeout = 1000000;
    while (!(uart_inb(port + UART_LSR) & UART_LSR_TEMT)) {
        if (--timeout == 0) break;
    }
}

/* 设置波特率 */
int uart64_set_baud(int com, uint32_t baud)
{
    if (com < 0 || com >= 4 || !uart_devices[com].initialized) return -1;
    if (baud == 0 || baud > 115200) return -1;

    uart_device_t *dev = &uart_devices[com];
    uint16_t port = dev->port;

    /* 保存当前LCR */
    uint8_t lcr = uart_inb(port + UART_LCR);

    /* 设置DLAB */
    uart_outb(port + UART_LCR, lcr | UART_LCR_DLAB);

    /* 设置除数 */
    uint16_t divisor = 115200 / baud;
    if (divisor == 0) divisor = 1;
    uart_outb(port + 0, divisor & 0xFF);
    uart_outb(port + 1, (divisor >> 8) & 0xFF);

    /* 恢复LCR */
    uart_outb(port + UART_LCR, lcr);

    dev->baud_rate = baud;
    return 0;
}

/* 获取串口状态 */
void uart64_get_status(int com, uint8_t *lsr, uint8_t *msr)
{
    if (com < 0 || com >= 4 || !uart_devices[com].initialized) return;

    uint16_t port = uart_devices[com].port;
    if (lsr) *lsr = uart_inb(port + UART_LSR);
    if (msr) *msr = uart_inb(port + UART_MSR);
}

/* 串口是否已初始化 */
int uart64_is_initialized(int com)
{
    if (com < 0 || com >= 4) return 0;
    return uart_devices[com].initialized;
}

/* 获取串口数量 */
int uart64_get_count(void)
{
    return 4;
}

/* 获取串口信息 */
int uart64_get_info(int com, uint16_t *port, uint8_t *irq, uint32_t *baud)
{
    if (com < 0 || com >= 4) return -1;

    if (port) *port = uart_devices[com].port;
    if (irq) *irq = uart_devices[com].irq;
    if (baud) *baud = uart_devices[com].baud_rate;
    return 0;
}
