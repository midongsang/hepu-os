/* aarch64 架构移植点实现（见 kernel/include/arch_port.h）
 *
 * 目标环境：QEMU virt（-M virt）或真板。地址按 QEMU virt 的约定，
 * 真板接入时只改这里的基址/中断控制器，不动上层。
 */
#include "arch_port.h"

/* ---- PL011 UART（QEMU virt: 0x0900_0000） ---- */
#define PL011_BASE 0x09000000UL
#define PL011_DR   (*(volatile uint32_t *)(PL011_BASE + 0x00))
#define PL011_FR   (*(volatile uint32_t *)(PL011_BASE + 0x18))
#define FR_TXFF    (1u << 5)

uint64_t arch_ticks(void)
{
    /* 虚拟计数器（QEMU 默认 ~62.5MHz 或 24MHz，取决于板；单调即可） */
    uint64_t v;
    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v));
    return v;
}

static void pl011_putc(char c)
{
    while (PL011_FR & FR_TXFF) { }
    PL011_DR = (uint32_t)c;
}

void arch_console_puts(const char *s)
{
    while (s && *s) pl011_putc(*s++);
}

void arch_poweroff(void)
{
    /* PSCI SYSTEM_OFF (0x8400_0008)：QEMU virt 走 hvc，真板可能 smc */
    __asm__ volatile(
        "mov w0, #0x8\n"
        "movk w0, #0x8400, lsl #16\n"
        "hvc #0" : : : "x0");
    for (;;) __asm__ volatile("wfi");
}
