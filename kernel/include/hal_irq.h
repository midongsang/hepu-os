#ifndef HAL_IRQ_H
#define HAL_IRQ_H

#include <stdint.h>

/* 中断处理函数类型 */
typedef void (*irq_handler_t)(uint32_t irq, void *context);

/* 初始化中断管理子系统 */
void hal_irq_init(void);

/* 注册/注销中断 */
int  hal_irq_register(uint32_t irq, const char *name,
                      irq_handler_t handler, void *context);
int  hal_irq_unregister(uint32_t irq);

/* 使能/禁用 */
int  hal_irq_enable(uint32_t irq);
int  hal_irq_disable(uint32_t irq);

/* 配置 */
int  hal_irq_set_priority(uint32_t irq, uint8_t priority);
int  hal_irq_set_trigger(uint32_t irq, uint8_t level_triggered);

/* 中断分发（底层入口调用） */
void hal_irq_dispatch(uint32_t irq);

/* 全局中断控制 */
void hal_irq_global_enable(void);
void hal_irq_global_disable(void);

/* 统计 */
uint32_t hal_irq_get_count(uint32_t irq);
uint32_t hal_irq_total(void);

/* 调试 */
void hal_irq_dump(void);

#endif
