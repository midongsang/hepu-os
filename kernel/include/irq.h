#ifndef IRQ_H
#define IRQ_H
#include <stdint.h>

/* IRQ 号 (PIC重映射后, 中断号32-47) */
#define IRQ_TIMER    0   /* 8254 PIT */
#define IRQ_KEYBOARD 1   /* PS/2键盘 */
#define IRQ_CASCADE  2   /* PIC级联 */
#define IRQ_COM2     3
#define IRQ_COM1     4
#define IRQ_LPT2     5
#define IRQ_FLOPPY   6
#define IRQ_LPT1     7
#define IRQ_RTC      8
#define IRQ_MOUSE    12  /* PS/2鼠标 */
#define IRQ_FPU      13
#define IRQ_ATA1     14
#define IRQ_ATA2     15

/* IRQ 处理函数类型 */
typedef void (*irq_handler_t)(void);

/* 初始化IRQ框架 */
void irq_init(void);

/* 注册IRQ处理函数 */
void irq_register_handler(uint8_t irq, irq_handler_t handler);

/* 注销IRQ处理函数 */
void irq_unregister_handler(uint8_t irq);

/* IRQ通用处理函数（由汇编入口调用） */
void irq_handler(uint32_t irq_number);

/* 开中断/关中断 */
static inline void irq_enable(void) { __asm__ volatile ("sti"); }
static inline void irq_disable(void) { __asm__ volatile ("cli"); }
/* V3.87-E3: 保存/恢复 EFLAGS(含中断标志)——用于中断安全临界区 */
static inline uint32_t irq_save(void)
{
    uint32_t f;
    __asm__ volatile ("pushfl; popl %0" : "=r"(f) :: "memory");
    return f;
}
static inline void irq_restore(uint32_t f)
{
    __asm__ volatile ("pushl %0; popfl" :: "r"(f) : "memory");
}

#endif /* IRQ_H */
