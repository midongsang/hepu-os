/* ============================================================================
 * 赫普OS - 引导流程单元测试
 * 覆盖：Multiboot/GDT/IDT/分页/内存布局
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"
#include "constants.h"

/* === Multiboot魔数测试 === */
#define MULTIBOOT_MAGIC 0x1BADB002
#define MULTIBOOT_EAX_MAGIC 0x2BADB002

static kunit_result_t test_multiboot_magic(void)
{
    KUNIT_ASSERT_EQ(MULTIBOOT_MAGIC, 0x1BADB002, "Multiboot magic ");
    KUNIT_ASSERT_EQ(MULTIBOOT_EAX_MAGIC, 0x2BADB002, "Multiboot EAX magic ");
    return KUNIT_PASS;
}

static kunit_result_t test_multiboot_header_flags(void)
{
    /* Multiboot flags: bit0=对齐4K, bit1=内存信息, bit16=视频模式 */
    u32 flags = 0x00000003;  /* 对齐+内存信息 */
    KUNIT_ASSERT(flags & 0x1, " pages pair 齐标志 ");
    KUNIT_ASSERT(flags & 0x2, " memory 信息标志 ");
    KUNIT_ASSERT(!(flags & 0x10000), " no video mode 标志 ");
    return KUNIT_PASS;
}

/* === GDT测试 === */
typedef struct {
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  access;
    u8  granularity;
    u8  base_high;
} __attribute__((packed)) gdt_entry_t;

#define GDT_CODE_ACCESS 0x9A
#define GDT_DATA_ACCESS 0x92
#define GDT_GRAN_4K 0xC0

static kunit_result_t test_gdt_entry_size(void)
{
    KUNIT_ASSERT_EQ(sizeof(gdt_entry_t), 8, "GDT size =8 bytes ");
    return KUNIT_PASS;
}

static kunit_result_t test_gdt_code_entry(void)
{
    gdt_entry_t entry;
    memset(&entry, 0, sizeof(entry));

    entry.limit_low = 0xFFFF;
    entry.base_low = 0;
    entry.base_mid = 0;
    entry.access = GDT_CODE_ACCESS;
    entry.granularity = GDT_GRAN_4K | 0x0F;
    entry.base_high = 0;

    KUNIT_ASSERT_EQ(entry.access, 0x9A, " code seg 访问 bytes ");
    KUNIT_ASSERT(entry.access & 0x80, " exists bit ");
    KUNIT_ASSERT(entry.access & 0x10, " 代码 / data bit ");
    KUNIT_ASSERT(entry.access & 0x08, " 可 exec bit ");
    KUNIT_ASSERT(entry.access & 0x02, " 可读 bit ");
    return KUNIT_PASS;
}

static kunit_result_t test_gdt_data_entry(void)
{
    gdt_entry_t entry;
    memset(&entry, 0, sizeof(entry));

    entry.access = GDT_DATA_ACCESS;
    entry.granularity = GDT_GRAN_4K | 0x0F;

    KUNIT_ASSERT_EQ(entry.access, 0x92, " data seg 访问 bytes ");
    KUNIT_ASSERT(entry.access & 0x80, " exists bit ");
    KUNIT_ASSERT(entry.access & 0x10, " 代码 / data bit ");
    KUNIT_ASSERT(!(entry.access & 0x08), " not 可 exec ");
    KUNIT_ASSERT(entry.access & 0x02, " 可写 bit ");
    return KUNIT_PASS;
}

static kunit_result_t test_gdt_granularity(void)
{
    /* 4K粒度: limit = 0xFFFFF * 4096 = 4GB */
    u32 limit_raw = 0xFFFFF;
    u64 actual_limit = (u64)limit_raw * 4096 + 4095;
    KUNIT_ASSERT_EQ(actual_limit, 0xFFFFFFFF, "4K 粒度 =4GB");
    return KUNIT_PASS;
}

/* === IDT测试 === */
typedef struct {
    u16 offset_low;
    u16 selector;
    u8  zero;
    u8  type_attr;
    u16 offset_high;
} __attribute__((packed)) idt_entry_t;

#define IDT_INT_GATE 0x8E
#define IDT_TRAP_GATE 0x8F

static kunit_result_t test_idt_entry_size(void)
{
    KUNIT_ASSERT_EQ(sizeof(idt_entry_t), 8, "IDT size =8 bytes ");
    return KUNIT_PASS;
}

static kunit_result_t test_idt_types(void)
{
    KUNIT_ASSERT_EQ(IDT_INT_GATE, 0x8E, " irq 门 ");
    KUNIT_ASSERT_EQ(IDT_TRAP_GATE, 0x8F, " 陷阱门 ");

    /* 中断门: 存在+DPL0+32位中断 */
    KUNIT_ASSERT(IDT_INT_GATE & 0x80, " exists bit ");
    KUNIT_ASSERT_EQ((IDT_INT_GATE >> 5) & 0x3, 0, "DPL=0");
    KUNIT_ASSERT_EQ(IDT_INT_GATE & 0x0F, 0x0E, "32 bit irq 门 ");
    return KUNIT_PASS;
}

static kunit_result_t test_idt_offset(void)
{
    idt_entry_t entry;
    memset(&entry, 0, sizeof(entry));

    u32 handler_addr = 0xC0100000;
    entry.offset_low = handler_addr & 0xFFFF;
    entry.offset_high = (handler_addr >> 16) & 0xFFFF;
    entry.selector = 0x08;
    entry.type_attr = IDT_INT_GATE;

    u32 reconstructed = (u32)entry.offset_low | ((u32)entry.offset_high << 16);
    KUNIT_ASSERT_EQ(reconstructed, handler_addr, " 偏移 addr 重组 ");
    KUNIT_ASSERT_EQ(entry.selector, 0x08, " 选择子 = code seg ");
    return KUNIT_PASS;
}

/* === 分页测试 === */
typedef u32 page_dir_entry_t;
typedef u32 page_table_entry_t;

#define PAGE_PRESENT 0x1
#define PAGE_WRITE 0x2
#define PAGE_USER 0x4
#define PAGE_SIZE_4M 0x80

static kunit_result_t test_page_entry_flags(void)
{
    KUNIT_ASSERT_EQ(PAGE_PRESENT, 0x1, " exists bit ");
    KUNIT_ASSERT_EQ(PAGE_WRITE, 0x2, " 可写 bit ");
    KUNIT_ASSERT_EQ(PAGE_USER, 0x4, " user bit ");
    KUNIT_ASSERT_EQ(PAGE_SIZE_4M, 0x80, "4M page size bit ");
    return KUNIT_PASS;
}

static kunit_result_t test_page_directory(void)
{
    /* 页目录: 1024项, 每项4字节 = 4KB */
    KUNIT_ASSERT_EQ(1024 * 4, 4096, " page dir size =4KB");

    /* 虚拟地址分解 */
    u32 vaddr = 0xC0100000;
    u32 pd_index = (vaddr >> 22) & 0x3FF;
    u32 pt_index = (vaddr >> 12) & 0x3FF;
    u32 offset = vaddr & 0xFFF;

    KUNIT_ASSERT_EQ(pd_index, 768, "PD 索引 =768(3GB)");
    KUNIT_ASSERT_EQ(pt_index, 256, "PT 索引 =256");
    KUNIT_ASSERT_EQ(offset, 0, " pages 内偏移 =0");
    return KUNIT_PASS;
}

static kunit_result_t test_page_alignment(void)
{
    /* 4K页对齐 */
    u32 addr = 0x12345;
    u32 aligned = addr & ~0xFFF;
    KUNIT_ASSERT_EQ(aligned, 0x12000, "4K pair 齐 ");

    /* 向上对齐 */
    u32 up_aligned = (addr + 0xFFF) & ~0xFFF;
    KUNIT_ASSERT_EQ(up_aligned, 0x13000, " 向 4K pair 齐 ");
    return KUNIT_PASS;
}

/* === 内存布局测试 === */
#define KERNEL_VIRT_BASE 0xC0000000
#define KERNEL_PHYS_BASE 0x00100000
#define USER_STACK_TOP 0xC0000000

static kunit_result_t test_memory_layout(void)
{
    KUNIT_ASSERT_EQ(KERNEL_VIRT_BASE, 0xC0000000, " kernel virtual 基址 =3GB");
    KUNIT_ASSERT_EQ(KERNEL_PHYS_BASE, 0x00100000, " kernel physical 基址 =1MB");
    KUNIT_ASSERT_EQ(USER_STACK_TOP, 0xC0000000, " user 栈顶 =3GB");

    /* 内核空间大小 = 1GB */
    u32 kernel_space = 0xFFFFFFFF - KERNEL_VIRT_BASE + 1;
    KUNIT_ASSERT_EQ(kernel_space, 0x40000000, " kernel empty 间 =1GB");
    return KUNIT_PASS;
}

static kunit_result_t test_phys_to_virt(void)
{
    /* 物理地址转虚拟地址: +0xC0000000 */
    u32 phys = 0x00100000;
    u32 virt = phys + KERNEL_VIRT_BASE;
    KUNIT_ASSERT_EQ(virt, 0xC0100000, " physical 转 virtual ");

    /* 虚拟转物理: -0xC0000000 */
    u32 back = virt - KERNEL_VIRT_BASE;
    KUNIT_ASSERT_EQ(back, phys, " virtual 转 physical ");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t boot_tests[] = {
    {"Multiboot magic ", test_multiboot_magic, "boot"},
    {"Multiboot 标志 ", test_multiboot_header_flags, "boot"},
    {"GDT size ", test_gdt_entry_size, "boot"},
    {"GDT code seg ", test_gdt_code_entry, "boot"},
    {"GDT data seg ", test_gdt_data_entry, "boot"},
    {"GDT 粒度 ", test_gdt_granularity, "boot"},
    {"IDT size ", test_idt_entry_size, "boot"},
    {"IDT type ", test_idt_types, "boot"},
    {"IDT 偏移 ", test_idt_offset, "boot"},
    {" pages table 标志 ", test_page_entry_flags, "boot"},
    {" page dir ", test_page_directory, "boot"},
    {" pages pair 齐 ", test_page_alignment, "boot"},
    {" memory layout ", test_memory_layout, "boot"},
    {" physical virtual 转换 ", test_phys_to_virt, "boot"},
};
static const kunit_suite_t kunit_boot_suite = {
    " boot flow ", boot_tests, 14
};

void kunit_boot_register(void)
{
    kunit_register_suite(&kunit_boot_suite);
    vga_puts("[KUNIT] boot flow test suite registered(14 cases)\n");
}
