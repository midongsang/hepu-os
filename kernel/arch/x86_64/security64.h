/* ============================================================================
 * 赫普OS 64位 - 安全裁决层
 *
 * 统一接口定义在 shared/security/hepu_security.h（以其为真源）。
 * 本头文件提供 sec64_* → sec_* 的宏兼容层，
 * 保证64位现有调用点无需改动即可切换到统一接口。
 * ========================================================================== */

#ifndef HEPU_SECURITY64_H
#define HEPU_SECURITY64_H

#include "shared/security/hepu_security.h"
#include "capability64.h"
#include "domain64.h"
#include "devfs64.h"

/* 兼容旧名：64位原有的细粒度能力检查 */
/* 注意！sec64_check_access 签名与 hepu_security.h 的 sec_check_access
 * 不同：前者是 (domain_id, sec_resource_class, ...)，
 * 后者是 (sec_context_t *, sec_context_t *, sec_object_class_t, ...)
 * 故映射到 sec_cap_check（细粒度）。 */
#define sec64_check_access      sec_cap_check

/* sec64_audit 是 64 位包装函数，内部调用 sec_audit + 打印 domain_id
 * 与 hepu_security.h 的 sec_audit 签名不同，保留原名。 */
/* #define sec64_audit         sec_audit */  /* 暂不统一 */

/* 以下签名完全一致，直接映射 */
#define sec64_init              sec_init
#define sec64_get_enforcing     sec_get_enforcing
#define sec64_set_enforcing     sec_set_enforcing

#endif /* HEPU_SECURITY64_H */
