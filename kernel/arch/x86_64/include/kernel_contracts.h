/* ============================================================================
 * 赫普OS - 64位内核统一契约接口
 * V0.10.6: 按照系统包袱准则，定义所有子系统的稳定契约边界
 * ========================================================================== */

#ifndef HEPU_KERNEL64_CONTRACTS_H
#define HEPU_KERNEL64_CONTRACTS_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * 物理内存管理契约
 * ========================================================================== */
struct pmm_ops {
    int (*init)(uint64_t memory_size, uint64_t bitmap_addr);
    uint64_t (*alloc_page)(void);
    void (*free_page)(uint64_t phys_addr);
    void (*get_stats)(uint64_t *total, uint64_t *used, uint64_t *free);
    uint64_t (*alloc_pages)(size_t count);
    void (*free_pages)(uint64_t phys_addr, size_t count);
};

/* ============================================================================
 * 虚拟内存管理契约
 * ========================================================================== */
struct vmm_ops {
    int (*init)(uint64_t pml4_addr);
    int (*map_page)(uint64_t pml4_addr, uint64_t virt, uint64_t phys, uint64_t flags);
    void (*unmap_page)(uint64_t pml4_addr, uint64_t virt);
    uint64_t (*virt_to_phys)(uint64_t pml4_addr, uint64_t virt);
    void (*load_cr3)(uint64_t pml4_addr);
    uint64_t (*get_kernel_pml4)(void);
};

/* ============================================================================
 * 中断管理契约
 * ========================================================================== */
struct irq_ops {
    int (*init)(void);
    void (*enable)(void);
    void (*disable)(void);
    void (*register_handler)(int vector, void (*handler)(int vector));
    void (*unregister_handler)(int vector);
};

/* ============================================================================
 * 系统调用契约
 * ========================================================================== */
struct syscall_ops {
    int (*init)(void);
    uint64_t (*handler)(uint64_t num, uint64_t arg1, uint64_t arg2,
                        uint64_t arg3, uint64_t arg4, uint64_t arg5);
    int (*register_syscall)(int num, uint64_t (*handler)(uint64_t, uint64_t,
                            uint64_t, uint64_t, uint64_t));
};

/* ============================================================================
 * 进程管理契约
 * ========================================================================== */
struct process_ops {
    int (*init)(void);
    uint64_t (*create)(const char *name, uint64_t entry, uint64_t stack_size);
    void (*exit)(uint64_t pid);
    void (*schedule)(void);
    void (*list)(void);
    uint64_t (*get_current_pid)(void);
};

/* ============================================================================
 * 存储设备契约（HAL）
 * ========================================================================== */
struct storage_ops {
    int (*init)(void);
    int (*read_sector)(uint32_t lba, void *buffer);
    int (*write_sector)(uint32_t lba, const void *buffer);
    int (*read_sectors)(uint32_t lba, uint8_t count, void *buffer);
    int (*write_sectors)(uint32_t lba, uint8_t count, const void *buffer);
};

/* ============================================================================
 * 输入设备契约（HAL）
 * ========================================================================== */
struct input_ops {
    int (*init)(void);
    int (*getchar)(void);
    char (*getchar_blocking)(void);
    void (*interrupt_handler)(void);
};

/* ============================================================================
 * 实时时钟契约（HAL）
 * ========================================================================== */
struct rtc_ops {
    int (*init)(void);
    uint64_t (*get_timestamp)(void);
    void (*get_time)(void *time_struct);
};

/* ============================================================================
 * CPU契约（HAL）
 * ========================================================================== */
struct cpu_ops {
    int (*detect)(void);
    int (*is_long_mode)(void);
    void (*enable_sce)(void);
    void (*enable_nxe)(void);
    uint64_t (*read_msr)(uint32_t msr);
    void (*write_msr)(uint32_t msr, uint64_t value);
};

/* ============================================================================
 * GDT/TSS契约
 * ========================================================================== */
struct gdt_ops {
    int (*init)(void);
    void (*set_kernel_stack)(uint64_t stack);
    uint64_t (*get_tss_addr)(void);
};

/* ============================================================================
 * 内核全局契约注册表
 * ========================================================================== */
struct kernel_contracts {
    const struct pmm_ops *pmm;
    const struct vmm_ops *vmm;
    const struct irq_ops *irq;
    const struct syscall_ops *syscall;
    const struct process_ops *process;
    const struct storage_ops *storage;
    const struct input_ops *input;
    const struct rtc_ops *rtc;
    const struct cpu_ops *cpu;
    const struct gdt_ops *gdt;
};

/* 获取全局契约注册表 */
struct kernel_contracts *kernel_get_contracts(void);

/* 注册契约（用于替换实现，支持渐进式替换） */
int kernel_register_pmm(const struct pmm_ops *ops);
int kernel_register_vmm(const struct vmm_ops *ops);
int kernel_register_irq(const struct irq_ops *ops);
int kernel_register_syscall(const struct syscall_ops *ops);
int kernel_register_process(const struct process_ops *ops);
int kernel_register_storage(const struct storage_ops *ops);
int kernel_register_input(const struct input_ops *ops);
int kernel_register_rtc(const struct rtc_ops *ops);
int kernel_register_cpu(const struct cpu_ops *ops);
int kernel_register_gdt(const struct gdt_ops *ops);

#endif /* HEPU_KERNEL64_CONTRACTS_H */
