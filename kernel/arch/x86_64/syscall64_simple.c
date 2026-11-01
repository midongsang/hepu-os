/* ============================================================================
 * 赫普OS - 64位系统调用（简化版）
 * V0.10.6: 初始实现（syscall/sysret机制）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 系统调用号 */
#define SYS_HELLO    0
#define SYS_PUTCHAR  1
#define SYS_PUTS     2
#define SYS_EXIT     3
#define SYS_GETPID   4

/* MSR地址 */
#define MSR_STAR     0xC0000081
#define MSR_LSTAR    0xC0000082
#define MSR_CSTAR    0xC0000083
#define MSR_SFMASK   0xC0000084
#define MSR_KERNEL_GS_BASE 0xC0000102

/* per-cpu数据结构（GS段指向这里） */
typedef struct {
    uint64_t kernel_stack;   /* GS:0x00 - 内核栈指针 */
    uint64_t user_stack;     /* GS:0x08 - 用户栈指针（保存用） */
    uint64_t reserved[6];
} percpu_data_t;

static percpu_data_t percpu_data;

/* 内核栈顶（在entry_64.S中定义） */
extern uint8_t kernel_stack_top[];

/* 外部函数 */
extern void vga_putchar(char c);
extern void vga_puts(const char *str);
extern uint64_t x86_64_read_msr(uint32_t msr);
extern void x86_64_write_msr(uint32_t msr, uint64_t value);
extern int copy_from_user(void *kernel_dst, const void *user_src, size_t size);

/* 系统调用入口（在entry_64.S中定义） */
extern void syscall_entry_64(void);

/* 当前进程ID */
static uint64_t current_pid = 1;

/* 完整系统调用分发（在syscall64_full.c中定义） */
extern int64_t syscall64_dispatch(int number, int64_t arg1, int64_t arg2,
                                   int64_t arg3, int64_t arg4, int64_t arg5);

/* 系统调用处理函数 */
uint64_t syscall_handler_64(uint64_t syscall_num, uint64_t arg1, uint64_t arg2,
                            uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    /* 优先使用完整系统调用分发（POSIX兼容） */
    int64_t result = syscall64_dispatch((int)syscall_num,
                                         (int64_t)arg1, (int64_t)arg2,
                                         (int64_t)arg3, (int64_t)arg4,
                                         (int64_t)arg5);
    if (result != -1 || syscall_num >= 1 && syscall_num <= 20) {
        return (uint64_t)result;
    }

    /* 简单系统调用（HepuOS扩展） */
    switch (syscall_num) {
        case SYS_HELLO:
            vga_puts("[SYSCALL] Hello from 64-bit syscall!\n");
            return 0;

        case SYS_PUTCHAR:
            vga_putchar((char)arg1);
            return 0;

        case SYS_PUTS: {
            /* 安全修复：从用户空间复制字符串，避免任意内核地址读取（漏洞C） */
            static char user_str_buf[1024];
            if (copy_from_user(user_str_buf, (const void *)arg1, 1023) != 0) {
                return -1;
            }
            user_str_buf[1023] = '\0';
            vga_puts(user_str_buf);
            return 0;
        }

        default:
            vga_puts("[SYSCALL] Unknown syscall: ");
            /* 简单的十六进制输出 */
            char hex[] = "0123456789ABCDEF";
            char buf[16];
            int i = 15;
            buf[i--] = '\n';
            if (syscall_num == 0) {
                buf[i--] = '0';
            } else {
                while (syscall_num > 0 && i >= 0) {
                    buf[i--] = hex[syscall_num & 0xF];
                    syscall_num >>= 4;
                }
            }
            vga_puts(buf + i + 1);
            return -1;
    }
}

/* 初始化系统调用 */
void syscall64_init(void)
{
    vga_puts("[SYSCALL64] Initializing 64-bit syscall...\n");

    /* 设置STAR MSR：
     * - 位32-47: 内核代码段选择子 (0x08)
     * - 位48-63: SYSRET基准段 (0x10=内核数据段)
     *   SYSRET: CS=0x10+16=0x20(用户代码64位,RPL3), SS=0x10+8=0x18(用户数据,RPL3)
     */
    uint64_t star = (0x08ULL << 32) | (0x10ULL << 48);
    x86_64_write_msr(MSR_STAR, star);

    /* 设置LSTAR MSR：系统调用入口地址 */
    x86_64_write_msr(MSR_LSTAR, (uint64_t)syscall_entry_64);

    /* 设置CSTAR MSR：兼容模式入口（暂时设为0） */
    x86_64_write_msr(MSR_CSTAR, 0);

    /* 设置SFMASK MSR：系统调用时清除的标志位
     * - 清除IF（中断标志），DF（方向标志）
     */
    x86_64_write_msr(MSR_SFMASK, (1 << 9) | (1 << 10));  /* IF + DF */

    /* 设置IA32_KERNEL_GS_BASE：swapgs后GS指向per-cpu数据 */
    percpu_data.kernel_stack = (uint64_t)kernel_stack_top;
    percpu_data.user_stack = 0;
    x86_64_write_msr(MSR_KERNEL_GS_BASE, (uint64_t)&percpu_data);

    vga_puts("[SYSCALL64] Syscall initialized (STAR/LSTAR/CSTAR/SFMASK/GS)\n");
    vga_puts("[SYSCALL64] Entry point: ");
    /* 输出入口地址 */
    char hex[] = "0123456789ABCDEF";
    char buf[18];
    uint64_t addr = (uint64_t)syscall_entry_64;
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        buf[17 - i] = hex[addr & 0xF];
        addr >>= 4;
    }
    buf[17] = '\n';
    vga_puts(buf);
}

/* 测试系统调用 */
void syscall64_test(void)
{
    vga_puts("[SYSCALL64] Testing syscall...\n");

    /* 直接调用系统调用处理函数（模拟用户态调用） */
    syscall_handler_64(SYS_HELLO, 0, 0, 0, 0, 0);
    syscall_handler_64(SYS_PUTS, (uint64_t)"[SYSCALL64] Test string via syscall\n", 0, 0, 0, 0);

    uint64_t pid = syscall_handler_64(SYS_GETPID, 0, 0, 0, 0, 0);
    vga_puts("[SYSCALL64] PID: ");
    char hex[] = "0123456789ABCDEF";
    char buf[8];
    int i = 6;
    buf[7] = '\n';
    if (pid == 0) {
        buf[i--] = '0';
    } else {
        while (pid > 0 && i >= 0) {
            buf[i--] = hex[pid & 0xF];
            pid >>= 4;
        }
    }
    vga_puts(buf + i + 1);

    vga_puts("[SYSCALL64] Test complete\n");
}
