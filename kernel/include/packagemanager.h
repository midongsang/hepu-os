#ifndef PACKAGEMANAGER_H
#define PACKAGEMANAGER_H

#include <stdint.h>

#define PM_MAX_PACKAGES 128
#define PKG_MAX_PERMISSIONS 32
#define PM_PACKAGE_NAME_LEN 64
#define PM_PERMISSION_NAME_LEN 64
#define PM_VERSION_NAME_LEN 32

/* 安装位置 */
typedef enum {
    INSTALL_LOCATION_INTERNAL = 0,
    INSTALL_LOCATION_EXTERNAL,
    INSTALL_LOCATION_AUTO,
} install_location_t;

/* 安装状态 */
typedef enum {
    INSTALL_STATUS_SUCCESS = 0,
    INSTALL_STATUS_ALREADY_EXISTS,
    INSTALL_STATUS_INVALID_PACKAGE,
    INSTALL_STATUS_INSUFFICIENT_STORAGE,
    INSTALL_STATUS_PERMISSION_DENIED,
    INSTALL_STATUS_SIGNATURE_MISMATCH,
    INSTALL_STATUS_VERSION_DOWNGRADE,
    INSTALL_STATUS_FAILED,
} install_status_t;

/* 权限保护级别 */
typedef enum {
    PERMISSION_NORMAL = 0,
    PERMISSION_DANGEROUS,
    PERMISSION_SIGNATURE,
    PERMISSION_SIGNATURE_OR_SYSTEM,
} permission_level_t;

/* 权限定义 */
typedef struct {
    char name[PM_PERMISSION_NAME_LEN];
    permission_level_t level;
    const char *description;
} permission_def_t;

/* 已安装包信息 */
typedef struct {
    int used;
    int uid;
    char name[PM_PACKAGE_NAME_LEN];
    char version_name[PM_VERSION_NAME_LEN];
    int version_code;
    char installer[PM_PACKAGE_NAME_LEN];
    uint64_t install_time;
    uint64_t update_time;
    int enabled;
    int system_app;
    install_location_t install_location;
    /* 权限 */
    char requested_permissions[PKG_MAX_PERMISSIONS][PM_PERMISSION_NAME_LEN];
    int permission_count;
    int granted_permissions[PKG_MAX_PERMISSIONS];
    /* 沙箱 */
    char data_dir[128];
    char cache_dir[128];
    uint64_t data_size;
    uint64_t cache_size;
    uint64_t code_size;
    /* 签名 */
    uint8_t signature[32];
    int signature_len;
} package_info_t;

/* 包管理器状态 */
typedef struct {
    int initialized;
    package_info_t packages[PM_MAX_PACKAGES];
    int package_count;
    permission_def_t permissions[PKG_MAX_PERMISSIONS];
    int permission_count;
    uint64_t total_installed_size;
} package_manager_t;

/* 初始化包管理器 */
void packagemanager_init(void);

/* 安装包 */
int pm_install(const char *package_path, install_location_t location);

/* 卸载包 */
int pm_uninstall(const char *package_name);

/* 更新包 */
int pm_update(const char *package_path);

/* 查询已安装包 */
int pm_get_package_info(const char *package_name, package_info_t *info);

/* 列出所有已安装包 */
int pm_list_packages(package_info_t *list, int max);

/* 列出系统应用 */
int pm_list_system_apps(package_info_t *list, int max);

/* 列出第三方应用 */
int pm_list_third_party(package_info_t *list, int max);

/* 启用/禁用包 */
int pm_set_enabled(const char *package_name, int enabled);

/* 清除数据 */
int pm_clear_data(const char *package_name);

/* 清除缓存 */
int pm_clear_cache(const char *package_name);

/* 权限管理 */
int pm_request_permission(const char *package_name, const char *permission);
int pm_revoke_permission(const char *package_name, const char *permission);
int pm_check_permission(const char *package_name, const char *permission);
int pm_get_app_permissions(const char *package_name, char *perms[], int max);

/* 注册权限定义 */
int pm_register_permission(const char *name, permission_level_t level, const char *desc);

/* 沙箱管理 */
int pm_create_sandbox(const char *package_name);
int pm_destroy_sandbox(const char *package_name);
int pm_get_data_dir(const char *package_name, char *buf, int len);
int pm_get_cache_dir(const char *package_name, char *buf, int len);

/* 存储统计 */
int pm_get_storage_stats(uint64_t *total, uint64_t *used, uint64_t *free);

/* 获取包数量 */
int pm_package_count(void);
int pm_system_app_count(void);
int pm_third_party_count(void);

/* 验证包签名 */
int pm_verify_signature(const char *package_path);

/* 获取包管理器状态 */
int pm_get_status(package_manager_t *status);

#endif /* PACKAGEMANAGER_H */
