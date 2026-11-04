/* ============================================================================
 * 赫普OS - Capability 统一接口（32/64位共用）
 *
 * 细粒度资源授权单位。内核只回答"这个域是否持有该资源的能力"。
 *
 * 设计：
 *   capability_t  —— 资源的内核对象（设备/缓冲/事件/fence/IPC/服务）
 *   handle_t      —— 某域对某能力的引用，带权利掩码
 *   handle_table_t—— 每域一份句柄表
 *   generation    —— 代际计数，revoke 时递增，旧句柄自动失效
 *   derive        —— 衰减派生：子句柄权利是父句柄的子集
 *
 * 以 64 位 capability64.h 为真源，32 位/64 位各自实现。
 * 所有字段用显式宽度类型，避免 32/64 位 ABI 漂移。
 * ========================================================================== */

#ifndef HEPU_SHARED_CAP_H
#define HEPU_SHARED_CAP_H

#include <stdint.h>

/* ---- 常量 ---- */
#define CAP_MAX_CAPS              256   /* 全局能力对象上限 */
#define CAP_HANDLES_PER_DOMAIN     64   /* 每域句柄表大小 */

/* ---- 能力类型 ---- */
enum cap_type {
    CAP_TYPE_DEVICE           = 0,  /* 设备（MMIO/IRQ/DMA） */
    CAP_TYPE_BUFFER           = 1,  /* 共享内存缓冲 */
    CAP_TYPE_EVENT            = 2,  /* 事件通知 */
    CAP_TYPE_FENCE            = 3,  /* 同步 fence */
    CAP_TYPE_IPC_ENDPOINT     = 4,  /* IPC 端点 */
    CAP_TYPE_SERVICE_ENDPOINT = 5,  /* 服务端点 */
    CAP_TYPE_COUNT
};

/* ---- 权利位（可组合） ---- */
#define CAP_RIGHT_READ     ((uint32_t)1U << 0)
#define CAP_RIGHT_WRITE    ((uint32_t)1U << 1)
#define CAP_RIGHT_EXECUTE  ((uint32_t)1U << 2)
#define CAP_RIGHT_DERIVE   ((uint32_t)1U << 3)  /* 允许派生（衰减） */
#define CAP_RIGHT_REVOKE   ((uint32_t)1U << 4)  /* 允许撤销该能力 */

#define CAP_RIGHT_ALL      (CAP_RIGHT_READ | CAP_RIGHT_WRITE | CAP_RIGHT_EXECUTE | \
                            CAP_RIGHT_DERIVE | CAP_RIGHT_REVOKE)

/* ---- 能力对象（显式宽度类型，32/64 通用） ---- */
typedef struct {
    int32_t  id;                  /* 全局能力 ID */
    int32_t  type;                /* cap_type */
    int32_t  owner_domain_id;     /* 创建该能力的域（mint 权持有者） */
    uint32_t base_rights;         /* 该能力的最大权利集合 */
    uint32_t generation;          /* 代际计数，revoke 时递增 */
    int32_t  revoked;             /* 是否已撤销 */
    int32_t  active;              /* 是否在用 */
    uint64_t resource_id;         /* 关联的资源标识（设备号/缓冲ID等） */
} capability_t;

/* ---- 句柄：某域对某能力的引用 ---- */
typedef struct {
    int32_t  capability_id;       /* 指向的能力 ID */
    uint32_t rights_mask;         /* 该句柄持有的权利（子集） */
    uint32_t generation;          /* 持有时的能力代际，过期则失效 */
    int32_t  active;
} handle_t;

/* ---- 句柄表：每域一份 ---- */
typedef struct {
    handle_t handles[CAP_HANDLES_PER_DOMAIN];
} handle_table_t;

/* ---- API ---- */

/* 初始化能力系统 */
void cap_init(void);

/* 创建能力（mint），返回能力 ID；失败返回 -1
 * owner_domain_id 必须为 mint 权持有者（当前为 kernel/system 域） */
int cap_mint(int owner_domain_id, int type, uint32_t base_rights, uint64_t resource_id);

/* 在 target_domain 中派生句柄（衰减：rights 必须是父权利的子集）
 * 返回句柄索引（在 target_domain 的句柄表中）；失败返回 -1 */
int cap_derive(int capability_id, int target_domain_id, uint32_t rights);

/* 撤销能力：generation++，所有句柄失效（级联） */
int cap_revoke(int capability_id);

/* 检查 domain_id 的句柄 handle_idx 是否持有 required_rights
 * 返回 1 通过，0 拒绝 */
int cap_check(int domain_id, int handle_idx, uint32_t required_rights);

/* 获取句柄对应的能力（已校验代际与撤销状态），失败返回 NULL */
capability_t *cap_get_valid(int domain_id, int handle_idx);

/* 释放某域的句柄 */
int cap_release(int domain_id, int handle_idx);

/* 统计：全局活动能力数 */
int cap_count(void);

/* 列出所有活动能力，返回数量 */
int cap_list(capability_t *out, int max);

/* 调试：列出某域的有效句柄，返回数量 */
int cap_list_domain(int domain_id, handle_t *out, int max);

#endif /* HEPU_SHARED_CAP_H */
