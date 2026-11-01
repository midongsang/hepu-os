/* V6.47: int 0x80入口调试——确认用户态系统调用是否触发 */
#include <stdint.h>

void syscall_debug_enter(uint32_t ecx, uint32_t edx, uint32_t ebx,
                         uint32_t eax, uint32_t eip)
{
    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puts(0x3F8, "[SYSCALL] eip=0x");
    serial_puthex(0x3F8, eip);
    serial_puts(0x3F8, " eax=0x");
    serial_puthex(0x3F8, eax);
    serial_puts(0x3F8, " ebx=0x");
    serial_puthex(0x3F8, ebx);
    serial_puts(0x3F8, " ecx=0x");
    serial_puthex(0x3F8, ecx);
    serial_puts(0x3F8, " edx=0x");
    serial_puthex(0x3F8, edx);
    serial_puts(0x3F8, "\n");
}
