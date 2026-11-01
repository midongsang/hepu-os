/* ============================================================================
 * 赫普OS - 8259 PIC 可编程中断控制器驱动
 * 功能: 初始化PIC，重新映射IRQ到中断号32-47，管理IRQ屏蔽
 * ========================================================================== */

#include <stdint.h>
#include "pic.h"
#include "vga.h"

/* x86 IO端口读写 */
static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void)
{
    __asm__ volatile ("outb %%al, $0x80" : : "a"(0));
}

/* 初始化PIC
 * 主PIC IRQ0-7 -> 中断号 32-39
 * 从PIC IRQ8-15 -> 中断号 40-47
 * 主PIC的IRQ2连接从PIC */
void pic_init(void)
{
    /* 保存当前掩码 */
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    /* ICW1: 开始初始化 */
    outb(PIC1_CMD, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    io_wait();
    outb(PIC2_CMD, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    io_wait();

    /* ICW2: 设置中断向量偏移 */
    outb(PIC1_DATA, 0x20);  /* 主PIC: 中断号32开始 */
    io_wait();
    outb(PIC2_DATA, 0x28);  /* 从PIC: 中断号40开始 */
    io_wait();

    /* ICW3: 告诉主PIC从PIC在IRQ2，告诉从PIC级联ID=2 */
    outb(PIC1_DATA, 0x04);  /* 主PIC: IRQ2有从PIC */
    io_wait();
    outb(PIC2_DATA, 0x02);  /* 从PIC: 级联ID=2 */
    io_wait();

    /* ICW4: 设置8086模式 */
    outb(PIC1_DATA, PIC_ICW4_8086);
    io_wait();
    outb(PIC2_DATA, PIC_ICW4_8086);
    io_wait();

    /* 恢复掩码（先全部屏蔽，需要时再开启） */
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);

    vga_puts("[PIC] 8259 PIC init done \n");
    vga_puts("[PIC] IRQ mapping: master PIC=32-39, slave PIC=40-47\n");
}

/* 发送EOI */
void pic_send_eoi(uint8_t irq)
{
    if (irq >= 8) {
        outb(PIC2_CMD, PIC_EOI);  /* 从PIC */
    }
    outb(PIC1_CMD, PIC_EOI);      /* 主PIC */
}

/* 屏蔽IRQ */
void pic_mask_irq(uint8_t irq)
{
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    value = inb(port) | (1 << irq);
    outb(port, value);
}

/* 取消屏蔽IRQ */
void pic_unmask_irq(uint8_t irq)
{
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    value = inb(port) & ~(1 << irq);
    outb(port, value);
}

/* 设置掩码 */
void pic_set_mask(uint16_t mask)
{
    outb(PIC1_DATA, mask & 0xFF);
    outb(PIC2_DATA, (mask >> 8) & 0xFF);
}
