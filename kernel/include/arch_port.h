#ifndef HEPU_ARCH_PORT_H
#define HEPU_ARCH_PORT_H

#include <stdint.h>

/* ============================================================================
 * 架构移植点（arch port layer）
 *
 * 目的：让共享层（桌面 hpdesk.c、公共驱动）不再直接写 x86 专用内联代码
 * （rdtsc、端口 IO、hlt），为 ARM64 / RISC-V64 / LoongArch64 留替换点。
 * 详见 docs/planning/MULTIARCH_PORT_EVALUATION.md §6。
 *
 * 命名说明：不叫 hal_*，因为 kernel/include/hal.h + kernel/hal/ 已被
 * H-HAL（驱动注册/设备支持等级）占用，两套东西语义不同，避免混淆。
 *
 * 约定：本头文件只声明，不内联架构代码；每个 arch 目录各自提供实现。
 *   x86_64 -> kernel/arch/x86_64/arch_port_x86_64.c
 *   x86    -> kernel/arch/x86/arch_port_x86_32.c
 *   arm64  -> kernel/arch/aarch64/arch_port_aarch64.c（待建）
 * ========================================================================== */

/* 单调递增时间基准（周期计数，非绝对墙钟）。
 * x86_64/x86: RDTSC; ARM64: CNTVCT_EL0; RISC-V: time CSR。 */
uint64_t arch_ticks(void);

/* 控制台/调试输出。
 * x86_64/x86: COM1; ARM64: PL011 UART。 */
void arch_console_puts(const char *s);

/* 整机关机，不返回。
 * x86_64/x86: ACPI PM1a (0x604 <- 0x2000); ARM64: PSCI SYSTEM_OFF。 */
void arch_poweroff(void) __attribute__((noreturn));
void arch_reboot(void) __attribute__((noreturn));

#endif /* HEPU_ARCH_PORT_H */
