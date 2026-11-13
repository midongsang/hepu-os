/* ============================================================================
 * 赫普OS - 64位GDT/TSS初始化
 * V0.10.6: 初始实现（64位全局描述符表和任务状态段）
 * ========================================================================== */

#include <stdint.h>
#include "smp.h"
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

/* GDT条目数量
 * V0.58.5 / SB-18: 原来只有 7 项、一份全局 TSS。AP 一旦跑用户态进程,
 * 它的用户态中断会用 tss.rsp0 切栈 —— 而那是 BSP 的栈, 两核写同一份就踩栈。
 * 现在给每个核一个 64 位 TSS 描述符(占 2 个槽), 索引从 5 开始:
 *   核 i 的 TSS 选择子 = 0x28 + i*16
 * 布局: [0]null [1]code [2]data [3]user_data [4]user_code [5..] TSS*MAX_CPUS */
#define GDT_ENTRIES (5 + MAX_CPUS * 2)

/* GDT表 */
static gdt_entry64_t gdt64[GDT_ENTRIES] __attribute__((aligned(16)));
static gdt_ptr_t gdt64_ptr;

/* 每个核独立的内核栈, 供该核 TSS 的 rsp0 使用。
 * 16KB * 16 核 = 256KB, 全在 .bss 里, 不进镜像文件。
 * BSP 仍用 kernel_stack_top 那一块(另有 64KB), 这里 0 号槽只是兜底。 */
#define PER_CPU_KSTACK_SIZE 16384
static uint8_t per_cpu_kstack[MAX_CPUS][PER_CPU_KSTACK_SIZE] __attribute__((aligned(16)));

/* 核 i 的 TSS 选择子 */
uint16_t gdt64_tss_selector_for_cpu(int cpu)
{
    if (cpu < 0 || cpu >= MAX_CPUS) cpu = 0;
    return (uint16_t)(0x28 + cpu * 16);
}

/* 本核内核栈顶(供 AP 初始化填 rsp0 用) */
uint64_t gdt64_kstack_top_for_cpu(int cpu)
{
    if (cpu < 0 || cpu >= MAX_CPUS) cpu = 0;
    return (uint64_t)&per_cpu_kstack[cpu][PER_CPU_KSTACK_SIZE];
}

/* 内核栈 */
extern uint8_t kernel_stack[];
/* V0.13.43 修复: kernel_main_64.c 定义的是指针变量 */
extern uint8_t *kernel_stack_top;

/* VGA输出 */
extern void vga_puts(const char *str);
extern void vga_put_hex(uint64_t val);
extern void *memset(void *dst, int val, size_t n);

/* 设置普通GDT条目 */
/* V0.55.1: 保留的 GDT 项构造工具, 当前 GDT 用静态表构建, 暂无调用方 */
static __attribute__((unused)) void gdt_set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt64[index].entry.limit_low = limit & 0xFFFF;
    gdt64[index].entry.base_low = base & 0xFFFF;
    gdt64[index].entry.base_mid = (base >> 16) & 0xFF;
    gdt64[index].entry.access = access;
    gdt64[index].entry.granularity = ((limit >> 16) & 0x0F) | gran;
    gdt64[index].entry.base_high = (base >> 24) & 0xFF;
    gdt64[index].base_high32 = 0;
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

    /* 条目3: 用户数据段 (0x18) — V0.13.43: 与 Linux 标准布局一致
     * (原为 0xFA 代码段, 与 syscall STAR/SYSRET 的 CS=0x20/SS=0x18 假设冲突) */
    gdt64[3].entry.limit_low = 0x0000;
    gdt64[3].entry.base_low = 0x0000;
    gdt64[3].entry.base_mid = 0x00;
    gdt64[3].entry.access = 0xF2;
    gdt64[3].entry.granularity = 0x00;
    gdt64[3].entry.base_high = 0x00;
    gdt64[3].base_high32 = 0x00000000;
    gdt64[3].reserved = 0;

    /* 条目4: 用户代码段 (0x20) */
    gdt64[4].entry.limit_low = 0x0000;
    gdt64[4].entry.base_low = 0x0000;
    gdt64[4].entry.base_mid = 0x00;
    gdt64[4].entry.access = 0xFA;
    gdt64[4].entry.granularity = 0x20;
    gdt64[4].entry.base_high = 0x00;
    gdt64[4].base_high32 = 0x00000000;
    gdt64[4].reserved = 0;

    /* 注意: 本函数的 gdt64[] 是死代码(从未被调用, BSP 实际用的是
     * entry_64.S 里的 kernel_gdt64)。每核 TSS 与描述符由 tss64_init()
     * 直接写进 kernel_gdt64/kernel_tss64, 不在这里重复一份。
     * V0.58.5 / SB-18 */

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

    /* 加载TSS (本核=0 号) */
    uint16_t tss_sel = gdt64_tss_selector_for_cpu(0);
    __asm__ volatile("ltr %%ax" : : "a"(tss_sel));
    vga_puts("[GDT64] TSS loaded (0x");
    vga_put_hex(tss_sel);
    vga_puts(")\n");

    vga_puts("[OK] GDT initialized\n");
}

/* 设置内核栈（更新TSS的RSP0） */
void gdt64_set_kernel_stack(uint64_t stack)
{
    /* SB-18: 只改本核那份; 越界回落 0 号, 避免写坏邻核 TSS。
     * 改的是 entry_64.S 里真正的 TSS 数组, 不是已废弃的 gdt64[] 那一份。 */
    extern uint32_t smp_get_current_cpu(void);
    extern uint8_t kernel_tss64[];
    int cpu = (int)smp_get_current_cpu();
    if (cpu < 0 || cpu >= MAX_CPUS) cpu = 0;
    *(uint64_t *)&kernel_tss64[cpu * 104 + 4] = stack;
}

uint64_t gdt64_get_tss_addr(void)
{
    /* 返回真表(kernel_tss64)的 0 号那份 —— 唯一调用方只做地址打印 */
    extern uint8_t kernel_tss64[];
    return (uint64_t)&kernel_tss64[0];
}

/* 外部符号：汇编中定义的GDT和TSS */
extern uint64_t kernel_gdt64[];
extern uint8_t kernel_tss64[];
extern uint8_t irq_stack_df_top[];
extern uint8_t irq_stack_nmi_top[];
extern uint8_t irq_stack_mc_top[];

/* 初始化并加载TSS（在汇编入口加载GDT之后调用）
 *
 * V0.58.5 / SB-18: 由"一份全局 TSS"改为"每核一份"。
 *
 * 原来是全核共用 tss64, rsp0 指向 BSP 的 64KB 内核栈。AP 现在只在内核态
 * 收中断(不切栈)所以还没出事, 但一旦 AP 上有用户态进程, 它的中断会用
 * tss.rsp0 切栈 —— 两个核写同一份栈就踩。
 *
 * 现在: GDT 里每个核一个 TSS 描述符(核 i 的选择子 = 0x28 + i*16),
 *       每核 rsp0 指向自己的 per_cpu_kstack 顶。
 * 注意: kernel_gdt64 必须够长 —— 在 entry_64.S 里已扩到 37 项。
 */
void tss64_init(void)
{
    vga_puts("[TSS64] Initializing per-CPU TSS...\n");

    /* 先切换到kernel_gdt64（汇编入口用的是gdt_64，TSS条目为空） */
    extern uint16_t kernel_gdt64_ptr[];
    __asm__ volatile("lgdt %0" : : "m"(kernel_gdt64_ptr));

    for (int cpu = 0; cpu < MAX_CPUS; cpu++) {
        uint8_t *tss = &kernel_tss64[cpu * 104];

        for (int i = 0; i < 104; i++) tss[i] = 0;

        /* RSP0: 核 0 沿用 BSP 那块 64KB 内核栈(已验证), 其余用各自 per-CPU 栈 */
        *(uint64_t *)&tss[4] = (cpu == 0)
            ? (uint64_t)kernel_stack_top
            : (uint64_t)&per_cpu_kstack[cpu][PER_CPU_KSTACK_SIZE];

        /* IST1-IST3: 双重故障 / NMI / 机器检查。
         * 这三次是"无论当前在哪个栈上都已经不可信"的场合, 继续共用同一组
         * 应急栈在多核下同样会撞, 但应急栈本身很小且触发概率极低 ——
         * 先保持共用, 单独记录为后续项。 */
        *(uint64_t *)&tss[36] = (uint64_t)&irq_stack_df_top;
        *(uint64_t *)&tss[44] = (uint64_t)&irq_stack_nmi_top;
        *(uint64_t *)&tss[52] = (uint64_t)&irq_stack_mc_top;

        /* IO Map 基地址 = TSS 大小, 表示无位图 */
        *(uint16_t *)&tss[102] = 104;

        /* 写 GDT 描述符: 核 cpu 占 gdt64[5 + cpu*2] 与 [6 + cpu*2] */
        uint64_t tss_base = (uint64_t)tss;
        uint32_t tss_limit = 103;
        int slot = 5 + cpu * 2;

        kernel_gdt64[slot] =
              (tss_base & 0xFFFFFF) << 16       /* base[23:0] */
            | (tss_limit & 0xFFFF)              /* limit[15:0] */
            | ((uint64_t)0x89 << 40)            /* P=1 DPL=0 Type=9 (可用64位TSS) */
            | ((uint64_t)(tss_limit >> 16) << 48)
            | (((tss_base >> 24) & 0xFF) << 56);

        kernel_gdt64[slot + 1] = (tss_base >> 32) & 0xFFFFFFFF;
    }

    vga_puts("[TSS64] Per-CPU TSS descriptors set (");
    vga_put_hex((uint64_t)MAX_CPUS);
    vga_puts(" cores)\n");

    /* 加载本核(=核0)的 TSS */
    __asm__ volatile("ltr %%ax" : : "a"((uint16_t)0x28));
    vga_puts("[OK] TSS loaded (0x28)\n");
}
