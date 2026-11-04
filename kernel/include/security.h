/* ============================================================================
 * 赫普OS 32位 - Security 接口
 *
 * 统一接口定义在 shared/security/hepu_security.h。
 * 本头文件 include 它，保留 32 位特有接口（selftest/pid_file/load_policy），
 * 并给 security_app.c 遗留的 active_app 变量加别名宏。
 * ========================================================================== */

#ifndef SECURITY_H
#define SECURITY_H

#include <stdint.h>
#include "shared/security/hepu_security.h"

/* ---- hepu_security.h 已定义（不再重复） ---- */
/* sec_domain_t, sec_type_t, sec_rule_t, sec_context_t,
 * sec_object_class_t, sec_perm, sec_init, sec_check_access, ...
 * sec_register_app, sec_path_type, sec_guard_file, sec_active_app, ... */

/* ---- 32 位特有：MAC self-test ---- */
void sec_selftest(void);

/* ---- 32 位特有：便捷封装 ---- */
int sec_check_pid_file(int pid, const char *path, uint8_t perm);

/* ---- 32 位特有：加载策略 ---- */
int sec_load_policy(const char *policy);

/* ---- 别名宏：security_app.c 遗留用 active_app，hepu_security.h 用 sec_active_app ---- */
#define active_app     sec_active_app
#define active_app_idx sec_active_app_idx

#endif /* SECURITY_H */
