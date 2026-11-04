/* ============================================================================
 * 赫普OS - PS/2 鼠标驱动 (IRQ12)
 * ========================================================================== */

#include <stdint.h>
#include "mouse.h"
#include "vga.h"
#include "irq.h"
#include "vga_gfx.h"
#include "softirq.h"  /* V3.87-E13 */

/* PS/2 控制器端口 */
#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_COMMAND 0x64

/* 状态寄存器位 */
#define PS2_OUTPUT_FULL  0x01
#define PS2_INPUT_FULL   0x02

/* 鼠标命令 */
#define MOUSE_SET_DEFAULTS 0xF6
#define MOUSE_ENABLE       0xF4
#define MOUSE_DISABLE      0xF5
#define MOUSE_ACK          0xFA

static mouse_state_t mouse = {0, 0, 0, 0, 0, 0};

/* V3.87-E13: 底半部前向声明(softirq_register 调用点在定义之前) */
static void softirq_mouse_poll(void);

/* V3.87-E13: 原始字节环形缓冲——顶半部只读 0x60 入队, 底半部消费 */
#define MOUSE_RAW_BUF 16
static uint8_t mouse_raw[MOUSE_RAW_BUF];
static volatile uint8_t mouse_raw_head = 0;
static volatile uint8_t mouse_raw_tail = 0;

static void mouse_raw_put(uint8_t b)
{
    uint8_t next = (uint8_t)((mouse_raw_head + 1) % MOUSE_RAW_BUF);
    if (next != mouse_raw_tail) {        /* 满则丢弃最旧语义: 丢新字节 */
        mouse_raw[mouse_raw_head] = b;
        mouse_raw_head = next;
    }
}

/* 鼠标数据包接收状态 */
static uint8_t mouse_packet[3];
static uint8_t mouse_packet_idx = 0;

/* ----------------------------------------------------------------------------
 * PS/2 控制器辅助函数
 * -------------------------------------------------------------------------- */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void) {
    __asm__ volatile ("outb %%al, $0x80" : : "a"(0));
}

/* 等待输入缓冲区为空 */
static void ps2_wait_input(void) {
    int timeout = 10000;
    while ((inb(PS2_STATUS) & PS2_INPUT_FULL) && timeout--) {
        io_wait();
    }
}

/* 等待输出缓冲区满 */
static uint8_t ps2_wait_output(void) {
    int timeout = 10000;
    while (!(inb(PS2_STATUS) & PS2_OUTPUT_FULL) && timeout--) {
        io_wait();
    }
    return inb(PS2_DATA);
}

/* 发送命令给鼠标 */
static void mouse_write(uint8_t cmd) {
    ps2_wait_input();
    outb(PS2_COMMAND, 0xD4);  /* 告诉控制器下一个字节发给鼠标 */
    ps2_wait_input();
    outb(PS2_DATA, cmd);
}

/* 从鼠标读取数据 */
static uint8_t mouse_read(void) {
    return ps2_wait_output();
}

/* ----------------------------------------------------------------------------
 * 初始化鼠标
 * -------------------------------------------------------------------------- */
void mouse_init(void)
{
    /* 1. 启用第二个PS/2端口 (鼠标) */
    ps2_wait_input();
    outb(PS2_COMMAND, 0xA8);  /* 启用辅助PS/2设备 */

    /* 2. 清空输出缓冲区 */
    while (inb(PS2_STATUS) & PS2_OUTPUT_FULL) {
        inb(PS2_DATA);
    }

    /* 3. 设置默认参数 */
    mouse_write(MOUSE_SET_DEFAULTS);
    mouse_read();  /* ACK */

    /* 4. 启用鼠标数据包流 */
    mouse_write(MOUSE_ENABLE);
    mouse_read();  /* ACK */

    /* 5. 注册IRQ12中断处理 + 底半部 */
    irq_register_handler(IRQ_MOUSE, mouse_handler);
    softirq_register(SOFTIRQ_MOUSE, softirq_mouse_poll);  /* V3.87-E13 */

    mouse.initialized = 1;
    mouse.x = 160;  /* 屏幕中心 */
    mouse.y = 100;

    vga_puts("[MOUSE] PS/2 mouse init done(IRQ12)\n");
}

/* ----------------------------------------------------------------------------
 * 鼠标中断处理
 * 数据包格式 (3字节):
 *   byte0: Yovf|Xovf|Ysign|Xsign|1|Middle|Right|Left
 *   byte1: X movement
 *   byte2: Y movement
 * -------------------------------------------------------------------------- */
/* V3.87-E13: 顶半部——只读尽 0x60 全部字节入环形缓冲(防 FIFO 锁死),
 * 标记鼠标底半部待执行; 状态机/位置计算全部移到底半部. */
void mouse_handler(void)
{
    extern void iopolicy_irq(int);
    iopolicy_irq(2);   /* IO_DEV_MOUSE: 中断计数 (E-47) */

    uint32_t m_guard = 0;
    while (inb(PS2_STATUS) & PS2_OUTPUT_FULL) {
        if (++m_guard > 100000) break;   /* E5 同款超时保护 */
        mouse_raw_put(inb(PS2_DATA));
        if (mouse_raw_head == (uint8_t)((mouse_raw_tail + 1) % MOUSE_RAW_BUF)) {
            /* 环形缓冲从空变非空: 只需 raise 一次 */
            softirq_raise(SOFTIRQ_MOUSE);
        }
    }
}

/* V3.87-E13: 底半部——原鼠标状态机(3 字节包组装 + 位置更新), 主循环执行 */
static void softirq_mouse_poll(void)
{
    while (mouse_raw_head != mouse_raw_tail) {
        uint8_t data = mouse_raw[mouse_raw_tail];
        mouse_raw_tail = (uint8_t)((mouse_raw_tail + 1) % MOUSE_RAW_BUF);
        switch (mouse_packet_idx) {
        case 0:
            /* 第一个字节: 检查bit3是否为1 (同步) */
            if ((data & 0x08) == 0) {
                /* 不同步，丢弃 */
                return;
            }
            mouse_packet[0] = data;
            mouse_packet_idx = 1;
            break;

        case 1:
            mouse_packet[1] = data;
            mouse_packet_idx = 2;
            break;

        case 2:
            mouse_packet[2] = data;
            mouse_packet_idx = 0;

            /* 解析数据包 */
            uint8_t flags = mouse_packet[0];
            int8_t dx = (int8_t)mouse_packet[1];
            int8_t dy = (int8_t)mouse_packet[2];

            /* 处理符号扩展 */
            if (flags & 0x10) dx |= 0xFFFFFF00;  /* X负 */
            if (flags & 0x20) dy |= 0xFFFFFF00;  /* Y负 */

            /* 更新位置 (Y轴翻转: 鼠标向上=屏幕Y减小) */
            mouse.dx = dx;
            mouse.dy = -dy;
            mouse.x += dx;
            mouse.y -= dy;

            /* 限制在屏幕范围内 */
            if (mouse.x < 0) mouse.x = 0;
            if (mouse.x > gfx_width - 1) mouse.x = gfx_width - 1;
            if (mouse.y < 0) mouse.y = 0;
            if (mouse.y > gfx_height - 1) mouse.y = gfx_height - 1;

            /* 按钮状态 */
            mouse.buttons = flags & 0x07;
            break;
        }
    }
}

mouse_state_t *mouse_get_state(void)
{
    return &mouse;
}

int mouse_get_x(void) { return mouse.x; }
int mouse_get_y(void) { return mouse.y; }
uint8_t mouse_get_buttons(void) { return mouse.buttons; }

int mouse_button_pressed(uint8_t button)
{
    return (mouse.buttons & button) ? 1 : 0;
}
