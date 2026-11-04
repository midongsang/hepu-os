/* ============================================================================
 * 赫普OS - Dev 模式 capability 化（路线C 细粒度层）
 *
 * P3 初版用 per-process dev_mode 标志实现会话隔离。
 * 现在升级为 capability 句柄：dev 是一种 SERVICE_ENDPOINT 类型能力，
 * 由 KERNEL 域 mint，需要的域通过 derive 获取句柄。
 *
 * hepu_is_dev() → 查当前 domain_id 是否持有 dev cap 句柄
 * hepu_set_dev_mode(on) → on=derive，off=release
 * ========================================================================== */

#ifndef HEPU_DEV_H
#define HEPU_DEV_H

#include <stdint.h>

/* 资源标识：dev 模式 capability 的 resource_id */
#define HEPU_DEV_CAP_RES_ID  ((uint64_t)0xDEADBEEFULL)

/* 全局 dev capability ID（初始化后有效） */
extern int g_hepu_dev_cap_id;

/* 初始化：KERNEL 域 mint dev capability */
void hepu_dev_cap_init(void);

/* 查询：当前进程所属域是否持有 dev capability 句柄 */
int hepu_is_dev(void);

/* 设置：当前进程所属域 derive/release dev 句柄 */
void hepu_set_dev_mode(int on);

#endif /* HEPU_DEV_H */
