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

/* SB-05: 带打印锁的对外入口(默认路径) */
void vga_puts(const char *str)
{
    uint64_t f = spinlock_lock_irqsave(&print_lock);
    vga_puts_nolock(str);
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

/* V0.13.46: 内核tick（vec40每1ms递增，未响应看门狗用） */
/* V0.58.5 / SB-02: 多核的 0x40 中断会并发写，改原子递增 */
uint64_t g_kernel_tick64 = 0;
/* V0.33: 用户态异常累计次数（启动自检电池用来给可判定结论） */
volatile uint64_t g_user_exception_count = 0;

/* 中断处理主体（原irq_handler_64逻辑） */
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
    } else if (vector == 0x40) {
        /* APIC定时器中断（每个CPU独立, 1000Hz=1ms） */
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
        extern void apic64_timer_eoi(void);
        extern void smp_timer_tick(void);
        smp_timer_tick();  /* SMP调度tick */
        apic64_timer_eoi();  /* 发送EOI到Local APIC */
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
        }
        while (1) { __asm__ volatile("hlt"); }
    }
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
