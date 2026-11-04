/* ============================================================================
 * 赫普OS - PS/2 鼠标驱动实现 (V2.23新增)
 * 8042 辅助设备完整初始化 + 轮询读包. 真实标准: 8042 / PS/2 mouse spec
 * ========================================================================== */
#include <stdint.h>
#include "ps2mouse.h"
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

/* 等待 8042 输入缓冲空闲 (bit1=0 可写) */
static int ps2_wait_input(void)
{
    for (int i = 0; i < 100000; i++) {
        if (!(inb(PS2_STATUS_PORT) & 0x02)) return 1;
    }
    return 0;
}

/* 等待 8042 输出缓冲有数据 (bit0=1) */
static int ps2_wait_output(void)
{
    for (int i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS_PORT) & 0x01) return 1;
    }
    return 0;
}

/* 向鼠标发送命令 (0xD4 前缀) */
int ps2mouse_command(uint8_t cmd)
{
    if (!ps2_wait_input()) return -1;
    outb(PS2_CMD_PORT, PS2_CMD_WRITE_AUX);
    if (!ps2_wait_input()) return -1;
    outb(PS2_DATA_PORT, cmd);
    if (!ps2_wait_output()) return -1;
    uint8_t ack = inb(PS2_DATA_PORT);
    return (ack == MOUSE_ACK) ? 0 : -1;
}

int ps2mouse_init(void)
{
    /* 1. 启用辅助设备接口 */
    if (!ps2_wait_input()) return -1;
    outb(PS2_CMD_PORT, PS2_CMD_ENABLE_AUX);
    if (!ps2_wait_input()) return -1;

    /* 2. 读命令字节, 置位辅助设备IRQ使能(bit1), 清时钟禁用(bit5) */
    outb(PS2_CMD_PORT, PS2_CMD_READ_CONFIG);
    if (!ps2_wait_output()) return -1;
    uint8_t cfg = inb(PS2_DATA_PORT);
    cfg |= 0x02;      /* enable aux IRQ */
    cfg &= ~0x20;     /* enable aux clock */
    if (!ps2_wait_input()) return -1;
    outb(PS2_CMD_PORT, PS2_CMD_WRITE_CONFIG);
    if (!ps2_wait_input()) return -1;
    outb(PS2_DATA_PORT, cfg);
    if (!ps2_wait_input()) return -1;

    /* 3. 复位鼠标 (0xFF): ACK + 设备ID */
    if (ps2mouse_command(MOUSE_CMD_RESET) == 0) {
        /* 读设备ID (0x00 标准 / 0x03 IntelliMouse) */
        if (ps2_wait_output()) {
            (void)inb(PS2_DATA_PORT);
        }
    }

    /* 4. 启用数据报告 (0xF4) */
    if (ps2mouse_command(MOUSE_CMD_ENABLE) != 0) {
        vga_puts("[MOUSE] PS/2 mouse enable report failed\n");
        return -1;
    }

    vga_puts("[MOUSE] PS/2 mouse ready (aux device, IRQ12)\n");
    return 0;
}

/* 轮询读取 3 字节标准 PS/2 鼠标包 */
int ps2mouse_poll(ps2_mouse_event_t *ev)
{
    if (!ev) return 0;

    /* 检查 8042 输出缓冲有辅助设备数据 (bit0 输出满 + bit5 辅助设备) */
    uint8_t st = inb(PS2_STATUS_PORT);
    if (!(st & 0x01)) return 0;        /* 输出缓冲空 */
    if (!(st & 0x20)) return 0;        /* 不是辅助设备数据 */

    /* 读首字节 (含同步位 bit3=1) */
    uint8_t b0 = inb(PS2_DATA_PORT);
    if (!(b0 & 0x08)) return 0;        /* 丢包/未同步, 丢弃 */

    /* 等待并读后续 2 字节 */
    if (!ps2_wait_output()) return 0;
    uint8_t b1 = inb(PS2_DATA_PORT);
    if (!ps2_wait_output()) return 0;
    uint8_t b2 = inb(PS2_DATA_PORT);

    /* 带符号移动量 (9 位补码) */
    int dx = (int)(int8_t)b1;
    int dy = (int)(int8_t)b2;
    /* 溢出位 */
    if (b0 & 0x40) dx = (dx > 0) ? 127 : -127;  /* X 溢出 */
    if (b0 & 0x80) dy = (dy > 0) ? 127 : -127;  /* Y 溢出 */

    ev->dx = dx;
    ev->dy = -dy;      /* PS/2 Y 轴向上为正, 屏幕坐标需翻转 */
    ev->left   = (b0 & 0x01) ? 1 : 0;
    ev->right  = (b0 & 0x02) ? 1 : 0;
    ev->middle = (b0 & 0x04) ? 1 : 0;
    ev->valid  = 1;
    return 1;
}
