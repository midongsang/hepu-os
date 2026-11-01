/* ============================================================================
 * 赫普OS - HAL GPIO抽象 (V0.97新增)
 * 实现：GPIO引脚配置、读写、中断触发、引脚复用
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "hal_gpio.h"
#include "vga.h"
#include "string.h"

#define GPIO_MAX_PINS 64
#define GPIO_NAME_MAX 16

/* GPIO引脚描述 */
typedef struct {
    uint8_t  used;
    uint8_t  direction;   /* 0=输入, 1=输出 */
    uint8_t  pull;        /* 0=无, 1=上拉, 2=下拉 */
    uint8_t  value;       /* 当前电平 */
    uint8_t  irq_enabled;
    uint8_t  irq_edge;    /* 0=上升沿, 1=下降沿, 2=双边沿 */
    char     name[GPIO_NAME_MAX];
    uint32_t alt_func;    /* 复用功能号 */
} gpio_pin_t;

static gpio_pin_t gpio_pins[GPIO_MAX_PINS];
static uint8_t gpio_initialized = 0;

void hal_gpio_init(void)
{
    memset(gpio_pins, 0, sizeof(gpio_pins));
    gpio_initialized = 1;
    vga_puts("[HAL-GPIO] GPIO subsystem init done \n");
}

/* 配置引脚方向 */
int hal_gpio_set_direction(uint32_t pin, uint8_t direction)
{
    if (!gpio_initialized || pin >= GPIO_MAX_PINS) return -1;
    gpio_pins[pin].used = 1;
    gpio_pins[pin].direction = direction ? 1 : 0;
    return 0;
}

/* 设置上下拉 */
int hal_gpio_set_pull(uint32_t pin, uint8_t pull)
{
    if (!gpio_initialized || pin >= GPIO_MAX_PINS) return -1;
    if (pull > 2) pull = 0;
    gpio_pins[pin].pull = pull;
    return 0;
}

/* 写引脚 */
int hal_gpio_write(uint32_t pin, uint8_t value)
{
    if (!gpio_initialized || pin >= GPIO_MAX_PINS) return -1;
    if (!gpio_pins[pin].used) return -1;
    if (gpio_pins[pin].direction != 1) return -1;  /* 不是输出模式 */
    gpio_pins[pin].value = value ? 1 : 0;
    return 0;
}

/* 读引脚 */
int hal_gpio_read(uint32_t pin)
{
    if (!gpio_initialized || pin >= GPIO_MAX_PINS) return -1;
    if (!gpio_pins[pin].used) return -1;
    return gpio_pins[pin].value;
}

/* 翻转引脚 */
int hal_gpio_toggle(uint32_t pin)
{
    if (!gpio_initialized || pin >= GPIO_MAX_PINS) return -1;
    if (!gpio_pins[pin].used) return -1;
    if (gpio_pins[pin].direction != 1) return -1;
    gpio_pins[pin].value ^= 1;
    return gpio_pins[pin].value;
}

/* 配置引脚复用功能 */
int hal_gpio_set_alt(uint32_t pin, uint32_t func)
{
    if (!gpio_initialized || pin >= GPIO_MAX_PINS) return -1;
    gpio_pins[pin].alt_func = func;
    return 0;
}

/* 获取复用功能 */
uint32_t hal_gpio_get_alt(uint32_t pin)
{
    if (!gpio_initialized || pin >= GPIO_MAX_PINS) return 0;
    return gpio_pins[pin].alt_func;
}

/* 使能GPIO中断 */
int hal_gpio_irq_enable(uint32_t pin, uint8_t edge)
{
    if (!gpio_initialized || pin >= GPIO_MAX_PINS) return -1;
    if (edge > 2) edge = 2;
    gpio_pins[pin].irq_enabled = 1;
    gpio_pins[pin].irq_edge = edge;
    return 0;
}

/* 禁用GPIO中断 */
int hal_gpio_irq_disable(uint32_t pin)
{
    if (!gpio_initialized || pin >= GPIO_MAX_PINS) return -1;
    gpio_pins[pin].irq_enabled = 0;
    return 0;
}

/* 命名引脚 */
int hal_gpio_name(uint32_t pin, const char *name)
{
    if (!gpio_initialized || pin >= GPIO_MAX_PINS) return -1;
    if (name) strncpy(gpio_pins[pin].name, name, GPIO_NAME_MAX - 1);
    return 0;
}

/* GPIO统计 */
void hal_gpio_stats(uint32_t *used, uint32_t *output, uint32_t *input)
{
    uint32_t u = 0, o = 0, i = 0;
    for (int p = 0; p < GPIO_MAX_PINS; p++) {
        if (gpio_pins[p].used) {
            u++;
            if (gpio_pins[p].direction == 1) o++;
            else i++;
        }
    }
    if (used) *used = u;
    if (output) *output = o;
    if (input) *input = i;
}

/* 打印GPIO状态 */
void hal_gpio_dump(void)
{
    vga_puts("[HAL-GPIO] pin state:\n");
    for (int p = 0; p < GPIO_MAX_PINS; p++) {
        if (gpio_pins[p].used) {
            vga_printf("  PIN %2d: %-16s %s val=%d pull=%d alt=%d\n",
                       p, gpio_pins[p].name,
                       gpio_pins[p].direction ? "OUT" : "IN",
                       gpio_pins[p].value, gpio_pins[p].pull,
                       gpio_pins[p].alt_func);
        }
    }
}
