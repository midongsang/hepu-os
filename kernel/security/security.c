/* ============================================================================
 * 赫普OS - 安全模块 (SELinux式强制访问控制)
 * 域/类型/规则/上下文/审计
 * ========================================================================== */

#include <stdint.h>
#include "security.h"
#include "permission_manager.h"
#include "vga.h"
#include "serial.h"
#include "string.h"

/* V2.65: per-app 沙箱表 (需在 sec_path_type 之前可见) */
char sec_app_names[SEC_MAX_APPS][24];
uint8_t sec_app_domains[SEC_MAX_APPS];
uint8_t sec_app_datatypes[SEC_MAX_APPS];
int sec_app_used[SEC_MAX_APPS];
char active_app[24];
int active_app_idx = -1;

sec_domain_t domains[SEC_MAX_DOMAINS];
sec_type_t types[SEC_MAX_TYPES];
static sec_rule_t rules[SEC_MAX_RULES];
int enforcing = 1;  /* 1=enforcing, 0=permissive, -1=disabled */
static int sec_initialized = 0;

/* 进程安全上下文表 (简化: 按PID索引) */
static sec_context_t process_contexts[64];
static int process_pids[64];

/* 文件安全上下文表 (简化: 路径哈希) */
typedef struct {
    char path[64];
    sec_context_t ctx;
    int used;
} file_ctx_t;
static file_ctx_t file_contexts[32];

void sec_init(void)
{
    for (int i = 0; i < SEC_MAX_DOMAINS; i++) domains[i].used = 0;
    for (int i = 0; i < SEC_MAX_TYPES; i++) types[i].used = 0;
    for (int i = 0; i < SEC_MAX_RULES; i++) rules[i].used = 0;
    for (int i = 0; i < 64; i++) process_pids[i] = -1;
    for (int i = 0; i < 32; i++) file_contexts[i].used = 0;

    /* V2.63: 注册默认域 (含手机应用分级) */
    sec_register_domain("kernel");        /* 内核 */
    sec_register_domain("init");          /* 初始化进程 */
    sec_register_domain("system");        /* 系统服务 */
    sec_register_domain("app");           /* 系统应用 */
    sec_register_domain("untrusted_app"); /* 第三方应用 */
    sec_register_domain("system_app");    /* V2.63: 高权限系统应用 */
    sec_register_domain("third_party_app");/* V2.63: 第三方应用独立域 */

    /* V2.63: 注册默认类型 */
    sec_register_type("kernel_t");
    sec_register_type("init_t");
    sec_register_type("system_t");
    sec_register_type("app_data_t");
    sec_register_type("public_t");
    sec_register_type("device_t");
    sec_register_type("socket_t");
    sec_register_type("private_t");   /* V2.63: 私有数据 */
    sec_register_type("contact_t");   /* V2.63: 联系人等敏感数据 */
    sec_register_type("sms_data_t");  /* V2.73: 短信数据域 */

    /* V2.63: 默认规则——最小权限 + 应用隔离 */
    sec_add_rule("kernel", "kernel_t", "process", SEC_PERM_ALL);
    sec_add_rule("kernel", "device_t", "device", SEC_PERM_ALL);
    sec_add_rule("init", "init_t", "process", SEC_PERM_ALL);
    sec_add_rule("system", "system_t", "file", SEC_PERM_READ | SEC_PERM_WRITE);
    sec_add_rule("system", "public_t", "file", SEC_PERM_READ);
    sec_add_rule("system_app", "system_t", "file", SEC_PERM_READ | SEC_PERM_WRITE);
    sec_add_rule("system_app", "app_data_t", "file", SEC_PERM_READ | SEC_PERM_WRITE);
    sec_add_rule("app", "app_data_t", "file", SEC_PERM_READ | SEC_PERM_WRITE);
    sec_add_rule("app", "public_t", "file", SEC_PERM_READ);
    sec_add_rule("untrusted_app", "public_t", "file", SEC_PERM_READ);
    sec_add_rule("untrusted_app", "contact_t", "file", SEC_PERM_READ);
    sec_add_rule("third_party_app", "public_t", "file", SEC_PERM_READ);
    /* 注意: untrusted_app / third_party_app 无 app_data_t 写权限 -> 应用隔离 */

    /* V2.65: 手机应用沙箱注册 (系统应用共享 system_app 域, 第三方应用独立域) */
    sec_register_app("phone", "system_app", "app_data_t");
    sec_register_app("sms", "system_app", "app_data_t");
    sec_register_app("camera", "system_app", "app_data_t");
    sec_register_app("gallery", "system_app", "app_data_t");
    sec_register_app("settings", "system_app", "app_data_t");
    sec_register_app("music", "system_app", "app_data_t");
    sec_register_app("browser", "browser_app", "browser_data_t");
    sec_register_app("calc", "calc_app", "calc_data_t");
    sec_register_app("terminal", "system_app", "app_data_t");

    sec_initialized = 1;
    vga_puts("[SEC] security module init done(MAC)\n");
    vga_printf(" mode: %s, domains:7, type:9, rules:12\n",
               enforcing ? "Enforcing" : "Permissive");
    serial_puts(0x3F8, "[SEC] MAC init done, domains=7 types=9 rules=12\n");
    }

int sec_register_domain(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < SEC_MAX_DOMAINS; i++) {
        if (domains[i].used && strcmp(domains[i].name, name) == 0) return i;
    }
    for (int i = 0; i < SEC_MAX_DOMAINS; i++) {
        if (!domains[i].used) {
            strncpy(domains[i].name, name, 31);
            domains[i].used = 1;
            return i;
        }
    }
    return -1;
}

int sec_register_type(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < SEC_MAX_TYPES; i++) {
        if (types[i].used && strcmp(types[i].name, name) == 0) return i;
    }
    for (int i = 0; i < SEC_MAX_TYPES; i++) {
        if (!types[i].used) {
            strncpy(types[i].name, name, 31);
            types[i].used = 1;
            return i;
        }
    }
    return -1;
}

static int class_to_idx(const char *name)
{
    if (!name) return -1;
    if (strcmp(name, "file") == 0) return SEC_CLASS_FILE;
    if (strcmp(name, "dir") == 0) return SEC_CLASS_DIR;
    if (strcmp(name, "process") == 0) return SEC_CLASS_PROCESS;
    if (strcmp(name, "socket") == 0) return SEC_CLASS_SOCKET;
    if (strcmp(name, "device") == 0) return SEC_CLASS_DEVICE;
    if (strcmp(name, "ipc") == 0) return SEC_CLASS_IPC;
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
        if (!rules[i].used) {
            rules[i].used = 1;
            rules[i].source_domain = src;
            rules[i].target_type = tgt;
            rules[i].object_class = cls;
            rules[i].permissions = permissions;
            return 0;
        }
    }
    return -1;
}

int sec_check_access(sec_context_t *subject, sec_context_t *object,
                     sec_object_class_t class, uint8_t requested_perm)
{
    if (enforcing < 0) return 1;  /* disabled: 全部允许 */
    if (!subject || !object) return 0;

    for (int i = 0; i < SEC_MAX_RULES; i++) {
        if (!rules[i].used) continue;
        if (rules[i].source_domain == subject->domain &&
            rules[i].target_type == object->type &&
            rules[i].object_class == (uint8_t)class) {
            if ((rules[i].permissions & requested_perm) == requested_perm) {
                return 1;  /* 允许 */
            }
        }
    }

    /* 拒绝 */
    if (enforcing) {
        sec_audit("DENY", domains[subject->domain].name,
                  types[object->type].name, "access denied");
        return 0;
    }
    /* permissive模式: 记录但允许 */
    sec_audit("ALLOW(permissive)", domains[subject->domain].name,
              types[object->type].name, "would be denied");
    return 1;
}

int sec_get_domain(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < SEC_MAX_DOMAINS; i++) {
        if (domains[i].used && strcmp(domains[i].name, name) == 0) return i;
    }
    return -1;
}

int sec_get_type(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < SEC_MAX_TYPES; i++) {
        if (types[i].used && strcmp(types[i].name, name) == 0) return i;
    }
    return -1;
}

int sec_set_process_context(int pid, sec_context_t *ctx)
{
    if (!ctx) return -1;
    for (int i = 0; i < 64; i++) {
        if (process_pids[i] == pid || process_pids[i] == -1) {
            process_pids[i] = pid;
            memcpy(&process_contexts[i], ctx, sizeof(sec_context_t));
            return 0;
        }
    }
    return -1;
}

int sec_get_process_context(int pid, sec_context_t *ctx)
{
    if (!ctx) return -1;
    for (int i = 0; i < 64; i++) {
        if (process_pids[i] == pid) {
            memcpy(ctx, &process_contexts[i], sizeof(sec_context_t));
            return 0;
        }
    }
    /* 默认: untrusted_app域 */
    ctx->domain = sec_get_domain("untrusted_app");
    ctx->type = sec_get_type("app_data_t");
    ctx->level = 0;
    ctx->category = 0;
    return 0;
}

int sec_set_file_context(const char *path, sec_context_t *ctx)
{
    if (!path || !ctx) return -1;
    for (int i = 0; i < 32; i++) {
        if (!file_contexts[i].used ||
            strcmp(file_contexts[i].path, path) == 0) {
            strncpy(file_contexts[i].path, path, 63);
            memcpy(&file_contexts[i].ctx, ctx, sizeof(sec_context_t));
            file_contexts[i].used = 1;
            return 0;
        }
    }
    return -1;
}

int sec_get_file_context(const char *path, sec_context_t *ctx)
{
    if (!path || !ctx) return -1;
    for (int i = 0; i < 32; i++) {
        if (file_contexts[i].used &&
            strcmp(file_contexts[i].path, path) == 0) {
            memcpy(ctx, &file_contexts[i].ctx, sizeof(sec_context_t));
            return 0;
        }
    }
    /* 默认: public_t */
    ctx->domain = 0;
    ctx->type = sec_get_type("public_t");
    ctx->level = 0;
    ctx->category = 0;
    return 0;
}

int sec_load_policy(const char *policy)
{
    (void)policy;
    /* 原型阶段: 策略在编译时内置 */
    vga_puts("[SEC] policy loaded(builtin default policy)\n");
    return 0;
}

void sec_set_enforcing(int mode)
{
    enforcing = mode;
    vga_printf("[SEC] exec mode: %s\n",
               mode > 0 ? "Enforcing" : (mode == 0 ? "Permissive" : "Disabled"));
}

int sec_get_enforcing(void)
{
    return enforcing;
}

void sec_audit(const char *action, const char *subject, const char *object,
               const char *result)
{
    vga_printf("[SEC-AUDIT] %s s=%s t=%s: %s\n", action, subject, object, result);
    serial_puts(0x3F8, "[SEC-AUDIT] ");
    serial_puts(0x3F8, action);
    serial_puts(0x3F8, " s=");
    serial_puts(0x3F8, subject);
    serial_puts(0x3F8, " t=");
    serial_puts(0x3F8, object);
    serial_puts(0x3F8, ": ");
    serial_puts(0x3F8, result);
    serial_puts(0x3F8, "\n");
}

/* ---- V2.63: 查询与便捷接口 ---- */

const char *sec_get_domain_name(int idx)
{
    if (idx < 0 || idx >= SEC_MAX_DOMAINS || !domains[idx].used) return "?";
    return domains[idx].name;
}

const char *sec_get_type_name(int idx)
{
    if (idx < 0 || idx >= SEC_MAX_TYPES || !types[idx].used) return "?";
    return types[idx].name;
}

int sec_get_domain_count(void)
{
    int n = 0;
    for (int i = 0; i < SEC_MAX_DOMAINS; i++) if (domains[i].used) n++;
    return n;
}

int sec_get_type_count(void)
{
    int n = 0;
    for (int i = 0; i < SEC_MAX_TYPES; i++) if (types[i].used) n++;
    return n;
}

int sec_get_rule_count(void)
{
    int n = 0;
    for (int i = 0; i < SEC_MAX_RULES; i++) if (rules[i].used) n++;
    return n;
}

int sec_get_rule(int i, sec_rule_t *out)
{
    if (!out || i < 0 || i >= SEC_MAX_RULES || !rules[i].used) return -1;
    memcpy(out, &rules[i], sizeof(sec_rule_t));
    return 0;
}

/* V2.63: 便捷封装——按 PID 访问文件路径的 MAC 检查 */
int sec_check_pid_file(int pid, const char *path, uint8_t perm)
{
    sec_context_t subj, obj;
    if (sec_get_process_context(pid, &subj) != 0) return 0;
    if (sec_get_file_context(path, &obj) != 0) return 0;
    return sec_check_access(&subj, &obj, SEC_CLASS_FILE, perm);
}

/* ---- V2.64: 文件访问 MAC 接入 ---- */

/* 文件路径 -> 安全类型 (原型规则: 系统路径=system_t, /data 下=app_data_t, 其余=public_t) */
int sec_path_type(const char *path, const char *name)
{
    if (name) {
        /* 前缀匹配: config.txt / etc/ 目录 / system 开头 / lost+found 均为系统文件 */
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
            /* V2.73: 敏感数据域——联系人/短信 (优先于 per-app) */
            if (strncmp(app, "contacts", 8) == 0 &&
                (app[8] == '/' || app[8] == '\0')) {
                return sec_get_type("contact_t");
            }
            if (strncmp(app, "sms", 3) == 0 &&
                (app[3] == '/' || app[3] == '\0')) {
                return sec_get_type("sms_data_t");
            }
            /* V2.65: per-app 数据目录 /data/<appid>/ -> 对应应用数据类型 */
            for (int i = 0; i < SEC_MAX_APPS; i++) {
                if (sec_app_used[i]) {
                    int n = strlen(sec_app_names[i]);
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

/* 文件访问门卫: 以 untrusted_app 域访问文件 (第三方应用场景) */
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

/* 应用沙箱功能已拆分到 security_app.c */
