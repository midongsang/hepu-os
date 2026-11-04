/* hello_linux.S - Linux i386 裸程序 (int 0x80 直接 syscall)
 * 验证 HEPU Linux ABI 兼容层: write(1,msg) + exit(0) */
.section .text
.global _start
_start:
    mov $4, %eax        /* Linux sys_write */
    mov $1, %ebx        /* fd = 1 */
    mov $msg, %ecx
    mov $len, %edx
    int $0x80
    mov $1, %eax        /* Linux sys_exit */
    xor %ebx, %ebx      /* code = 0 */
    int $0x80
msg:
    .ascii "Hello from Linux ELF on HEPU!\n"
len = . - msg
