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
    /* R3-7: 本核当前进程的 fork 快照槽地址。fork_trampoline_64 /
     * fork_return_to_parent 用 %gs:0x10 取它，从而按进程拿到属于自己的快照，
     * 不再读全局单份。 */
    uint64_t fork_snap;      /* GS:0x10 */
    uint64_t reserved[5];
} percpu_data_t;

/* SB-17: per-CPU 存储。每个核一份, GS 段基址指向自己那份。
 * 之前是单份全局结构, 只有 BSP 在 syscall64_init() 里写 MSR_KERNEL_GS_BASE,
 * 所以 AP 一旦进 syscall, swapgs 之后 %gs:0x00/0x08 读的是 BSP 的栈槽 ——
 * 两个核会互相踩栈。这里改成每核一份, AP 在自己的 ap_c_entry 里写自己的 MSR。 */
#define SYSCALL_MAX_CPUS 16
#define PERCPU_SIZE 64          /* 一个槽 64 字节 = 8 个 uint64_t, 页内布局对齐 */

static percpu_data_t percpu_data[SYSCALL_MAX_CPUS];
/* 编译期保证每份结构不超过 PERCPU_SIZE, 防止以后加字段时踩到别人的槽 */
typedef char percpu_size_check[(sizeof(percpu_data_t) <= PERCPU_SIZE) ? 1 : -1];

static int syscall_current_cpu(void)
{
    extern int smp_get_current_cpu(void);
    return smp_get_current_cpu();
}

/* 由调度器在切到某个进程时调用, 把该进程的 fork 快照槽挂到本核 per-CPU 槽 */
void syscall64_set_fork_snap(uint64_t addr);

/* 当前核的 percpu 结构 (GS 段基址就是它) */
percpu_data_t *syscall64_pcpu(void)
{
    return &percpu_data[syscall_current_cpu()];
}

void syscall64_set_fork_snap(uint64_t addr)
{
    syscall64_pcpu()->fork_snap = addr;
}

/* 当前核的栈槽地址。entry_64.S 走 %gs:0x00 / %gs:0x08, C 侧统一经这里取,
 * 保证"同一个数据只有一个来源"(旧代码里 g_syscall_*_slot 这个全局单值就是第二个来源)。 */
uint64_t *syscall64_kstack_slot_ptr(void) { return &syscall64_pcpu()->kernel_stack; }
uint64_t *syscall64_ustack_slot_ptr(void) { return &syscall64_pcpu()->user_stack; }

/* 兼容别名(过渡一个版本): 保留符号, 但语义已改为"当前核的值", 不再是独立存储。
 * 老代码里直接赋值 g_syscall_*_slot 的地方必须改成写 syscall64_*_slot_ptr(),
 * 否则写进去的值会被下一轮 per-CPU 切换覆盖掉。 */
uint64_t g_syscall_kstack_slot = 0;   /* 仅用于旧符号链接, 不再作为存储 */
uint64_t g_syscall_ustack_slot = 0;   /* 仅用于旧符号链接, 不再作为存储 */

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

/* SB-17: 为指定核心初始化 syscall 的 per-CPU 存储。
 * 核 0 由 syscall64_init() 调用; 其余核在 ap_c_entry 里各自调用,
 * 每个核写自己的 IA32_KERNEL_GS_BASE, 让 swapgs 后 %gs 落在本核的槽上。
 * kstack_top 传本核 syscall 用的内核栈顶 (0 表示暂用 kernel_stack_top)。 */
void syscall64_init_for_cpu(int cpu, uint64_t kstack_top)
{
    if (cpu < 0 || cpu >= SYSCALL_MAX_CPUS) return;
    if (!kstack_top) kstack_top = (uint64_t)kernel_stack_top;

    percpu_data[cpu].kernel_stack = kstack_top;
    percpu_data[cpu].user_stack = 0;
    for (int i = 0; i < 6; i++) percpu_data[cpu].reserved[i] = 0;

    x86_64_write_msr(MSR_KERNEL_GS_BASE, (uint64_t)&percpu_data[cpu]);
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

    /* 设置IA32_KERNEL_GS_BASE: swapgs 后 GS 指向本核的 per-cpu 数据。
     * SB-17: 这里只负责 BSP(核 0); 其余核在 ap_c_entry 里调
     * syscall64_init_for_cpu(cpu, kstack_top) 各自设置自己的 MSR。 */
    syscall64_init_for_cpu(0, (uint64_t)kernel_stack_top);
    g_syscall_kstack_slot = percpu_data[0].kernel_stack;  /* 旧符号, 仅镜像 */
    g_syscall_ustack_slot = 0;
    /* V0.13.43 调试: 验证 MSR 与 kernel_stack 值 */
    {
        extern void serial_hex(uint64_t v);
        vga_puts("[SYSCALL64] percpu=0x");
        serial_hex((uint64_t)&percpu_data[0]);
        vga_puts(" kstack=0x");
        serial_hex(percpu_data[0].kernel_stack);
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
