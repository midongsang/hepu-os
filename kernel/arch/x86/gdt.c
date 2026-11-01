/* ============================================================================
 * 赫普OS - 全局描述符表 (GDT)
 * ========================================================================== */

#include <stdint.h>
#include "gdt.h"
#include "constants.h"

#define GDT_ENTRIES 6

/* GDT 描述符 */
typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) gdt_entry_t;

/* GDT 指针 */
typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

gdt_entry_t gdt_entries[GDT_ENTRIES];
static gdt_ptr_t   gdt_ptr;

/* 汇编函数：加载 GDT */
extern void gdt_flush(uint32_t gdt_ptr);

static void gdt_set_gate(int num, uint32_t base, uint32_t limit,
                         uint8_t access, uint8_t gran)
{
    gdt_entries[num].base_low    = (base & GDT_BASE_LOW_MASK);
    gdt_entries[num].base_middle = (base >> 16) & GDT_BASE_MID_MASK;
    gdt_entries[num].base_high   = (base >> 24) & GDT_BASE_HIGH_MASK;
    gdt_entries[num].limit_low   = (limit & GDT_LIMIT_LOW_MASK);
    gdt_entries[num].granularity = (limit >> 16) & GDT_LIMIT_HIGH_MASK;
    gdt_entries[num].granularity |= gran & GDT_GRANULARITY_MASK;
    gdt_entries[num].access      = access;
}

void gdt_init(void)
{
    gdt_ptr.limit = sizeof(gdt_entry_t) * GDT_ENTRIES - 1;
    gdt_ptr.base  = (uint32_t)&gdt_entries;

    /* 第0项：空描述符 */
    gdt_set_gate(0, 0, 0, 0, 0);

    /* 第1项：内核代码段
     * 0x9A = 存在、内核级、代码段、可执行、可读
     * 0xCF = 4KB粒度、32位保护模式 */
    gdt_set_gate(1, 0, GDT_MAX_LIMIT, GDT_ACCESS_KERNEL_CODE, GDT_GRANULARITY_32BIT);

    /* 第2项：内核数据段
     * 0x92 = 存在、内核级、数据段、可写 */
    gdt_set_gate(2, 0, GDT_MAX_LIMIT, GDT_ACCESS_KERNEL_DATA, GDT_GRANULARITY_32BIT);

    /* 第3项：用户代码段
     * 0xFA = 存在、用户级(3)、代码段 */
    gdt_set_gate(3, 0, GDT_MAX_LIMIT, GDT_ACCESS_USER_CODE, GDT_GRANULARITY_32BIT);

    /* 第4项：用户数据段
     * 0xF2 = 存在、用户级(3)、数据段 */
    gdt_set_gate(4, 0, GDT_MAX_LIMIT, GDT_ACCESS_USER_DATA, GDT_GRANULARITY_32BIT);

    gdt_flush((uint32_t)&gdt_ptr);
}
