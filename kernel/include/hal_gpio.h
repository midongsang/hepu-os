#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <stdint.h>

/* GPIO方向 */
#define GPIO_INPUT  0
#define GPIO_OUTPUT 1

/* 上下拉 */
#define GPIO_PULL_NONE 0
#define GPIO_PULL_UP   1
#define GPIO_PULL_DOWN 2

/* 中断触发沿 */
#define GPIO_IRQ_RISING  0
#define GPIO_IRQ_FALLING 1
#define GPIO_IRQ_BOTH    2

/* 初始化 */
void hal_gpio_init(void);

/* 方向/上下拉 */
int hal_gpio_set_direction(uint32_t pin, uint8_t direction);
int hal_gpio_set_pull(uint32_t pin, uint8_t pull);

/* 读写 */
int hal_gpio_write(uint32_t pin, uint8_t value);
int hal_gpio_read(uint32_t pin);
int hal_gpio_toggle(uint32_t pin);

/* 复用功能 */
int      hal_gpio_set_alt(uint32_t pin, uint32_t func);
uint32_t hal_gpio_get_alt(uint32_t pin);

/* 中断 */
int hal_gpio_irq_enable(uint32_t pin, uint8_t edge);
int hal_gpio_irq_disable(uint32_t pin);

/* 命名 */
int hal_gpio_name(uint32_t pin, const char *name);

/* 统计/调试 */
void hal_gpio_stats(uint32_t *used, uint32_t *output, uint32_t *input);
void hal_gpio_dump(void);

#endif
