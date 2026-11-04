/* ============================================================================
 * 赫普OS 64位 - 安全裁决层实现
 *
 * 路线C 双关裁决：
 *   粗粒度层（域规则矩阵） + 细粒度层（能力句柄）
 *   裁决顺序：先域规则 → 后能力
 *
 * 域规则矩阵从 32 位 security.c 精简移植：7域 × 9类型 × 12规则
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "security64.h"

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);

/* ---- 域规则层数据 ---- */
static sec_domain_t  g_sec_domains[SEC_MAX_DOMAINS];
static sec_type_t    g_sec_types[SEC_MAX_TYPES];
static sec_rule_t    g_sec_rules[SEC_MAX_RULES];

static int enforcing = 1;  /* 1=enforcing, 0=permissive, -1=disabled */
static int sec_initialized = 0;

/* ---- 内部：资源类别 → 能力类型 ---- */
static int sec_res_to_cap_type(enum sec_resource_class cls)
{
    switch (cls) {
        case SEC_RES_DEVICE:  return CAP_TYPE_DEVICE;
        case SEC_RES_BUFFER:  return CAP_TYPE_BUFFER;
        case SEC_RES_IPC:     return CAP_TYPE_IPC_ENDPOINT;
        case SEC_RES_SERVICE: return CAP_TYPE_SERVICE_ENDPOINT;
        default:              return -1;
    }
}

/* ---- 内部：找/注册域 ---- */
int sec_get_domain(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < SEC_MAX_DOMAINS; i++) {
        if (g_sec_domains[i].used && strcmp(g_sec_domains[i].name, name) == 0) return i;
    }
    return -1;
}

int sec_get_type(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < SEC_MAX_TYPES; i++) {
        if (g_sec_types[i].used && strcmp(g_sec_types[i].name, name) == 0) return i;
    }
    return -1;
}

int sec_register_domain(const char *name)
{
    int existing = sec_get_domain(name);
    if (existing >= 0) return existing;
    for (int i = 0; i < SEC_MAX_DOMAINS; i++) {
        if (!g_sec_domains[i].used) {
            strncpy(g_sec_domains[i].name, name, 31);
            g_sec_domains[i].name[31] = '\0';
            g_sec_domains[i].used = 1;
            return i;
        }
    }
    return -1;
}

int sec_register_type(const char *name)
{
    int existing = sec_get_type(name);
    if (existing >= 0) return existing;
    for (int i = 0; i < SEC_MAX_TYPES; i++) {
        if (!g_sec_types[i].used) {
            strncpy(g_sec_types[i].name, name, 31);
            g_sec_types[i].name[31] = '\0';
            g_sec_types[i].used = 1;
            return i;
        }
    }
    return -1;
}

/* 对象类别名 → idx */
static int class_to_idx(const char *name)
{
    if (!name) return -1;
    if (strcmp(name, "file") == 0)   return SEC_CLASS_FILE;
    if (strcmp(name, "dir") == 0)    return SEC_CLASS_DIR;
    if (strcmp(name, "process") == 0)return SEC_CLASS_PROCESS;
    if (strcmp(name, "socket") == 0) return SEC_CLASS_SOCKET;
    if (strcmp(name, "device") == 0) return SEC_CLASS_DEVICE;
    if (strcmp(name, "ipc") == 0)    return SEC_CLASS_IPC;
    if (strcmp(name, "memory") == 0) return SEC_CLASS_MEMORY;
    return -1;
}

int sec_add_rule(const char *source_domain, const char *target_type,
                 const char *object_class, uint8_t permissions)
{
    int src = sec_get_domain(source_domain);
    int tgt = sec_get_type(target_type);
    int cls = class_to_idx(object_class);
    if (src < 0 || tgt < 0 || cls < 0) return -1;

    for (int i = 0; i < SEC_MAX_RULES; i++) {
        if (!g_sec_rules[i].used) {
            g_sec_rules[i].used = 1;
            g_sec_rules[i].source_domain = (uint8_t)src;
            g_sec_rules[i].target_type   = (uint8_t)tgt;
            g_sec_rules[i].object_class  = (uint8_t)cls;
            g_sec_rules[i].permissions   = permissions;
            return 0;
        }
    }
    return -1;
}

/* ---- 粗粒度：仅域规则层裁决 ---- */
int sec_check_access(sec_context_t *subject, sec_context_t *object,
                     sec_object_class_t class, uint8_t requested_perm)
{
    if (!subject || !object) return 0;
    if (enforcing < 0) return 1;  /* disabled */

    for (int i = 0; i < SEC_MAX_RULES; i++) {
        if (!g_sec_rules[i].used) continue;
        if (g_sec_rules[i].source_domain == subject->domain &&
            g_sec_rules[i].target_type   == object->type &&
            g_sec_rules[i].object_class  == (uint8_t)class) {
            if ((g_sec_rules[i].permissions & requested_perm) == requested_perm) {
                return 1;  /* 允许 */
            }
        }
    }

    if (enforcing) return 0;   /* 拒绝 */
    return 1;                   /* permissive: 记录但允许 */
}

/* ---- 细粒度：仅能力句柄裁决 ---- */
int sec_cap_check(int domain_id, enum sec_resource_class cls,
                  int resource_idx, uint32_t req_rights)
{
    if (enforcing < 0) return 1;  /* disabled */

    int expected_type = sec_res_to_cap_type(cls);
    if (expected_type < 0) return 0;

    capability_t *cap = cap_get_valid(domain_id, resource_idx);
    if (!cap || cap->type != expected_type) return 0;

    return cap_check(domain_id, resource_idx, req_rights) ? 1 : 0;
}

/* ---- 路线C 双关裁决 ---- */
sec_result_t sec_check_both(sec_context_t *subject, sec_context_t *object,
                            sec_object_class_t cls, uint8_t req_perm,
                            int domain_id, enum sec_resource_class res_cls,
                            uint32_t req_rights)
{
    if (enforcing < 0) return SEC_DISABLED;

    /* 第一步：粗粒度域规则 */
    if (!sec_check_access(subject, object, cls, req_perm)) {
        return SEC_DENY_RULE;
    }

    /* 第二步：细粒度能力 */
    if (!sec_cap_check(domain_id, res_cls, /* resource_idx=0 需传实际索引 */
                       (int)0, req_rights)) {
        return SEC_DENY_CAP;
    }

    return SEC_OK;
}

/* ---- 便捷函数 ---- */
int sec_rule_only(sec_context_t *subject, sec_context_t *object,
                  sec_object_class_t cls, uint8_t req_perm)
{
    return sec_check_access(subject, object, cls, req_perm);
}

int sec_cap_only(int domain_id, enum sec_resource_class res_cls,
                 uint32_t req_rights)
{
    /* resource_idx 需调用方持有，这里设为0做最简检查 */
    return sec_cap_check(domain_id, res_cls, 0, req_rights);
}

/* ---- 审计 ---- */
void sec_audit(const char *action, const char *subject, const char *object,
               const char *result)
{
    vga_puts("[SEC-AUDIT] ");
    vga_puts(action);
    vga_puts(" s=");
    vga_puts(subject ? subject : "?");
    vga_puts(" t=");
    vga_puts(object ? object : "?");
    vga_puts(": ");
    vga_puts(result ? result : "?");
    vga_puts("\n");
}

/* ---- 64位包装审计（兼容旧 sec64_audit，保留原名不映射） ---- */
void sec64_audit(const char *action, int domain_id, const char *resource,
                 const char *result)
{
    vga_puts("[SEC64-AUDIT] ");
    vga_puts(action);
    vga_puts(" domain=");
    vga_put_hex((uint64_t)domain_id);
    vga_puts(" res=");
    vga_puts(resource ? resource : "?");
    vga_puts(": ");
    vga_puts(result ? result : "?");
    vga_puts("\n");
}

/* ---- 初始化 + 预置策略 ---- */
void sec_init(void)
{
    if (sec_initialized) return;
    memset(g_sec_domains, 0, sizeof(g_sec_domains));
    memset(g_sec_types, 0, sizeof(g_sec_types));
    memset(g_sec_rules, 0, sizeof(g_sec_rules));
    enforcing = 1;

    /* 注册 7 域（与32位 security.c 一致） */
    sec_register_domain("kernel");
    sec_register_domain("init");
    sec_register_domain("system");
    sec_register_domain("app");
    sec_register_domain("untrusted_app");
    sec_register_domain("system_app");
    sec_register_domain("third_party_app");

    /* 注册 9 类型 */
    sec_register_type("kernel_t");
    sec_register_type("init_t");
    sec_register_type("system_t");
    sec_register_type("app_data_t");
    sec_register_type("public_t");
    sec_register_type("device_t");
    sec_register_type("socket_t");
    sec_register_type("private_t");
    sec_register_type("contact_t");

    /* 预置 12 条规则（与32位一致：最小权限 + 应用隔离） */
    sec_add_rule("kernel",         "kernel_t",      "process", SEC_PERM_ALL);
    sec_add_rule("kernel",         "device_t",      "device",  SEC_PERM_ALL);
    sec_add_rule("init",           "init_t",        "process", SEC_PERM_ALL);
    sec_add_rule("system",         "system_t",      "file",    SEC_PERM_READ | SEC_PERM_WRITE);
    sec_add_rule("system",         "public_t",      "file",    SEC_PERM_READ);
    sec_add_rule("system_app",     "system_t",      "file",    SEC_PERM_READ | SEC_PERM_WRITE);
    sec_add_rule("system_app",     "app_data_t",    "file",    SEC_PERM_READ | SEC_PERM_WRITE);
    sec_add_rule("app",            "app_data_t",    "file",    SEC_PERM_READ | SEC_PERM_WRITE);
    sec_add_rule("app",            "public_t",      "file",    SEC_PERM_READ);
    sec_add_rule("untrusted_app",  "public_t",      "file",    SEC_PERM_READ);
    sec_add_rule("untrusted_app",  "contact_t",     "file",    SEC_PERM_READ);
    sec_add_rule("third_party_app","public_t",      "file",    SEC_PERM_READ);

    /* ---- Per-app 沙箱：初始化表 + 预置系统应用 ---- */
    memset(sec_app_used, 0, sizeof(sec_app_used));
    sec_active_app_idx = -1;
    sec_active_app[0] = '\0';
    sec_register_app("phone",    "system_app", "app_data_t");
    sec_register_app("sms",      "system_app", "app_data_t");
    sec_register_app("camera",   "system_app", "app_data_t");
    sec_register_app("gallery",  "system_app", "app_data_t");
    sec_register_app("terminal", "system_app", "app_data_t");

    sec_initialized = 1;
    vga_puts("[SEC64] security layer init done (route C: MAC + cap, enforcing=1)\n");
    vga_puts("  domains=7, types=9, rules=12, apps=5\n");
}

void sec_set_enforcing(int mode)
{
    enforcing = mode;
    vga_puts("[SEC64] mode: ");
    vga_puts(enforcing > 0 ? "Enforcing" : (enforcing == 0 ? "Permissive" : "Disabled"));
    vga_puts("\n");
}

int sec_get_enforcing(void) { return enforcing; }

/* ---- 查询 ---- */
const char *sec_get_domain_name(int idx)
{
    if (idx < 0 || idx >= SEC_MAX_DOMAINS || !g_sec_domains[idx].used) return "?";
    return g_sec_domains[idx].name;
}

const char *sec_get_type_name(int idx)
{
    if (idx < 0 || idx >= SEC_MAX_TYPES || !g_sec_types[idx].used) return "?";
    return g_sec_types[idx].name;
}

int sec_get_domain_count(void)
{
    int n = 0;
    for (int i = 0; i < SEC_MAX_DOMAINS; i++) if (g_sec_domains[i].used) n++;
    return n;
}

int sec_get_type_count(void)
{
    int n = 0;
    for (int i = 0; i < SEC_MAX_TYPES; i++) if (g_sec_types[i].used) n++;
    return n;
}

int sec_get_rule_count(void)
{
    int n = 0;
    for (int i = 0; i < SEC_MAX_RULES; i++) if (g_sec_rules[i].used) n++;
    return n;
}

/* ---- 进程/文件上下文（最小实现，后续完善） ---- */
static sec_context_t g_pid_ctx[64];
static int           g_pid_used[64];
static int           g_pid_pids[64];

int sec_set_process_context(int pid, sec_context_t *ctx)
{
    if (!ctx) return -1;
    for (int i = 0; i < 64; i++) {
        if (g_pid_pids[i] == pid || g_pid_pids[i] == -1) {
            g_pid_pids[i] = pid;
            g_pid_ctx[i] = *ctx;
            g_pid_used[i] = 1;
            return 0;
        }
    }
    return -1;
}

int sec_get_process_context(int pid, sec_context_t *ctx)
{
    if (!ctx) return -1;
    for (int i = 0; i < 64; i++) {
        if (g_pid_used[i] && g_pid_pids[i] == pid) {
            *ctx = g_pid_ctx[i];
            return 0;
        }
    }
    ctx->domain = sec_get_domain("untrusted_app");
    ctx->type   = sec_get_type("public_t");
    ctx->level  = 0;
    ctx->category = 0;
    return 0;
}

int sec_set_file_context(const char *path, sec_context_t *ctx) { (void)path;(void)ctx; return 0; }
int sec_get_file_context(const char *path, sec_context_t *ctx)
{
    if (!ctx) return -1; (void)path;
    ctx->domain = 0;
    ctx->type = sec_get_type("public_t");
    ctx->level = 0; ctx->category = 0;
    return 0;
}

/* ---- Per-app 沙箱表（与 hepu_security.h 一致） ---- */
char   sec_app_names[SEC_MAX_APPS][24];
uint8_t sec_app_domains[SEC_MAX_APPS];
uint8_t sec_app_datatypes[SEC_MAX_APPS];
int    sec_app_used[SEC_MAX_APPS];
char   sec_active_app[24];
int    sec_active_app_idx = -1;

/* 注册应用：自动注册域/类型/规则（自己数据 rw + public r） */
int sec_register_app(const char *name, const char *domain_name,
                     const char *data_type_name)
{
    if (!name || !domain_name || !data_type_name) return -1;

    /* 已注册 → 直接返回 */
    for (int i = 0; i < SEC_MAX_APPS; i++) {
        if (sec_app_used[i] && strcmp(sec_app_names[i], name) == 0) return i;
    }

    int d = sec_register_domain(domain_name);
    int t = sec_register_type(data_type_name);
    if (d < 0 || t < 0) return -1;

    sec_add_rule(domain_name, data_type_name, "file",
                 SEC_PERM_READ | SEC_PERM_WRITE);
    sec_add_rule(domain_name, "public_t", "file", SEC_PERM_READ);

    for (int i = 0; i < SEC_MAX_APPS; i++) {
        if (!sec_app_used[i]) {
            strncpy(sec_app_names[i], name, 23);
            sec_app_names[i][23] = '\0';
            sec_app_domains[i] = (uint8_t)d;
            sec_app_datatypes[i] = (uint8_t)t;
            sec_app_used[i] = 1;
            return i;
        }
    }
    return -1;
}

int sec_app_data_type(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < SEC_MAX_APPS; i++) {
        if (sec_app_used[i] && strcmp(sec_app_names[i], name) == 0) {
            return sec_app_datatypes[i];
        }
    }
    return -1;
}

int sec_set_active_app(const char *name)
{
    if (!name) { sec_active_app_idx = -1; sec_active_app[0] = '\0'; return -1; }
    for (int i = 0; i < SEC_MAX_APPS; i++) {
        if (sec_app_used[i] && strcmp(sec_app_names[i], name) == 0) {
            sec_active_app_idx = i;
            strncpy(sec_active_app, name, 23);
            sec_active_app[23] = '\0';
            return 0;
        }
    }
    return -1;
}

const char *sec_get_active_app(void)
{
    return sec_active_app;
}

/* 文件路径 → 安全类型（系统=system_t, /data/<app>=对应 app_datatype, 默认=public_t） */
int sec_path_type(const char *path, const char *name)
{
    if (name) {
        if (strncmp(name, "config", 6) == 0 ||
            strncmp(name, "etc", 3) == 0 ||
            strncmp(name, "system", 6) == 0 ||
            strncmp(name, "lost+found", 10) == 0) {
            return sec_get_type("system_t");
        }
    }
    if (path) {
        if (strcmp(path, "/system") == 0 || strcmp(path, "/etc") == 0) {
            return sec_get_type("system_t");
        }
        if (path[0] == '/' && path[1] == 'd' && path[2] == 'a' &&
            path[3] == 't' && path[4] == 'a' && path[5] == '/') {
            const char *app = path + 6;
            for (int i = 0; i < SEC_MAX_APPS; i++) {
                if (sec_app_used[i]) {
                    int n = (int)strlen(sec_app_names[i]);
                    if (strncmp(app, sec_app_names[i], n) == 0 &&
                        (app[n] == '/' || app[n] == '\0')) {
                        return sec_app_datatypes[i];
                    }
                }
            }
            return sec_get_type("app_data_t");
        }
    }
    return sec_get_type("public_t");
}

/* 文件访问门卫（以 untrusted_app 域），返回 1=允许 0=拒绝 */
int sec_guard_file(const char *dirpath, const char *name, uint8_t perm)
{
    sec_context_t subj, obj;
    memset(&subj, 0, sizeof(subj));
    memset(&obj, 0, sizeof(obj));
    subj.domain = sec_get_domain("untrusted_app");
    obj.type = sec_path_type(dirpath, name);
    obj.level = 0;
    obj.category = 0;
    return sec_check_access(&subj, &obj, SEC_CLASS_FILE, perm);
}

int sec_check_active_file(const char *path, const char *name, uint8_t perm)
{
    if (sec_active_app_idx < 0) return 0;
    sec_context_t subj, obj;
    memset(&subj, 0, sizeof(subj));
    memset(&obj, 0, sizeof(obj));
    subj.domain = sec_app_domains[sec_active_app_idx];
    obj.type = sec_path_type(path, name);
    return sec_check_access(&subj, &obj, SEC_CLASS_FILE, perm);
}

