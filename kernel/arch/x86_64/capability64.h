/* ============================================================================
 * 赫普OS 64位 - 能力（Capability）系统
 *
 * 统一接口定义在 shared/capability/hepu_cap.h（以其为真源）。
 * 本头文件提供 cap64_* → cap_* 的宏兼容层，
 * 保证64位现有调用点无需改动即可切换到统一接口。
 * ========================================================================== */

#ifndef HEPU_CAPABILITY64_H
#define HEPU_CAPABILITY64_H

#include "shared/capability/hepu_cap.h"
#include "domain64.h"

/* 兼容旧名 */
#define CAP64_MAX_CAPS             CAP_MAX_CAPS
#define CAP64_HANDLES_PER_DOMAIN   CAP_HANDLES_PER_DOMAIN

/* cap64_* → cap_* 宏映射，保持现有调用点兼容 */
#define cap64_init          cap_init
#define cap64_mint          cap_mint
#define cap64_derive        cap_derive
#define cap64_revoke        cap_revoke
#define cap64_check         cap_check
#define cap64_get_valid     cap_get_valid
#define cap64_release       cap_release
#define cap64_count         cap_count
#define cap64_list          cap_list
#define cap64_list_domain   cap_list_domain

#endif /* HEPU_CAPABILITY64_H */
