/* ============================================================================
 * 赫普OS - 测试套件统一注册
 * 从kernel.c拆分，集中管理所有KUnit测试套件注册
 * ========================================================================== */

#include <stdint.h>
#include "vga.h"

/* 所有测试套件注册函数声明 */
extern void kunit_extended_register(void);
extern void kunit_crypto_register(void);
extern void kunit_network_register(void);
extern void kunit_fs_register(void);
extern void kunit_drivers_register(void);
extern void kunit_sched_register(void);
extern void kunit_mm_register(void);
extern void kunit_ipc_register(void);
extern void kunit_irq_register(void);
extern void kunit_syscall_register(void);
extern void kunit_net_deep_register(void);
extern void kunit_crypto_deep_register(void);
extern void kunit_fs_deep_register(void);
extern void kunit_boot_register(void);
extern void kunit_drivers_deep_register(void);
extern void kunit_ds_deep_register(void);
extern void kunit_algo_register(void);
extern void kunit_concurrency_register(void);
extern void kunit_error_boundary_register(void);
extern void kunit_memory_safety_register(void);
extern void kunit_time_register(void);
extern void kunit_io_register(void);
extern void kunit_config_register(void);
extern void kunit_i18n_register(void);
extern void kunit_security_register(void);
extern void kunit_process_register(void);
extern void kunit_netsec_register(void);
extern void kunit_storage_register(void);
extern void kunit_power_register(void);

/* V1.02 新模块测试 */
extern void kunit_v102_mem_register(void);
extern void kunit_v102_hal_register(void);

/* V3.97-E43 压力测试 */
extern void kunit_stress_register(void);

/* V0.10.1 系统调用深度测试 */
extern void kunit_syscall_deep_register(void);
/* V0.10.1 内存管理深度测试 */
extern void kunit_mm_deep_register(void);

/* V0.10.4 第四周验证 - 用户场景/恢复/性能基准 */
extern void kunit_user_scenario_register(void);
extern void kunit_recovery_register(void);
extern void kunit_perf_benchmark_register(void);

/* V0.10.3 第三周验证 - 集成/安全/兼容/压力测试 */
extern void kunit_integration_register(void);
extern void kunit_security_e2e_register(void);
extern void kunit_compatibility_register(void);
extern void kunit_stress_e2e_register(void);

/* V0.10.2 第二周验证 - 端到端测试 */
extern void kunit_fs_e2e_register(void);
extern void kunit_net_e2e_register(void);
extern void kunit_driver_e2e_register(void);
extern void kunit_gfx_register(void);
extern void kunit_perf_register(void);

/* 性能基准注册 */
extern void kbench_report_init(void);
extern void kunit_report_init(void);

void kunit_register_all_suites(void)
{
    vga_puts("[KUNIT] start registered test suite ...\n");

    /* V0.78 扩展测试 */
    kunit_extended_register();
    /* V0.80 加密/网络测试 */
    kunit_crypto_register();
    kunit_network_register();
    /* V0.81 文件系统测试 */
    kunit_fs_register();
    /* V0.81 测试报告 */
    kunit_report_init();
    /* V0.82 驱动测试 */
    kunit_drivers_register();
    /* V0.82 性能报告 */
    kbench_report_init();
    /* V0.83 调度器/内存测试 */
    kunit_sched_register();
    kunit_mm_register();
    /* V0.84 IPC/中断测试 */
    kunit_ipc_register();
    kunit_irq_register();
    /* V0.85 系统调用/网络深度测试 */
    kunit_syscall_register();
    kunit_net_deep_register();
    /* V0.86 加密/文件系统深度测试 */
    kunit_crypto_deep_register();
    kunit_fs_deep_register();
    /* V0.87 引导/驱动深度测试 */
    kunit_boot_register();
    kunit_drivers_deep_register();
    /* V0.88 数据结构/算法测试 */
    kunit_ds_deep_register();
    kunit_algo_register();
    /* V0.89 并发/错误边界测试 */
    kunit_concurrency_register();
    kunit_error_boundary_register();
    /* V0.90 内存安全/时间测试 */
    kunit_memory_safety_register();
    kunit_time_register();
    /* V0.91 输入输出/配置测试 */
    kunit_io_register();
    kunit_config_register();
    /* V0.92 多语言/安全测试 */
    kunit_i18n_register();
    kunit_security_register();
    /* V0.93 进程线程/网络安全测试 */
    kunit_process_register();
    kunit_netsec_register();
    /* V0.94 存储/电源测试 */
    kunit_storage_register();
    kunit_power_register();
    /* V1.02 新模块测试 */
    kunit_v102_mem_register();
    kunit_v102_hal_register();
    /* V3.97-E43 压力套件 */
    kunit_stress_register();
    /* V0.10.1 系统调用深度测试 */
    kunit_syscall_deep_register();
    /* V0.10.1 内存管理深度测试 */
    kunit_mm_deep_register();
    /* V0.10.4 第四周验证 - 用户场景/恢复/性能基准 */
    kunit_user_scenario_register();
    kunit_recovery_register();
    kunit_perf_benchmark_register();
    /* V0.10.3 第三周验证 - 集成/安全/兼容/压力测试 */
    kunit_integration_register();
    kunit_security_e2e_register();
    kunit_compatibility_register();
    kunit_stress_e2e_register();
    /* V0.10.2 第二周验证 - 端到端测试 */
    kunit_fs_e2e_register();
    kunit_net_e2e_register();
    kunit_driver_e2e_register();
    kunit_gfx_register();
    kunit_perf_register();

    vga_puts("[KUNIT] all test suite registered done \n");
}
