/* ============================================================================
 * 赫普OS - 安全子系统：应用沙箱
 * 从 security.c 拆分（V6.46 代码重构，大文件拆分目标<500行）
 * 包含：MAC自测、应用注册/激活、应用数据类型、活动应用文件访问检查
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "security.h"
#include "vga.h"
#include "permission_manager.h"

/* 依赖 security.c 中的内部变量 (不在 security.h 中声明) */
extern char sec_app_names[][24];
extern uint8_t sec_app_domains[];
extern uint8_t sec_app_datatypes[];
extern int sec_app_used[];
extern char active_app[];
extern int active_app_idx;
extern sec_domain_t domains[];
extern sec_type_t types[];
extern int enforcing;

void sec_selftest(void)
{
    sec_context_t app, untrusted, sysapp, pub, appdata, sysdata, priv;
    memset(&app, 0, sizeof(app));
    memset(&untrusted, 0, sizeof(untrusted));
    memset(&sysapp, 0, sizeof(sysapp));
    memset(&pub, 0, sizeof(pub));
    memset(&appdata, 0, sizeof(appdata));
    memset(&sysdata, 0, sizeof(sysdata));
    memset(&priv, 0, sizeof(priv));

    app.domain = sec_get_domain("app");
    untrusted.domain = sec_get_domain("untrusted_app");
    sysapp.domain = sec_get_domain("system_app");
    pub.type = sec_get_type("public_t");
    appdata.type = sec_get_type("app_data_t");
    sysdata.type = sec_get_type("system_t");
    priv.type = sec_get_type("private_t");

    int pass = 0, fail = 0;
    /* 1. app 读 public -> ALLOW */
    if (sec_check_access(&app, &pub, SEC_CLASS_FILE, SEC_PERM_READ)) pass++; else fail++;
    /* 2. untrusted_app 读 public -> ALLOW */
    if (sec_check_access(&untrusted, &pub, SEC_CLASS_FILE, SEC_PERM_READ)) pass++; else fail++;
    /* 3. untrusted_app 写 app_data -> DENY (应用隔离) */
    if (!sec_check_access(&untrusted, &appdata, SEC_CLASS_FILE, SEC_PERM_WRITE)) pass++; else fail++;
    /* 4. untrusted_app 写 system_t -> DENY */
    if (!sec_check_access(&untrusted, &sysdata, SEC_CLASS_FILE, SEC_PERM_WRITE)) pass++; else fail++;
    /* 5. system_app 读写 system_t -> ALLOW */
    if (sec_check_access(&sysapp, &sysdata, SEC_CLASS_FILE, SEC_PERM_READ | SEC_PERM_WRITE)) pass++; else fail++;
    /* 6. untrusted_app 读 private_t -> DENY (私有数据隔离) */
    if (!sec_check_access(&untrusted, &priv, SEC_CLASS_FILE, SEC_PERM_READ)) pass++; else fail++;

    /* 7. permissive 模式: 被拒绝场景(3)变允许并记录审计 */
    int saved = enforcing;
    sec_set_enforcing(0);
    if (sec_check_access(&untrusted, &appdata, SEC_CLASS_FILE, SEC_PERM_WRITE)) pass++; else fail++;
    sec_set_enforcing(saved);

    /* V2.64: 8. 文件访问门卫——第三方读公共文件 ALLOW */
    if (sec_guard_file("/", "hello.txt", 0x01)) pass++; else fail++;
    /* V2.64: 9. 文件访问门卫——第三方读系统文件(lost+found) DENY */
    if (!sec_guard_file("/", "lost+found", 0x01)) pass++; else fail++;

    /* V2.65/2.68: 10. 显式撤销 STORAGE -> 浏览器读自己数据 DENY (权限联动, 与磁盘恢复态无关) */
    sec_set_active_app("browser");
    {
        int pid = prm_app_register("browser", "com.hepu.browser");
        prm_revoke_permission(pid, PM_PERM_STORAGE);
        if (!sec_check_active_file("/data/browser/", "f", 0x01)) pass++; else fail++;
        /* V2.68: 11. 授予 STORAGE 权限后 -> ALLOW */
        prm_grant_permission(pid, PM_PERM_STORAGE, PM_GRANT_ALLOWED);
        if (sec_check_active_file("/data/browser/", "f", 0x01)) pass++; else fail++;
    }
    /* V2.65: 12. 浏览器沙箱读计算器数据 DENY (per-app 隔离) */
    if (!sec_check_active_file("/data/calc/", "f", 0x01)) pass++; else fail++;
    /* V2.65: 13. 浏览器沙箱写系统文件 DENY */
    if (!sec_check_active_file("/", "config.txt", 0x02)) pass++; else fail++;
    /* V2.65: 14. 无活动应用(严格 untrusted_app)读公共文件 ALLOW */
    sec_set_active_app(0);
    if (sec_check_active_file("/", "hello.txt", 0x01)) pass++; else fail++;

    /* V2.73: 15. 联系人数据域——browser 无 CONTACTS 权限 -> DENY */
    sec_set_active_app("browser");
    {
        int pid = prm_app_register("browser", "com.hepu.browser");
        prm_revoke_permission(pid, PM_PERM_CONTACTS);
        if (!sec_check_active_file("/data/contacts/", "f", 0x01)) pass++; else fail++;
        /* V2.73: 16. 授予 CONTACTS -> ALLOW */
        prm_grant_permission(pid, PM_PERM_CONTACTS, PM_GRANT_ALLOWED);
        if (sec_check_active_file("/data/contacts/", "f", 0x01)) pass++; else fail++;
        prm_revoke_permission(pid, PM_PERM_CONTACTS);
    }

    vga_printf("[SEC-SELFTEST] MAC: %d pass, %d fail\n", pass, fail);
    serial_puts(0x3F8, "[SEC-SELFTEST] MAC: ");
    serial_puts(0x3F8, pass == 16 ? "16/16 PASS" : "FAIL");
    serial_puts(0x3F8, "\n");
}

/* ============================================================================
 * V2.65: 应用沙箱——per-app 独立安全域 + 数据目录隔离
 * 每个应用注册独立域 + 独立数据类型; 启动应用时切换"当前活动应用",
 * 文件访问按活动应用域强制检查 (无活动应用 -> 严格 untrusted_app).
 * ========================================================================== */

/* 注册应用: 自动注册其域/数据类型的 MAC 规则 (域->自己数据 rw + public r) */
int sec_register_app(const char *name, const char *domain_name,
                     const char *data_type_name)
{
    if (!name || !domain_name || !data_type_name) return -1;
    /* 已注册 -> 直接返回 */
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

/* 应用名 -> 数据目录类型 (未注册 -> -1) */
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

/* 切换当前活动应用 (进入某应用时调用); 返回 0 成功 */
int sec_set_active_app(const char *name)
{
    if (!name) { active_app_idx = -1; active_app[0] = '\0'; return -1; }
    for (int i = 0; i < SEC_MAX_APPS; i++) {
        if (sec_app_used[i] && strcmp(sec_app_names[i], name) == 0) {
            active_app_idx = i;
            strncpy(active_app, name, 23);
            active_app[23] = '\0';
            return 0;
        }
    }
    return -1;
}

const char *sec_get_active_app(void)
{
    return active_app;
}

/* V2.68/2.73: 判断类型是否为数据目录类型 (per-app 数据 + 敏感数据域) */
static int sec_is_app_data_type(uint8_t t)
{
    for (int i = 0; i < SEC_MAX_APPS; i++) {
        if (sec_app_used[i] && sec_app_datatypes[i] == t) return 1;
    }
    /* V2.73: 联系人/短信敏感数据域同样受权限联动门控 */
    if (t == sec_get_type("contact_t")) return 1;
    if (t == sec_get_type("sms_data_t")) return 1;
    return 0;
}

/* V2.68: 活动应用对应的权限管理器 id (package 与 mobile 端 com.hepu.<app> 一致) */
static int sec_perm_id_for_active(void)
{
    if (active_app_idx < 0 || !sec_app_used[active_app_idx]) return -1;
    char pkg[32];
    int i = 0;
    const char *pre = "com.hepu.";
    while (pre[i]) { pkg[i] = pre[i]; i++; }
    const char *nm = sec_app_names[active_app_idx];
    int j = 0;
    while (nm[j] && i < 30) { pkg[i++] = nm[j++]; }
    pkg[i] = '\0';
    return prm_app_register(sec_app_names[active_app_idx], pkg);
}

/* 按当前活动应用域检查文件访问 (无活动应用 -> untrusted_app 严格模式)
 * V2.68: 数据目录访问额外要求 STORAGE 权限 (权限->MAC 联动) */
int sec_check_active_file(const char *path, const char *name, uint8_t perm)
{
    sec_context_t subj, obj;
    memset(&subj, 0, sizeof(subj));
    memset(&obj, 0, sizeof(obj));
    if (active_app_idx >= 0 && sec_app_used[active_app_idx]) {
        subj.domain = sec_app_domains[active_app_idx];
    } else {
        subj.domain = sec_get_domain("untrusted_app");
    }
    obj.type = sec_path_type(path, name);
    obj.level = 0;
    obj.category = 0;

    /* V2.73: 数据域 -> 所需权限映射 (联系人/短信精确权限; 其余应用数据=STORAGE) */
    if ((perm & (SEC_PERM_READ | SEC_PERM_WRITE)) &&
        sec_is_app_data_type(obj.type)) {
        int pid = sec_perm_id_for_active();
        int granted = 0;
        int need = PM_PERM_STORAGE;
        const char *need_name = "storage";
        if (obj.type == sec_get_type("contact_t")) {
            need = PM_PERM_CONTACTS;
            need_name = "contacts";
        } else if (obj.type == sec_get_type("sms_data_t")) {
            need = PM_PERM_SMS;
            need_name = "sms";
        }
        if (pid >= 0) prm_check_permission(pid, need, &granted);
        if (!granted) {
            sec_audit("PERM-DENY", domains[subj.domain].name,
                      types[obj.type].name, need_name);
            return 0;
        }
        /* V2.73: 权限已授予后——仅"自己数据域"或"敏感数据域"放行;
         * 其他应用数据域仍落 MAC 规则 (per-app 隔离不被通用权限绕过) */
        if (obj.type == sec_get_type("contact_t") ||
            obj.type == sec_get_type("sms_data_t") ||
            (active_app_idx >= 0 && sec_app_used[active_app_idx] &&
             obj.type == sec_app_datatypes[active_app_idx])) {
            return 1;
        }
    }
    return sec_check_access(&subj, &obj, SEC_CLASS_FILE, perm);
}
