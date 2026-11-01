/* V3.87-E11: 断言失败处理——打印 + 停机(不继续执行) */
#include <stdint.h>
#include "vga.h"
#include "serial.h"

void assert_fail(const char *file, int line, const char *expr_s)
{
    __asm__ volatile ("cli");
    serial_puts(0x3F8, "\r\n[ASSERT] FAILED: ");
    serial_puts(0x3F8, expr_s ? expr_s : "?");
    serial_puts(0x3F8, " at ");
    serial_puts(0x3F8, file ? file : "?");
    /* 行号十进制 */
    {
        char buf[12]; int i = 0; int n = line;
        if (n == 0) buf[i++] = '0';
        while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
        while (i > 0) serial_putc(0x3F8, buf[--i]);
    }
    serial_puts(0x3F8, "\r\n");

    vga_puts("\n[ASSERT] FAILED: ");
    vga_puts(expr_s ? expr_s : "?");
    vga_puts("\n[ASSERT] system halted.\n");
    for (;;) { __asm__ volatile ("hlt"); }
}
