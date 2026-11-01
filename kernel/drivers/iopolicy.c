/* ============================================================================
 * 赫普OS - IO 策略框架(轮询 vs 中断) + 忙等让步
 * V4.02-E47: 优化清单 3.3(轮询 vs 中断) / 4.3(消除忙等死循环)
 *
 * 原则:
 * - 低速设备(键盘/鼠标): 中断驱动(IRQ) + 底半部(softirq 已有 E-38);
 * - 高速设备(IDE): 轮询(PIO/DMA 状态寄存器) —— 轮询循环必须让步,
 *   不得 while(flag) 空转烧 CPU: 每 64 次循环发 pause 指令降低总线竞争;
 * - 统计各设备 poll_loops/irq_count/waits/relax_count, 供 iostat 诊断.
 * ========================================================================== */

#include <stdint.h>
#include "iopolicy.h"
#include "vga.h"
#include "string.h"
#include "serial.h"

#define IO_DEV_MAX 8

typedef struct {
    const char *name;
    io_policy_t policy;
    uint32_t poll_loops;   /* 轮询忙等总循环次数 */
    uint32_t irq_count;    /* 中断触发次数 */
    uint32_t waits;        /* 等待调用次数 */
    uint32_t relax_count;  /* 让步(pause)次数 */
} io_dev_t;

static io_dev_t io_devs[IO_DEV_MAX];
static int io_dev_count = 0;

void iopolicy_init(void)
{
    int i;
    for (i = 0; i < IO_DEV_MAX; i++) {
        io_devs[i].name = NULL;
        io_devs[i].policy = IO_POLICY_POLL;
        io_devs[i].poll_loops = 0;
        io_devs[i].irq_count = 0;
        io_devs[i].waits = 0;
        io_devs[i].relax_count = 0;
    }
    io_dev_count = 0;
    iopolicy_register("kbd",    IO_POLICY_IRQ);   /* 0 */
    iopolicy_register("ide0",   IO_POLICY_POLL);  /* 1 */
    iopolicy_register("mouse",  IO_POLICY_IRQ);   /* 2 */
    iopolicy_register("serial", IO_POLICY_POLL);  /* 3 */
    vga_puts("[IOPOLICY] devices: kbd(IRQ) ide0(POLL) mouse(IRQ) serial(POLL)\n");
}

int iopolicy_register(const char *name, io_policy_t policy)
{
    if (io_dev_count >= IO_DEV_MAX) return -1;
    io_devs[io_dev_count].name = name;
    io_devs[io_dev_count].policy = policy;
    return io_dev_count++;
}

/* 忙等循环中调用: 统计 + 每 64 次 pause 让步(任何上下文安全) */
void iopolicy_relax(int idx)
{
    io_dev_t *d;
    if (idx < 0 || idx >= io_dev_count) return;
    d = &io_devs[idx];
    d->poll_loops++;
    if ((d->poll_loops & 0x3Fu) == 0) {
        __asm__ volatile("pause" ::: "memory");
        d->relax_count++;
    }
}

/* 等待开始(记录一次等待事务) */
void iopolicy_wait_start(int idx)
{
    if (idx >= 0 && idx < io_dev_count)
        io_devs[idx].waits++;
}

/* ISR 顶半部调用: 中断计数 */
void iopolicy_irq(int idx)
{
    if (idx >= 0 && idx < io_dev_count)
        io_devs[idx].irq_count++;
}

void iopolicy_dump(void)
{
    int i;
    vga_puts("== IO policy ==\n");
    for (i = 0; i < io_dev_count; i++) {
        io_dev_t *d = &io_devs[i];
        vga_printf(" %s %s: waits=%d poll=%d irq=%d relax=%d\n",
                   d->name,
                   (d->policy == IO_POLICY_IRQ) ? "IRQ " : "POLL",
                   d->waits, d->poll_loops, d->irq_count, d->relax_count);
    }
}
