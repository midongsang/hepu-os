/* ============================================================================
 * 赫普OS - 64位系统调用（简化版）
 * V0.10.6: 初始实现（syscall/sysret机制）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "syscall.h"   /* V0.54.6: 统一 syscall 号定义（SYS_GETPID） */

/* V0.33.4 SEC-14 修复: 旧扩展号 SYS_PUTS=2 / SYS_GETPID=4 与原生号表
 * (2=read, 4=close) 撞号, 已删除这两个 case。
 * 保留 SYS_HELLO=0 (原生表未占用) 和 SYS_PUTCHAR=1 (启动早期调试用,
 * 原生 write=1 由 dispatch 优先处理, 不会落到这里)。 */
#define SYS_HELLO    0
#define SYS_PUTCHAR  1

/* V0.33: 原生 Hepu ABI 的 getpid 号（见 syscall64_full.c 的 SYSCALL_GETPID）。
 * 上面那组 SYS_* 是**早期 Hepu 扩展号**，与原生号表大面积撞号
 * （2=read / 4=close），已被 syscall64_full.c 的原生号表取代。
 * 需要「真的是 getpid」的地方必须用这个。 */
/* V0.54.6 修复：syscall 号统一以 syscall.h 为唯一真源
 * 旧值 20 是 SYS_SIGNAL（赫普OS 自己的号），getpid 是 7 */
#define SYSCALL_GETPID_NATIVE SYS_GETPID

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

/* V0.13.43 修复: QEMU 上 GS 段寻址(gs:0x00/0x08)不可靠, 提供绝对地址槽给 entry_64.S 使用
 * V0.13.43: 槽改为直接值语义 (entry 直接读写; 用指针中转会覆盖 rax 返回值) */
uint64_t g_syscall_kstack_slot = 0;
uint64_t g_syscall_ustack_slot = 0;

/* 内核栈顶（在entry_64.S中定义） */
/* V0.13.43 修复: kernel_main_64.c 定义的是指针变量, 数组声明导致取到符号地址 */
extern uint8_t *kernel_stack_top;

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
                                   int64_t arg3, int64_t arg4, int64_t arg5,
                                   int64_t arg6);
/* V0.33: 上一次 dispatch 是否处理了该调用 —— 见 syscall64_full.c 里的契约说明。
 * 旧代码用「返回值 == -1」来判定「未处理」，而 -1 同时也是合法 errno，
 * 于是 errno=-1 的正常调用被误判成未知系统调用（见 g_syscall64_handled）。 */
extern int syscall64_last_handled(void);

/* 系统调用处理函数 */
uint64_t syscall_handler_64(uint64_t syscall_num, uint64_t arg1, uint64_t arg2,
                            uint64_t arg3, uint64_t arg4, uint64_t arg5,
                            uint64_t arg6)
{
    /* 优先使用完整系统调用分发（POSIX兼容） */
    int64_t result = syscall64_dispatch((int)syscall_num,
                                         (int64_t)arg1, (int64_t)arg2,
                                         (int64_t)arg3, (int64_t)arg4,
                                         (int64_t)arg5, (int64_t)arg6);
    /* V0.33: 「是否处理过」与「返回值」彻底分离。
     * 命中表就算处理过 —— 哪怕处理结果是 errno=-1，也要原样交给用户态。
     * 只有真正没被处理的号，才落到下面的 Hepu 扩展分支。 */
    if (syscall64_last_handled()) {
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

        default:
            vga_puts("[SYSCALL] Unknown syscall: ");
            /* 简单的十六进制输出
             *
             * V0.33 修复（栈越界读）：旧代码 char buf[16] 从下标 15 往上写，
             * 写完数字后 `vga_puts(buf + i + 1)` 打印的是**未终止**的字符串 ——
             * buf 里没有任何 NUL 结尾符。于是 vga_puts 越过 buf 末尾继续读
             * 相邻的栈内存，恰好把紧挨着的 `char hex[] = "0123456789ABCDEF"`
             * 表给读了出来，日志里就凭空多出一串 "0123456789ABCDEF"。
             * 现在显式留出 NUL 位，越界读消失。 */
            static const char hex[] = "0123456789ABCDEF";
            char buf[20];
            /* 从前往后填数字，最后补 NUL —— 避免「倒着写再回填 NUL」
             * 时把已经写好的数字覆盖掉（那会把输出变成空串）。 */
            int len = 0;
            if (syscall_num == 0) {
                buf[len++] = '0';
            } else {
                unsigned long long v = syscall_num;
                char tmp[20];
                int tlen = 0;
                while (v > 0 && tlen < 16) { tmp[tlen++] = hex[v & 0xF]; v >>= 4; }
                while (tlen > 0) buf[len++] = tmp[--tlen];
            }
            buf[len] = '\n';
            buf[len + 1] = '\0';     /* V0.33: 显式 NUL 结尾，杜绝越界读 */
            vga_puts(buf);
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
    /* 安全修复：除 IF/DF 外还必须清除 TF(单步)、NT(嵌套任务)、AC(对齐检查/SMAP)，
     * 否则用户态设置的这些标志会带入内核执行 */
    x86_64_write_msr(MSR_SFMASK, (1 << 8) | (1 << 9) | (1 << 10) | (1 << 14) | (1 << 18));  /* TF+IF+DF+NT+AC */

    /* 设置IA32_KERNEL_GS_BASE：swapgs后GS指向per-cpu数据 */
    percpu_data.kernel_stack = (uint64_t)kernel_stack_top;
    percpu_data.user_stack = 0;
    x86_64_write_msr(MSR_KERNEL_GS_BASE, (uint64_t)&percpu_data);
    /* V0.13.43 修复: 绝对地址槽 (entry 绕过 GS 段寻址; 直接值语义) */
    g_syscall_kstack_slot = percpu_data.kernel_stack;  /* 内核栈顶值 */
    g_syscall_ustack_slot = 0;                          /* 运行时 entry 写入 */
    /* V0.13.43 调试: 验证 MSR 与 kernel_stack 值 */
    {
        extern void serial_hex(uint64_t v);
        vga_puts("[SYSCALL64] percpu=0x");
        serial_hex((uint64_t)&percpu_data);
        vga_puts(" kstack=0x");
        serial_hex(percpu_data.kernel_stack);
        vga_puts(" msr_gsbase=");
        serial_hex(x86_64_read_msr(MSR_KERNEL_GS_BASE));
        vga_puts("\n");
    }

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

/* 测试系统调用
 *
 * V0.33 修复（自测本身在说谎）：
 *   旧实现有三个问题，导致这段「自测」的输出全是误导：
 *   1. 它在**内核态**直接调 handler，却传**内核地址**给 SYS_PUTS，
 *      而 copy_from_user 只接受用户地址 ⇒ 这一调用必然失败，
 *      日志里凭空多一条 "[SECURITY] copy_from_user: invalid user address"；
 *   2. 它用 SYS_PUTS=2 / SYS_GETPID=4，可这两个号在原生号表里
 *      分别是 read=2 / close=4（撞号）⇒ 测的根本不是 putc/getpid；
 *   3. 它跑在 syscall64_full_init() **之前**，dispatch 当时还没就绪。
 *   结果日志里出现 "PID: FFFFFFF"（-1 的十六进制），看着像故障，
 *   其实是自测写错了。
 *
 * 现在：自测跑在 full_init 之后（见 kernel_main_64.c 的调用顺序），
 * 并且只调用**语义明确、参数合法**的原生系统调用。
 */
void syscall64_test(void)
{
    vga_puts("[SYSCALL64] Testing syscall...\n");

    /* V0.33: 原生 getpid = 20（见 syscall64_full.c 的 SYSCALL_GETPID）。
     * 旧代码测的是 SYS_GETPID=4，那是 close —— 撞号；
     * 还有一个 SYS_HELLO=0 也不测任何东西（原生表没有 0 号），
     * 只会让日志多一行 "Unimplemented syscall: 0"。都去掉。 */
    uint64_t pid = syscall_handler_64(SYSCALL_GETPID_NATIVE, 0, 0, 0, 0, 0, 0);
    vga_puts("[SYSCALL64] PID: ");
    /* V0.33 修复（栈越界读）：旧写法 char buf[8] 写完数字后没有 NUL 结尾，
     * vga_puts 会越过 buf 末尾读到相邻的 hex[] 表 —— 日志里多出
     * "0123456789ABCDEF"。改成从前往后填充 + 显式 NUL。 */
    {
        static const char hex[] = "0123456789ABCDEF";
        char buf[20];
        int len = 0;
        if (pid == 0) {
            buf[len++] = '0';
        } else {
            uint64_t v = pid;
            char tmp[20];
            int tlen = 0;
            while (v > 0 && tlen < 16) { tmp[tlen++] = hex[v & 0xF]; v >>= 4; }
            while (tlen > 0) buf[len++] = tmp[--tlen];
        }
        buf[len] = '\n';
        buf[len + 1] = '\0';
        vga_puts(buf);
    }

    vga_puts("[SYSCALL64] Test complete\n");
}
