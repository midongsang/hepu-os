/* ============================================================================
 * 赫普OS - 64位基础库函数
 * V0.10.6: 初始实现（内存操作、字符串、VGA输出）
 * ========================================================================== */

#include <stdint.h>
#include "atomic64.h"
#include "spinlock.h"
#include <stddef.h>
#include <stdarg.h>

/* VGA文本模式缓冲区 */
#define VGA_BUFFER 0xB8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_COLOR 0x0F  /* 黑底白字 */

/* 串口（COM1） */
#define SERIAL_PORT 0x3F8

static int vga_x = 0;
static int vga_y = 0;

/* 输出重定向钩子（图形终端用） */
static void (*g_output_hook)(const char *s, int len) = 0;
static int g_output_hook_active = 0;

/* ---- 早期帧缓冲文本镜像（V0.13.60: UEFI GOP 下 0xB8000 不可见, 双路画到 fb） ---- */
extern const uint8_t font8x16[128][16];   /* 定义在 gfx64.c */
static uint8_t *g_efb = 0;
static int g_efb_w = 0, g_efb_h = 0, g_efb_pitch = 0, g_efb_bpp = 0;
static int g_efb_enabled = 0;

/* 激活早期帧缓冲文本镜像（内核早期调用, fb 需已映射） */
void early_fb_text_init(uint8_t *fb, int w, int h, int pitch, int bpp)
{
    g_efb = fb; g_efb_w = w; g_efb_h = h;
    g_efb_pitch = pitch; g_efb_bpp = bpp;
    g_efb_enabled = (fb != 0 && w > 0 && h > 0);
}

/* 关闭（图形桌面接管后） */
void early_fb_text_disable(void)
{
    g_efb_enabled = 0;
}

/* 在 fb 上画 8x16 字符（白色, 与 VGA 文本同位置） */
static void efb_draw_char(int ch, int col, int row)
{
    if (ch < 0 || ch > 127) ch = '?';
    int x0 = col * 8, y0 = row * 16;
    if (x0 + 8 > g_efb_w || y0 + 16 > g_efb_h) return;
    int bpp = g_efb_bpp / 8;
    if (bpp < 2) bpp = 2;
    for (int r = 0; r < 16; r++) {
        uint8_t line = font8x16[ch][r];
        for (int c = 0; c < 8; c++) {
            if (line & (0x80 >> c)) {
                uint8_t *p = g_efb + (y0 + r) * g_efb_pitch + (x0 + c) * bpp;
                p[0] = 0xFF; p[1] = 0xFF; p[2] = 0xFF;
                if (bpp >= 4) p[3] = 0xFF;
            }
        }
    }
}

/* fb 文本滚动（16 行上移, 清末行） */
static void efb_scroll(void)
{
    if (g_efb_h < 32) return;
    for (int y = 0; y < g_efb_h - 16; y++) {
        __builtin_memcpy(g_efb + y * g_efb_pitch, g_efb + (y + 16) * g_efb_pitch, g_efb_pitch);
    }
    for (int y = g_efb_h - 16; y < g_efb_h; y++) {
        __builtin_memset(g_efb + y * g_efb_pitch, 0, g_efb_pitch);
    }
}

void set_output_hook(void (*hook)(const char *s, int len)) {
    g_output_hook = hook;
    g_output_hook_active = (hook != 0);
}

int is_output_hook_active(void) {
    return g_output_hook_active;
}

/* 内联I/O函数 */
static inline uint8_t __inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void __outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* 串口输出一个字符 */
static void serial_putchar(char c)
{
    /* 等待发送缓冲区为空 */
    while ((__inb(SERIAL_PORT + 5) & 0x20) == 0) {}
    __outb(SERIAL_PORT, c);
}

/* 串口输出字符串 */
static void serial_puts_local(const char *str)
{
    while (*str) {
        if (*str == '\n') serial_putchar('\r');
        serial_putchar(*str++);
    }
}

/* V0.13.43: 供 syscall write 镜像用户态 stdout 到串口 */
void syscall_serial_out(uint8_t c)
{
    if (c == '\n') serial_putchar('\r');
    serial_putchar((char)c);
}

/* V0.13.43: 串口输出 64 位 hex (16 位, 供异常/调试) */
void serial_hex(uint64_t v)
{
    const char *h = "0123456789ABCDEF";
    for (int i = 60; i >= 0; i -= 4) {
        serial_putchar(h[(v >> i) & 0xF]);
    }
}

/* 清空VGA屏幕 */
void vga_clear(void)
{
    uint16_t *buf = (uint16_t *)VGA_BUFFER;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        buf[i] = (VGA_COLOR << 8) | ' ';
    }
    vga_x = 0;
    vga_y = 0;
}

/* 输出单个字符 */
void vga_putchar(char c)
{
    /* 输出重定向到钩子 */
    if (g_output_hook_active && g_output_hook) {
        char buf[2] = {c, 0};
        g_output_hook(buf, 1);
        return;
    }

    uint16_t *buf = (uint16_t *)VGA_BUFFER;

    if (c == '\n') {
        vga_x = 0;
        vga_y++;
    } else if (c == '\r') {
        vga_x = 0;
    } else if (c == '\t') {
        vga_x = (vga_x + 8) & ~7;
    } else {
        buf[vga_y * VGA_WIDTH + vga_x] = (VGA_COLOR << 8) | c;
        vga_x++;
    }

    if (vga_x >= VGA_WIDTH) {
        vga_x = 0;
        vga_y++;
    }

    if (vga_y >= VGA_HEIGHT) {
        /* 滚动屏幕 */
        for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
            buf[i] = buf[i + VGA_WIDTH];
        }
        for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++) {
            buf[i] = (VGA_COLOR << 8) | ' ';
        }
        vga_y = VGA_HEIGHT - 1;
        if (g_efb_enabled) efb_scroll();
    }

    /* 双路: 早期帧缓冲镜像（UEFI GOP 下可见） */
    if (g_efb_enabled) {
        if (c == '\n' || c == '\r' || c == '\t') {
            /* 光标已在文本模式维护, fb 镜像仅需重绘当前行区域 */
        } else {
            int col = vga_x, row = vga_y;
            if (c == '\b') { if (col) col--; }
            else {
                /* 先算写入后的位置 */
                col = vga_x - 1;
                if (col < 0) col = 0;
                efb_draw_char(c, col, row);
            }
        }
    }
}

/* SB-05: 多核打印串行化。
 * vga_puts 会在多个核上并发调用, 而内部的 vga_x/vga_y、滚动、输出钩子都是
 * 全局状态, 不加锁就会出现光标错位和半行交错。这里用一把 irqsave 自旋锁把
 * 整个输出过程包住: 关中断 + 拿锁 -> 输出 -> 解锁 + 按原 IF 恢复。
 *
 * 注意两点:
 *   1) 锁内只做字节输出, 不调用任何可能再次进入 vga_puts 的路径
 *      (serial_puts_local 直接写端口, 不回调, 安全);
 *   2) panic 路径用 vga_puts_nolock() —— 崩溃时可能正持有这把锁,
 *      再去拿就是自死锁。 */
static spinlock_t print_lock = { 0, 0, 0 };

/* 无锁版: 内部实现 + panic 等不可加锁的场景直接用 */
void vga_puts_nolock(const char *str)
{
    serial_puts_local(str);

    /* 输出重定向到钩子 */
    if (g_output_hook_active && g_output_hook) {
        int len = 0;
        while (str[len]) len++;
        g_output_hook(str, len);
        return;
    }

    while (*str) {
        vga_putchar(*str++);
    }
}

/* SB-05 补口: 给"绕过 vga_puts 直接写串口"的路径提供同一把锁。
 * smp.c 的 sb18_serial_puts() 是 static 直写 COM1 端口的, 完全不走 vga_puts,
 * 于是 AP 的 [SB19] 和 BSP 的 [SMP] 在串口上互相插进半个词:
 *   "[SB19] cpu=[SMP] 0x1 lapic_id=AP 0x1 ver=0x14"
 * 这里把同一把锁开一对 acquire/release 出来, 让那些路径也能串行化。 */
uint64_t print_lock_acquire(void)
{
    return spinlock_lock_irqsave(&print_lock);
}

void print_lock_release(uint64_t flags)
{
    spinlock_unlock_irqrestore(&print_lock, flags);
}

/* SB-05: 带打印锁的对外入口(默认路径) */
void vga_puts(const char *str)
{
    uint64_t f = spinlock_lock_irqsave(&print_lock);
    vga_puts_nolock(str);
    spinlock_unlock_irqrestore(&print_lock, f);
}

/* V0.59.21: 只写串口、不上屏的对外入口。
 * 用途: 桌面终端的命令输出被 shell_set_output 重定向进 t_lines 缓冲,
 * 不进 vga_puts -> 串口也拿不到; 一旦窗口渲染出问题, 输出就彻底不可见
 * (autostart 无人值守取证就是这么失效的)。这里给一条独立的串口旁路,
 * 让终端输出同时落一份到 ser_*.log, 不依赖任何渲染路径。 */
void serial_puts_raw(const char *str)
{
    if (!str) return;
    uint64_t f = spinlock_lock_irqsave(&print_lock);
    serial_puts_local(str);
    spinlock_unlock_irqrestore(&print_lock, f);
}

/* 输出16进制数（64位） */
void vga_put_hex(uint64_t val)
{
    char hex[] = "0123456789ABCDEF";
    char buf[17];
    int i;

    buf[16] = '\0';
    for (i = 15; i >= 0; i--) {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }

    /* 去掉前导零 */
    int start = 0;
    while (buf[start] == '0' && start < 15) {
        start++;
    }

    vga_puts("0x");
    vga_puts(buf + start);
}

/* 内存设置 */
void *memset(void *dst, int val, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n--) {
        *d++ = (uint8_t)val;
    }
    return dst;
}

/* 内存复制 */
void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

/* 字符串长度 */
size_t strlen(const char *str)
{
    size_t len = 0;
    while (*str++) {
        len++;
    }
    return len;
}

/* 字符串复制 */
char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++)) {}
    return dst;
}

/* 字符串比较 */
int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

/* 有限字符串比较 */
int strncmp(const char *s1, const char *s2, size_t n)
{
    if (n == 0) return 0;
    while (n > 0 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

/* 查找字符 */
char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    if (c == 0) return (char *)s;
    return 0;
}

/* 反向查找字符 */
char *strrchr(const char *s, int c)
{
    const char *last = 0;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == 0) return (char *)s;
    return (char *)last;
}

/* ============================================================================
 * 桩函数（用于entry_64.S中的未定义引用，后续会实现完整功能）
 * ========================================================================== */

/* IRQ处理函数表 */
typedef void (*irq_handler_t)(void);
static irq_handler_t irq_handlers[16] = {0};

/* 注册IRQ处理程序 */
void irq64_register_handler(int irq, irq_handler_t handler)
{
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = handler;
    }
}

/* 中断处理 - 支持IRQ和异常 */
/* per-CPU FPU状态保存区（SB-08: 原 8 槽 × 512 字节, 而 MAX_CPUS 是 16,
   核 ID >= 8 时会用 & 7 回绕到低编号核的槽, 两个核共用一块 FXSAVE 区。
   现在扩到 16 槽并去掉 & 7, 越界时明确报错而不是踩邻槽。 */
#define FPU_SAVE_MAX_CPUS 16
static uint8_t fpu_save_area[FPU_SAVE_MAX_CPUS][512] __attribute__((aligned(64)));

/* SB-08 验收探针: 用 SSE 做一段结果可预测的浮点计算。
 * 1000 次 +1.0 在 double 下必须精确等于 1000.0 —— 只要中途有一次中断
 * 把 XMM 状态存错/恢复到别人的槽, 累加器就会漂, 返回值立刻变 0。
 * 这是目前唯一能把"FPU 保存区是否串核"变成可判定信号的测试。 */
int sb08_fpu_probe(void)
{
    volatile double acc = 0.0;
    for (int i = 0; i < 1000; i++) acc += 1.0;
    if (acc != 1000.0) return 0;
    /* 再用单精度走一遍, 覆盖 xmm 的不同位宽写路径 */
    volatile float fa = 0.0f;
    for (int i = 0; i < 256; i++) fa += 1.0f;
    return (fa == 256.0f) ? 1 : 0;
}

/* V0.13.46: 内核tick（vec40每1ms递增，未响应看门狗用） */
/* V0.58.5 / SB-02: 多核的 0x40 中断会并发写，改原子递增 */
uint64_t g_kernel_tick64 = 0;
/* V0.33: 用户态异常累计次数（启动自检电池用来给可判定结论） */
volatile uint64_t g_user_exception_count = 0;

/* 中断处理主体（原irq_handler_64逻辑） */
/* 2026-11-19: 与 32 位对齐的诊断报告格式 —— STOP 码/异常名/诊断提示。
 * 64 位原先只有 vector/error/rip 三行，现场可读性远低于 32 位那一版。 */
static const char *k64_exc_name(uint64_t v)
{
    static const char *n[32] = {
        "Divide Error","Debug","NMI","Breakpoint","Overflow","BOUND Range",
        "Invalid Opcode","Device Not Available","Double Fault","Coprocessor Segment",
        "Invalid TSS","Segment Not Present","Stack Fault","General Protection",
        "Page Fault","Reserved (15)","x87 FP","Alignment Check","Machine Check",
        "SIMD FP","Virtualization","Control Protection","Reserved (22)",
        "Reserved (23)","Reserved (24)","Reserved (25)","Reserved (26)",
        "Reserved (27)","Hypervisor Injection","VMM Communication","Security",
        "Reserved (31)"
    };
    return v < 32 ? n[v] : "Unknown";
}

static const char *k64_hint(uint64_t v)
{
    switch (v) {
    case 14: return "page fault: CR2 holds faulting address; check the access bits above";
    case 13: return "general protection: bad selector, non-canonical address, or privilege violation";
    case 8:  return "double fault: exception handler itself faulted; check stack and IST";
    case 12: return "stack fault: kernel stack overflow or bad SS:RSP";
    case  6: return "invalid opcode: check instruction stream / corrupted code page";
    case  0: return "divide error: divide by zero or quotient overflow";
    default: return "unexpected exception in kernel mode; see rip and stack trace below";
    }
}

static void k64_stop_code(uint64_t v, char *out)
{
    static const char hexd[] = "0123456789ABCDEF";
    int i;
    out[0] = '0'; out[1] = 'x';
    for (i = 0; i < 8; i++) out[2 + i] = hexd[(v >> (4 * (7 - i))) & 0xF];
    out[10] = 0;
}

static void bsod64_gfx_report(uint64_t vector, uint64_t error, uint64_t rip,
                              uint64_t rsp, uint64_t cr2);

static void hepu_panic_kernel_fault(uint64_t *sf);
static void irq_handler_64_body(uint64_t *stack_frame)
{
    /* 中断帧布局（栈顶开始）：
     * [0] = es, [1] = ds
     * [2-16] = r15,r14,...,rax (15个通用寄存器)
     * [17] = vector (中断号)
     * [18] = error code
     * [19] = rip, [20] = cs, [21] = rflags, [22] = rsp, [23] = ss
     */
    uint64_t vector = stack_frame[17];
    uint64_t error = stack_frame[18];
    uint64_t rip = stack_frame[19];


    /* V0.13.46: 用户态进程强制终止截获点（未响应进程：kill请求后由下一个IRQ截获）
     * 中断帧[19]=RIP [20]=CS [21]=RFLAGS [22]=RSP [23]=SS
     * 用户态IRQ时 CS=0x23/SS=0x1B；改为内核段并跳到 user_return_point 完成清理 */
    extern int user_process_kill_pending(void);
    extern uint64_t g_user_saved_rip;
    extern uint64_t g_user_saved_rsp;
    extern uint64_t g_user_saved_cr3;
    if (user_process_kill_pending() && (stack_frame[20] & 3) == 3) {
        /* 切回内核页表 */
        __asm__ volatile("mov %0, %%cr3" : : "r"(g_user_saved_cr3) : "memory");
        /* 改写中断帧：iretq 跳回内核 user_return_point 清理 */
        stack_frame[19] = g_user_saved_rip;
        stack_frame[20] = 0x08;   /* 内核代码段 */
        stack_frame[22] = g_user_saved_rsp;
        stack_frame[23] = 0x10;   /* 内核数据段 */
        /* 后续EOI照常发送，iretq 后进入内核清理 */
    }

    if (vector >= 32 && vector < 48) {
        /* IRQ处理 */
        int irq = vector - 32;
        if (irq_handlers[irq]) {
            irq_handlers[irq]();
        }
        /* V0.57.4: 用户线程轮转。
         * PIT 走的是 IRQ0（vector 32），不是 APIC 定时器的 0x40 —— 用户态
         * 时钟实际落在这里，线程切换必须接在这一条路径上。 */
        if (irq == 0 && (stack_frame[20] & 3) == 3) {
            extern void uthread_timer_tick(uint64_t *frame);
            extern int user_process_kill_pending(void);
            extern int user_process_timeout_triggered(void);
            if (!user_process_kill_pending() && !user_process_timeout_triggered()) {
                uthread_timer_tick(stack_frame);
            }
        }
        /* V0.13.46: 键盘IRQ直通检查 Ctrl+Alt+K（未响应进程强制结束，不依赖GUI主循环） */
        if (irq == 1) {
            extern int keyboard64_force_kill_triggered(void);
            extern int user_process_kill_current(void);
            if (keyboard64_force_kill_triggered()) {
                user_process_kill_current();
            }
        }
        /* 发送EOI */
        extern int apic64_is_apic_mode(void);
        extern void apic64_send_eoi(void);
        if (apic64_is_apic_mode()) {
            apic64_send_eoi();  /* APIC模式：发送EOI到Local APIC */
        } else {
            /* PIC模式：发送EOI到PIC */
            if (irq >= 8) {
                __asm__ volatile("movb $0x20, %al\noutb %al, $0xA0");  /* 从PIC EOI */
            }
            __asm__ volatile("movb $0x20, %al\noutb %al, $0x20");  /* 主PIC EOI */
        }
    } else if (vector == 0x40 || vector == 0x42) {
        /* APIC定时器(0x40) / HPET 主 tick(0x42) 中断（1000Hz=1ms），调度主体共用 */
        extern uint64_t g_kernel_tick64;
        atomic_inc64(&g_kernel_tick64);
        /* V0.13.46: 用户态强制终止截获（kill请求 或 未响应超时看门狗） */
        extern int user_process_kill_pending(void);
        extern int user_process_timeout_triggered(void);
        extern int user_process_current_pid(void);
        if ((stack_frame[20] & 3) == 3) {  /* 用户态 */
            /* V0.20.99: 保存用户RIP */
            extern uint64_t g_last_user_rip;
            g_last_user_rip = stack_frame[19];
            if (user_process_kill_pending() || user_process_timeout_triggered()) {
                extern uint64_t g_user_saved_rip;
                extern uint64_t g_user_saved_rsp;
                extern uint64_t g_user_saved_cr3;

                extern void user_force_exit_trampoline(void);
                uint64_t cur_rsp = 0;
                __asm__ volatile("mov %%rsp, %0" : "=r"(cur_rsp));
                __asm__ volatile("mov %0, %%cr3" : : "r"(g_user_saved_cr3) : "memory");
                stack_frame[19] = (uint64_t)user_force_exit_trampoline;
                stack_frame[20] = 0x08;      /* 内核代码段 */
                stack_frame[22] = cur_rsp;   /* 当前中断处理栈 */
                stack_frame[23] = 0x10;      /* 内核数据段 */
            }
        }
        /* V0.57.4: 用户线程轮转（只在 CPL=3 且同 owner 有 >=2 线程时生效） */
        extern void uthread_timer_tick(uint64_t *frame);
        if ((stack_frame[20] & 3) == 3) {
            extern int user_process_kill_pending(void);
            extern int user_process_timeout_triggered(void);
            if (!user_process_kill_pending() && !user_process_timeout_triggered()) {
                uthread_timer_tick(stack_frame);
            }
        }
        extern void smp_timer_tick(void);
        smp_timer_tick();  /* SMP调度tick */
        if (vector == 0x40) {
            extern void apic64_timer_eoi(void);
            apic64_timer_eoi();  /* LAPIC: EOI 到 Local APIC */
        } else {
            /* HPET 主 tick：计数留证后走 IOAPIC EOI */
            extern void hpet64_tick_handler(void);
            extern void apic64_send_eoi(void);
            hpet64_tick_handler();
            apic64_send_eoi();
        }
    } else if (vector == 0x41) {
        /* AHCI/SATA 控制器 PCI 中断（IOAPIC 路由，级别触发） */
        extern void ahci64_irq_handler(void);
        ahci64_irq_handler();
        extern void apic64_send_eoi(void);
        apic64_send_eoi();
    } else if (vector == 0x50) {
        /* IPI核间中断 */
        extern void apic64_timer_eoi(void);
        extern void smp_ipi_handler(void);
        smp_ipi_handler();
        apic64_timer_eoi();  /* 发送EOI到Local APIC */
    } else {
        /* 异常处理 */
        vga_puts("\n[EXCEPTION] vector=");
        extern void vga_put_hex(uint64_t);
        vga_put_hex(vector);
        vga_puts(" error=");
        vga_put_hex(error);
        vga_puts(" rip=");
        vga_put_hex(rip);
        /* V0.20.78: dump CR2 and PTE */
        if (vector == 14) {
            uint64_t cr2;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            vga_puts("\n  CR2="); vga_put_hex(cr2);
            extern uint64_t vmm64_phys_to_virt(uint64_t);
            uint64_t cr3; __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
            uint64_t *pml4 = (uint64_t*)vmm64_phys_to_virt(cr3 & 0xFFFFFFFFF000ULL);
            int pi = cr2 >> 39 & 0x1FF;
            vga_puts("\n  PML4e="); vga_put_hex(pml4[pi]);
            if (pml4[pi] & 1) {
                uint64_t *pdpt = (uint64_t*)vmm64_phys_to_virt(pml4[pi] & 0xFFFFFFFFF000ULL);
                int di = cr2 >> 30 & 0x1FF;
                vga_puts(" PDPTe="); vga_put_hex(pdpt[di]);
                if (pdpt[di] & 1) {
                    uint64_t *pd = (uint64_t*)vmm64_phys_to_virt(pdpt[di] & 0xFFFFFFFFF000ULL);
                    int ti = cr2 >> 21 & 0x1FF;
                    vga_puts(" PDe="); vga_put_hex(pd[ti]);
                    if (pd[ti] & 1 && !(pd[ti] & 0x80)) {
                        int si = cr2 >> 12 & 0x1FF;
                        uint64_t *pt = (uint64_t*)vmm64_phys_to_virt(pd[ti] & 0xFFFFFFFFF000ULL);
                        vga_puts(" PTe="); vga_put_hex(pt[si]);
                    }
                }
            }
        }
        vga_puts("\n");

        /* ------------------------------------------------------------------
         * V0.33: 用户态异常 —— 只终止该进程，不整机挂死
         *
         * 契约依据：OPEN_BUGS SEC-07「用户态异常只终止该进程、不再整机 hlt」。
         * 这条契约 v0.13.68 就写下了，但 64 位这条路径没落地：
         * 任何用户态异常（非法地址、空指针、除零）都会走到下面那句 hlt，
         * 一个用户程序就能把整机带死 —— 启动自检里 glibc_test 缺一个 syscall
         * 就足以让系统进不了桌面。
         *
         * 做法（与既有的「未响应进程强制退出」路径同构）：
         *   1. 标记当前用户进程为异常终止；
         *   2. 切回内核页表；
         *   3. 改写中断帧，让 iretq 回到内核的 user_return_trampoline，
         *      由它做进程清理并把控制权交还内核（exec 调用者）。
         * 内核态异常维持原样（hlt 停机）—— 内核自己出异常没法「只杀一个进程」。
         * ------------------------------------------------------------------ */
        if ((stack_frame[20] & 3) == 3) {   /* CS.RPL == 3 → 用户态 */
            /* COW: 写错误码(bit1=write) + 页存在(bit0=present) → 写时复制 */
            if (vector == 14 && (stack_frame[18] & 2) && (stack_frame[18] & 1)) {
                uint64_t cr2;
                __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
                extern int cow_handle_fault(uint64_t cr2);
                if (cow_handle_fault(cr2) == 0) {
                    vga_puts("[COW] page copied, resuming\n");
                    return;  /* 成功，恢复执行 */
                }
            }
            /* V0.33: 打印用户寄存器 —— 定位「为什么飞」的第一手材料。
             * 帧布局：[2..16] = r15,r14,...,rax；[19]=rip [20]=cs */
            vga_puts("\n  user regs:");
            {
                static const char *rn[15] = {
                    "r15","r14","r13","r12","r11","r10","r9","r8","rbp",
                    "rdi","rsi","rdx","rcx","rbx","rax"
                };
                for (int i = 0; i < 15; i++) {
                    vga_puts(" ");
                    vga_puts(rn[i]);
                    vga_puts("=");
                    vga_put_hex(stack_frame[2 + i]);
                }
                /* V0.33: 用户栈指针 —— 判断「对齐类 #GP」的关键证据 */
                vga_puts(" rsp=");
                vga_put_hex(stack_frame[22]);
                vga_puts("\n");
            }
#ifdef HEPU_TRACE_LINUX_SYSCALL
            {
                uint32_t fslo = 0, fshi = 0;
                __asm__ volatile("rdmsr" : "=a"(fslo), "=d"(fshi) : "c"(0xC0000100));
                vga_puts("\n  FS_BASE="); vga_put_hex(((uint64_t)fshi << 32) | fslo);
                uint64_t dbg_fs = 0; extern uint64_t user_process_get_fsbase(void);
                dbg_fs = user_process_get_fsbase();
                vga_puts(" proc_fsbase="); vga_put_hex(dbg_fs);
                vga_puts("\n");
                /* 证据优先: 回溯用户栈, 还原「谁调用了野指针」的调用链 */
                extern uint64_t user_process_current_pml4(void);
                extern void user_process_dump_user_stack(uint64_t pml4, uint64_t rsp);
                uint64_t pml4 = user_process_current_pml4();
                user_process_dump_user_stack(pml4, stack_frame[22]);
                /* 故障点指令字节 —— 判断 rip 是否落在指令边界上 */
                extern void user_process_dump_fault_bytes(uint64_t pml4, uint64_t rip);
                user_process_dump_fault_bytes(pml4, stack_frame[19]);
                /* glibc __run_exit_handlers 崩溃时 r14 = __exit_funcs 链表头 */
                extern void user_process_dump_exit_list(uint64_t pml4, uint64_t head, uint64_t fs);
                uint64_t fsb = 0;
                { uint32_t lo = 0, hi = 0;
                  __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000100));
                  fsb = ((uint64_t)hi << 32) | lo; }
                user_process_dump_exit_list(pml4, stack_frame[3], fsb);
            }
#endif
            extern int user_process_fault_current(uint64_t vector);
            extern uint64_t g_user_saved_rip;
            extern uint64_t g_user_saved_rsp;
            extern uint64_t g_user_saved_cr3;
            if (user_process_fault_current(vector)) {
                /* 切回内核页表 */
                __asm__ volatile("mov %0, %%cr3" : : "r"(g_user_saved_cr3) : "memory");
                /* 改写中断帧：iretq 跳回内核 trampoline 走正常清理流程 */
                stack_frame[19] = g_user_saved_rip;
                stack_frame[20] = 0x08;   /* 内核代码段 */
                stack_frame[22] = g_user_saved_rsp;
                stack_frame[23] = 0x10;   /* 内核数据段 */
                vga_puts("[EXCEPTION] user-mode fault handled: process terminated, returning to kernel\n");
                /* V0.33: 用户态异常计数 —— 供启动自检电池给出可判定结论 */
                atomic_inc64(&g_user_exception_count);   /* SB-04: 多核并发递增 */
                return;   /* 交回中断桩，桩会 iretq 回内核 */
            }
            vga_puts("[EXCEPTION] user-mode fault with no active process, stopping\n");
            /* 无活跃进程可终止: 也是内核没法收拾的局面, 走带回溯的 panic */
            hepu_panic_kernel_fault(stack_frame);
        }
        /* 内核态异常: 内核自己出异常无法"只杀一个进程", 但不能无声 hlt ——
         * 至少要留下 cr2/rip/栈回溯, 否则定位一次崩溃要反复猜。 */
        hepu_panic_kernel_fault(stack_frame);
    }
}

/* SEC-06: 内核态异常/缺页的现场转储 + 停机。
 *
 * 原来内核态异常直接落到 while(1) hlt —— 只打印了 vector/rip/cr2 就静止,
 * 没有任何栈回溯, 定位一次内核崩溃要反复猜测改代码重编。
 * hepu_panic() 早就写好了(rbp 链回溯), 但只被显式调用点用到, 异常路径没接上。
 *
 * 这里用中断帧里的 rbp(stack_frame[10])做回溯起点 —— 注意不能用当前函数的
 * rbp, 那是 irq_handler_64_body 自己的, 往回走只会走到中断处理函数, 看不到
 * 出事那条调用链。
 *
 * 帧布局: [2..16]=r15,r14,r13,r12,r11,r10,r9,r8,rbp,rdi,rsi,rdx,rcx,rbx,rax
 *         [19]=rip [20]=cs [21]=rflags [22]=rsp [23]=ss
 */
static void hepu_panic_kernel_fault(uint64_t *sf)
{
    uint64_t vector = sf[17];
    uint64_t error  = sf[18];
    uint64_t rip    = sf[19];
    uint64_t rbp    = sf[10];
    uint64_t cr2    = 0;

    /* 关键: 内核态崩溃时必须走串口。
     * vga_puts 写的是 VGA 文本缓冲(0xB8000), 在 GOP/VBE 图形模式下屏幕上
     * 什么都看不到 —— 实测触发缺页后屏幕只有 "Guest has not initialized the
     * display", 串口停在触发点之后什么都没有, 等于崩溃现场完全丢失。
     * 串口是唯一无条件可用的诊断通道。 */
    extern void serial_puts(uint16_t port, const char *s);
    extern void serial_hex(uint64_t v);

    {
        char stop[11];
        k64_stop_code(vector, stop);
        serial_puts(0x3F8, "\n=== HEPU OS - DIAGNOSTIC REPORT (64-bit kernel) ===\n");
        serial_puts(0x3F8, "STOP: "); serial_puts(0x3F8, stop);
        serial_puts(0x3F8, "\nException: "); serial_puts(0x3F8, k64_exc_name(vector));
        serial_puts(0x3F8, "\n");
        serial_puts(0x3F8, "Hint: "); serial_puts(0x3F8, k64_hint(vector));
        serial_puts(0x3F8, "\n\n");
    }
    serial_puts(0x3F8, "  vector=0x"); serial_hex(vector);
    serial_puts(0x3F8, " error=0x");   serial_hex(error);
    serial_puts(0x3F8, "\n  rip=0x");    serial_hex(rip);
    serial_puts(0x3F8, " cs=0x");        serial_hex(sf[20]);
    serial_puts(0x3F8, " rflags=0x");    serial_hex(sf[21]);
    serial_puts(0x3F8, " rsp=0x");       serial_hex(sf[22]);
    serial_puts(0x3F8, "\n");
    if (vector == 14) {
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        serial_puts(0x3F8, "  CR2=0x"); serial_hex(cr2);
        serial_puts(0x3F8, (error & 0x1) ? " (protection violation)" : " (not present)");
        serial_puts(0x3F8, (error & 0x2) ? " write" : " read");
        serial_puts(0x3F8, "\n");
    }
    /* 栈回溯: rbp 链。用中断帧里的 rbp(sf[10]), 不是当前函数的 rbp ——
     * 后者往回走只会看到中断处理函数自己, 看不到出事那条调用链。
     * 限制 8 层并做对齐/单调检查: 无帧指针编译时链会提前断, 不能当真无限走。 */
    serial_puts(0x3F8, "Stack trace (rbp chain, approximate):\n");
    int frames = 0;
    for (int i = 0; i < 8 && rbp; i++) {
        if (rbp < 0x1000ULL || (rbp & 0x7)) break;
        uint64_t ret = ((uint64_t *)rbp)[1];
        if (!ret) break;
        serial_puts(0x3F8, "  [0x"); serial_hex(ret); serial_puts(0x3F8, "]\n");
        frames++;
        uint64_t next = ((uint64_t *)rbp)[0];
        if (next <= rbp) break;
        rbp = next;
    }
    if (!frames) {
        /* 实测本内核以 -fomit-frame-pointer 编译, 绝大多数函数没有 rbp 链,
         * 回溯拿到空是常态。与其打一行空标题, 不如把原始 rbp 与 rsp 附近
         * 的栈字打出来 —— 人工用 objdump 对着 rip 反查, 一样能定位。 */
        serial_puts(0x3F8, "  (rbp chain unavailable; kernel built without frame pointers)\n");
        serial_puts(0x3F8, "  raw rbp=0x"); serial_hex(sf[10]); serial_puts(0x3F8, "\n");
        {
            uint64_t sp = sf[22];
            serial_puts(0x3F8, "  raw stack at fault rsp:\n");
            if (sp >= 0x1000ULL && !(sp & 0x7)) {
                for (int k = 0; k < 8; k++) {
                    uint64_t v;
                    /* 逐字读; 若某一格本身又被换出/未映射, 这里会再缺一次页,
                     * 但异常处理期间 IF=0 且同栈, 风险有限。 */
                    v = ((uint64_t *)sp)[k];
                    serial_puts(0x3F8, "   +"); serial_hex((uint64_t)(k * 8));
                    serial_puts(0x3F8, " 0x"); serial_hex(v); serial_puts(0x3F8, "\n");
                }
            }
        }
    }
    serial_puts(0x3F8, "System halted (kernel fault, cannot isolate).\n");
    /* 屏幕通道: 图形模式下也能看见, 不只是串口 */
    bsod64_gfx_report(vector, error, rip, sf[22], cr2);
    /* 跨重启摘要: 只写 CMOS, 不碰磁盘 */
    {
        extern void crash_nvram_save(uint32_t, uint64_t, uint64_t);
        crash_nvram_save((uint32_t)vector, rip, cr2);
    }
    while (1) { __asm__ volatile("hlt"); }
}

/* 2026-11-19: 崩溃摘要跨重启。
 * crashlog 只写内存环形缓冲，重启即清；崩溃上下文又不能用 ext2（会 GPF）。
 * 折中: 把最小的现场信息（STOP 码 / RIP / CR2）写进 CMOS NVRAM —— 纯端口 IO，
 * 不碰磁盘控制器，崩溃现场相对安全。下次启动读回来打一条横幅再清掉。 */
#define CRASH_NV_MAGIC 0x53415243u   /* 'CRAS' */
/* 0x30-0x31 是传统 BIOS 的"扩展内存大小"字节, reset 时会被重写 ——
 * 实测把 0x41 打成 0x17 就是这个原因。改用 0x50 起的真正空闲区。 */
#define CRASH_NV_BASE  0x50

static void ccmos_write(uint8_t reg, uint8_t val)
{
    __asm__ volatile ("outb %%al, %%dx" : : "a"(reg), "d"((uint16_t)0x70));
    __asm__ volatile ("outb %%al, %%dx" : : "a"(val), "d"((uint16_t)0x71));
}

static uint8_t ccmos_read(uint8_t reg)
{
    uint8_t v;
    __asm__ volatile ("outb %%al, %%dx" : : "a"(reg), "d"((uint16_t)0x70));
    __asm__ volatile ("inb %%dx, %%al" : "=a"(v) : "d"((uint16_t)0x71));
    return v;
}

static void nv_put32(uint8_t off, uint32_t v)
{
    int i;
    for (i = 0; i < 4; i++) ccmos_write(CRASH_NV_BASE + off + i, (uint8_t)(v >> (8 * i)));
    for (i = 0; i < 4; i++) ccmos_write(CRASH_NV_BASE + off + i, (uint8_t)(v >> (8 * i)));
}

static uint32_t nv_get32(uint8_t off)
{
    uint32_t v = 0;
    int i;
    for (i = 0; i < 4; i++) {
        uint8_t b = ccmos_read(CRASH_NV_BASE + off + i);
        v |= (uint32_t)b << (8 * i);
    }
    return v;
}

static void nv_put64(uint8_t off, uint64_t v)
{
    nv_put32(off, (uint32_t)(v & 0xFFFFFFFFu));
    nv_put32(off + 4, (uint32_t)(v >> 32));
}

static uint64_t nv_get64(uint8_t off)
{
    uint64_t lo = nv_get32(off);
    uint64_t hi = nv_get32(off + 4);
    return lo | (hi << 32);
}

/* 布局: 0x30 magic / 0x34 stop / 0x38 rip(8) / 0x40 cr2(8) —— 共 24 字节 */
void crash_nvram_save(uint32_t stop, uint64_t rip, uint64_t cr2)
{
    nv_put32(0, CRASH_NV_MAGIC);
    nv_put32(4, stop);
    nv_put64(8, rip);
    nv_put64(16, cr2);
}

uint32_t crash_nvram_raw_magic(void)
{
    return nv_get32(0);
}

int crash_nvram_load(uint32_t *stop, uint64_t *rip, uint64_t *cr2)
{
    if (nv_get32(0) != CRASH_NV_MAGIC) return 0;
    if (stop) *stop = nv_get32(4);
    if (rip)  *rip  = nv_get64(8);
    if (cr2)  *cr2  = nv_get64(16);
    return 1;
}

void crash_nvram_clear(void)
{
    nv_put32(0, 0);
}

/* 2026-11-19: 64 位崩溃也要能"看见"。原先只有串口 + vga_puts 文本，
 * 图形模式下屏幕一片空白，等于现场只剩串口一份。
 * 这里不经过 gfx 状态机(崩溃时它未必完好)，直接拿 front_buffer 逐像素画：
 * 深蓝底 + 白字，字形取自 font_data.c 的 font8x16。 */
static void bsod64_gfx_report(uint64_t vector, uint64_t error, uint64_t rip,
                              uint64_t rsp, uint64_t cr2)
{
    extern void gfx64_set_target_front(void);
    extern uint8_t *gfx64_get_framebuffer(void);
    extern int gfx64_get_width(void);
    extern int gfx64_get_height(void);
    extern const uint8_t font8x16[128][16];

    uint8_t *fb8;
    uint32_t *fb;
    int w, h, i, y;
    int scale = 2;
    uint32_t blue = 0x0000008Bu, white = 0x00FFFFFFu;

    gfx64_set_target_front();
    fb8 = gfx64_get_framebuffer();
    w = gfx64_get_width();
    h = gfx64_get_height();
    /* 合理性检查: 崩溃现场这些值本身可能是坏的 */
    if (!fb8 || w < 640 || w > 3840 || h < 400 || h > 2160) return;
    fb = (uint32_t *)fb8;

    for (y = 0; y < h; y++) {
        uint32_t *row = fb + (uint64_t)y * w;
        for (i = 0; i < w; i++) row[i] = blue;
    }

    {
        int x = 48, ty = 60;
        const char *lines[7];
        char l2[40], l3[40], l4[40], l5[48];
        /* STOP 码 */
        l2[0]='S';l2[1]='T';l2[2]='O';l2[3]='P';l2[4]=':';l2[5]=' ';l2[6]='0';l2[7]='x';
        for (i = 0; i < 8; i++) {
            static const char hx[] = "0123456789ABCDEF";
            l2[8 + i] = hx[(vector >> (4 * (7 - i))) & 0xF];
        }
        l2[16] = 0;
        /* Exception 名 */
        { const char *nm = k64_exc_name(vector); int k = 0;
          const char *pfx = "Exception: ";
          while (pfx[k]) { l3[k] = pfx[k]; k++; }
          i = 0; while (nm[i] && k < 38) l3[k++] = nm[i++];
          l3[k] = 0; }
        /* RIP */
        { const char *pfx = "RIP: 0x"; int k = 0; i = 0;
          while (pfx[k]) { l4[k] = pfx[k]; k++; }
          for (i = 0; i < 16; i++) {
            static const char hx[] = "0123456789ABCDEF";
            l4[k++] = hx[(rip >> (4 * (15 - i))) & 0xF];
          }
          l4[k] = 0; }
        { const char *pfx = "CR2: 0x"; int k = 0; i = 0;
          while (pfx[k]) { l5[k] = pfx[k]; k++; }
          for (i = 0; i < 16; i++) {
            static const char hx[] = "0123456789ABCDEF";
            l5[k++] = hx[(cr2 >> (4 * (15 - i))) & 0xF];
          }
          l5[k] = 0; }
        (void)error; (void)rsp;

        lines[0] = "*** HEPU OS - DIAGNOSTIC REPORT (64-bit) ***";
        lines[1] = l2; lines[2] = l3; lines[3] = l4; lines[4] = l5;
        lines[5] = "";
        lines[6] = "System halted. See serial log for full stack trace.";
        for (y = 0; y < 7; y++) {
            const char *sp = lines[y];
            x = 48; ty = 60 + y * (16 * scale + 10);
            while (*sp) {
                unsigned char c = (unsigned char)*sp;
                if (c >= 32 && c < 128) {
                    const uint8_t *gl = font8x16[c];
                    int gy, gx;
                    for (gy = 0; gy < 16; gy++) {
                        uint8_t rowb = gl[gy];
                        for (gx = 0; gx < 8; gx++) {
                            if (rowb & (0x80 >> gx)) {
                                int sy, sx;
                                for (sy = 0; sy < scale; sy++)
                                    for (sx = 0; sx < scale; sx++) {
                                        int px = x + gx * scale + sx;
                                        int py = ty + gy * scale + sy;
                                        if (px >= 0 && px < w && py >= 0 && py < h)
                                            fb[(uint64_t)py * w + px] = white;
                                    }
                            }
                        }
                    }
                }
                x += 8 * scale;
                sp++;
            }
        }
    }
}

/* 2026-11-19: 开机诊断自检套件。
 * 和 /bsod_selftest 不同: 那个跑完会停机(它是真崩溃路径), 这个只验证
 * 诊断子系统的各个零件还完好, 跑完系统继续正常启动。
 * 检查: 串口通道 / 图形表面 / 异常名表 / STOP 码格式化。 */
int diag_selftest64(void)
{
    extern void serial_puts(uint16_t port, const char *s);
    extern uint8_t *gfx64_get_framebuffer(void);
    extern int gfx64_get_width(void);
    extern int gfx64_get_height(void);
    extern void gfx64_set_target_front(void);
    int pass = 0, total = 0, i;

    /* 1. 串口通道: 能写出去就算通 */
    total++;
    serial_puts(0x3F8, "[DIAG-SELFTEST] serial channel: PASS\n");
    pass++;

    /* 2. 图形表面: 崩溃时唯一能画屏的通道, 必须可用 */
    total++;
    {
        uint8_t *fb;
        int w, h;
        gfx64_set_target_front();
        fb = gfx64_get_framebuffer();
        w = gfx64_get_width();
        h = gfx64_get_height();
        if (fb && w >= 640 && w <= 3840 && h >= 400 && h <= 2160) {
            serial_puts(0x3F8, "[DIAG-SELFTEST] gfx surface: PASS\n");
            pass++;
        } else {
            serial_puts(0x3F8, "[DIAG-SELFTEST] gfx surface: FAIL (no usable framebuffer)\n");
        }
    }

    /* 3. 异常名表: 32 个向量都要能取到非空名字, 否则报告会出现空白 */
    total++;
    {
        int ok = 1;
        for (i = 0; i < 32; i++) {
            const char *n = k64_exc_name((uint64_t)i);
            if (!n || !n[0]) { ok = 0; break; }
        }
        if (ok) { serial_puts(0x3F8, "[DIAG-SELFTEST] exception names: PASS (32/32)\n"); pass++; }
        else    { serial_puts(0x3F8, "[DIAG-SELFTEST] exception names: FAIL\n"); }
    }

    /* 4. STOP 码格式化: 必须是 0x + 8 位定长, 否则日志检索会错位 */
    total++;
    {
        int ok = 1;
        for (i = 0; i < 32; i++) {
            char sb[11];
            k64_stop_code((uint64_t)i, sb);
            if (sb[0] != '0' || sb[1] != 'x' || sb[10] != 0) { ok = 0; break; }
        }
        if (ok) { serial_puts(0x3F8, "[DIAG-SELFTEST] stop code format: PASS\n"); pass++; }
        else    { serial_puts(0x3F8, "[DIAG-SELFTEST] stop code format: FAIL\n"); }
    }

    serial_puts(0x3F8, pass == total
        ? "[DIAG-SELFTEST] RESULT: PASS\n"
        : "[DIAG-SELFTEST] RESULT: FAIL\n");
    return pass;
}

/* 2026-11-19: shell `bsod` 的 64 位实现 —— 构造一个假中断帧，
 * 直接走 hepu_panic_kernel_fault，验证 STOP 码/异常名/Hint/回溯整条链。 */
void cmd_bsod64(void)
{
    uint64_t sf[24];
    int i;
    extern void serial_puts(uint16_t port, const char *s);

    for (i = 0; i < 24; i++) sf[i] = 0;
    __asm__ volatile ("mov %%cs, %0"    : "=r"(sf[20]));
    __asm__ volatile ("pushfq; pop %0"  : "=r"(sf[21]));
    __asm__ volatile ("mov %%rsp, %0"   : "=r"(sf[22]));
    __asm__ volatile ("mov %%rbp, %0"   : "=r"(sf[10]));
    __asm__ volatile ("call 1f\n1:\tpopq %0" : "=r"(sf[19]));
    sf[17] = 14;    /* page fault */
    sf[18] = 0x2;   /* write, not present */

    serial_puts(0x3F8, "[BSOD] 64-bit self-test requested from shell\n");
    hepu_panic_kernel_fault(sf);
}

/* Hepu panic: 打印错误信息+栈帧后停机（SEC-06） */
void hepu_panic(const char *msg)
{
    vga_puts("\n*** HEPU PANIC ***\n");
    vga_puts(msg);
    vga_puts("\n");
    /* 打印当前栈回溯（从rbp链向上） */
    vga_puts("Stack trace:\n");
    uint64_t *rbp;
    __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));
    for (int i = 0; i < 8 && rbp; i++) {
        uint64_t rip = rbp[1];
        char buf[20]; int j = 0; uint64_t v = rip;
        if (v == 0) buf[j++] = '0';
        while (v > 0) { buf[j++] = '0' + (v & 0xF); v >>= 4; }
        vga_puts("  [0x");
        for (int k = j-1; k >= 0; k--) { char c[2]; c[0]=buf[k]; c[1]=0; vga_puts(c); }
        vga_puts("]\n");
        rbp = (uint64_t *)rbp[0];
    }
    vga_puts("System halted.\n");
    while (1) { __asm__ volatile("hlt"); }
}

/* 中断处理包装函数：用per-CPU缓冲区保护FPU状态（解决SMP 4核竞态）
   中断在处理期间IF=0不会嵌套，因此每CPU一个缓冲区足够 */
void irq_handler_64(uint64_t *stack_frame)
{
    extern int apic64_is_apic_mode(void);
    uint32_t cpu = 0;
    if (apic64_is_apic_mode()) {
        /* 读取Local APIC ID（虚拟地址0xF0000020，每CPU读到自己的ID） */
        cpu = ((*(volatile uint32_t *)0xF0000020) >> 24) & 0xFF;
    }
    /* SB-08: 不再 & 7 回绕。核 ID 超出保存区就是真 bug, 明确报错停机,
     * 而不是悄悄和别的核共用一个 FXSAVE 缓冲。 */
    if (cpu >= FPU_SAVE_MAX_CPUS) {
        vga_puts("[FPU] ERROR: cpu id beyond fpu_save_area\n");
        while (1) { __asm__ volatile("cli; hlt"); }
    }
    uint8_t *area = fpu_save_area[cpu];

    /* 保存被中断者的FPU/x87/SSE状态 */
    __asm__ volatile("fxsave (%0)" : : "r"(area) : "memory");

    /* 处理中断 */
    irq_handler_64_body(stack_frame);

    /* 恢复被中断者的FPU状态 */
    __asm__ volatile("fxrstor (%0)" : : "r"(area) : "memory");
}

/* 进程退出 */
void process_exit_64(void)
{
    extern void process64_exit(int exit_code);
    process64_exit(0);
    /* process64_exit不会返回，这里是兜底 */
    while (1) { __asm__ volatile("hlt"); }
}

/* ============================================================================
 * Crypto 支持函数（P0-1：64位补齐 crypto 模块依赖的符号）
 * ========================================================================== */

/* serial 输出路由到 uart64 COM1 */
void serial_puts(uint16_t base, const char *s)
{
    (void)base;
    extern int uart64_puts(int com, const char *str);
    if (s) uart64_puts(0, s);
}

void serial_puthex(uint16_t base, uint32_t val)
{
    char buf[9];
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[8] = '\0';
    serial_puts(base, buf);
}

/* memcmp 包装到 shared_memcmp */
int memcmp(const void *s1, const void *s2, size_t n)
{
    extern int shared_memcmp(const void *s1, const void *s2, size_t n);
    return shared_memcmp(s1, s2, n);
}

/* 极简 vga_printf：支持 %s %d %u %x %llu %% */
void vga_printf(const char *fmt, ...)
{
    extern void vga_puts(const char *s);
    extern char *shared_itoa(int value, char *str, int base);
    va_list ap;
    va_start(ap, fmt);
    char buf[32];
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == 's') {
                const char *s = va_arg(ap, const char *);
                vga_puts(s ? s : "(null)");
                fmt++;
            } else if (*fmt == 'd') {
                int v = va_arg(ap, int);
                shared_itoa(v, buf, 10);
                vga_puts(buf);
                fmt++;
            } else if (*fmt == 'u') {
                unsigned int v = va_arg(ap, unsigned int);
                shared_itoa((int)v, buf, 10);
                vga_puts(buf);
                fmt++;
            } else if (*fmt == 'x') {
                unsigned int v = va_arg(ap, unsigned int);
                shared_itoa((int)v, buf, 16);
                vga_puts(buf);
                fmt++;
            } else if (*fmt == 'l') {
                fmt++;
                if (*fmt == 'l') {
                    fmt++;
                    if (*fmt == 'u') {
                        unsigned long long v = va_arg(ap, unsigned long long);
                        if (v == 0) { vga_puts("0"); }
                        else {
                            char tmp[24]; int i = 23; tmp[23] = '\0';
                            while (v) { tmp[--i] = '0' + (char)(v % 10); v /= 10; }
                            vga_puts(&tmp[i]);
                        }
                        fmt++;
                    }
                }
            } else if (*fmt == '%') {
                vga_puts("%");
                fmt++;
            } else {
                vga_puts("%");
                /* 不支持的格式直接跳过 */
                fmt++;
            }
        } else {
            char c[2] = {*fmt, '\0'};
            vga_puts(c);
            fmt++;
        }
    }
    va_end(ap);
}

/* ============================================================================
 * 32 位网络栈 API 兼容桩（供 TLS 客户端链接使用）
 * 64 位内核网络栈（net64_tcp 等）接口不同，P0-2 阶段先提供桩实现，
 * 使 TLS 符号可被链接保留；真正的 TCP 集成留待后续阶段。
 * ========================================================================== */

/* 定时器 tick：转发到 64 位 PIT */
uint32_t pit_get_ticks(void)
{
    extern uint64_t pit64_get_ticks(void);
    return (uint32_t)pit64_get_ticks();
}

/* 网络轮询：64 位内核暂未对接，空实现 */
void net_poll(void)
{
}

/* TCP 状态查询：未集成，返回错误 */
int tcp_get_state(int fd, void *state)
{
    (void)fd;
    (void)state;
    return -1;
}

/* TCP 发送：未集成，返回错误 */
int tcp_send(int fd, const uint8_t *data, uint32_t length)
{
    (void)fd;
    (void)data;
    (void)length;
    return -1;
}

/* TCP 接收：未集成，返回错误 */
int tcp_receive(int fd, uint8_t *buffer, uint32_t max_length)
{
    (void)fd;
    (void)buffer;
    (void)max_length;
    return -1;
}

/* TCP 关闭：未集成，返回错误 */
int tcp_close(int fd)
{
    (void)fd;
    return -1;
}

/* TCP 连接：未集成，返回错误 */
int tcp_connect(uint32_t remote_ip, uint16_t remote_port, uint16_t local_port)
{
    (void)remote_ip;
    (void)remote_port;
    (void)local_port;
    return -1;
}

/* strncpy：转发到共享库实现 */
char *strncpy(char *dest, const char *src, size_t n)
{
    extern char *shared_strncpy(char *dest, const char *src, size_t n);
    return shared_strncpy(dest, src, n);
}
