#ifndef SECURITY_H
#define SECURITY_H

#include <stdint.h>

#define SEC_MAX_DOMAINS 16
#define SEC_MAX_TYPES 32
#define SEC_MAX_RULES 128
#define SEC_MAX_APPS 16
#define SEC_MAX_CLASSES 16

/* 安全域 (类似SELinux的domain) */
typedef struct {
    char name[32];
    int used;
} sec_domain_t;

/* 安全类型 (类似SELinux的type) */
typedef struct {
    char name[32];
    int used;
} sec_type_t;

/* 对象类别 */
typedef enum {
    SEC_CLASS_FILE = 0,
    SEC_CLASS_DIR,
    SEC_CLASS_PROCESS,
    SEC_CLASS_SOCKET,
    SEC_CLASS_DEVICE,
    SEC_CLASS_IPC,
    SEC_CLASS_MEMORY,
} sec_object_class_t;

/* 访问权限位 */
#define SEC_PERM_READ    0x01
#define SEC_PERM_WRITE   0x02
#define SEC_PERM_EXECUTE 0x04
#define SEC_PERM_APPEND  0x08
#define SEC_PERM_CREATE  0x10
#define SEC_PERM_DELETE  0x20
#define SEC_PERM_IOCTL   0x40
#define SEC_PERM_ALL     0xFF

/* 访问控制规则 (allow rule) */
typedef struct {
    int used;
    uint8_t source_domain;   /* 源域索引 */
    uint8_t target_type;     /* 目标类型索引 */
    uint8_t object_class;    /* 对象类别 */
    uint8_t permissions;     /* 权限位 */
} sec_rule_t;

/* 安全上下文 */
typedef struct {
    uint8_t domain;   /* 域索引 */
    uint8_t type;     /* 类型索引 */
    uint8_t level;    /* 安全级别 (MLS) */
    uint8_t category; /* 类别 */
} sec_context_t;

/* 初始化安全模块 */
void sec_init(void);

/* 注册域 */
int sec_register_domain(const char *name);

/* 注册类型 */
int sec_register_type(const char *name);

/* 添加允许规则 */
int sec_add_rule(const char *source_domain, const char *target_type,
                 const char *object_class, uint8_t permissions);

/* 检查访问权限 */
int sec_check_access(sec_context_t *subject, sec_context_t *object,
                     sec_object_class_t class, uint8_t requested_perm);

/* 获取域索引 */
int sec_get_domain(const char *name);

/* 获取类型索引 */
int sec_get_type(const char *name);

/* 设置进程安全上下文 */
int sec_set_process_context(int pid, sec_context_t *ctx);

/* 获取进程安全上下文 */
int sec_get_process_context(int pid, sec_context_t *ctx);

/* 设置文件安全上下文 */
int sec_set_file_context(const char *path, sec_context_t *ctx);

/* 获取文件安全上下文 */
int sec_get_file_context(const char *path, sec_context_t *ctx);

/* 加载安全策略 */
int sec_load_policy(const char *policy);

/* 切换执行模式 (enforcing/permissive/disabled) */
void sec_set_enforcing(int mode);

/* 获取执行模式 */
int sec_get_enforcing(void);

/* 审计日志 */
void sec_audit(const char *action, const char *subject, const char *object,
               const char *result);

/* ---- V2.63: 查询与便捷接口 ---- */
const char *sec_get_domain_name(int idx);
const char *sec_get_type_name(int idx);
int sec_get_domain_count(void);
int sec_get_type_count(void);
int sec_get_rule_count(void);
int sec_get_rule(int i, sec_rule_t *out);
/* 按 PID 访问文件路径的 MAC 检查 */
int sec_check_pid_file(int pid, const char *path, uint8_t perm);
/* MAC 自测 */
void sec_selftest(void);

/* ---- V2.64: 文件访问 MAC 接入 ---- */
/* 文件路径 -> 安全类型 (系统=system_t, /data=app_data_t, 默认=public_t) */
int sec_path_type(const char *path, const char *name);
/* 文件访问门卫 (以 untrusted_app 域), 返回 1=允许 0=拒绝 */
int sec_guard_file(const char *dirpath, const char *name, uint8_t perm);

/* ---- V2.65: 应用沙箱 per-app 域 ---- */
int sec_register_app(const char *name, const char *domain_name,
                     const char *data_type_name);
int sec_app_data_type(const char *name);
int sec_set_active_app(const char *name);
const char *sec_get_active_app(void);
int sec_check_active_file(const char *path, const char *name, uint8_t perm);

#endif /* SECURITY_H */
