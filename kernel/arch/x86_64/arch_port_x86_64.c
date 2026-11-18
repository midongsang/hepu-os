/* x86_64 架构移植点实现（见 kernel/include/arch_port.h） */
#include "arch_port.h"

extern void vga_puts(const char *s);

uint64_t arch_ticks(void)
{
    uint32_t a, d;
    __asm__ volatile("rdtsc" : "=a"(a), "=d"(d));
    return ((uint64_t)d << 32) | a;
}

void arch_console_puts(const char *s)
{
    vga_puts(s);
}

void arch_poweroff(void)
{
    __asm__ volatile(
        "movw $0x604, %%dx\n"
        "movl $0x2000, %%eax\n"
        "outl %%eax, %%dx" : : : "eax", "edx");
    for (;;) __asm__ volatile("hlt");
}
