/* ============================================================================
 * 赫普OS - Security 统一接口（32/64位共用）
 *
 * 路线C 分层裁决：
 *   粗粒度层：域 × 类型 × 规则矩阵（来自 32 位 security.h）
 *   细粒度层：能力句柄 + 权利位（来自 shared/capability/hepu_cap.h）
 *   裁决顺序：先过域规则 → 再过能力校验 → 都通过才允许
 *
 * 以 32 位 security.h 为粗粒度层真源，结构体字段全部显式宽度类型。
 * ========================================================================== */

#ifndef HEPU_SHARED_SECURITY_H
#define HEPU_SHARED_SECURITY_H

#include <stdint.h>
#include "shared/capability/hepu_cap.h"

/* ---- 常量 ---- */
#define SEC_MAX_DOMAINS  16
#define SEC_MAX_TYPES    32
#define SEC_MAX_RULES    128
#define SEC_MAX_APPS     16
#define SEC_MAX_CLASSES  16

/* ---- 安全域（域规则层用，与 domain_t 分离但可映射） ---- */
typedef struct {
    char   name[32];
    int32_t used;
} sec_domain_t;

/* ---- 安全类型 ---- */
typedef struct {
    char   name[32];
    int32_t used;
} sec_type_t;

/* ---- 对象类别（域规则层） ---- */
typedef enum {
    SEC_CLASS_FILE    = 0,
    SEC_CLASS_DIR     = 1,
    SEC_CLASS_PROCESS = 2,
    SEC_CLASS_SOCKET  = 3,
    SEC_CLASS_DEVICE  = 4,
    SEC_CLASS_IPC     = 5,
    SEC_CLASS_MEMORY  = 6,
    SEC_CLASS_SYSFS   = 7,
    SEC_CLASS_COUNT
} sec_object_class_t;

/* ---- 访问权限位（域规则层） ---- */
#define SEC_PERM_READ     ((uint8_t)0x01)
#define SEC_PERM_WRITE    ((uint8_t)0x02)
#define SEC_PERM_EXECUTE  ((uint8_t)0x04)
#define SEC_PERM_APPEND   ((uint8_t)0x08)
#define SEC_PERM_CREATE   ((uint8_t)0x10)
#define SEC_PERM_DELETE   ((uint8_t)0x20)
#define SEC_PERM_IOCTL    ((uint8_t)0x40)
#define SEC_PERM_ALL      ((uint8_t)0xFF)

/* ---- 资源类别（细粒度层，与 cap_type 对齐） ---- */
enum sec_resource_class {
    SEC_RES_DEVICE  = 0,
    SEC_RES_BUFFER  = 1,
    SEC_RES_IPC     = 2,
    SEC_RES_SERVICE = 3,
    SEC_RES_COUNT
};

/* ---- 访问控制规则 ---- */
typedef struct {
    int32_t  used;
    uint8_t  source_domain;   /* 源域索引 */
    uint8_t  target_type;     /* 目标类型索引 */
    uint8_t  object_class;    /* 对象类别 */
    uint8_t  permissions;     /* 权限位（SEC_PERM_*） */
} sec_rule_t;

/* ---- 安全上下文（域规则层：域+类型+MLS） ---- */
typedef struct {
    uint8_t domain;
    uint8_t type;
    uint8_t level;
    uint8_t category;
} sec_context_t;

/* ---- 统一裁决结果 ---- */
typedef enum {
    SEC_OK         = 0,   /* 两层都通过 */
    SEC_DENY_RULE  = 1,   /* 域规则拒绝 */
    SEC_DENY_CAP   = 2,   /* 能力校验拒绝 */
    SEC_DISABLED   = 3    /* 裁决层禁用 */
} sec_result_t;

/* ---- 域规则层 API ---- */

void sec_init(void);

int sec_register_domain(const char *name);
int sec_register_type(const char *name);
int sec_add_rule(const char *source_domain, const char *target_type,
                 const char *object_class, uint8_t permissions);

/* 粗粒度：仅域规则层裁决 */
int sec_check_access(sec_context_t *subject, sec_context_t *object,
                     sec_object_class_t class, uint8_t requested_perm);

int sec_get_domain(const char *name);
int sec_get_type(const char *name);

int sec_set_process_context(int pid, sec_context_t *ctx);
int sec_get_process_context(int pid, sec_context_t *ctx);
int sec_set_file_context(const char *path, sec_context_t *ctx);
int sec_get_file_context(const char *path, sec_context_t *ctx);

int sec_get_domain_count(void);
int sec_get_type_count(void);
int sec_get_rule_count(void);

const char *sec_get_domain_name(int idx);
const char *sec_get_type_name(int idx);

void sec_set_enforcing(int mode);
int  sec_get_enforcing(void);

void sec_audit(const char *action, const char *subject, const char *object,
               const char *result);

/* ---- 路线C 双关裁决 ---- */

/* 统一裁决：先域规则，后能力。返回 sec_result_t。
 * domain_id 是细粒度层能力检查的域标识。 */
sec_result_t sec_check_both(sec_context_t *subject, sec_context_t *object,
                            sec_object_class_t cls, uint8_t req_perm,
                            int domain_id, enum sec_resource_class res_cls,
                            uint32_t req_rights);

/* 便捷：只过域规则（不含能力检查） */
int sec_rule_only(sec_context_t *subject, sec_context_t *object,
                  sec_object_class_t cls, uint8_t req_perm);

/* 细粒度能力检查：domain_id + 资源类别 + 请求权利 */
int sec_cap_check(int domain_id, enum sec_resource_class cls,
                  int resource_idx, uint32_t req_rights);

/* 便捷：只过能力（不含域规则） */
int sec_cap_only(int domain_id, enum sec_resource_class res_cls,
                 uint32_t req_rights);

/* ---- Per-app 沙箱接口（跨架构统一） ---- */

/* 注册应用：自动注册其域/数据类型/MAC 规则
 * 成功返回 app 索引，失败返回 -1 */
int sec_register_app(const char *name, const char *domain_name,
                     const char *data_type_name);

/* 应用名 → 数据目录类型索引 */
int sec_app_data_type(const char *name);

/* 切换当前活动应用 */
int sec_set_active_app(const char *name);
const char *sec_get_active_app(void);

/* 文件路径 → 安全类型（系统=system_t, /data/<app>=对应 app_datatype, 默认=public_t） */
int sec_path_type(const char *path, const char *name);

/* 文件访问门卫（以 untrusted_app 域），返回 1=允许 0=拒绝 */
int sec_guard_file(const char *dirpath, const char *name, uint8_t perm);

/* 活动应用上下文的文件检查 */
int sec_check_active_file(const char *path, const char *name, uint8_t perm);

/* per-app 沙箱表（跨架构统一） */
extern char   sec_app_names[SEC_MAX_APPS][24];
extern uint8_t sec_app_domains[SEC_MAX_APPS];
extern uint8_t sec_app_datatypes[SEC_MAX_APPS];
extern int    sec_app_used[SEC_MAX_APPS];
extern char   sec_active_app[24];
extern int    sec_active_app_idx;

#endif /* HEPU_SHARED_SECURITY_H */
