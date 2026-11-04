/* ============================================================================
 * 赫普OS 64位 - 隔离域（Domain）对象
 *
 * P2-2: 结构与接口统一定义在 shared/domain/hepu_domain.h，
 *       本头文件提供 domain64_* → domain_* 的宏兼容层，
 *       保证64位现有调用点无需改动即可切换到统一接口。
 * ========================================================================== */

#ifndef HEPU_DOMAIN64_H
#define HEPU_DOMAIN64_H

#include "shared/domain/hepu_domain.h"

/* 兼容旧名 */
#define DOMAIN64_MAX_SLOTS  DOMAIN_MAX_SLOTS

/* domain64_* → domain_* 宏映射，保持现有调用点兼容 */
#define domain64_init           domain_init
#define domain64_create         domain_create
#define domain64_get            domain_get
#define domain64_get_by_type    domain_get_by_type
#define domain64_count          domain_count
#define domain64_list           domain_list
#define domain64_env_to_id      domain_env_to_id
#define domain64_mem_usage      domain_mem_usage
#define domain64_refresh_stats  domain_refresh_stats

#endif /* HEPU_DOMAIN64_H */
