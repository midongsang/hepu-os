/* ============================================================================
 * 赫普OS - x86_64支持头文件
 * V8.38: 初始实现（长模式切换+64位页表）
 * ========================================================================== */

#ifndef _X86_64_H
#define _X86_64_H

#include <stdint.h>

/* 控制寄存器位 */
#define CR0_PE          (1 << 0)   /* 保护模式启用 */
#define CR0_MP          (1 << 1)   /* 监控协处理器 */
#define CR0_EM          (1 << 2)   /* 模拟协处理器 */
#define CR0_TS          (1 << 3)   /* 任务切换 */
#define CR0_ET          (1 << 4)   /* 扩展类型 */
#define CR0_NE          (1 << 5)   /* 数值错误 */
#define CR0_WP          (1 << 16)  /* 写保护 */
#define CR0_AM          (1 << 18)  /* 对齐掩码 */
#define CR0_NW          (1 << 29)  /* 非写直达 */
#define CR0_CD          (1 << 30)  /* 缓存禁用 */
#define CR0_PG          (1 << 31)  /* 分页启用 */

#define CR4_VME         (1 << 0)   /* 虚拟8086模式扩展 */
#define CR4_PVI         (1 << 1)   /* 保护模式虚拟中断 */
#define CR4_TSD         (1 << 2)   /* 时间戳禁用 */
#define CR4_DE          (1 << 3)   /* 调试扩展 */
#define CR4_PSE         (1 << 4)   /* 页大小扩展 */
#define CR4_PAE         (1 << 5)   /* 物理地址扩展 */
#define CR4_MCE         (1 << 6)   /* 机器检查异常 */
#define CR4_PGE         (1 << 7)   /* 页全局启用 */
#define CR4_PCE         (1 << 8)   /* 性能计数器启用 */
#define CR4_OSFXSR      (1 << 9)   /* OS支持FXSAVE/FXRSTOR */
#define CR4_OSXMMEXCPT  (1 << 10)  /* OS支持未屏蔽SIMD异常 */
#define CR4_VMXE        (1 << 13)  /* VMX启用 */
#define CR4_SMXE        (1 << 14)  /* SMX启用 */
#define CR4_FSGSBASE    (1 << 16)  /* FS/GS基址指令 */
#define CR4_PCIDE       (1 << 17)  /* PCID启用 */
#define CR4_OSXSAVE     (1 << 18)  /* OS支持XSAVE */
#define CR4_SMEP        (1 << 20)  /* 监督模式执行保护 */
#define CR4_SMAP        (1 << 21)  /* 监督模式访问保护 */

/* EFER MSR */
#define EFER_MSR        0xC0000080
#define EFER_SCE        (1 << 0)   /* 系统调用扩展 */
#define EFER_LME        (1 << 8)   /* 长模式启用 */
#define EFER_LMA        (1 << 10)  /* 长模式激活 */
#define EFER_NXE        (1 << 11)  /* 不执行启用 */
#define EFER_FXSAVE     (1 << 14)  /* FXSAVE优化 */

/* 64位页表项标志 */
#define PT64_PRESENT    (1ULL << 0)
#define PT64_WRITABLE   (1ULL << 1)
#define PT64_USER       (1ULL << 2)
#define PT64_WRITETHROUGH (1ULL << 3)
#define PT64_CACHEDISABLE (1ULL << 4)
#define PT64_ACCESSED   (1ULL << 5)
#define PT64_DIRTY      (1ULL << 6)
#define PT64_HUGE       (1ULL << 7)
#define PT64_GLOBAL     (1ULL << 8)
#define PT64_NOEXEC     (1ULL << 63)

/* 页表级别 */
#define PML4_SHIFT      39
#define PDPT_SHIFT      30
#define PD_SHIFT        21
#define PT_SHIFT        12

#define PML4_MASK       0x1FF
#define PDPT_MASK       0x1FF
#define PD_MASK         0x1FF
#define PT_MASK         0x1FF

#define PAGE_SIZE_4K    0x1000
#define PAGE_SIZE_2M    0x200000
#define PAGE_SIZE_1G    0x40000000

/* 64位描述符 */
typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) gdt_entry64_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdt_ptr64_t;

/* 64位中断描述符 */
typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed)) idt_entry64_t;

/* 64位TSS */
typedef struct {
    uint32_t reserved1;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved2;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved3;
    uint16_t reserved4;
    uint16_t iomap_base;
} __attribute__((packed)) tss64_t;

/* 64位寄存器状态 */
typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip, rflags;
    uint64_t cs, ss, ds, es, fs, gs;
    uint64_t cr0, cr3, cr4, cr8;
    uint64_t efer;
} cpu_state64_t;

/* 函数声明 */
int x86_64_detect(void);
int x86_64_enter_long_mode(void);
void x86_64_far_jump(void);           /* 汇编：32位远跳转到64位 */
void x86_64_kernel_main(void);         /* 64位C语言入口 */
void x86_64_init_paging(uint64_t *pml4);
void x86_64_map_page(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags);
uint64_t x86_64_virt_to_phys(uint64_t *pml4, uint64_t virt);
void x86_64_init_gdt(void);
void x86_64_init_idt(void);
void x86_64_init_tss(void);
void x86_64_set_kernel_stack(uint64_t stack);
uint64_t x86_64_read_msr(uint32_t msr);
void x86_64_write_msr(uint32_t msr, uint64_t value);
void x86_64_enable_sce(void);
void x86_64_enable_nxe(void);
void x86_64_enable_smep(void);
void x86_64_enable_smap(void);
int x86_64_is_long_mode(void);

/* 内联汇编函数 */
static inline uint64_t x86_64_read_cr0(void)
{
    uint64_t val;
    __asm__ volatile("mov %%cr0, %0" : "=r"(val));
    return val;
}

static inline void x86_64_write_cr0(uint64_t val)
{
    __asm__ volatile("mov %0, %%cr0" :: "r"(val));
}

static inline uint64_t x86_64_read_cr3(void)
{
    uint64_t val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(val));
    return val;
}

static inline void x86_64_write_cr3(uint64_t val)
{
    __asm__ volatile("mov %0, %%cr3" :: "r"(val));
}

static inline uint64_t x86_64_read_cr4(void)
{
    uint64_t val;
    __asm__ volatile("mov %%cr4, %0" : "=r"(val));
    return val;
}

static inline void x86_64_write_cr4(uint64_t val)
{
    __asm__ volatile("mov %0, %%cr4" :: "r"(val));
}

/* ============================================================================
 * 64位进程管理（V8.88新增）
 * ========================================================================== */

/* 前向声明 */
struct process_64;

/* 进程状态 */
typedef enum {
    PROC64_UNUSED = 0,
    PROC64_READY,
    PROC64_RUNNING,
    PROC64_BLOCKED,
    PROC64_SLEEPING,
    PROC64_ZOMBIE
} proc64_state_t;

/* 进程上下文 */
typedef struct {
    uint64_t rsp;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;
    uint64_t cr3;
    uint64_t fs_base;
    uint64_t gs_base;
} proc64_context_t;

/* 进程控制块 */
typedef struct process_64 {
    int              pid;
    char             name[64];
    proc64_state_t   state;
    int              priority;
    int              time_slice;
    int              remaining_time;
    uint64_t         entry_point;
    uint64_t         stack_top;
    uint64_t         stack_bottom;
    proc64_context_t context;
    uint64_t         user_time;
    uint64_t         kernel_time;
    uint64_t         context_switches;
    int              used;
} process_64_t;

/* 进程管理 */
int process_64_init(void);
process_64_t *process_64_create(const char *name, uint64_t entry,
                                int priority, int flags);
int process_64_destroy(int pid);
process_64_t *process_64_get(int pid);
process_64_t *process_64_current(void);
int process_64_count(void);
int process_64_block(void);
int process_64_wakeup(int pid);
int process_64_sleep(uint64_t milliseconds);
void process_64_exit(int exit_code);
void process_64_get_stats(int *total, int *running, int *ready,
                          int *blocked, int *sleeping);
int process_64_list(process_64_t **out, int max);

/* 调度器 */
void scheduler_64_tick(void);
void scheduler_64_yield(void);

/* 汇编函数 */
extern void context_switch_64(void *old_ctx, void *new_ctx);
extern void process_start_64(void);

#endif /* _X86_64_H */
