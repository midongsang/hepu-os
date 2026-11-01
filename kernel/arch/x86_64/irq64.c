/* ============================================================================
 * 赫普OS - 64位中断处理框架（V8.80新增，P5-1）
 * IDT配置、中断处理程序、异常处理
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "x86_64.h"
#include "../../include/vga.h"
#include "../../include/kheap.h"

/* 中断向量定义 */
#define IRQ0_VECTOR     32   /* 定时器 */
#define IRQ1_VECTOR     33   /* 键盘 */
#define IRQ2_VECTOR     34   /* 级联 */
#define IRQ3_VECTOR     35   /* COM2 */
#define IRQ4_VECTOR     36   /* COM1 */
#define IRQ5_VECTOR     37   /* LPT2 */
#define IRQ6_VECTOR     38   /* 软盘 */
#define IRQ7_VECTOR     39   /* LPT1 */
#define IRQ8_VECTOR     40   /* RTC */
#define IRQ9_VECTOR     41   /* ACPI */
#define IRQ10_VECTOR    42   /* 可用 */
#define IRQ11_VECTOR    43   /* 可用 */
#define IRQ12_VECTOR    44   /* PS/2鼠标 */
#define IRQ13_VECTOR    45   /* FPU */
#define IRQ14_VECTOR    46   /* 主IDE */
#define IRQ15_VECTOR    47   /* 从IDE */

/* 异常名称 */
static const char *exception_names[] = {
    "Divide Error",
    "Debug",
    "NMI",
    "Breakpoint",
    "Overflow",
    "BOUND Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 FPU Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point",
    "Virtualization",
    "Control Protection",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection",
    "VMM Communication",
    "Security Exception",
    "Reserved"
};

/* 中断处理函数类型 */
typedef void (*irq_handler_t)(void *context);

/* 中断处理表 */
static irq_handler_t irq_handlers[256];
static void *irq_handler_data[256];
static int irq64_initialized = 0;

/* 中断统计 */
static struct {
    uint64_t total_irqs;
    uint64_t per_irq[256];
    uint64_t exceptions[32];
} irq64_stats;

/* ============================================================================
 * IDT设置
 * ========================================================================== */

/* 设置IDT条目 */
static void idt64_set_gate(int vector, uint64_t handler, uint16_t selector,
                           uint8_t type_attr, uint8_t ist)
{
    /* 64位IDT条目是16字节 */
    static idt_entry64_t idt[256] __attribute__((aligned(16)));
    static int idt_loaded = 0;

    idt[vector].offset_low = handler & 0xFFFF;
    idt[vector].selector = selector;
    idt[vector].ist = ist;
    idt[vector].type_attr = type_attr;
    idt[vector].offset_mid = (handler >> 16) & 0xFFFF;
    idt[vector].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[vector].reserved = 0;

    if (!idt_loaded) {
        struct {
            uint16_t limit;
            uint64_t base;
        } __attribute__((packed)) idt_ptr;

        idt_ptr.limit = sizeof(idt) - 1;
        idt_ptr.base = (uint64_t)idt;

        __asm__ volatile("lidt %0" : : "m"(idt_ptr));
        idt_loaded = 1;
        vga_puts("[IRQ64] IDT loaded (256 entries, 64-bit)\n");
    }
}

/* ============================================================================
 * 通用中断处理程序
 * ========================================================================== */

/* 通用异常处理 */
void irq64_exception_handler(int vector, uint64_t error_code,
                             uint64_t rip, uint64_t rsp)
{
    irq64_stats.exceptions[vector]++;

    vga_printf("\n[EXCEPTION] %s (vector %d)\n",
               vector < 32 ? exception_names[vector] : "Unknown",
               vector);
    vga_printf("  Error code: 0x%llx\n", (unsigned long long)error_code);
    vga_printf("  RIP: 0x%llx\n", (unsigned long long)rip);
    vga_printf("  RSP: 0x%llx\n", (unsigned long long)rsp);

    /* 对于致命异常，挂起系统 */
    if (vector == 8 || vector == 13 || vector == 14) {
        vga_puts("[FATAL] System halted due to exception\n");
        while (1) {
            __asm__ volatile("hlt");
        }
    }
}

/* 通用IRQ处理 */
void irq64_irq_handler(int vector)
{
    irq64_stats.total_irqs++;
    irq64_stats.per_irq[vector]++;

    if (irq_handlers[vector]) {
        irq_handlers[vector](irq_handler_data[vector]);
    }

    /* 发送EOI（End of Interrupt） */
    /* 主PIC */
    if (vector >= IRQ0_VECTOR && vector <= IRQ7_VECTOR) {
        __asm__ volatile("movb $0x20, %al\noutb %al, $0x20");
    }
    /* 从PIC */
    if (vector >= IRQ8_VECTOR && vector <= IRQ15_VECTOR) {
        __asm__ volatile("movb $0x20, %al\noutb %al, $0xA0\noutb %al, $0x20");
    }
}

/* ============================================================================
 * 中断注册
 * ========================================================================== */

int irq64_register(int vector, irq_handler_t handler, void *data)
{
    if (vector < 0 || vector >= 256 || !handler) return -1;
    irq_handlers[vector] = handler;
    irq_handler_data[vector] = data;
    return 0;
}

int irq64_unregister(int vector)
{
    if (vector < 0 || vector >= 256) return -1;
    irq_handlers[vector] = NULL;
    irq_handler_data[vector] = NULL;
    return 0;
}

/* ============================================================================
 * PIC编程
 * ========================================================================== */

static void pic64_init(void)
{
    /* 初始化主PIC */
    __asm__ volatile(
        "movb $0x11, %al\noutb %al, $0x20\n"   /* ICW1: 级联+需要ICW4 */
        "movb $0x20, %al\noutb %al, $0x21\n"   /* ICW2: 中断向量起始32 */
        "movb $0x04, %al\noutb %al, $0x21\n"   /* ICW3: 从PIC在IRQ2 */
        "movb $0x01, %al\noutb %al, $0x21\n"   /* ICW4: 8086模式 */
        "movb $0x00, %al\noutb %al, $0x21\n"   /* OCW1: 启用所有中断 */
    );

    /* 初始化从PIC */
    __asm__ volatile(
        "movb $0x11, %al\noutb %al, $0xA0\n"   /* ICW1 */
        "movb $0x28, %al\noutb %al, $0xA1\n"   /* ICW2: 中断向量起始40 */
        "movb $0x02, %al\noutb %al, $0xA1\n"   /* ICW3: 连接主PIC IRQ2 */
        "movb $0x01, %al\noutb %al, $0xA1\n"   /* ICW4 */
        "movb $0x00, %al\noutb %al, $0xA1\n"   /* OCW1 */
    );

    vga_puts("[IRQ64] PIC initialized (master/slave)\n");
}

/* ============================================================================
 * 中断启用/禁用
 * ========================================================================== */

void irq64_enable(void)
{
    __asm__ volatile("sti");
    vga_puts("[IRQ64] Interrupts enabled\n");
}

void irq64_disable(void)
{
    __asm__ volatile("cli");
    vga_puts("[IRQ64] Interrupts disabled\n");
}

void irq64_mask(int irq)
{
    uint16_t port = (irq < 8) ? 0x21 : 0xA1;
    uint8_t line = (irq < 8) ? irq : irq - 8;
    uint8_t mask;
    __asm__ volatile("inb %1, %0" : "=a"(mask) : "Nd"(port));
    mask |= (1 << line);
    __asm__ volatile("outb %0, %1" : : "a"(mask), "Nd"(port));
}

void irq64_unmask(int irq)
{
    uint16_t port = (irq < 8) ? 0x21 : 0xA1;
    uint8_t line = (irq < 8) ? irq : irq - 8;
    uint8_t mask;
    __asm__ volatile("inb %1, %0" : "=a"(mask) : "Nd"(port));
    mask &= ~(1 << line);
    __asm__ volatile("outb %0, %1" : : "a"(mask), "Nd"(port));
}

/* ============================================================================
 * 初始化
 * ========================================================================== */

int irq64_init(void)
{
    if (irq64_initialized) return 0;

    vga_puts("[IRQ64] Initializing 64-bit interrupt handling...\n");

    /* 清空处理表 */
    memset(irq_handlers, 0, sizeof(irq_handlers));
    memset(irq_handler_data, 0, sizeof(irq_handler_data));
    memset(&irq64_stats, 0, sizeof(irq64_stats));

    /* 初始化PIC */
    pic64_init();

    /* 设置异常门（前32个向量） */
    for (int i = 0; i < 32; i++) {
        /* 简化：使用通用处理程序地址
         * 实际需要为每个异常编写汇编桩函数
         */
    }

    irq64_initialized = 1;
    vga_puts("[IRQ64] 64-bit interrupt handling initialized\n");

    return 0;
}

/* ============================================================================
 * 统计信息
 * ========================================================================== */

void irq64_get_stats(uint64_t *total, uint64_t *per_irq,
                     uint64_t *exceptions)
{
    if (total) *total = irq64_stats.total_irqs;
    if (per_irq) {
        memcpy(per_irq, irq64_stats.per_irq, sizeof(uint64_t) * 256);
    }
    if (exceptions) {
        memcpy(exceptions, irq64_stats.exceptions, sizeof(uint64_t) * 32);
    }
}

int irq64_is_initialized(void)
{
    return irq64_initialized;
}
