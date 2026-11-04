/* ============================================================================
 * 赫普OS - 包管理器
 * .hug应用安装/卸载/更新 + 权限管理 + 应用沙箱 + 签名验证
 * ========================================================================== */

#include <stdint.h>
#include "packagemanager.h"
#include "vga.h"
#include "string.h"
static package_manager_t pm;

/* 预定义权限 */
static const char *default_permissions[] = {
    "hepu.permission.INTERNET",
    "hepu.permission.ACCESS_NETWORK_STATE",
    "hepu.permission.ACCESS_WIFI_STATE",
    "hepu.permission.BLUETOOTH",
    "hepu.permission.CAMERA",
    "hepu.permission.RECORD_AUDIO",
    "hepu.permission.READ_EXTERNAL_STORAGE",
    "hepu.permission.WRITE_EXTERNAL_STORAGE",
    "hepu.permission.READ_CONTACTS",
    "hepu.permission.WRITE_CONTACTS",
    "hepu.permission.READ_SMS",
    "hepu.permission.SEND_SMS",
    "hepu.permission.CALL_PHONE",
    "hepu.permission.READ_PHONE_STATE",
    "hepu.permission.ACCESS_FINE_LOCATION",
    "hepu.permission.ACCESS_COARSE_LOCATION",
    "hepu.permission.VIBRATE",
    "hepu.permission.WAKE_LOCK",
    "hepu.permission.RECEIVE_BOOT_COMPLETED",
    "hepu.permission.SYSTEM_ALERT_WINDOW",
};

void packagemanager_init(void)
{
    memset(&pm, 0, sizeof(package_manager_t));
    pm.initialized = 1;

    /* 注册默认权限 */
    for (int i = 0; i < 20; i++) {
        permission_level_t level = PERMISSION_NORMAL;
        if (strstr(default_permissions[i], "CAMERA") ||
            strstr(default_permissions[i], "RECORD_AUDIO") ||
            strstr(default_permissions[i], "CONTACTS") ||
            strstr(default_permissions[i], "SMS") ||
            strstr(default_permissions[i], "CALL_PHONE") ||
            strstr(default_permissions[i], "LOCATION") ||
            strstr(default_permissions[i], "STORAGE")) {
            level = PERMISSION_DANGEROUS;
        }
        pm_register_permission(default_permissions[i], level, "");
    }

    /* 预装系统应用 */
    package_info_t *pkg = &pm.packages[pm.package_count];
    memset(pkg, 0, sizeof(package_info_t));
    pkg->used = 1;
    pkg->uid = 1000;
    strcpy(pkg->name, "com.hepu.systemui");
    strcpy(pkg->version_name, "V0.15");
    pkg->version_code = 15;
    pkg->system_app = 1;
    pkg->enabled = 1;
    pkg->install_location = INSTALL_LOCATION_INTERNAL;
    strcpy(pkg->data_dir, "/data/systemui");
    strcpy(pkg->cache_dir, "/cache/systemui");
    pm.package_count++;

    pkg = &pm.packages[pm.package_count];
    memset(pkg, 0, sizeof(package_info_t));
    pkg->used = 1;
    pkg->uid = 1001;
    strcpy(pkg->name, "com.hepu.settings");
    strcpy(pkg->version_name, "V0.15");
    pkg->version_code = 15;
    pkg->system_app = 1;
    pkg->enabled = 1;
    strcpy(pkg->data_dir, "/data/settings");
    strcpy(pkg->cache_dir, "/cache/settings");
    pm.package_count++;

    pkg = &pm.packages[pm.package_count];
    memset(pkg, 0, sizeof(package_info_t));
    pkg->used = 1;
    pkg->uid = 1002;
    strcpy(pkg->name, "com.hepu.launcher");
    strcpy(pkg->version_name, "V0.15");
    pkg->version_code = 15;
    pkg->system_app = 1;
    pkg->enabled = 1;
    strcpy(pkg->data_dir, "/data/launcher");
    strcpy(pkg->cache_dir, "/cache/launcher");
    pm.package_count++;

    vga_puts("[PM] package manager init done \n");
    vga_printf(" installed: %d app, registered perm: %d \n",
               pm.package_count, pm.permission_count);
}

int pm_install(const char *package_path, install_location_t location)
{
    if (!pm.initialized || !package_path) return INSTALL_STATUS_FAILED;

    /* 原型阶段: 从路径提取包名 */
    const char *name_start = strrchr(package_path, '/');
    if (name_start) name_start++;
    else name_start = package_path;

    char pkg_name[PM_PACKAGE_NAME_LEN];
    strncpy(pkg_name, name_start, PM_PACKAGE_NAME_LEN - 1);
    /* 去掉.hug后缀 */
    char *dot = strstr(pkg_name, ".hug");
    if (dot) *dot = '\0';

    /* 检查是否已安装 */
    for (int i = 0; i < PM_MAX_PACKAGES; i++) {
        if (pm.packages[i].used && strcmp(pm.packages[i].name, pkg_name) == 0) {
            vga_printf("[PM] install failed: %s already exists \n", pkg_name);
            return INSTALL_STATUS_ALREADY_EXISTS;
        }
    }

    /* 验证签名 */
    if (pm_verify_signature(package_path) != 0) {
        return INSTALL_STATUS_SIGNATURE_MISMATCH;
    }

    /* 安装 */
    for (int i = 0; i < PM_MAX_PACKAGES; i++) {
        if (!pm.packages[i].used) {
            package_info_t *pkg = &pm.packages[i];
            memset(pkg, 0, sizeof(package_info_t));
            pkg->used = 1;
            pkg->uid = 10000 + i;
            strncpy(pkg->name, pkg_name, PM_PACKAGE_NAME_LEN - 1);
            strcpy(pkg->version_name, "1.0");
            pkg->version_code = 1;
            pkg->enabled = 1;
            pkg->system_app = 0;
            pkg->install_location = location;
            pkg->code_size = 1024 * 1024;  /* 1MB */
            pm_create_sandbox(pkg_name);
            pm.package_count++;
            vga_printf("[PM] install ok: %s(uid=%d)\n", pkg_name, pkg->uid);
            return INSTALL_STATUS_SUCCESS;
        }
    }
    return INSTALL_STATUS_FAILED;
}

int pm_uninstall(const char *package_name)
{
    if (!pm.initialized || !package_name) return -1;
    for (int i = 0; i < PM_MAX_PACKAGES; i++) {
        if (pm.packages[i].used && strcmp(pm.packages[i].name, package_name) == 0) {
            if (pm.packages[i].system_app) {
                vga_printf("[PM] uninstall failed: %s is system apps \n", package_name);
                return -1;
            }
            pm_destroy_sandbox(package_name);
            pm.packages[i].used = 0;
            pm.package_count--;
            vga_printf("[PM] uninstall ok: %s\n", package_name);
            return 0;
        }
    }
    return -1;
}

int pm_update(const char *package_path)
{
    if (!pm.initialized || !package_path) return -1;
    /* 原型阶段: 卸载后重新安装 */
    const char *name_start = strrchr(package_path, '/');
    if (name_start) name_start++;
    else name_start = package_path;

    char pkg_name[PM_PACKAGE_NAME_LEN];
    strncpy(pkg_name, name_start, PM_PACKAGE_NAME_LEN - 1);
    char *dot = strstr(pkg_name, ".hug");
    if (dot) *dot = '\0';

    /* 查找已安装版本 */
    for (int i = 0; i < PM_MAX_PACKAGES; i++) {
        if (pm.packages[i].used && strcmp(pm.packages[i].name, pkg_name) == 0) {
            pm.packages[i].version_code++;
            vga_printf("[PM] update ok: %s -> version %d\n", pkg_name, pm.packages[i].version_code);
            return 0;
        }
    }
    /* 未安装则直接安装 */
    return pm_install(package_path, INSTALL_LOCATION_INTERNAL);
}

int pm_get_package_info(const char *package_name, package_info_t *info)
{
    if (!package_name || !info) return -1;
    for (int i = 0; i < PM_MAX_PACKAGES; i++) {
        if (pm.packages[i].used && strcmp(pm.packages[i].name, package_name) == 0) {
            memcpy(info, &pm.packages[i], sizeof(package_info_t));
            return 0;
        }
    }
    return -1;
}

int pm_list_packages(package_info_t *list, int max)
{
    if (!list || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < PM_MAX_PACKAGES && count < max; i++) {
        if (pm.packages[i].used) memcpy(&list[count++], &pm.packages[i], sizeof(package_info_t));
    }
    return count;
}

int pm_list_system_apps(package_info_t *list, int max)
{
    if (!list || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < PM_MAX_PACKAGES && count < max; i++) {
        if (pm.packages[i].used && pm.packages[i].system_app) {
            memcpy(&list[count++], &pm.packages[i], sizeof(package_info_t));
        }
    }
    return count;
}

int pm_list_third_party(package_info_t *list, int max)
{
    if (!list || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < PM_MAX_PACKAGES && count < max; i++) {
        if (pm.packages[i].used && !pm.packages[i].system_app) {
            memcpy(&list[count++], &pm.packages[i], sizeof(package_info_t));
        }
    }
    return count;
}

int pm_set_enabled(const char *package_name, int enabled)
{
    if (!package_name) return -1;
    for (int i = 0; i < PM_MAX_PACKAGES; i++) {
        if (pm.packages[i].used && strcmp(pm.packages[i].name, package_name) == 0) {
            pm.packages[i].enabled = enabled ? 1 : 0;
            return 0;
        }
    }
    return -1;
}

int pm_clear_data(const char *package_name)
{
    if (!package_name) return -1;
    for (int i = 0; i < PM_MAX_PACKAGES; i++) {
        if (pm.packages[i].used && strcmp(pm.packages[i].name, package_name) == 0) {
            pm.packages[i].data_size = 0;
            vga_printf("[PM] clear data: %s\n", package_name);
            return 0;
        }
    }
    return -1;
}

int pm_clear_cache(const char *package_name)
{
    if (!package_name) return -1;
    for (int i = 0; i < PM_MAX_PACKAGES; i++) {
        if (pm.packages[i].used && strcmp(pm.packages[i].name, package_name) == 0) {
            pm.packages[i].cache_size = 0;
            return 0;
        }
    }
    return -1;
}

int pm_request_permission(const char *package_name, const char *permission)
{
    if (!package_name || !permission) return -1;
    for (int i = 0; i < PM_MAX_PACKAGES; i++) {
        if (pm.packages[i].used && strcmp(pm.packages[i].name, package_name) == 0) {
            for (int p = 0; p < pm.packages[i].permission_count; p++) {
                if (strcmp(pm.packages[i].requested_permissions[p], permission) == 0) {
                    pm.packages[i].granted_permissions[p] = 1;
                    vga_printf("[PM] grant perm: %s -> %s\n", package_name, permission);
                    return 0;
                }
            }
            /* 添加新权限 */
            if (pm.packages[i].permission_count < PKG_MAX_PERMISSIONS) {
                strncpy(pm.packages[i].requested_permissions[pm.packages[i].permission_count],
                        permission, PM_PERMISSION_NAME_LEN - 1);
                pm.packages[i].granted_permissions[pm.packages[i].permission_count] = 1;
                pm.packages[i].permission_count++;
                return 0;
            }
        }
    }
    return -1;
}

int pm_revoke_permission(const char *package_name, const char *permission)
{
    if (!package_name || !permission) return -1;
    for (int i = 0; i < PM_MAX_PACKAGES; i++) {
        if (pm.packages[i].used && strcmp(pm.packages[i].name, package_name) == 0) {
            for (int p = 0; p < pm.packages[i].permission_count; p++) {
                if (strcmp(pm.packages[i].requested_permissions[p], permission) == 0) {
                    pm.packages[i].granted_permissions[p] = 0;
                    return 0;
                }
            }
        }
    }
    return -1;
}

int pm_check_permission(const char *package_name, const char *permission)
{
    if (!package_name || !permission) return 0;
    for (int i = 0; i < PM_MAX_PACKAGES; i++) {
        if (pm.packages[i].used && strcmp(pm.packages[i].name, package_name) == 0) {
            for (int p = 0; p < pm.packages[i].permission_count; p++) {
                if (strcmp(pm.packages[i].requested_permissions[p], permission) == 0) {
                    return pm.packages[i].granted_permissions[p];
                }
            }
        }
    }
    return 0;
}

int pm_get_app_permissions(const char *package_name, char *perms[], int max)
{
    if (!package_name || !perms || max <= 0) return 0;
    for (int i = 0; i < PM_MAX_PACKAGES; i++) {
        if (pm.packages[i].used && strcmp(pm.packages[i].name, package_name) == 0) {
            int count = (pm.packages[i].permission_count < max) ? pm.packages[i].permission_count : max;
            for (int p = 0; p < count; p++) {
                perms[p] = pm.packages[i].requested_permissions[p];
            }
            return count;
        }
    }
    return 0;
}

int pm_register_permission(const char *name, permission_level_t level, const char *desc)
{
    if (!name || pm.permission_count >= PKG_MAX_PERMISSIONS) return -1;
    strncpy(pm.permissions[pm.permission_count].name, name, PM_PERMISSION_NAME_LEN - 1);
    pm.permissions[pm.permission_count].level = level;
    pm.permissions[pm.permission_count].description = desc;
    pm.permission_count++;
    return 0;
}

int pm_create_sandbox(const char *package_name)
{
    if (!package_name) return -1;
    for (int i = 0; i < PM_MAX_PACKAGES; i++) {
        if (pm.packages[i].used && strcmp(pm.packages[i].name, package_name) == 0) {
            strcpy(pm.packages[i].data_dir, "/data/");
            strncat(pm.packages[i].data_dir, package_name, 120);
            strcpy(pm.packages[i].cache_dir, "/cache/");
            strncat(pm.packages[i].cache_dir, package_name, 120);
            return 0;
        }
    }
    return -1;
}

int pm_destroy_sandbox(const char *package_name)
{
    if (!package_name) return -1;
    /* 原型阶段: 清空目录路径 */
    return 0;
}

int pm_get_data_dir(const char *package_name, char *buf, int len)
{
    if (!package_name || !buf || len <= 0) return -1;
    for (int i = 0; i < PM_MAX_PACKAGES; i++) {
        if (pm.packages[i].used && strcmp(pm.packages[i].name, package_name) == 0) {
            strncpy(buf, pm.packages[i].data_dir, len - 1);
            buf[len - 1] = '\0';
            return 0;
        }
    }
    return -1;
}

int pm_get_cache_dir(const char *package_name, char *buf, int len)
{
    if (!package_name || !buf || len <= 0) return -1;
    for (int i = 0; i < PM_MAX_PACKAGES; i++) {
        if (pm.packages[i].used && strcmp(pm.packages[i].name, package_name) == 0) {
            strncpy(buf, pm.packages[i].cache_dir, len - 1);
            buf[len - 1] = '\0';
            return 0;
        }
    }
    return -1;
}

int pm_get_storage_stats(uint64_t *total, uint64_t *used, uint64_t *free)
{
    if (total) *total = 16ULL * 1024 * 1024 * 1024;  /* 16GB */
    if (used) *used = pm.total_installed_size;
    if (free) *free = (total ? *total : 0) - (used ? *used : 0);
    return 0;
}

int pm_package_count(void) { return pm.package_count; }

int pm_system_app_count(void)
{
    int count = 0;
    for (int i = 0; i < PM_MAX_PACKAGES; i++) {
        if (pm.packages[i].used && pm.packages[i].system_app) count++;
    }
    return count;
}

int pm_third_party_count(void)
{
    int count = 0;
    for (int i = 0; i < PM_MAX_PACKAGES; i++) {
        if (pm.packages[i].used && !pm.packages[i].system_app) count++;
    }
    return count;
}

int pm_verify_signature(const char *package_path)
{
    (void)package_path;
    /* 原型阶段: 所有包签名验证通过 */
    return 0;
}

int pm_get_status(package_manager_t *status)
{
    if (!status) return -1;
    memcpy(status, &pm, sizeof(package_manager_t));
    return 0;
}
