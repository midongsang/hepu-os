#ifndef HUG_SDK_H
#define HUG_SDK_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS .hug SDK 核心
 * 项目管理 / 构建 / 打包 / 签名
 * ========================================================================== */

#define HSDK_MAX_PROJECTS 8
#define HSDK_MAX_SOURCES 32
#define HSDK_MAX_RESOURCES 16
#define HSDK_MAX_PERMISSIONS 16
#define HSDK_NAME_LEN 128
#define HSDK_PATH_LEN 256
#define HSDK_VERSION_LEN 32

/* 项目类型 */
typedef enum {
    HSDK_TYPE_APP = 0,      /* 普通应用 */
    HSDK_TYPE_GAME,         /* 游戏 */
    HSDK_TYPE_SERVICE,      /* 后台服务 */
    HSDK_TYPE_WIDGET,       /* 桌面小部件 */
    HSDK_TYPE_THEME,        /* 主题包 */
    HSDK_TYPE_PLUGIN,       /* 插件 */
    HSDK_TYPE_LIBRARY,      /* 共享库 */
} hsdk_project_type_t;

/* 构建配置 */
typedef enum {
    HSDK_BUILD_DEBUG = 0,
    HSDK_BUILD_RELEASE,
    HSDK_BUILD_PROFILE,
    HSDK_BUILD_BETA,
} hsdk_build_type_t;

/* 目标架构 */
typedef enum {
    HSDK_ARCH_ARM64 = 0,
    HSDK_ARCH_ARM32,
    HSDK_ARCH_X86_64,
    HSDK_ARCH_X86,
    HSDK_ARCH_RISCV64,
    HSDK_ARCH_ALL,
} hsdk_arch_t;

/* 源文件 */
typedef struct {
    int used;
    char path[HSDK_PATH_LEN];
    char name[HSDK_NAME_LEN];
    int is_header;
    uint64_t size;
    uint32_t lines;
    int compiled;
} hsdk_source_t;

/* 资源文件 */
typedef struct {
    int used;
    char path[HSDK_PATH_LEN];
    char name[HSDK_NAME_LEN];
    char type[32];  /* image/audio/video/font/layout/string */
    uint64_t size;
    int compressed;
} hsdk_resource_t;

/* 权限声明 */
typedef struct {
    char name[64];
    char description[256];
    int level;  /* 0=normal,1=dangerous,2=special */
    int required;
} hsdk_permission_t;

/* 项目配置 */
typedef struct {
    int used;
    int id;
    char name[HSDK_NAME_LEN];
    char package_name[128];  /* com.example.app */
    char version[HSDK_VERSION_LEN];
    int version_code;
    hsdk_project_type_t type;
    char description[512];
    char author[128];
    char license[64];
    char homepage[256];
    /* 构建 */
    hsdk_build_type_t build_type;
    hsdk_arch_t target_arch;
    int min_sdk;
    int target_sdk;
    /* 路径 */
    char source_dir[HSDK_PATH_LEN];
    char resource_dir[HSDK_PATH_LEN];
    char output_dir[HSDK_PATH_LEN];
    /* 文件 */
    hsdk_source_t sources[HSDK_MAX_SOURCES];
    int source_count;
    hsdk_resource_t resources[HSDK_MAX_RESOURCES];
    int resource_count;
    /* 权限 */
    hsdk_permission_t permissions[HSDK_MAX_PERMISSIONS];
    int permission_count;
    /* 依赖 */
    char dependencies[64][128];
    int dependency_count;
    /* 统计 */
    uint64_t total_size;
    uint64_t build_time;
    int build_count;
} hsdk_project_t;

/* 构建结果 */
typedef struct {
    int success;
    char output_path[HSDK_PATH_LEN];
    uint64_t output_size;
    int source_count;
    int resource_count;
    uint64_t build_time_ms;
    int warnings;
    int errors;
    char error_log[4096];
} hsdk_build_result_t;

/* SDK状态 */
typedef struct {
    int initialized;
    char sdk_version[HSDK_VERSION_LEN];
    char sdk_path[HSDK_PATH_LEN];
    hsdk_project_t projects[HSDK_MAX_PROJECTS];
    int project_count;
    int current_project;
    uint64_t total_builds;
} hug_sdk_t;

/* 初始化 */
void hug_sdk_init(void);

/* 项目管理 */
int hsdk_project_create(const char *name, const char *package, hsdk_project_type_t type);
int hsdk_project_load(const char *path);
int hsdk_project_save(int id);
int hsdk_project_delete(int id);
int hsdk_project_get(int id, hsdk_project_t *proj);
int hsdk_project_list(hsdk_project_t *projects, int max);
int hsdk_project_set_current(int id);
int hsdk_project_get_current(hsdk_project_t *proj);

/* 源文件管理 */
int hsdk_source_add(int project_id, const char *path);
int hsdk_source_remove(int project_id, const char *path);
int hsdk_source_list(int project_id, hsdk_source_t *sources, int max);
int hsdk_source_scan(int project_id, const char *dir);

/* 资源管理 */
int hsdk_resource_add(int project_id, const char *path, const char *type);
int hsdk_resource_remove(int project_id, const char *path);
int hsdk_resource_list(int project_id, hsdk_resource_t *resources, int max);

/* 权限管理 */
int hsdk_permission_add(int project_id, const char *name, const char *desc, int level);
int hsdk_permission_remove(int project_id, const char *name);
int hsdk_permission_list(int project_id, hsdk_permission_t *perms, int max);

/* 依赖管理 */
int hsdk_dependency_add(int project_id, const char *dep);
int hsdk_dependency_remove(int project_id, const char *dep);
int hsdk_dependency_list(int project_id, char deps[][128], int max);

/* 构建 */
int hsdk_build(int project_id, hsdk_build_type_t type, hsdk_build_result_t *result);
int hsdk_clean(int project_id);
int hsdk_rebuild(int project_id, hsdk_build_result_t *result);
int hsdk_install(int project_id);
int hsdk_run(int project_id);

/* 打包 */
int hsdk_package(int project_id, const char *output_path);
int hsdk_sign(int project_id, const char *key_path);
int hsdk_verify(const char *hug_path);

/* SDK信息 */
int hsdk_get_version(char *buf, int size);
int hsdk_get_stats(hug_sdk_t *stats);

#endif /* HUG_SDK_H */
