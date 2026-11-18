/* ARM64 裸机 M0：证明 aarch64 代码能在 virt 上跑起来、串口能出字、PSCI 能关机 */
#include "arch_port.h"

static void put_dec(uint64_t v)
{
    char b[24]; int n = 0;
    if (!v) { arch_console_puts("0"); return; }
    while (v && n < 23) { b[n++] = (char)('0' + v % 10); v /= 10; }
    while (n--) { char s[2] = { b[n], 0 }; arch_console_puts(s); }
}

void bare_main(void)
{
    arch_console_puts("\n[HEPU-A64] bare metal M0 alive\n");
    arch_console_puts("[HEPU-A64] cntvct_el0 = ");
    put_dec(arch_ticks());
    arch_console_puts("\n[HEPU-A64] arch_port on aarch64 works; poweroff via PSCI\n");
    arch_poweroff();
}
