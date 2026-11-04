/* ============================================================================
 * 赫普OS - .hug 依赖与版本管理器
 * ========================================================================== */

#include <stdint.h>
#include "hug_depman.h"
#include "vga.h"
#include "string.h"

static hug_depman_t hdm;

void hug_depman_init(void)
{
    memset(&hdm, 0, sizeof(hug_depman_t));
    hdm.initialized = 1;

    /* 默认仓库 */
    hdm_repo_add(" 官方 repo ", "https://repo.hepu.os/packages");
    hdm_repo_add(" 社 region repo ", "https://community.hepu.os/packages");

    /* 测试包 */
    hdm_package_register("libhepu", "1.0.0");
    hdm_package_register("hepu-ui", "2.1.0");
    hdm_package_register("hepu-net", "1.5.2");
    hdm_package_register("hepu-media", "3.0.0");

    vga_puts("[HDEPMAN] dep manager init done \n");
    vga_printf(" packages: %d, repo: %d \n", hdm.package_count, hdm.repository_count);
}

/* 版本操作 */
int hdm_version_parse(const char *str, hdm_version_t *ver)
{
    if (!str || !ver) return -1;
    memset(ver, 0, sizeof(hdm_version_t));
    strncpy(ver->full, str, 31);
    /* 简化解析: major.minor.patch */
    int i = 0, val = 0, part = 0;
    while (str[i] && part < 3) {
        if (str[i] >= '0' && str[i] <= '9') {
            val = val * 10 + (str[i] - '0');
        } else if (str[i] == '.') {
            if (part == 0) ver->major = val;
            else if (part == 1) ver->minor = val;
            part++;
            val = 0;
        } else if (str[i] == '-') {
            break;
        }
        i++;
    }
    if (part == 2) ver->patch = val;
    else if (part == 1) ver->minor = val;
    else ver->major = val;
    return 0;
}

int hdm_version_compare(const hdm_version_t *a, const hdm_version_t *b)
{
    if (!a || !b) return 0;
    if (a->major != b->major) return a->major > b->major ? 1 : -1;
    if (a->minor != b->minor) return a->minor > b->minor ? 1 : -1;
    if (a->patch != b->patch) return a->patch > b->patch ? 1 : -1;
    return 0;
}

int hdm_version_to_string(const hdm_version_t *ver, char *buf, int size)
{
    if (!ver || !buf || size <= 0) return -1;
    int len = 0;
    int parts[3] = {ver->major, ver->minor, ver->patch};
    for (int p = 0; p < 3; p++) {
        if (p > 0) buf[len++] = '.';
        if (parts[p] == 0) { buf[len++] = '0'; }
        else {
            char tmp[16]; int tlen = 0;
            int v = parts[p];
            while (v > 0) { tmp[tlen++] = '0' + v % 10; v /= 10; }
            while (tlen > 0) buf[len++] = tmp[--tlen];
        }
    }
    buf[len] = 0;
    return len;
}

int hdm_version_satisfies(const hdm_version_t *ver, const hdm_dependency_t *dep)
{
    if (!ver || !dep) return 0;
    int cmp_min = hdm_version_compare(ver, &dep->min_version);
    int cmp_max = hdm_version_compare(ver, &dep->max_version);
    switch (dep->constraint) {
        case HDM_CONSTRAINT_EQ: return cmp_min == 0;
        case HDM_CONSTRAINT_GE: return cmp_min >= 0;
        case HDM_CONSTRAINT_LE: return cmp_min <= 0;
        case HDM_CONSTRAINT_GT: return cmp_min > 0;
        case HDM_CONSTRAINT_LT: return cmp_min < 0;
        case HDM_CONSTRAINT_RANGE: return cmp_min >= 0 && cmp_max <= 0;
        case HDM_CONSTRAINT_ANY: return 1;
    }
    return 0;
}

/* 包管理 */
int hdm_package_register(const char *name, const char *version)
{
    for (int i = 0; i < HDM_MAX_PACKAGES; i++) {
        if (!hdm.packages[i].used) {
            memset(&hdm.packages[i], 0, sizeof(hdm_package_t));
            hdm.packages[i].used = 1;
            hdm.packages[i].id = i;
            if (name) strncpy(hdm.packages[i].name, name, 127);
            hdm_version_parse(version, &hdm.packages[i].versions[0]);
            hdm.packages[i].version_count = 1;
            hdm.packages[i].current_version = 0;
            hdm.packages[i].status = HDM_STATUS_AVAILABLE;
            hdm.packages[i].size = 1024 * 1024;
            hdm.package_count++;
            return i;
        }
    }
    return -1;
}

int hdm_package_get(const char *name, hdm_package_t *pkg)
{
    if (!name || !pkg) return -1;
    for (int i = 0; i < HDM_MAX_PACKAGES; i++) {
        if (hdm.packages[i].used && strcmp(hdm.packages[i].name, name) == 0) {
            memcpy(pkg, &hdm.packages[i], sizeof(hdm_package_t));
            return i;
        }
    }
    return -1;
}

int hdm_package_list(hdm_package_t *pkgs, int max)
{
    if (!pkgs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HDM_MAX_PACKAGES && count < max; i++) {
        if (hdm.packages[i].used) memcpy(&pkgs[count++], &hdm.packages[i], sizeof(hdm_package_t));
    }
    return count;
}

int hdm_package_search(const char *query, hdm_package_t *results, int max)
{
    if (!query || !results || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HDM_MAX_PACKAGES && count < max; i++) {
        if (hdm.packages[i].used) {
            int match = 1;
            int qlen = 0; while (query[qlen]) qlen++;
            for (int j = 0; j < qlen; j++) {
                int found = 0;
                for (int k = 0; hdm.packages[i].name[k]; k++)
                    if (hdm.packages[i].name[k] == query[j]) { found = 1; break; }
                if (!found) { match = 0; break; }
            }
            if (match) memcpy(&results[count++], &hdm.packages[i], sizeof(hdm_package_t));
        }
    }
    return count;
}

int hdm_package_install(const char *name, const char *version)
{
    for (int i = 0; i < HDM_MAX_PACKAGES; i++) {
        if (hdm.packages[i].used && strcmp(hdm.packages[i].name, name) == 0) {
            hdm.packages[i].status = HDM_STATUS_INSTALLED;
            hdm.packages[i].installed_version = hdm.packages[i].current_version;
            hdm.total_installs++;
            return 0;
        }
    }
    (void)version;
    return -1;
}

int hdm_package_uninstall(const char *name)
{
    for (int i = 0; i < HDM_MAX_PACKAGES; i++) {
        if (hdm.packages[i].used && strcmp(hdm.packages[i].name, name) == 0) {
            hdm.packages[i].status = HDM_STATUS_AVAILABLE;
            hdm.packages[i].installed_version = -1;
            return 0;
        }
    }
    return -1;
}

int hdm_package_update(const char *name)
{
    for (int i = 0; i < HDM_MAX_PACKAGES; i++) {
        if (hdm.packages[i].used && strcmp(hdm.packages[i].name, name) == 0) {
            hdm.packages[i].installed_version = hdm.packages[i].current_version;
            hdm.packages[i].status = HDM_STATUS_INSTALLED;
            hdm.total_updates++;
            return 0;
        }
    }
    return -1;
}

int hdm_package_update_all(void)
{
    int count = 0;
    for (int i = 0; i < HDM_MAX_PACKAGES; i++) {
        if (hdm.packages[i].used && hdm.packages[i].status == HDM_STATUS_UPDATE_AVAILABLE) {
            hdm_package_update(hdm.packages[i].name);
            count++;
        }
    }
    return count;
}

int hdm_package_get_installed(hdm_package_t *pkgs, int max)
{
    if (!pkgs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HDM_MAX_PACKAGES && count < max; i++) {
        if (hdm.packages[i].used && hdm.packages[i].status == HDM_STATUS_INSTALLED)
            memcpy(&pkgs[count++], &hdm.packages[i], sizeof(hdm_package_t));
    }
    return count;
}

int hdm_package_get_updates(hdm_package_t *pkgs, int max)
{
    if (!pkgs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HDM_MAX_PACKAGES && count < max; i++) {
        if (hdm.packages[i].used && hdm.packages[i].status == HDM_STATUS_UPDATE_AVAILABLE)
            memcpy(&pkgs[count++], &hdm.packages[i], sizeof(hdm_package_t));
    }
    return count;
}

/* 依赖管理 */
int hdm_dependency_add(const char *package, const char *dep_name, const char *constraint)
{
    for (int i = 0; i < HDM_MAX_PACKAGES; i++) {
        if (hdm.packages[i].used && strcmp(hdm.packages[i].name, package) == 0) {
            if (hdm.packages[i].dependency_count < HDM_MAX_DEPENDENCIES) {
                int idx = hdm.packages[i].dependency_count++;
                strncpy(hdm.packages[i].dependencies[idx].name, dep_name, 127);
                hdm.packages[i].dependencies[idx].constraint = HDM_CONSTRAINT_ANY;
                (void)constraint;
                return 0;
            }
        }
    }
    return -1;
}

int hdm_dependency_remove(const char *package, const char *dep_name)
{
    for (int i = 0; i < HDM_MAX_PACKAGES; i++) {
        if (hdm.packages[i].used && strcmp(hdm.packages[i].name, package) == 0) {
            for (int j = 0; j < hdm.packages[i].dependency_count; j++) {
                if (strcmp(hdm.packages[i].dependencies[j].name, dep_name) == 0) {
                    for (int k = j; k < hdm.packages[i].dependency_count - 1; k++)
                        hdm.packages[i].dependencies[k] = hdm.packages[i].dependencies[k+1];
                    hdm.packages[i].dependency_count--;
                    return 0;
                }
            }
        }
    }
    return -1;
}

int hdm_dependency_list(const char *package, hdm_dependency_t *deps, int max)
{
    if (!deps || max <= 0) return 0;
    for (int i = 0; i < HDM_MAX_PACKAGES; i++) {
        if (hdm.packages[i].used && strcmp(hdm.packages[i].name, package) == 0) {
            int count = hdm.packages[i].dependency_count < max ?
                        hdm.packages[i].dependency_count : max;
            for (int j = 0; j < count; j++)
                memcpy(&deps[j], &hdm.packages[i].dependencies[j], sizeof(hdm_dependency_t));
            return count;
        }
    }
    return 0;
}

int hdm_dependency_resolve(const char *package, hdm_resolve_result_t *result)
{
    if (!result) return -1;
    memset(result, 0, sizeof(hdm_resolve_result_t));
    result->success = 1;
    for (int i = 0; i < HDM_MAX_PACKAGES; i++) {
        if (hdm.packages[i].used && strcmp(hdm.packages[i].name, package) == 0) {
            result->packages_to_install = hdm.packages[i].dependency_count;
            return 0;
        }
    }
    return -1;
}

int hdm_dependency_check(const char *package)
{
    hdm_resolve_result_t result;
    return hdm_dependency_resolve(package, &result);
}

/* 仓库管理 */
int hdm_repo_add(const char *name, const char *url)
{
    for (int i = 0; i < 16; i++) {
        if (!hdm.repositories[i].used) {
            memset(&hdm.repositories[i], 0, sizeof(hdm_repository_t));
            hdm.repositories[i].used = 1;
            hdm.repositories[i].id = i;
            if (name) strncpy(hdm.repositories[i].name, name, 127);
            if (url) strncpy(hdm.repositories[i].url, url, 255);
            hdm.repositories[i].enabled = 1;
            hdm.repositories[i].priority = 50;
            hdm.repository_count++;
            return i;
        }
    }
    return -1;
}

int hdm_repo_remove(int id)
{
    if (id < 0 || id >= 16 || !hdm.repositories[id].used) return -1;
    hdm.repositories[id].used = 0;
    hdm.repository_count--;
    return 0;
}

int hdm_repo_enable(int id, int enable)
{
    if (id < 0 || id >= 16 || !hdm.repositories[id].used) return -1;
    hdm.repositories[id].enabled = enable ? 1 : 0;
    return 0;
}

int hdm_repo_list(hdm_repository_t *repos, int max)
{
    if (!repos || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < 16 && count < max; i++) {
        if (hdm.repositories[i].used) memcpy(&repos[count++], &hdm.repositories[i], sizeof(hdm_repository_t));
    }
    return count;
}

int hdm_repo_sync(int id)
{
    if (id < 0 || id >= 16 || !hdm.repositories[id].used) return -1;
    hdm.repositories[id].last_sync = 1;
    return 0;
}

int hdm_repo_sync_all(void)
{
    int count = 0;
    for (int i = 0; i < 16; i++) {
        if (hdm.repositories[i].used && hdm.repositories[i].enabled) {
            hdm_repo_sync(i);
            count++;
        }
    }
    return count;
}

/* 统计 */
int hdm_get_stats(hug_depman_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &hdm, sizeof(hug_depman_t));
    return 0;
}
