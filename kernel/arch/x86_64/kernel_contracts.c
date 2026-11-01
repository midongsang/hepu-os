/* ============================================================================
 * 赫普OS - 64位内核契约注册表实现
 * V0.10.6: 按照系统包袱准则，实现子系统契约的注册和查询
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "kernel_contracts.h"

/* 全局契约注册表 */
static struct kernel_contracts g_contracts;

/* 获取全局契约注册表 */
struct kernel_contracts *kernel_get_contracts(void)
{
    return &g_contracts;
}

/* 注册物理内存管理契约 */
int kernel_register_pmm(const struct pmm_ops *ops)
{
    if (!ops) return -1;
    g_contracts.pmm = ops;
    return 0;
}

/* 注册虚拟内存管理契约 */
int kernel_register_vmm(const struct vmm_ops *ops)
{
    if (!ops) return -1;
    g_contracts.vmm = ops;
    return 0;
}

/* 注册中断管理契约 */
int kernel_register_irq(const struct irq_ops *ops)
{
    if (!ops) return -1;
    g_contracts.irq = ops;
    return 0;
}

/* 注册系统调用契约 */
int kernel_register_syscall(const struct syscall_ops *ops)
{
    if (!ops) return -1;
    g_contracts.syscall = ops;
    return 0;
}

/* 注册进程管理契约 */
int kernel_register_process(const struct process_ops *ops)
{
    if (!ops) return -1;
    g_contracts.process = ops;
    return 0;
}

/* 注册存储设备契约 */
int kernel_register_storage(const struct storage_ops *ops)
{
    if (!ops) return -1;
    g_contracts.storage = ops;
    return 0;
}

/* 注册输入设备契约 */
int kernel_register_input(const struct input_ops *ops)
{
    if (!ops) return -1;
    g_contracts.input = ops;
    return 0;
}

/* 注册实时时钟契约 */
int kernel_register_rtc(const struct rtc_ops *ops)
{
    if (!ops) return -1;
    g_contracts.rtc = ops;
    return 0;
}

/* 注册CPU契约 */
int kernel_register_cpu(const struct cpu_ops *ops)
{
    if (!ops) return -1;
    g_contracts.cpu = ops;
    return 0;
}

/* 注册GDT/TSS契约 */
int kernel_register_gdt(const struct gdt_ops *ops)
{
    if (!ops) return -1;
    g_contracts.gdt = ops;
    return 0;
}

/* 初始化契约注册表（注册所有默认实现） */
void kernel_contracts_init(void)
{
    /* 各模块在自己的init函数中注册契约 */
    /* 这里只清零注册表 */
    g_contracts.pmm = 0;
    g_contracts.vmm = 0;
    g_contracts.irq = 0;
    g_contracts.syscall = 0;
    g_contracts.process = 0;
    g_contracts.storage = 0;
    g_contracts.input = 0;
    g_contracts.rtc = 0;
    g_contracts.cpu = 0;
    g_contracts.gdt = 0;
}
