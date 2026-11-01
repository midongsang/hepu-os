#ifndef PIC_H
#define PIC_H

#include <stdint.h>

/* 8259 PIC 端口 */
#define PIC1_CMD  0x20   /* 主PIC命令端口 */
#define PIC1_DATA 0x21   /* 主PIC数据端口 */
#define PIC2_CMD  0xA0   /* 从PIC命令端口 */
#define PIC2_DATA 0xA1   /* 从PIC数据端口 */

/* ICW1 初始化控制字 */
#define PIC_ICW1_INIT    0x10  /* 初始化 */
#define PIC_ICW1_ICW4    0x01  /* 需要ICW4 */

/* ICW4 */
#define PIC_ICW4_8086    0x01  /* 8086模式 */

/* OCW2 操作控制字 */
#define PIC_EOI          0x20  /* 中断结束 */

/* IRQ 号 */
#define IRQ_TIMER    0    /* PIT时钟 */
#define IRQ_KEYBOARD 1    /* 键盘 */
#define IRQ_CASCADE  2    /* PIC级联 */
#define IRQ_COM2     3
#define IRQ_COM1     4
#define IRQ_LPT2     5
#define IRQ_FLOPPY   6
#define IRQ_LPT1     7
#define IRQ_RTC      8
#define IRQ_MOUSE    12   /* PS/2鼠标 */
#define IRQ_FPU      13
#define IRQ_ATA1     14
#define IRQ_ATA2     15

/* 初始化PIC，重新映射IRQ到中断号32-47 */
void pic_init(void);

/* 发送EOI（中断结束）信号 */
void pic_send_eoi(uint8_t irq);

/* 屏蔽/取消屏蔽某个IRQ */
void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq);

/* 设置IRQ掩码（16位，1=屏蔽） */
void pic_set_mask(uint16_t mask);

#endif /* PIC_H */
