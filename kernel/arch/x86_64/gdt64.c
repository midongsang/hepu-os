/* ============================================================================
 * 赫普OS - 64位GDT/TSS初始化
 * V0.10.6: 初始实现（64位全局描述符表和任务状态段）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* GDT条目 */
typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) gdt_entry_t;

/* 64位GDT条目（含基址高32位） */
typedef struct {
    gdt_entry_t entry;
    uint32_t base_high32;
    uint32_t reserved;
} __attribute__((packed)) gdt_entry64_t;

/* GDT指针 */
typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdt_ptr_t;

/* TSS（64位任务状态段） */
typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;      /* 特权级0栈指针 */
    uint64_t rsp1;      /* 特权级1栈指针 */
    uint64_t rsp2;      /* 特权级2栈指针 */
    uint64_t reserved1;
    uint64_t ist1;      /* 中断栈表1 */
    uint64_t ist2;      /* 中断栈表2 */
    uint64_t ist3;      /* 中断栈表3 */
    uint64_t ist4;      /* 中断栈表4 */
    uint64_t ist5;      /* 中断栈表5 */
    uint64_t ist6;      /* 中断栈表6 */
    uint64_t ist7;      /* 中断栈表7 */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
} __attribute__((packed)) tss64_t;

/* GDT条目数量 */
#define GDT_ENTRIES 7

/* GDT表 */
static gdt_entry64_t gdt64[GDT_ENTRIES] __attribute__((aligned(16)));
static gdt_ptr_t gdt64_ptr;

/* TSS */
static tss64_t tss64 __attribute__((aligned(16)));

/* 内核栈 */
extern uint8_t kernel_stack[];
extern uint8_t kernel_stack_top[];

/* VGA输出 */
extern void vga_puts(const char *str);
extern void vga_put_hex(uint64_t val);
extern void *memset(void *dst, int val, size_t n);

/* 设置普通GDT条目 */
static void gdt_set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt64[index].entry.limit_low = limit & 0xFFFF;
    gdt64[index].entry.base_low = base & 0xFFFF;
    gdt64[index].entry.base_mid = (base >> 16) & 0xFF;
    gdt64[index].entry.access = access;
    gdt64[index].entry.granularity = (limit >> 16) & 0x0F | gran;
    gdt64[index].entry.base_high = (base >> 24) & 0xFF;
    gdt64[index].base_high32 = 0;
    gdt64[index].reserved = 0;
}

/* 设置TSS条目（64位系统段） */
static void gdt_set_tss(int index, uint64_t base, uint32_t limit)
{
    gdt64[index].entry.limit_low = limit & 0xFFFF;
    gdt64[index].entry.base_low = base & 0xFFFF;
    gdt64[index].entry.base_mid = (base >> 16) & 0xFF;
    gdt64[index].entry.access = 0x89;  /* Present, DPL=0, Type=9 (64位TSS可用) */
    gdt64[index].entry.granularity = (limit >> 16) & 0x0F;
    gdt64[index].entry.base_high = (base >> 24) & 0xFF;
    gdt64[index].base_high32 = (base >> 32) & 0xFFFFFFFF;
    gdt64[index].reserved = 0;
}

/* 外部汇编函数 */
extern void gdt64_load_test(gdt_ptr_t *ptr);
extern void gdt64_load_asm(gdt_ptr_t *ptr);

/* 初始化GDT */
void gdt64_init(void)
{
    vga_puts("[GDT64] Initializing 64-bit GDT...\n");

    /* 清零GDT */
    for (int i = 0; i < GDT_ENTRIES; i++) {
        gdt64[i].entry.limit_low = 0;
        gdt64[i].entry.base_low = 0;
        gdt64[i].entry.base_mid = 0;
        gdt64[i].entry.access = 0;
        gdt64[i].entry.granularity = 0;
        gdt64[i].entry.base_high = 0;
        gdt64[i].base_high32 = 0;
        gdt64[i].reserved = 0;
    }

    /* 用验证过的描述符值（来自multiboot_entry.S） */
    /* 条目1: 内核代码段 (0x08) - 0x00209A0000000000 */
    gdt64[1].entry.limit_low = 0x0000;
    gdt64[1].entry.base_low = 0x0000;
    gdt64[1].entry.base_mid = 0x00;
    gdt64[1].entry.access = 0x9A;
    gdt64[1].entry.granularity = 0x20;  /* L=1, G=0, D=0 */
    gdt64[1].entry.base_high = 0x00;
    gdt64[1].base_high32 = 0x00000000;
    gdt64[1].reserved = 0;

    /* 条目2: 内核数据段 (0x10) - 0x0000920000000000 */
    gdt64[2].entry.limit_low = 0x0000;
    gdt64[2].entry.base_low = 0x0000;
    gdt64[2].entry.base_mid = 0x00;
    gdt64[2].entry.access = 0x92;
    gdt64[2].entry.granularity = 0x00;  /* L=0, G=0, D=0 */
    gdt64[2].entry.base_high = 0x00;
    gdt64[2].base_high32 = 0x00000000;
    gdt64[2].reserved = 0;

    /* 条目3: 用户代码段 (0x18) */
    gdt64[3].entry.limit_low = 0x0000;
    gdt64[3].entry.base_low = 0x0000;
    gdt64[3].entry.base_mid = 0x00;
    gdt64[3].entry.access = 0xFA;
    gdt64[3].entry.granularity = 0x20;
    gdt64[3].entry.base_high = 0x00;
    gdt64[3].base_high32 = 0x00000000;
    gdt64[3].reserved = 0;

    /* 条目4: 用户数据段 (0x20) */
    gdt64[4].entry.limit_low = 0x0000;
    gdt64[4].entry.base_low = 0x0000;
    gdt64[4].entry.base_mid = 0x00;
    gdt64[4].entry.access = 0xF2;
    gdt64[4].entry.granularity = 0x00;
    gdt64[4].entry.base_high = 0x00;
    gdt64[4].base_high32 = 0x00000000;
    gdt64[4].reserved = 0;

    /* 条目5-6: TSS (0x28) */
    memset(&tss64, 0, sizeof(tss64));
    tss64.rsp0 = (uint64_t)kernel_stack_top;
    tss64.io_map_base = sizeof(tss64);
    gdt_set_tss(5, (uint64_t)&tss64, sizeof(tss64) - 1);

    /* 设置GDT指针 */
    gdt64_ptr.limit = sizeof(gdt64) - 1;
    gdt64_ptr.base = (uint64_t)&gdt64;

    vga_puts("[GDT64] GDT entries configured, loading...\n");
    vga_puts("[GDT64] GDT ptr at: ");
    vga_put_hex((uint64_t)&gdt64_ptr);
    vga_puts(" base=");
    vga_put_hex(gdt64_ptr.base);
    vga_puts(" limit=");
    vga_put_hex(gdt64_ptr.limit);
    vga_puts("\n");

    /* 输出GDT条目0和1的原始值 */
    uint64_t *gdt_raw = (uint64_t *)&gdt64[0];
    vga_puts("[GDT64] Entry0: ");
    vga_put_hex(gdt_raw[0]);
    vga_puts(" Entry1: ");
    vga_put_hex(gdt_raw[2]);
    vga_puts("\n");

    vga_puts("[GDT64] Testing lgdt only...\n");
    gdt64_load_test(&gdt64_ptr);
    vga_puts("[GDT64] lgdt OK, now full load...\n");

    /* 加载GDT（汇编函数） */
    gdt64_load_asm(&gdt64_ptr);

    vga_puts("[GDT64] GDT loaded and CS refreshed\n");

    /* 加载TSS */
    __asm__ volatile("ltr %%ax" : : "a"(0x28));
    vga_puts("[GDT64] TSS loaded (0x28)\n");

    vga_puts("[OK] GDT initialized\n");
}

/* 设置内核栈（更新TSS的RSP0） */
void gdt64_set_kernel_stack(uint64_t stack)
{
    tss64.rsp0 = stack;
}

/* 获取TSS地址 */
uint64_t gdt64_get_tss_addr(void)
{
    return (uint64_t)&tss64;
}

/* 外部符号：汇编中定义的GDT和TSS */
extern uint64_t kernel_gdt64[];
extern uint8_t kernel_tss64[];
extern uint8_t irq_stack_df_top[];
extern uint8_t irq_stack_nmi_top[];
extern uint8_t irq_stack_mc_top[];

/* 初始化并加载TSS（在汇编入口加载GDT之后调用） */
void tss64_init(void)
{
    vga_puts("[TSS64] Initializing TSS...\n");

    /* 先切换到kernel_gdt64（汇编入口用的是gdt_64，TSS条目为空） */
    extern uint16_t kernel_gdt64_ptr[];
    __asm__ volatile("lgdt %0" : : "m"(kernel_gdt64_ptr));
    vga_puts("[TSS64] Switched to kernel_gdt64\n");

    /* 清零TSS */
    for (int i = 0; i < 104; i++) {
        kernel_tss64[i] = 0;
    }

    /* 设置RSP0（内核栈） */
    uint64_t *tss_rsp0 = (uint64_t *)&kernel_tss64[4];
    *tss_rsp0 = (uint64_t)&kernel_stack_top;

    /* 设置IST1-IST3（中断栈表） */
    uint64_t *tss_ist1 = (uint64_t *)&kernel_tss64[36];
    uint64_t *tss_ist2 = (uint64_t *)&kernel_tss64[44];
    uint64_t *tss_ist3 = (uint64_t *)&kernel_tss64[52];
    *tss_ist1 = (uint64_t)&irq_stack_df_top;    /* IST1: 双重故障 */
    *tss_ist2 = (uint64_t)&irq_stack_nmi_top;   /* IST2: NMI */
    *tss_ist3 = (uint64_t)&irq_stack_mc_top;    /* IST3: 机器检查 */

    /* 设置IO Map基地址（TSS大小） */
    uint16_t *io_map = (uint16_t *)&kernel_tss64[102];
    *io_map = 104;

    /* 设置GDT中的TSS描述符（条目5，选择器0x28） */
    uint64_t tss_base = (uint64_t)&kernel_tss64[0];
    uint32_t tss_limit = 103;  /* TSS大小-1 */

    /* TSS描述符低8字节 */
    kernel_gdt64[5] = (tss_base & 0xFFFFFF) << 16 |  /* base[23:0] */
                      (tss_limit & 0xFFFF) |         /* limit[15:0] */
                      ((uint64_t)0x89 << 40) |       /* access: Present, DPL=0, Type=9 (64位TSS可用) */
                      ((uint64_t)(tss_limit >> 16) << 48) |  /* limit[19:16] */
                      ((tss_base >> 24) & 0xFF) << 56;       /* base[31:24] */

    /* TSS描述符高8字节 */
    kernel_gdt64[6] = (tss_base >> 32) & 0xFFFFFFFF;  /* base[63:32] */

    vga_puts("[TSS64] TSS descriptor set in GDT\n");

    /* 加载TSS */
    __asm__ volatile("ltr %%ax" : : "a"(0x28));
    vga_puts("[OK] TSS loaded (0x28)\n");
}
