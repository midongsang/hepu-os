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

/* V0.62: 重启 —— ACPI 0xCF9 system reset，失败再退回 8042 脉冲 */
void arch_reboot(void)
{
    __asm__ volatile("movw $0xCF9, %%dx\n movb $0x06, %%al\n outb %%al, %%dx" ::: "eax", "edx");
    __asm__ volatile("movw $0x64, %%dx\n movb $0xFE, %%al\n outb %%al, %%dx" ::: "eax", "edx");
    for (;;) __asm__ volatile("hlt");
}
