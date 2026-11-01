/* ============================================================================
 * 赫普OS - TSS (Task State Segment) 任务状态段
 * 功能: 用户态中断/异常时，CPU自动从TSS加载内核栈(SS0:ESP0)
 * ========================================================================== */

#include <stdint.h>
#include "tss.h"
#include "gdt.h"
#include "vga.h"

static tss_t kernel_tss;

/* 汇编函数: 加载任务寄存器 TR */
extern void tss_flush(void);

/* GDT 条目结构 (与 gdt.c 中一致) */
typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) gdt_entry_t;

/* 外部GDT数组 (定义在 gdt.c) */
extern gdt_entry_t gdt_entries[];

void tss_init(void)
{
    uint32_t base = (uint32_t)&kernel_tss;
    uint32_t limit = sizeof(tss_t) - 1;

    /* 清空TSS */
    for (uint32_t i = 0; i < sizeof(tss_t); i++) {
        ((uint8_t *)&kernel_tss)[i] = 0;
    }

    /* 设置内核栈 (初始值，进程切换时更新) */
    kernel_tss.ss0 = 0x10;       /* 内核数据段选择子 */
    kernel_tss.esp0 = 0;         /* 内核栈指针 (进程切换时设置) */

    /* 设置CS和DS为内核态 */
    kernel_tss.cs = 0x08;        /* 内核代码段 */
    kernel_tss.ds = 0x10;        /* 内核数据段 */
    kernel_tss.es = 0x10;
    kernel_tss.fs = 0x10;
    kernel_tss.gs = 0x10;
    kernel_tss.ss = 0x10;

    /* I/O映射表基址 (设为TSS大小，表示没有I/O映射) */
    kernel_tss.iomap_base = sizeof(tss_t);

    /* 在GDT中设置TSS描述符 (第5个索引，即第6个条目)
     * GDT布局: 0=空, 1=内核代码, 2=内核数据, 3=用户代码, 4=用户数据, 5=TSS
     * TSS描述符: access=0x89 (存在、系统段、32位TSS、可用)
     * granularity=0x40 (32位) */
    gdt_entries[5].limit_low   = limit & 0xFFFF;
    gdt_entries[5].base_low    = base & 0xFFFF;
    gdt_entries[5].base_middle = (base >> 16) & 0xFF;
    gdt_entries[5].access      = 0x89;  /* P=1, DPL=0, S=0, Type=9(32-bit TSS available) */
    gdt_entries[5].granularity = 0x40;  /* D/B=1 (32位) */
    gdt_entries[5].base_high   = (base >> 24) & 0xFF;

    /* 重新加载GDT */
    extern void gdt_flush(uint32_t);
    extern uint32_t gdt_ptr_value; (void)gdt_ptr_value;  /* 我们需要重新加载GDT */

    /* 注意: GDT已经在gdt_init中加载过，添加TSS后需要重新加载
     * 但gdt_ptr是局部变量，这里用内联汇编重新加载 */
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) gdt_ptr_local;

    gdt_ptr_local.limit = sizeof(gdt_entry_t) * 6 - 1;
    gdt_ptr_local.base = (uint32_t)gdt_entries;
    __asm__ volatile ("lgdt %0" : : "m"(gdt_ptr_local));

    /* 加载任务寄存器 TR (选择子=5*8=0x28) */
    tss_flush();

    vga_puts("[TSS] TSS init done(TR=0x28)\n");
}

void tss_set_kernel_stack(uint32_t esp0)
{
    kernel_tss.esp0 = esp0;
}

tss_t *tss_get(void)
{
    return &kernel_tss;
}

uint32_t tss_get_esp0(void)
{
    return kernel_tss.esp0;
}
