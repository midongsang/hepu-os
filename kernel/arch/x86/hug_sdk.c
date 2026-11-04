/* ============================================================================
 * 赫普OS - .hug SDK 核心
 * ========================================================================== */

#include <stdint.h>
#include "hug_sdk.h"
#include "vga.h"
#include "string.h"

static hug_sdk_t sdk;

void hug_sdk_init(void)
{
    memset(&sdk, 0, sizeof(hug_sdk_t));
    sdk.initialized = 1;
    strncpy(sdk.sdk_version, "1.0.0", 31);
    strncpy(sdk.sdk_path, "/opt/hepu/sdk", 255);
    sdk.current_project = -1;

    vga_puts("[HUGSDK] .hug SDK init done \n");
    vga_printf(" version: %s, path: %s\n", sdk.sdk_version, sdk.sdk_path);
}

/* 项目管理 */
int hsdk_project_create(const char *name, const char *package, hsdk_project_type_t type)
{
    for (int i = 0; i < HSDK_MAX_PROJECTS; i++) {
        if (!sdk.projects[i].used) {
            memset(&sdk.projects[i], 0, sizeof(hsdk_project_t));
            sdk.projects[i].used = 1;
            sdk.projects[i].id = i;
            if (name) strncpy(sdk.projects[i].name, name, 127);
            if (package) strncpy(sdk.projects[i].package_name, package, 127);
            sdk.projects[i].type = type;
            strncpy(sdk.projects[i].version, "1.0.0", 31);
            sdk.projects[i].version_code = 1;
            sdk.projects[i].build_type = HSDK_BUILD_DEBUG;
            sdk.projects[i].target_arch = HSDK_ARCH_ARM64;
            sdk.projects[i].min_sdk = 21;
            sdk.projects[i].target_sdk = 34;
            strncpy(sdk.projects[i].source_dir, "src", 255);
            strncpy(sdk.projects[i].resource_dir, "res", 255);
            strncpy(sdk.projects[i].output_dir, "build", 255);
            sdk.project_count++;
            return i;
        }
    }
    return -1;
}

int hsdk_project_load(const char *path)
{
    (void)path;
    return -1;
}

int hsdk_project_save(int id)
{
    if (id < 0 || id >= HSDK_MAX_PROJECTS || !sdk.projects[id].used) return -1;
    return 0;
}

int hsdk_project_delete(int id)
{
    if (id < 0 || id >= HSDK_MAX_PROJECTS || !sdk.projects[id].used) return -1;
    sdk.projects[id].used = 0;
    sdk.project_count--;
    return 0;
}

int hsdk_project_get(int id, hsdk_project_t *proj)
{
    if (id < 0 || id >= HSDK_MAX_PROJECTS || !sdk.projects[id].used || !proj) return -1;
    memcpy(proj, &sdk.projects[id], sizeof(hsdk_project_t));
    return 0;
}

int hsdk_project_list(hsdk_project_t *projects, int max)
{
    if (!projects || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HSDK_MAX_PROJECTS && count < max; i++) {
        if (sdk.projects[i].used) memcpy(&projects[count++], &sdk.projects[i], sizeof(hsdk_project_t));
    }
    return count;
}

int hsdk_project_set_current(int id)
{
    if (id < 0 || id >= HSDK_MAX_PROJECTS || !sdk.projects[id].used) return -1;
    sdk.current_project = id;
    return 0;
}

int hsdk_project_get_current(hsdk_project_t *proj)
{
    if (sdk.current_project < 0 || !proj) return -1;
    return hsdk_project_get(sdk.current_project, proj);
}

/* 源文件管理 */
int hsdk_source_add(int project_id, const char *path)
{
    if (project_id < 0 || project_id >= HSDK_MAX_PROJECTS || !sdk.projects[project_id].used) return -1;
    for (int i = 0; i < HSDK_MAX_SOURCES; i++) {
        if (!sdk.projects[project_id].sources[i].used) {
            memset(&sdk.projects[project_id].sources[i], 0, sizeof(hsdk_source_t));
            sdk.projects[project_id].sources[i].used = 1;
            if (path) strncpy(sdk.projects[project_id].sources[i].path, path, 255);
            sdk.projects[project_id].source_count++;
            return i;
        }
    }
    return -1;
}

int hsdk_source_remove(int project_id, const char *path)
{
    if (project_id < 0 || project_id >= HSDK_MAX_PROJECTS || !sdk.projects[project_id].used) return -1;
    for (int i = 0; i < HSDK_MAX_SOURCES; i++) {
        if (sdk.projects[project_id].sources[i].used &&
            strcmp(sdk.projects[project_id].sources[i].path, path) == 0) {
            sdk.projects[project_id].sources[i].used = 0;
            sdk.projects[project_id].source_count--;
            return 0;
        }
    }
    return -1;
}

int hsdk_source_list(int project_id, hsdk_source_t *sources, int max)
{
    if (!sources || max <= 0) return 0;
    if (project_id < 0 || project_id >= HSDK_MAX_PROJECTS || !sdk.projects[project_id].used) return 0;
    int count = 0;
    for (int i = 0; i < HSDK_MAX_SOURCES && count < max; i++) {
        if (sdk.projects[project_id].sources[i].used)
            memcpy(&sources[count++], &sdk.projects[project_id].sources[i], sizeof(hsdk_source_t));
    }
    return count;
}

int hsdk_source_scan(int project_id, const char *dir)
{
    (void)dir;
    if (project_id < 0 || project_id >= HSDK_MAX_PROJECTS || !sdk.projects[project_id].used) return -1;
    return 0;
}

/* 资源管理 */
int hsdk_resource_add(int project_id, const char *path, const char *type)
{
    if (project_id < 0 || project_id >= HSDK_MAX_PROJECTS || !sdk.projects[project_id].used) return -1;
    for (int i = 0; i < HSDK_MAX_RESOURCES; i++) {
        if (!sdk.projects[project_id].resources[i].used) {
            memset(&sdk.projects[project_id].resources[i], 0, sizeof(hsdk_resource_t));
            sdk.projects[project_id].resources[i].used = 1;
            if (path) strncpy(sdk.projects[project_id].resources[i].path, path, 255);
            if (type) strncpy(sdk.projects[project_id].resources[i].type, type, 31);
            sdk.projects[project_id].resource_count++;
            return i;
        }
    }
    return -1;
}

int hsdk_resource_remove(int project_id, const char *path)
{
    if (project_id < 0 || project_id >= HSDK_MAX_PROJECTS || !sdk.projects[project_id].used) return -1;
    for (int i = 0; i < HSDK_MAX_RESOURCES; i++) {
        if (sdk.projects[project_id].resources[i].used &&
            strcmp(sdk.projects[project_id].resources[i].path, path) == 0) {
            sdk.projects[project_id].resources[i].used = 0;
            sdk.projects[project_id].resource_count--;
            return 0;
        }
    }
    return -1;
}

int hsdk_resource_list(int project_id, hsdk_resource_t *resources, int max)
{
    if (!resources || max <= 0) return 0;
    if (project_id < 0 || project_id >= HSDK_MAX_PROJECTS || !sdk.projects[project_id].used) return 0;
    int count = 0;
    for (int i = 0; i < HSDK_MAX_RESOURCES && count < max; i++) {
        if (sdk.projects[project_id].resources[i].used)
            memcpy(&resources[count++], &sdk.projects[project_id].resources[i], sizeof(hsdk_resource_t));
    }
    return count;
}

/* 权限管理 */
int hsdk_permission_add(int project_id, const char *name, const char *desc, int level)
{
    if (project_id < 0 || project_id >= HSDK_MAX_PROJECTS || !sdk.projects[project_id].used) return -1;
    for (int i = 0; i < HSDK_MAX_PERMISSIONS; i++) {
        if (sdk.projects[project_id].permissions[i].name[0] == 0) {
            if (name) strncpy(sdk.projects[project_id].permissions[i].name, name, 63);
            if (desc) strncpy(sdk.projects[project_id].permissions[i].description, desc, 255);
            sdk.projects[project_id].permissions[i].level = level;
            sdk.projects[project_id].permissions[i].required = 1;
            sdk.projects[project_id].permission_count++;
            return i;
        }
    }
    return -1;
}

int hsdk_permission_remove(int project_id, const char *name)
{
    if (project_id < 0 || project_id >= HSDK_MAX_PROJECTS || !sdk.projects[project_id].used) return -1;
    for (int i = 0; i < HSDK_MAX_PERMISSIONS; i++) {
        if (strcmp(sdk.projects[project_id].permissions[i].name, name) == 0) {
            memset(&sdk.projects[project_id].permissions[i], 0, sizeof(hsdk_permission_t));
            for (int j = i; j < sdk.projects[project_id].permission_count - 1; j++)
                sdk.projects[project_id].permissions[j] = sdk.projects[project_id].permissions[j+1];
            sdk.projects[project_id].permission_count--;
            return 0;
        }
    }
    return -1;
}

int hsdk_permission_list(int project_id, hsdk_permission_t *perms, int max)
{
    if (!perms || max <= 0) return 0;
    if (project_id < 0 || project_id >= HSDK_MAX_PROJECTS || !sdk.projects[project_id].used) return 0;
    int count = sdk.projects[project_id].permission_count < max ?
                sdk.projects[project_id].permission_count : max;
    for (int i = 0; i < count; i++)
        memcpy(&perms[i], &sdk.projects[project_id].permissions[i], sizeof(hsdk_permission_t));
    return count;
}

/* 依赖管理 */
int hsdk_dependency_add(int project_id, const char *dep)
{
    if (project_id < 0 || project_id >= HSDK_MAX_PROJECTS || !sdk.projects[project_id].used) return -1;
    if (sdk.projects[project_id].dependency_count < 64) {
        strncpy(sdk.projects[project_id].dependencies[sdk.projects[project_id].dependency_count], dep, 127);
        sdk.projects[project_id].dependency_count++;
        return 0;
    }
    return -1;
}

int hsdk_dependency_remove(int project_id, const char *dep)
{
    if (project_id < 0 || project_id >= HSDK_MAX_PROJECTS || !sdk.projects[project_id].used) return -1;
    for (int i = 0; i < sdk.projects[project_id].dependency_count; i++) {
        if (strcmp(sdk.projects[project_id].dependencies[i], dep) == 0) {
            for (int j = i; j < sdk.projects[project_id].dependency_count - 1; j++)
                strncpy(sdk.projects[project_id].dependencies[j],
                        sdk.projects[project_id].dependencies[j+1], 127);
            sdk.projects[project_id].dependency_count--;
            return 0;
        }
    }
    return -1;
}

int hsdk_dependency_list(int project_id, char deps[][128], int max)
{
    if (!deps || max <= 0) return 0;
    if (project_id < 0 || project_id >= HSDK_MAX_PROJECTS || !sdk.projects[project_id].used) return 0;
    int count = sdk.projects[project_id].dependency_count < max ?
                sdk.projects[project_id].dependency_count : max;
    for (int i = 0; i < count; i++)
        strncpy(deps[i], sdk.projects[project_id].dependencies[i], 127);
    return count;
}

/* 构建 */
int hsdk_build(int project_id, hsdk_build_type_t type, hsdk_build_result_t *result)
{
    if (project_id < 0 || project_id >= HSDK_MAX_PROJECTS || !sdk.projects[project_id].used) return -1;
    if (!result) return -1;
    memset(result, 0, sizeof(hsdk_build_result_t));
    result->success = 1;
    result->source_count = sdk.projects[project_id].source_count;
    result->resource_count = sdk.projects[project_id].resource_count;
    result->build_time_ms = 1500;
    result->output_size = 2 * 1024 * 1024;
    strncpy(result->output_path, "build/app.hug", 255);
    sdk.projects[project_id].build_type = type;
    sdk.projects[project_id].build_count++;
    sdk.total_builds++;
    return 0;
}

int hsdk_clean(int project_id)
{
    if (project_id < 0 || project_id >= HSDK_MAX_PROJECTS || !sdk.projects[project_id].used) return -1;
    return 0;
}

int hsdk_rebuild(int project_id, hsdk_build_result_t *result)
{
    hsdk_clean(project_id);
    return hsdk_build(project_id, HSDK_BUILD_DEBUG, result);
}

int hsdk_install(int project_id)
{
    if (project_id < 0 || project_id >= HSDK_MAX_PROJECTS || !sdk.projects[project_id].used) return -1;
    return 0;
}

int hsdk_run(int project_id)
{
    if (project_id < 0 || project_id >= HSDK_MAX_PROJECTS || !sdk.projects[project_id].used) return -1;
    return 0;
}

/* 打包 */
int hsdk_package(int project_id, const char *output_path)
{
    (void)output_path;
    if (project_id < 0 || project_id >= HSDK_MAX_PROJECTS || !sdk.projects[project_id].used) return -1;
    return 0;
}

int hsdk_sign(int project_id, const char *key_path)
{
    (void)key_path;
    if (project_id < 0 || project_id >= HSDK_MAX_PROJECTS || !sdk.projects[project_id].used) return -1;
    return 0;
}

int hsdk_verify(const char *hug_path)
{
    (void)hug_path;
    return 0;
}

/* SDK信息 */
int hsdk_get_version(char *buf, int size)
{
    if (!buf || size <= 0) return -1;
    strncpy(buf, sdk.sdk_version, size - 1);
    return 0;
}

int hsdk_get_stats(hug_sdk_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &sdk, sizeof(hug_sdk_t));
    return 0;
}
