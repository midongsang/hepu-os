/* ============================================================================
 * 赫普OS - 启动安全保护模块头文件 (V0.10.5)
 * ========================================================================== */

#ifndef BOOT_GUARD_H
#define BOOT_GUARD_H

#include <stdint.h>

/* 统一启动日志输出 */
void bguard_log(const char *msg);
void bguard_log_hex(const char *msg, uint32_t val);

/* 安全调用初始化函数（无返回值） */
int boot_safe_call(const char *name, void (*func)(void));

/* 安全调用初始化函数（有返回值） */
int boot_safe_call_ret(const char *name, int (*func)(void));

/* 阶段标记 */
void boot_phase_start(const char *phase_name);
void boot_phase_done(const char *phase_name);

/* 失败记录 */
void boot_failure(const char *name, const char *reason);

/* 状态查询 */
int boot_get_failures(void);
int boot_get_skipped(void);
int boot_in_safe_mode(void);

/* 启动总结 */
void boot_summary(void);

#endif /* BOOT_GUARD_H */
