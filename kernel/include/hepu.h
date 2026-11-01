/* ============================================================================
 * 赫普OS - 内核公共头文件
 * ========================================================================== */

#ifndef HEPU_H
#define HEPU_H

#include <stdint.h>
#include <stddef.h>

/* 内核版本 */
#define HEPU_KERNEL_NAME    "HEPU Hyper Evolve OS"
#define HEPU_KERNEL_VERSION "0.0.1-prototype"
#define HEPU_KERNEL_ARCH    "x86"

/* 版权指纹魔数 */
#define HEPU_FINGERPRINT_MAGIC 0x48455055

/* 系统调用号（预留） */
#define SYS_HEPU_INFO     0
#define SYS_HEPU_FP_VERIFY 1

#endif /* HEPU_H */
