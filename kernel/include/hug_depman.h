#ifndef HUG_DEPMAN_H
#define HUG_DEPMAN_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS .hug 依赖与版本管理器
 * 依赖解析 / 版本控制 / 包管理 / 仓库
 * ========================================================================== */

#define HDM_MAX_PACKAGES 32
#define HDM_MAX_VERSIONS 8
#define HDM_MAX_DEPENDENCIES 16
#define HDM_NAME_LEN 128
#define HDM_VERSION_LEN 32
#define HDM_PATH_LEN 256

/* 依赖状态 */
typedef enum {
    HDM_STATUS_AVAILABLE = 0,
    HDM_STATUS_INSTALLED,
    HDM_STATUS_UPDATE_AVAILABLE,
    HDM_STATUS_BROKEN,
    HDM_STATUS_BLOCKED,
} hdm_status_t;

/* 版本约束类型 */
typedef enum {
    HDM_CONSTRAINT_EQ = 0,   /* == */
    HDM_CONSTRAINT_GE,       /* >= */
    HDM_CONSTRAINT_LE,       /* <= */
    HDM_CONSTRAINT_GT,       /* > */
    HDM_CONSTRAINT_LT,       /* < */
    HDM_CONSTRAINT_RANGE,    /* >= a, <= b */
    HDM_CONSTRAINT_ANY,      /* * */
} hdm_constraint_t;

/* 版本号 */
typedef struct {
    int major;
    int minor;
    int patch;
    int build;
    char pre_release[32];  /* alpha/beta/rc */
    char full[HDM_VERSION_LEN];
} hdm_version_t;

/* 依赖项 */
typedef struct {
    char name[HDM_NAME_LEN];
    hdm_constraint_t constraint;
    hdm_version_t min_version;
    hdm_version_t max_version;
    int optional;
    int resolved;
    int resolved_version;
} hdm_dependency_t;

/* 包信息 */
typedef struct {
    int used;
    int id;
    char name[HDM_NAME_LEN];
    char description[512];
    char author[128];
    char license[64];
    char homepage[256];
    char repository[256];
    /* 版本 */
    hdm_version_t versions[HDM_MAX_VERSIONS];
    int version_count;
    int current_version;
    int installed_version;
    /* 依赖 */
    hdm_dependency_t dependencies[HDM_MAX_DEPENDENCIES];
    int dependency_count;
    /* 状态 */
    hdm_status_t status;
    uint64_t size;
    uint64_t download_count;
    uint64_t install_count;
    int rating;
    /* 路径 */
    char install_path[HDM_PATH_LEN];
    char download_url[HDM_PATH_LEN];
    /* 校验 */
    char sha256[65];
    char signature[256];
} hdm_package_t;

/* 仓库 */
typedef struct {
    int used;
    int id;
    char name[HDM_NAME_LEN];
    char url[HDM_PATH_LEN];
    int enabled;
    int priority;
    uint64_t last_sync;
    int package_count;
} hdm_repository_t;

/* 解析结果 */
typedef struct {
    int success;
    int packages_to_install;
    int packages_to_update;
    int packages_to_remove;
    int conflicts;
    int unresolved;
    char conflict_log[4096];
} hdm_resolve_result_t;

/* 依赖管理器状态 */
typedef struct {
    int initialized;
    hdm_package_t packages[HDM_MAX_PACKAGES];
    int package_count;
    hdm_repository_t repositories[16];
    int repository_count;
    uint64_t total_installs;
    uint64_t total_updates;
} hug_depman_t;

/* 初始化 */
void hug_depman_init(void);

/* 版本操作 */
int hdm_version_parse(const char *str, hdm_version_t *ver);
int hdm_version_compare(const hdm_version_t *a, const hdm_version_t *b);
int hdm_version_to_string(const hdm_version_t *ver, char *buf, int size);
int hdm_version_satisfies(const hdm_version_t *ver, const hdm_dependency_t *dep);

/* 包管理 */
int hdm_package_register(const char *name, const char *version);
int hdm_package_get(const char *name, hdm_package_t *pkg);
int hdm_package_list(hdm_package_t *pkgs, int max);
int hdm_package_search(const char *query, hdm_package_t *results, int max);
int hdm_package_install(const char *name, const char *version);
int hdm_package_uninstall(const char *name);
int hdm_package_update(const char *name);
int hdm_package_update_all(void);
int hdm_package_get_installed(hdm_package_t *pkgs, int max);
int hdm_package_get_updates(hdm_package_t *pkgs, int max);

/* 依赖管理 */
int hdm_dependency_add(const char *package, const char *dep_name, const char *constraint);
int hdm_dependency_remove(const char *package, const char *dep_name);
int hdm_dependency_list(const char *package, hdm_dependency_t *deps, int max);
int hdm_dependency_resolve(const char *package, hdm_resolve_result_t *result);
int hdm_dependency_check(const char *package);

/* 仓库管理 */
int hdm_repo_add(const char *name, const char *url);
int hdm_repo_remove(int id);
int hdm_repo_enable(int id, int enable);
int hdm_repo_list(hdm_repository_t *repos, int max);
int hdm_repo_sync(int id);
int hdm_repo_sync_all(void);

/* 统计 */
int hdm_get_stats(hug_depman_t *stats);

#endif /* HUG_DEPMAN_H */
