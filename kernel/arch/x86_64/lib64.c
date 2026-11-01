/* ============================================================================
 * 赫普OS - 64位基础库函数
 * V0.10.6: 初始实现（内存操作、字符串、VGA输出）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

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
static void serial_puts(const char *str)
{
    while (*str) {
        if (*str == '\n') serial_putchar('\r');
        serial_putchar(*str++);
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
    }
}

/* 输出字符串（同时输出到VGA和串口） */
void vga_puts(const char *str)
{
    serial_puts(str);

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
/* per-CPU FPU状态保存区（8个CPU × 512字节，64字节对齐）
   解决SMP多核中断时全局单缓冲区互相覆盖的竞态 */
static uint8_t fpu_save_area[8][512] __attribute__((aligned(64)));

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

    if (vector >= 32 && vector < 48) {
        /* IRQ处理 */
        int irq = vector - 32;
        if (irq_handlers[irq]) {
            irq_handlers[irq]();
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
        /* APIC定时器中断（每个CPU独立） */
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
        vga_puts("\n");
        while (1) { __asm__ volatile("hlt"); }
    }
}

/* 中断处理包装函数：用per-CPU缓冲区保护FPU状态（解决SMP 4核竞态）
   中断在处理期间IF=0不会嵌套，因此每CPU一个缓冲区足够 */
void irq_handler_64(uint64_t *stack_frame)
{
    extern int apic64_is_apic_mode(void);
    uint32_t cpu = 0;
    if (apic64_is_apic_mode()) {
        /* 读取Local APIC ID（虚拟地址0xF0000020，每CPU读到自己的ID） */
        cpu = ((*(volatile uint32_t *)0xF0000020) >> 24) & 7;
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
