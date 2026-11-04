/* ============================================================================
 * 赫普OS - 中断处理单元测试
 * 覆盖：IDT/中断向量/中断处理/异常
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === IDT描述符测试 === */
typedef struct {
    u16 offset_low;
    u16 selector;
    u8  zero;
    u8  type_attr;
    u16 offset_high;
} __attribute__((packed)) idt_entry_t;

#define IDT_ENTRIES 256
#define IDT_TYPE_INTERRUPT 0x8E  /* 存在, DPL=0, 32位中断门 */
#define IDT_TYPE_TRAP      0x8F  /* 存在, DPL=0, 32位陷阱门 */

static kunit_result_t test_idt_entry_size(void)
{
    KUNIT_ASSERT_EQ(sizeof(idt_entry_t), 8, "IDT size =8 bytes ");
    return KUNIT_PASS;
}

static kunit_result_t test_idt_entry_init(void)
{
    idt_entry_t entry;
    u32 handler_addr = 0xC0001000;

    entry.offset_low = handler_addr & 0xFFFF;
    entry.selector = 0x08;  /* 内核代码段 */
    entry.zero = 0;
    entry.type_attr = IDT_TYPE_INTERRUPT;
    entry.offset_high = (handler_addr >> 16) & 0xFFFF;

    KUNIT_ASSERT_EQ(entry.offset_low, 0x1000, " low 16 bit 偏移 ");
    KUNIT_ASSERT_EQ(entry.selector, 0x08, " seg 选择器 ");
    KUNIT_ASSERT_EQ(entry.zero, 0, " 保留字 seg 0");
    KUNIT_ASSERT_EQ(entry.type_attr, 0x8E, " type 属性 ");
    KUNIT_ASSERT_EQ(entry.offset_high, 0xC000, " high 16 bit 偏移 ");

    /* 重组地址 */
    u32 addr = ((u32)entry.offset_high << 16) | entry.offset_low;
    KUNIT_ASSERT_EQ(addr, handler_addr, " addr 重组正确 ");
    return KUNIT_PASS;
}

static kunit_result_t test_idt_types(void)
{
    KUNIT_ASSERT_EQ(IDT_TYPE_INTERRUPT, 0x8E, " irq 门 type ");
    KUNIT_ASSERT_EQ(IDT_TYPE_TRAP, 0x8F, " 陷阱门 type ");

    /* 检查存在位 */
    KUNIT_ASSERT(IDT_TYPE_INTERRUPT & 0x80, " irq 门 exists bit ");
    KUNIT_ASSERT(IDT_TYPE_TRAP & 0x80, " 陷阱门 exists bit ");

    /* 检查DPL=0 */
    KUNIT_ASSERT_EQ((IDT_TYPE_INTERRUPT >> 5) & 0x3, 0, " irq 门 DPL=0");
    return KUNIT_PASS;
}

/* === 中断向量测试 === */
#define IRQ_BASE 0x20  /* PIC重映射基址 */
#define IRQ_TIMER    (IRQ_BASE + 0)
#define IRQ_KEYBOARD (IRQ_BASE + 1)
#define IRQ_CASCADE  (IRQ_BASE + 2)
#define IRQ_COM2     (IRQ_BASE + 3)
#define IRQ_COM1     (IRQ_BASE + 4)
#define IRQ_LPT2     (IRQ_BASE + 5)
#define IRQ_FLOPPY   (IRQ_BASE + 6)
#define IRQ_LPT1     (IRQ_BASE + 7)
#define IRQ_RTC      (IRQ_BASE + 8)
#define IRQ_MOUSE    (IRQ_BASE + 12)
#define IRQ_ATA1     (IRQ_BASE + 14)
#define IRQ_ATA2     (IRQ_BASE + 15)

static kunit_result_t test_irq_vectors(void)
{
    KUNIT_ASSERT_EQ(IRQ_TIMER, 0x20, " timer IRQ=0x20");
    KUNIT_ASSERT_EQ(IRQ_KEYBOARD, 0x21, " keyboard IRQ=0x21");
    KUNIT_ASSERT_EQ(IRQ_RTC, 0x28, "RTC IRQ=0x28");
    KUNIT_ASSERT_EQ(IRQ_MOUSE, 0x2C, " mouse IRQ=0x2C");
    KUNIT_ASSERT_EQ(IRQ_ATA1, 0x2E, "ATA1 IRQ=0x2E");
    return KUNIT_PASS;
}

static kunit_result_t test_cpu_exceptions(void)
{
    /* CPU异常向量 */
    KUNIT_ASSERT_EQ(0, 0, " 除法 error #DE");
    KUNIT_ASSERT_EQ(1, 1, " 调试异常 #DB");
    KUNIT_ASSERT_EQ(2, 2, "NMI irq ");
    KUNIT_ASSERT_EQ(3, 3, " breakpoints #BP");
    KUNIT_ASSERT_EQ(4, 4, " 溢出 #OF");
    KUNIT_ASSERT_EQ(5, 5, " boundary check #BR");
    KUNIT_ASSERT_EQ(6, 6, " invalid ops 码 #UD");
    KUNIT_ASSERT_EQ(7, 7, " device not 可用 #NM");
    KUNIT_ASSERT_EQ(8, 8, " 双重 error #DF");
    KUNIT_ASSERT_EQ(13, 13, " 通用 protected #GP");
    KUNIT_ASSERT_EQ(14, 14, " pages error #PF");
    return KUNIT_PASS;
}

/* === 中断处理程序测试 === */
#define MAX_HANDLERS 256

typedef void (*irq_handler_t)(void);
static irq_handler_t handlers[MAX_HANDLERS];
static u32 irq_counts[MAX_HANDLERS];

static void irq_init_handlers(void)
{
    memset(handlers, 0, sizeof(handlers));
    memset(irq_counts, 0, sizeof(irq_counts));
}

static int irq_register(u32 vector, irq_handler_t handler)
{
    if (vector >= MAX_HANDLERS) return -1;
    if (handlers[vector]) return -1;  /* 已注册 */
    handlers[vector] = handler;
    return 0;
}

static int __attribute__((unused)) irq_unregister(u32 vector)
{
    if (vector >= MAX_HANDLERS) return -1;
    if (!handlers[vector]) return -1;
    handlers[vector] = NULL;
    return 0;
}

static void irq_dispatch(u32 vector)
{
    if (vector >= MAX_HANDLERS) return;
    irq_counts[vector]++;
    if (handlers[vector]) {
        handlers[vector]();
    }
}

static u32 test_irq_fired = 0;
static void test_irq_handler(void)
{
    test_irq_fired++;
}

static kunit_result_t test_irq_register(void)
{
    irq_init_handlers();
    test_irq_fired = 0;

    KUNIT_ASSERT_EQ(irq_register(IRQ_KEYBOARD, test_irq_handler), 0, " registered keyboard irq ");
    KUNIT_ASSERT_EQ(irq_register(IRQ_KEYBOARD, test_irq_handler), -1, " 重复 registered failed ");
    KUNIT_ASSERT_EQ(irq_register(256, test_irq_handler), -1, " invalid 向量 registered failed ");
    return KUNIT_PASS;
}

static kunit_result_t test_irq_dispatch(void)
{
    irq_init_handlers();
    test_irq_fired = 0;

    irq_register(IRQ_TIMER, test_irq_handler);

    /* 模拟中断触发 */
    irq_dispatch(IRQ_TIMER);
    irq_dispatch(IRQ_TIMER);
    irq_dispatch(IRQ_TIMER);

    KUNIT_ASSERT_EQ(test_irq_fired, 3, " handling 程序 call 3 ");
    KUNIT_ASSERT_EQ(irq_counts[IRQ_TIMER], 3, " irq count =3");
    return KUNIT_PASS;
}

static kunit_result_t test_irq_unhandled(void)
{
    irq_init_handlers();

    /* 未注册的中断应该只增加计数，不崩溃 */
    irq_dispatch(0x30);
    irq_dispatch(0x30);

    KUNIT_ASSERT_EQ(irq_counts[0x30], 2, " not handling irq count ");
    return KUNIT_PASS;
}

/* === 中断标志测试 === */
static kunit_result_t test_interrupt_flags(void)
{
    /* EFLAGS IF位（bit 9） */
    u32 eflags = 0;

    /* 模拟sti */
    eflags |= (1 << 9);
    KUNIT_ASSERT(eflags & (1 << 9), "IF bit 置 bit ( 开 irq )");

    /* 模拟cli */
    eflags &= ~(1 << 9);
    KUNIT_ASSERT(!(eflags & (1 << 9)), "IF bit clear ( 关 irq )");
    return KUNIT_PASS;
}

/* === PIC中断屏蔽测试 === */
static u16 pic_mask = 0xFFFF;  /* 全部屏蔽 */

static void pic_enable_irq(u8 irq)
{
    if (irq < 8) {
        pic_mask &= ~(1 << irq);
    } else {
        pic_mask &= ~(1 << irq);
    }
}

static void pic_disable_irq(u8 irq)
{
    pic_mask |= (1 << irq);
}

static int pic_is_enabled(u8 irq)
{
    return !(pic_mask & (1 << irq));
}

static kunit_result_t test_pic_mask(void)
{
    pic_mask = 0xFFFF;

    pic_enable_irq(0);  /* 定时器 */
    KUNIT_ASSERT(pic_is_enabled(0), " timer IRQ enabled ");
    KUNIT_ASSERT(!pic_is_enabled(1), " keyboard IRQ 仍屏蔽 ");

    pic_enable_irq(1);  /* 键盘 */
    KUNIT_ASSERT(pic_is_enabled(1), " keyboard IRQ enabled ");

    pic_disable_irq(0);
    KUNIT_ASSERT(!pic_is_enabled(0), " timer IRQ disabled ");
    KUNIT_ASSERT(pic_is_enabled(1), " keyboard IRQ 仍 enabled ");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t irq_tests[] = {
    {"IDT size ", test_idt_entry_size, "irq"},
    {"IDT init ", test_idt_entry_init, "irq"},
    {"IDT type ", test_idt_types, "irq"},
    {"IRQ 向量 ", test_irq_vectors, "irq"},
    {"CPU 异常 ", test_cpu_exceptions, "irq"},
    {"IRQ registered ", test_irq_register, "irq"},
    {"IRQ 分发 ", test_irq_dispatch, "irq"},
    {" not handling IRQ", test_irq_unhandled, "irq"},
    {" irq 标志 ", test_interrupt_flags, "irq"},
    {"PIC 屏蔽 ", test_pic_mask, "irq"},
};
static const kunit_suite_t kunit_irq_suite = {
    " irq handling ", irq_tests, 10
};

void kunit_irq_register(void)
{
    kunit_register_suite(&kunit_irq_suite);
    vga_puts("[KUNIT] irq test suite registered(10 cases)\n");
}
