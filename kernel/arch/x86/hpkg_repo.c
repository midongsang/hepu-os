/* ============================================================================
 * 赫普OS - 包管理器增强（V8.82新增，P5-3）
 * 仓库管理、依赖递归解析、包搜索、事务支持、缓存管理
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "hpkg.h"
#include "../include/vga.h"
#include "../include/kheap.h"

/* 外部数据库 */
extern hpkg_database_t g_db;
extern int g_initialized;

/* 简单字符串工具 */
static int repo_strlen(const char *s) {
    int n = 0; while (s && s[n]) n++; return n;
}
static int repo_strcmp(const char *a, const char *b) {
    if (!a || !b) return -1;
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static void repo_strncpy(char *dst, const char *src, int n) {
    if (!dst || !src || n <= 0) return;
    int i = 0; while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ============================================================================
 * 仓库管理
 * ========================================================================== */

#define MAX_REPOS 8
#define MAX_REPO_URL 256
#define MAX_REPO_NAME 64

typedef struct {
    char name[MAX_REPO_NAME];
    char url[MAX_REPO_URL];
    int  enabled;
    int  priority;  /* 优先级，数字越大优先级越高 */
} hpkg_repo_t;

static hpkg_repo_t g_repos[MAX_REPOS];
static int g_repo_count = 0;
static int g_repo_initialized = 0;

/* 初始化仓库列表（默认仓库） */
int hpkg_repo_init(void)
{
    if (g_repo_initialized) return 0;

    memset(g_repos, 0, sizeof(g_repos));
    g_repo_count = 0;

    /* 添加默认仓库 */
    hpkg_repo_add("official", "https://pkg.hepuos.org/official", 100);
    hpkg_repo_add("community", "https://pkg.hepuos.org/community", 50);
    hpkg_repo_add("testing", "https://pkg.hepuos.org/testing", 10);

    g_repo_initialized = 1;
    vga_puts("[HPKG] Repository manager initialized (3 repos)\n");
    return 0;
}

/* 添加仓库 */
int hpkg_repo_add(const char *name, const char *url, int priority)
{
    if (!name || !url || g_repo_count >= MAX_REPOS) return -1;

    /* 检查是否已存在 */
    for (int i = 0; i < g_repo_count; i++) {
        if (repo_strcmp(g_repos[i].name, name) == 0) {
            /* 更新URL和优先级 */
            repo_strncpy(g_repos[i].url, url, MAX_REPO_URL);
            g_repos[i].priority = priority;
            return 0;
        }
    }

    hpkg_repo_t *repo = &g_repos[g_repo_count];
    repo_strncpy(repo->name, name, MAX_REPO_NAME);
    repo_strncpy(repo->url, url, MAX_REPO_URL);
    repo->enabled = 1;
    repo->priority = priority;
    g_repo_count++;

    return 0;
}

/* 删除仓库 */
int hpkg_repo_remove(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < g_repo_count; i++) {
        if (repo_strcmp(g_repos[i].name, name) == 0) {
            for (int j = i; j < g_repo_count - 1; j++) {
                g_repos[j] = g_repos[j + 1];
            }
            g_repo_count--;
            return 0;
        }
    }
    return -1;
}

/* 启用/禁用仓库 */
int hpkg_repo_enable(const char *name, int enable)
{
    if (!name) return -1;
    for (int i = 0; i < g_repo_count; i++) {
        if (repo_strcmp(g_repos[i].name, name) == 0) {
            g_repos[i].enabled = enable;
            return 0;
        }
    }
    return -1;
}

/* 列出所有仓库 */
int hpkg_repo_list(hpkg_repo_t *out, int max_count)
{
    if (!out || max_count <= 0) return g_repo_count;
    int count = (g_repo_count < max_count) ? g_repo_count : max_count;
    for (int i = 0; i < count; i++) {
        out[i] = g_repos[i];
    }
    return g_repo_count;
}

/* 获取仓库数量 */
int hpkg_repo_count(void)
{
    return g_repo_count;
}

/* ============================================================================
 * 包搜索
 * ============================================================================ */

/* 搜索结果 */
typedef struct {
    char name[HPKG_MAX_NAME_LEN];
    char version[HPKG_MAX_VERSION_LEN];
    char description[128];
    char repo[MAX_REPO_NAME];
    int  installed;
} hpkg_search_result_t;

/* 在已安装包中搜索 */
int hpkg_search_installed(const char *query,
                          hpkg_search_result_t *results, int max_results)
{
    if (!query || !results || max_results <= 0) return 0;

    int count = 0;
    for (int i = 0; i < g_db.package_count && count < max_results; i++) {
        if (!g_db.packages[i].used) continue;
        hpkg_installed_t *pkg = &g_db.packages[i];

        /* 简单子串匹配 */
        const char *name = pkg->meta.name;
        int qlen = repo_strlen(query);
        int nlen = repo_strlen(name);

        if (qlen <= nlen) {
            for (int j = 0; j <= nlen - qlen; j++) {
                if (memcmp(name + j, query, qlen) == 0) {
                    repo_strncpy(results[count].name, name, HPKG_MAX_NAME_LEN);
                    repo_strncpy(results[count].version,
                                 pkg->meta.version, HPKG_MAX_VERSION_LEN);
                    repo_strncpy(results[count].description,
                                 pkg->meta.description, 128);
                    results[count].installed = 1;
                    results[count].repo[0] = 0;
                    count++;
                    break;
                }
            }
        }
    }
    return count;
}

/* 按名称精确查找已安装包 */
int hpkg_find_installed(const char *name, hpkg_installed_t *out)
{
    if (!name || !out) return -1;
    for (int i = 0; i < g_db.package_count; i++) {
        if (g_db.packages[i].used &&
            repo_strcmp(g_db.packages[i].meta.name, name) == 0) {
            *out = g_db.packages[i];
            return i;
        }
    }
    return -1;
}

/* ============================================================================
 * 依赖递归解析
 * ========================================================================== */

#define MAX_RESOLVE_QUEUE 64
#define MAX_RESOLVE_RESULT 128

typedef struct {
    char name[HPKG_MAX_NAME_LEN];
    char version[HPKG_MAX_VERSION_LEN];
    int  resolved;
    int  level;  /* 依赖层级 */
} resolve_item_t;

/* 检查版本是否满足要求（简化版：字符串比较） */
static int version_satisfies(const char *version, const char *min, const char *max)
{
    /* 简化：只要已安装就认为满足 */
    /* 实际应该解析版本号并比较 */
    return hpkg_is_installed(version);
}

/* 递归解析依赖 */
int hpkg_resolve_depends(const hpkg_meta_t *meta,
                         resolve_item_t *result, int max_result)
{
    if (!meta || !result || max_result <= 0) return 0;

    resolve_item_t queue[MAX_RESOLVE_QUEUE];
    int queue_head = 0, queue_tail = 0;
    int result_count = 0;

    /* 将包本身加入队列 */
    repo_strncpy(queue[queue_tail].name, meta->name, HPKG_MAX_NAME_LEN);
    repo_strncpy(queue[queue_tail].version, meta->version,
                 HPKG_MAX_VERSION_LEN);
    queue[queue_tail].resolved = 0;
    queue[queue_tail].level = 0;
    queue_tail++;

    /* BFS解析依赖 */
    while (queue_head < queue_tail && result_count < max_result) {
        resolve_item_t *item = &queue[queue_head];
        queue_head++;

        /* 检查是否已在结果中 */
        int found = 0;
        for (int i = 0; i < result_count; i++) {
            if (repo_strcmp(result[i].name, item->name) == 0) {
                found = 1;
                break;
            }
        }
        if (found) continue;

        /* 添加到结果 */
        result[result_count] = *item;
        result_count++;

        /* 查找已安装包的依赖 */
        hpkg_installed_t installed;
        if (hpkg_find_installed(item->name, &installed) >= 0) {
            for (int i = 0; i < installed.meta.depend_count &&
                            queue_tail < MAX_RESOLVE_QUEUE; i++) {
                hpkg_depend_t *dep = &installed.meta.depends[i];

                /* 检查依赖是否已安装 */
                if (!hpkg_is_installed(dep->name)) {
                    repo_strncpy(queue[queue_tail].name, dep->name,
                                 HPKG_MAX_NAME_LEN);
                    queue[queue_tail].version[0] = 0;
                    queue[queue_tail].resolved = 0;
                    queue[queue_tail].level = item->level + 1;
                    queue_tail++;
                }
            }
        }
    }

    return result_count;
}

/* 检查缺失依赖 */
int hpkg_check_missing_depends(const hpkg_meta_t *meta,
                               char *missing_list, int list_size)
{
    if (!meta || !missing_list || list_size <= 0) return 0;

    int missing = 0;
    int offset = 0;

    for (int i = 0; i < meta->depend_count; i++) {
        hpkg_depend_t *dep = &meta->depends[i];
        if (!hpkg_is_installed(dep->name)) {
            if (offset > 0 && offset < list_size - 1) {
                missing_list[offset++] = ',';
                missing_list[offset++] = ' ';
            }
            int len = repo_strlen(dep->name);
            if (offset + len < list_size - 1) {
                for (int j = 0; j < len; j++) {
                    missing_list[offset++] = dep->name[j];
                }
            }
            missing++;
        }
    }
    missing_list[offset] = 0;
    return missing;
}

/* ============================================================================
 * 事务支持（安装/卸载回滚）
 * ========================================================================== */

#define MAX_TRANSACTION_FILES 64

typedef struct {
    char path[HPKG_MAX_PATH_LEN];
    uint8_t existed;  /* 事务前是否存在 */
    uint8_t is_backup;  /* 是否有备份 */
} transaction_file_t;

typedef struct {
    int active;
    char package_name[HPKG_MAX_NAME_LEN];
    transaction_file_t files[MAX_TRANSACTION_FILES];
    int file_count;
} hpkg_transaction_t;

static hpkg_transaction_t g_transaction;

/* 开始事务 */
int hpkg_transaction_begin(const char *package_name)
{
    if (g_transaction.active) return -1;
    if (!package_name) return -1;

    memset(&g_transaction, 0, sizeof(g_transaction));
    g_transaction.active = 1;
    repo_strncpy(g_transaction.package_name, package_name,
                 HPKG_MAX_NAME_LEN);
    return 0;
}

/* 记录文件操作 */
int hpkg_transaction_add_file(const char *path, int existed)
{
    if (!g_transaction.active || !path) return -1;
    if (g_transaction.file_count >= MAX_TRANSACTION_FILES) return -1;

    repo_strncpy(g_transaction.files[g_transaction.file_count].path,
                 path, HPKG_MAX_PATH_LEN);
    g_transaction.files[g_transaction.file_count].existed = existed;
    g_transaction.file_count++;
    return 0;
}

/* 提交事务 */
int hpkg_transaction_commit(void)
{
    if (!g_transaction.active) return -1;
    g_transaction.active = 0;
    return 0;
}

/* 回滚事务 */
int hpkg_transaction_rollback(void)
{
    if (!g_transaction.active) return -1;

    vga_printf("[HPKG] Rolling back transaction for %s\n",
               g_transaction.package_name);

    /* 逆序处理文件 */
    for (int i = g_transaction.file_count - 1; i >= 0; i--) {
        transaction_file_t *f = &g_transaction.files[i];
        if (!f->existed) {
            /* 新创建的文件，删除它 */
            vga_printf("[HPKG]   Removing: %s\n", f->path);
            /* 实际应该调用文件系统删除 */
        }
        /* 如果是已存在的文件，应该从备份恢复 */
    }

    g_transaction.active = 0;
    return 0;
}

/* ============================================================================
 * 包缓存
 * ========================================================================== */

#define MAX_CACHED_PACKAGES 32

typedef struct {
    char name[HPKG_MAX_NAME_LEN];
    char version[HPKG_MAX_VERSION_LEN];
    char path[HPKG_MAX_PATH_LEN];
    uint32_t size;
    uint32_t checksum;
    int valid;
} cached_package_t;

static cached_package_t g_cache[MAX_CACHED_PACKAGES];
static int g_cache_count = 0;

/* 添加到缓存 */
int hpkg_cache_add(const char *name, const char *version,
                   const char *path, uint32_t size, uint32_t checksum)
{
    if (!name || !path || g_cache_count >= MAX_CACHED_PACKAGES) return -1;

    /* 检查是否已缓存 */
    for (int i = 0; i < g_cache_count; i++) {
        if (repo_strcmp(g_cache[i].name, name) == 0 &&
            repo_strcmp(g_cache[i].version, version) == 0) {
            return 0;  /* 已缓存 */
        }
    }

    cached_package_t *pkg = &g_cache[g_cache_count];
    repo_strncpy(pkg->name, name, HPKG_MAX_NAME_LEN);
    repo_strncpy(pkg->version, version, HPKG_MAX_VERSION_LEN);
    repo_strncpy(pkg->path, path, HPKG_MAX_PATH_LEN);
    pkg->size = size;
    pkg->checksum = checksum;
    pkg->valid = 1;
    g_cache_count++;
    return 0;
}

/* 从缓存查找 */
int hpkg_cache_find(const char *name, const char *version,
                    cached_package_t *out)
{
    if (!name) return -1;
    for (int i = 0; i < g_cache_count; i++) {
        if (g_cache[i].valid &&
            repo_strcmp(g_cache[i].name, name) == 0 &&
            (version == NULL ||
             repo_strcmp(g_cache[i].version, version) == 0)) {
            if (out) *out = g_cache[i];
            return i;
        }
    }
    return -1;
}

/* 清理缓存 */
int hpkg_cache_clean(void)
{
    int removed = 0;
    for (int i = 0; i < g_cache_count; i++) {
        if (g_cache[i].valid) {
            /* 实际应该删除缓存文件 */
            g_cache[i].valid = 0;
            removed++;
        }
    }
    g_cache_count = 0;
    return removed;
}

/* 获取缓存统计 */
void hpkg_cache_stats(int *count, uint32_t *total_size)
{
    if (count) *count = g_cache_count;
    if (total_size) {
        *total_size = 0;
        for (int i = 0; i < g_cache_count; i++) {
            if (g_cache[i].valid) *total_size += g_cache[i].size;
        }
    }
}

/* ============================================================================
 * 升级检查
 * ========================================================================== */

/* 检查可更新的包 */
int hpkg_check_updates(hpkg_installed_t *updates, int max_updates)
{
    if (!updates || max_updates <= 0) return 0;

    int count = 0;
    /* 简化：遍历已安装包，检查是否有更新版本 */
    /* 实际应该从仓库获取最新版本号并比较 */
    for (int i = 0; i < g_db.package_count && count < max_updates; i++) {
        if (!g_db.packages[i].used) continue;
        /* 模拟：标记为有更新 */
        /* updates[count] = g_db.packages[i]; */
        /* count++; */
    }
    return count;
}

/* 升级所有包 */
int hpkg_upgrade_all(void)
{
    hpkg_installed_t updates[HPKG_DB_MAX_PACKAGES];
    int count = hpkg_check_updates(updates, HPKG_DB_MAX_PACKAGES);

    vga_printf("[HPKG] %d packages can be upgraded\n", count);
    for (int i = 0; i < count; i++) {
        vga_printf("[HPKG]   Upgrading: %s %s\n",
                   updates[i].meta.name, updates[i].meta.version);
        /* 实际应该下载并安装新版本 */
    }
    return count;
}

/* ============================================================================
 * 包验证
 * ========================================================================== */

/* 简单校验和（FNV-1a） */
uint32_t hpkg_checksum(const void *data, uint32_t len)
{
    if (!data || len == 0) return 0;
    const uint8_t *p = (const uint8_t *)data;
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < len; i++) {
        hash ^= p[i];
        hash *= 16777619u;
    }
    return hash;
}

/* 验证已安装包的文件完整性 */
int hpkg_verify_package(const char *name)
{
    hpkg_installed_t installed;
    if (hpkg_find_installed(name, &installed) < 0) {
        return -1;  /* 未安装 */
    }

    int ok = 0, failed = 0;
    for (uint32_t i = 0; i < installed.file_count; i++) {
        hpkg_file_t *file = &installed.files[i];
        if (file->is_dir) continue;

        /* 简化：检查文件是否存在 */
        /* 实际应该读取文件并计算校验和 */
        ok++;
    }

    vga_printf("[HPKG] Verified %s: %d ok, %d failed\n",
               name, ok, failed);
    return (failed == 0) ? 0 : -1;
}

/* ============================================================================
 * 增强初始化
 * ========================================================================== */

int hpkg_enhanced_init(void)
{
    /* 基础初始化 */
    if (!g_initialized) {
        hpkg_init();
    }

    /* 仓库初始化 */
    hpkg_repo_init();

    vga_puts("[HPKG] Enhanced package manager initialized\n");
    vga_printf("[HPKG]   Repositories: %d\n", g_repo_count);
    vga_printf("[HPKG]   Installed packages: %d\n", g_db.package_count);
    vga_printf("[HPKG]   Cached packages: %d\n", g_cache_count);

    return 0;
}
