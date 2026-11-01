/* ============================================================================
 * 赫普OS - HAL中断管理 (V0.97新增)
 * 实现：中断控制器抽象、IRQ注册/注销、中断使能/禁用、中断统计
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "hal_irq.h"
#include "vga.h"
#include "string.h"

#define IRQ_MAX 64
#define IRQ_NAME_MAX 16

/* 中断处理函数类型 */
typedef void (*irq_handler_t)(uint32_t irq, void *context);

/* IRQ描述符 */
typedef struct {
    uint8_t      used;
    uint8_t      enabled;
    uint8_t      level;        /* 0=边沿触发, 1=电平触发 */
    uint8_t      priority;     /* 中断优先级 0-15 */
    char         name[IRQ_NAME_MAX];
    irq_handler_t handler;
    void        *context;
    uint32_t     count;        /* 中断触发计数 */
    uint32_t     last_count;   /* 上次统计时的计数 */
} irq_desc_t;

static irq_desc_t irq_table[IRQ_MAX];
static uint32_t irq_total_count = 0;
static uint8_t irq_initialized = 0;

void hal_irq_init(void)
{
    memset(irq_table, 0, sizeof(irq_table));
    irq_total_count = 0;
    irq_initialized = 1;
    vga_puts("[HAL-IRQ] irq mgr subsystem init done \n");
}

/* 注册中断处理函数 */
int hal_irq_register(uint32_t irq, const char *name,
                     irq_handler_t handler, void *context)
{
    if (!irq_initialized || irq >= IRQ_MAX || !handler) return -1;
    if (irq_table[irq].used) return -2;  /* 已被注册 */

    irq_table[irq].used = 1;
    irq_table[irq].enabled = 0;
    irq_table[irq].level = 0;
    irq_table[irq].priority = 8;  /* 默认中等优先级 */
    if (name) strncpy(irq_table[irq].name, name, IRQ_NAME_MAX - 1);
    irq_table[irq].handler = handler;
    irq_table[irq].context = context;
    irq_table[irq].count = 0;
    irq_table[irq].last_count = 0;
    return 0;
}

/* 注销中断 */
int hal_irq_unregister(uint32_t irq)
{
    if (!irq_initialized || irq >= IRQ_MAX) return -1;
    if (!irq_table[irq].used) return -1;
    irq_table[irq].used = 0;
    irq_table[irq].enabled = 0;
    irq_table[irq].handler = NULL;
    return 0;
}

/* 使能中断 */
int hal_irq_enable(uint32_t irq)
{
    if (!irq_initialized || irq >= IRQ_MAX) return -1;
    if (!irq_table[irq].used) return -1;
    irq_table[irq].enabled = 1;
    return 0;
}

/* 禁用中断 */
int hal_irq_disable(uint32_t irq)
{
    if (!irq_initialized || irq >= IRQ_MAX) return -1;
    irq_table[irq].enabled = 0;
    return 0;
}

/* 设置中断优先级 */
int hal_irq_set_priority(uint32_t irq, uint8_t priority)
{
    if (!irq_initialized || irq >= IRQ_MAX) return -1;
    if (priority > 15) priority = 15;
    irq_table[irq].priority = priority;
    return 0;
}

/* 设置触发方式 */
int hal_irq_set_trigger(uint32_t irq, uint8_t level_triggered)
{
    if (!irq_initialized || irq >= IRQ_MAX) return -1;
    irq_table[irq].level = level_triggered ? 1 : 0;
    return 0;
}

/* 中断分发（由底层中断入口调用） */
void hal_irq_dispatch(uint32_t irq)
{
    if (!irq_initialized || irq >= IRQ_MAX) return;
    if (!irq_table[irq].used || !irq_table[irq].enabled) return;

    irq_table[irq].count++;
    irq_total_count++;
    if (irq_table[irq].handler) {
        irq_table[irq].handler(irq, irq_table[irq].context);
    }
}

/* 全局中断使能/禁用 */
void hal_irq_global_enable(void)
{
    __asm__ volatile ("sti");
}

void hal_irq_global_disable(void)
{
    __asm__ volatile ("cli");
}

/* 获取中断统计 */
uint32_t hal_irq_get_count(uint32_t irq)
{
    if (irq >= IRQ_MAX) return 0;
    return irq_table[irq].count;
}

uint32_t hal_irq_total(void)
{
    return irq_total_count;
}

/* 打印中断表 */
void hal_irq_dump(void)
{
    vga_printf("[HAL-IRQ] irq table(total trigger: %d):\n", irq_total_count);
    for (int i = 0; i < IRQ_MAX; i++) {
        if (irq_table[i].used) {
            vga_printf("  IRQ %2d: %-16s %s prio=%d count=%d\n",
                       i, irq_table[i].name,
                       irq_table[i].enabled ? "EN" : "DIS",
                       irq_table[i].priority, irq_table[i].count);
        }
    }
}
