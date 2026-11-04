/* ============================================================================
 * 赫普OS - 包管理器 (Hepu Package Manager)
 * V8.53: 初始实现
 * 包格式: .hpkg (Hepu Package) - tar+gzip压缩，含meta.ini和文件清单
 * ========================================================================== */

#ifndef _HPKG_H
#define _HPKG_H

#include <stdint.h>

/* 包管理器版本 */
#define HPKG_VERSION          "1.0"
#define HPKG_MAGIC            "HPKG"
#define HPKG_MAGIC_LEN        4

/* 最大限制 */
#define HPKG_MAX_NAME_LEN     64
#define HPKG_MAX_VERSION_LEN  32
#define HPKG_MAX_DEPENDS      16
#define HPKG_MAX_FILES        256
#define HPKG_MAX_PATH_LEN     256
#define HPKG_DB_MAX_PACKAGES  128

/* 包状态 */
typedef enum {
    HPKG_STATUS_NOT_INSTALLED = 0,
    HPKG_STATUS_INSTALLED,
    HPKG_STATUS_BROKEN,
    HPKG_STATUS_UPDATE_AVAILABLE
} hpkg_status_t;

/* 依赖关系 */
typedef struct {
    char name[HPKG_MAX_NAME_LEN];
    char min_version[HPKG_MAX_VERSION_LEN];
    char max_version[HPKG_MAX_VERSION_LEN];
} hpkg_depend_t;

/* 文件清单项 */
typedef struct {
    char path[HPKG_MAX_PATH_LEN];
    uint32_t size;
    uint32_t checksum;  /* 简单校验和 */
    uint8_t  is_dir;
    uint8_t  is_config; /* 配置文件，卸载时保留 */
} hpkg_file_t;

/* 包元数据 */
typedef struct {
    char name[HPKG_MAX_NAME_LEN];
    char version[HPKG_MAX_VERSION_LEN];
    char description[128];
    char author[64];
    char license[32];
    uint32_t install_size;  /* 安装后大小(字节) */
    uint32_t file_count;
    hpkg_depend_t depends[HPKG_MAX_DEPENDS];
    int depend_count;
} hpkg_meta_t;

/* 已安装包记录（存储在数据库中） */
typedef struct {
    uint8_t  used;
    hpkg_meta_t meta;
    hpkg_status_t status;
    uint32_t install_time;  /* 安装时间戳 */
    uint32_t file_count;
    hpkg_file_t files[HPKG_MAX_FILES];
} hpkg_installed_t;

/* 包数据库 */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t package_count;
    hpkg_installed_t packages[HPKG_DB_MAX_PACKAGES];
} hpkg_database_t;

/* 函数声明 */

/* 初始化包管理器（加载数据库） */
int hpkg_init(void);

/* 解析包元数据（从meta.ini字符串） */
int hpkg_parse_meta(const char *ini_data, uint32_t ini_len, hpkg_meta_t *meta);

/* 安装包（从.hpkg文件路径） */
int hpkg_install(const char *package_path);

/* 卸载包（按名称） */
int hpkg_remove(const char *package_name, int remove_configs);

/* 更新包（先卸载再安装新版本） */
int hpkg_update(const char *package_path);

/* 查询包信息 */
int hpkg_query(const char *package_name, hpkg_installed_t *out);

/* 列出所有已安装包 */
int hpkg_list(hpkg_installed_t *out_list, int max_count);

/* 检查包是否已安装 */
int hpkg_is_installed(const char *package_name);

/* 检查依赖是否满足 */
int hpkg_check_depends(const hpkg_meta_t *meta, char *missing_list, int list_size);

/* 保存数据库到磁盘 */
int hpkg_save_database(void);

/* 加载数据库从磁盘 */
int hpkg_load_database(void);

/* 获取包管理器状态字符串 */
const char *hpkg_status_str(hpkg_status_t status);

#endif /* _HPKG_H */
