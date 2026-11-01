/* ============================================================================
 * 赫普OS - 64位中断处理（简化版）
 * V0.10.6: 初始实现（IDT初始化、异常处理）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 中断描述符 */
typedef struct {
    uint16_t offset_low;    /* 偏移地址低16位 */
    uint16_t selector;      /* 代码段选择子 */
    uint8_t  ist;           /* 中断栈表索引 */
    uint8_t  type_attr;     /* 类型和属性 */
    uint16_t offset_mid;    /* 偏移地址中16位 */
    uint32_t offset_high;   /* 偏移地址高32位 */
    uint32_t reserved;      /* 保留 */
} __attribute__((packed)) idt_entry64_t;

/* IDT指针 */
typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr64_t;

/* IDT表（256个中断） */
#define IDT_ENTRIES 256
static idt_entry64_t idt64[IDT_ENTRIES] __attribute__((aligned(16)));
static idt_ptr64_t idt64_ptr;

/* 中断计数 */
static uint64_t irq_count[IDT_ENTRIES];

/* 外部中断处理函数（在entry_64.S中定义） */
extern void irq_entry_0(void);
extern void irq_entry_1(void);
extern void irq_entry_2(void);
extern void irq_entry_3(void);
extern void irq_entry_4(void);
extern void irq_entry_5(void);
extern void irq_entry_6(void);
extern void irq_entry_7(void);
extern void irq_entry_8(void);
extern void irq_entry_9(void);
extern void irq_entry_10(void);
extern void irq_entry_11(void);
extern void irq_entry_12(void);
extern void irq_entry_13(void);
extern void irq_entry_14(void);
extern void irq_entry_15(void);
extern void irq_entry_16(void);
extern void irq_entry_17(void);
extern void irq_entry_18(void);
extern void irq_entry_19(void);
extern void irq_entry_20(void);
extern void irq_entry_21(void);
extern void irq_entry_22(void);
extern void irq_entry_23(void);
extern void irq_entry_24(void);
extern void irq_entry_25(void);
extern void irq_entry_26(void);
extern void irq_entry_27(void);
extern void irq_entry_28(void);
extern void irq_entry_29(void);
extern void irq_entry_30(void);
extern void irq_entry_31(void);

/* VGA输出函数 */
extern void vga_puts(const char *str);
extern void vga_put_hex(uint64_t val);
extern void *memset(void *dst, int val, size_t n);

/* 设置IDT条目 */
static void idt64_set_gate(int vector, uint64_t handler, uint16_t selector, uint8_t type_attr, uint8_t ist)
{
    idt64[vector].offset_low = handler & 0xFFFF;
    idt64[vector].selector = selector;
    idt64[vector].ist = ist;
    idt64[vector].type_attr = type_attr;
    idt64[vector].offset_mid = (handler >> 16) & 0xFFFF;
    idt64[vector].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt64[vector].reserved = 0;
}

/* 初始化IDT */
void irq64_init(void)
{
    vga_puts("[IRQ64] Initializing 64-bit IDT...\n");

    /* 清零IDT */
    memset(idt64, 0, sizeof(idt64));

    /* 设置异常处理（0-31） */
    uint64_t handlers[] = {
        (uint64_t)irq_entry_0, (uint64_t)irq_entry_1,
        (uint64_t)irq_entry_2, (uint64_t)irq_entry_3,
        (uint64_t)irq_entry_4, (uint64_t)irq_entry_5,
        (uint64_t)irq_entry_6, (uint64_t)irq_entry_7,
        (uint64_t)irq_entry_8, (uint64_t)irq_entry_9,
        (uint64_t)irq_entry_10, (uint64_t)irq_entry_11,
        (uint64_t)irq_entry_12, (uint64_t)irq_entry_13,
        (uint64_t)irq_entry_14, (uint64_t)irq_entry_15,
        (uint64_t)irq_entry_16, (uint64_t)irq_entry_17,
        (uint64_t)irq_entry_18, (uint64_t)irq_entry_19,
        (uint64_t)irq_entry_20, (uint64_t)irq_entry_21,
        (uint64_t)irq_entry_22, (uint64_t)irq_entry_23,
        (uint64_t)irq_entry_24, (uint64_t)irq_entry_25,
        (uint64_t)irq_entry_26, (uint64_t)irq_entry_27,
        (uint64_t)irq_entry_28, (uint64_t)irq_entry_29,
        (uint64_t)irq_entry_30, (uint64_t)irq_entry_31,
    };

    for (int i = 0; i < 32; i++) {
        /* 中断门：Present, DPL=0, Type=Interrupt Gate (0xE) */
        uint8_t ist = 0;
        /* 为关键异常设置独立中断栈（IST） */
        if (i == 2) ist = 2;   /* NMI -> IST2 */
        if (i == 8) ist = 1;   /* Double Fault -> IST1 */
        if (i == 18) ist = 3;  /* Machine Check -> IST3 */
        idt64_set_gate(i, handlers[i], 0x08, 0x8E, ist);
    }

    /* 设置IDT指针 */
    idt64_ptr.limit = sizeof(idt64) - 1;
    idt64_ptr.base = (uint64_t)idt64;

    /* 加载IDT */
    __asm__ volatile("lidt %0" : : "m"(idt64_ptr));

    vga_puts("[IRQ64] IDT loaded (256 entries)\n");

    /* 检查是否为APIC模式 */
    extern int apic64_is_apic_mode(void);
    int use_apic = apic64_is_apic_mode();

    if (!use_apic) {
        /* 初始化PIC（8259） */
        vga_puts("[IRQ64] Initializing PIC (8259)...\n");
        /* 主PIC */
        __asm__ volatile(
            "movb $0x11, %al\noutb %al, $0x20\n"   /* ICW1 */
            "movb $0x20, %al\noutb %al, $0x21\n"   /* ICW2: 向量32 */
            "movb $0x04, %al\noutb %al, $0x21\n"   /* ICW3 */
            "movb $0x01, %al\noutb %al, $0x21\n"   /* ICW4 */
            "movb $0xFC, %al\noutb %al, $0x21\n"   /* OCW1: 启用IRQ0(PIT)+IRQ1(键盘) */
        );
        /* 从PIC */
        __asm__ volatile(
            "movb $0x11, %al\noutb %al, $0xA0\n"
            "movb $0x28, %al\noutb %al, $0xA1\n"
            "movb $0x02, %al\noutb %al, $0xA1\n"
            "movb $0x01, %al\noutb %al, $0xA1\n"
            "movb $0xFF, %al\noutb %al, $0xA1\n"   /* 屏蔽所有从PIC中断 */
        );
        vga_puts("[IRQ64] PIC initialized (IRQ0 PIT + IRQ1 keyboard enabled)\n");
    } else {
        vga_puts("[IRQ64] APIC mode active, PIC skipped\n");
    }

    /* 设置IRQ入口（32-47） */
    extern void irq_entry_32(void);
    extern void irq_entry_33(void);
    extern void irq_entry_34(void);
    extern void irq_entry_35(void);
    extern void irq_entry_36(void);
    extern void irq_entry_37(void);
    extern void irq_entry_38(void);
    extern void irq_entry_39(void);
    extern void irq_entry_40(void);
    extern void irq_entry_41(void);
    extern void irq_entry_42(void);
    extern void irq_entry_43(void);
    extern void irq_entry_44(void);
    extern void irq_entry_45(void);
    extern void irq_entry_46(void);
    extern void irq_entry_47(void);
    extern void irq_entry_spurious(void);

    uint64_t irq_handlers[] = {
        (uint64_t)irq_entry_32, (uint64_t)irq_entry_33,
        (uint64_t)irq_entry_34, (uint64_t)irq_entry_35,
        (uint64_t)irq_entry_36, (uint64_t)irq_entry_37,
        (uint64_t)irq_entry_38, (uint64_t)irq_entry_39,
        (uint64_t)irq_entry_40, (uint64_t)irq_entry_41,
        (uint64_t)irq_entry_42, (uint64_t)irq_entry_43,
        (uint64_t)irq_entry_44, (uint64_t)irq_entry_45,
        (uint64_t)irq_entry_46, (uint64_t)irq_entry_47,
    };
    for (int i = 0; i < 16; i++) {
        idt64_set_gate(32 + i, irq_handlers[i], 0x08, 0x8E, 0);
    }
    vga_puts("[IRQ64] IRQ entries 32-47 set\n");

    /* 注册APIC定时器中断向量0x40 */
    extern void irq_entry_64(void);
    idt64_set_gate(0x40, (uint64_t)irq_entry_64, 0x08, 0x8E, 0);
    vga_puts("[IRQ64] APIC timer vector 0x40 set\n");

    /* 注册IPI核间中断向量0x50 */
    extern void irq_entry_80(void);
    idt64_set_gate(0x50, (uint64_t)irq_entry_80, 0x08, 0x8E, 0);
    vga_puts("[IRQ64] IPI vector 0x50 set\n");

    /* 设置APIC spurious中断向量(0xFF)和错误向量(0xFE) */
    idt64_set_gate(0xFE, (uint64_t)irq_entry_47, 0x08, 0x8E, 0);
    idt64_set_gate(0xFF, (uint64_t)irq_entry_spurious, 0x08, 0x8E, 0);
    vga_puts("[IRQ64] APIC spurious/error vectors set (0xFE/0xFF, spurious has dedicated entry)\n");

    /* 清零中断计数 */
    memset(irq_count, 0, sizeof(irq_count));

    /* 注册中断处理函数 */
    extern void irq64_register_handler(int irq, void (*handler)(void));
    extern void pit64_interrupt(void);
    extern void keyboard64_interrupt(void);
    irq64_register_handler(0, pit64_interrupt);      /* IRQ0: PIT定时器 */
    irq64_register_handler(1, keyboard64_interrupt);  /* IRQ1: 键盘 */
    vga_puts("[IRQ64] PIT+keyboard handlers registered\n");

    /* 启用中断 */
    if (use_apic) {
        vga_puts("[IRQ64] Interrupts enabled (APIC mode)\n");
    } else {
        vga_puts("[IRQ64] Interrupts enabled (PIC mode)\n");
    }

    /* IDT已完全填充并加载，通知等待中的AP核可以加载IDT并启用中断。
       必须在所有idt64_set_gate完成、lidt之后设置。 */
    extern volatile int g_apic_idt_ready;
    g_apic_idt_ready = 1;
    __asm__ volatile("" ::: "memory");  /* 强制标志写入对AP核可见 */

    __asm__ volatile("sti");
}

/* 为AP核加载IDT（IDTR是per-CPU的，trampoline只加载了GDT/CR3，
   没有加载IDT）。AP核在启用定时器中断/ sti 之前必须调用，
   否则首个定时器中断会落入无效IDT导致#GP/#DF。 */
void idt64_load_for_ap(void)
{
    __asm__ volatile("lidt %0" : : "m"(idt64_ptr));
}

/* Spurious中断处理函数（不发送EOI，只计数） */
void irq_spurious_handler(uint64_t *stack_frame)
{
    irq_count[0xFF]++;
    /* spurious中断不需要发送EOI，直接返回 */
}

/* 异常处理函数（从汇编入口调用） */
void irq64_exception_handler(int vector, uint64_t error_code, uint64_t rip, uint64_t cs, uint64_t rflags)
{
    irq_count[vector]++;

    vga_puts("\n[EXCEPTION] ");
    vga_put_hex(vector);
    vga_puts(" (");
    switch (vector) {
        case 0: vga_puts("Divide Error"); break;
        case 1: vga_puts("Debug"); break;
        case 2: vga_puts("NMI"); break;
        case 3: vga_puts("Breakpoint"); break;
        case 4: vga_puts("Overflow"); break;
        case 5: vga_puts("Bound Range"); break;
        case 6: vga_puts("Invalid Opcode"); break;
        case 7: vga_puts("Device Not Available"); break;
        case 8: vga_puts("Double Fault"); break;
        case 10: vga_puts("Invalid TSS"); break;
        case 11: vga_puts("Segment Not Present"); break;
        case 12: vga_puts("Stack Fault"); break;
        case 13: vga_puts("General Protection"); break;
        case 14: vga_puts("Page Fault"); break;
        case 16: vga_puts("x87 FP Error"); break;
        case 17: vga_puts("Alignment Check"); break;
        case 18: vga_puts("Machine Check"); break;
        case 19: vga_puts("SIMD FP Error"); break;
        default: vga_puts("Unknown"); break;
    }
    vga_puts(")\n");

    vga_puts("  RIP: ");
    vga_put_hex(rip);
    vga_puts("\n  CS: ");
    vga_put_hex(cs);
    vga_puts("\n  RFLAGS: ");
    vga_put_hex(rflags);
    vga_puts("\n  Error Code: ");
    vga_put_hex(error_code);
    vga_puts("\n");

    /* 致命异常停机 */
    if (vector == 8 || vector == 13 || vector == 14) {
        vga_puts("\n[FATAL] System halted\n");
        while (1) { __asm__ volatile("hlt"); }
    }
}

/* 通用中断处理 */
void irq64_handler(int vector)
{
    irq_count[vector]++;
    /* 后续添加具体中断处理 */
}

/* 获取中断统计 */
void irq64_get_stats(uint64_t *counts, int max_entries)
{
    int n = max_entries < IDT_ENTRIES ? max_entries : IDT_ENTRIES;
    for (int i = 0; i < n; i++) {
        counts[i] = irq_count[i];
    }
}

/* 启用中断 */
void irq64_enable(void)
{
    __asm__ volatile("sti");
    vga_puts("[IRQ64] Interrupts enabled\n");
}

/* 禁用中断 */
void irq64_disable(void)
{
    __asm__ volatile("cli");
    vga_puts("[IRQ64] Interrupts disabled\n");
}
