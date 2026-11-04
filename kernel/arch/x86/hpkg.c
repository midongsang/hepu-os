/* ============================================================================
 * 赫普OS - 包管理器核心实现
 * V8.53: 初始实现
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "hpkg.h"
#include "string.h"
#include "vga.h"
#include "kstdio.h"

/* 全局数据库 */
static hpkg_database_t g_db;
static int g_initialized = 0;

/* 简单字符串工具 */
static int hpkg_strlen(const char *s) {
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static int hpkg_strcmp(const char *a, const char *b) {
    if (!a || !b) return -1;
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static void hpkg_strncpy(char *dst, const char *src, int n) {
    if (!dst || !src || n <= 0) return;
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* 查找换行符位置 */
static const char *hpkg_find_line(const char *data, int len, const char *key) {
    int key_len = hpkg_strlen(key);
    for (int i = 0; i < len - key_len; i++) {
        if (data[i] == '\n' || i == 0) {
            int start = (i == 0) ? 0 : i + 1;
            if (start + key_len <= len &&
                memcmp(data + start, key, key_len) == 0) {
                return data + start + key_len;
            }
        }
    }
    return NULL;
}

/* 解析key=value行 */
static int hpkg_parse_kv(const char *line, int max_len, char *value, int val_size) {
    const char *eq = NULL;
    for (int i = 0; i < max_len && line[i]; i++) {
        if (line[i] == '=') { eq = line + i + 1; break; }
    }
    if (!eq) return -1;
    int i = 0;
    while (eq[i] && eq[i] != '\n' && eq[i] != '\r' && i < val_size - 1) {
        value[i] = eq[i];
        i++;
    }
    value[i] = 0;
    return 0;
}

/* 初始化包管理器 */
int hpkg_init(void) {
    if (g_initialized) return 0;

    memset(&g_db, 0, sizeof(g_db));
    g_db.magic = 0x474B5048;  /* "HPKG" 小端 */
    g_db.version = 1;
    g_db.package_count = 0;

    /* 尝试加载已有数据库 */
    hpkg_load_database();

    g_initialized = 1;
    vga_puts("[HPKG] package manager initialized\n");
    return 0;
}

/* 解析包元数据（从meta.ini字符串） */
int hpkg_parse_meta(const char *ini_data, uint32_t ini_len, hpkg_meta_t *meta) {
    if (!ini_data || !meta || ini_len == 0) return -1;

    memset(meta, 0, sizeof(hpkg_meta_t));

    char value[128];

    /* 解析基本字段 */
    const char *p;

    p = hpkg_find_line(ini_data, ini_len, "name=");
    if (p && hpkg_parse_kv(p - 5, 128, value, sizeof(value)) == 0)
        hpkg_strncpy(meta->name, value, HPKG_MAX_NAME_LEN);

    p = hpkg_find_line(ini_data, ini_len, "version=");
    if (p && hpkg_parse_kv(p - 8, 128, value, sizeof(value)) == 0)
        hpkg_strncpy(meta->version, value, HPKG_MAX_VERSION_LEN);

    p = hpkg_find_line(ini_data, ini_len, "description=");
    if (p && hpkg_parse_kv(p - 12, 256, value, sizeof(value)) == 0)
        hpkg_strncpy(meta->description, value, 128);

    p = hpkg_find_line(ini_data, ini_len, "author=");
    if (p && hpkg_parse_kv(p - 7, 128, value, sizeof(value)) == 0)
        hpkg_strncpy(meta->author, value, 64);

    p = hpkg_find_line(ini_data, ini_len, "license=");
    if (p && hpkg_parse_kv(p - 8, 128, value, sizeof(value)) == 0)
        hpkg_strncpy(meta->license, value, 32);

    p = hpkg_find_line(ini_data, ini_len, "install_size=");
    if (p) {
        const char *eq = p;
        while (*eq && *eq != '=') eq++;
        if (*eq == '=') meta->install_size = atoi(eq + 1);
    }

    /* 解析依赖 (depend.0=name>=version) */
    meta->depend_count = 0;
    for (int i = 0; i < HPKG_MAX_DEPENDS; i++) {
        char key[16];
        snprintf(key, sizeof(key), "depend.%d=", i);
        p = hpkg_find_line(ini_data, ini_len, key);
        if (!p) break;

        const char *eq = p;
        while (*eq && *eq != '=') eq++;
        if (*eq != '=') continue;
        eq++;

        /* 解析 name>=version 或 name<=version 或 name */
        int j = 0;
        while (eq[j] && eq[j] != '>' && eq[j] != '<' && eq[j] != '\n' &&
               j < HPKG_MAX_NAME_LEN - 1) {
            meta->depends[i].name[j] = eq[j];
            j++;
        }
        meta->depends[i].name[j] = 0;

        if (eq[j] == '>' && eq[j+1] == '=') {
            hpkg_strncpy(meta->depends[i].min_version, eq + j + 2, HPKG_MAX_VERSION_LEN);
        } else if (eq[j] == '<' && eq[j+1] == '=') {
            hpkg_strncpy(meta->depends[i].max_version, eq + j + 2, HPKG_MAX_VERSION_LEN);
        }

        meta->depend_count++;
    }

    return (meta->name[0]) ? 0 : -1;
}

/* 查找已安装包索引 */
static int hpkg_find_installed(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < HPKG_DB_MAX_PACKAGES; i++) {
        if (g_db.packages[i].used &&
            hpkg_strcmp(g_db.packages[i].meta.name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* 安装包（简化实现：记录元数据，实际文件解压由用户态工具完成） */
int hpkg_install(const char *package_path) {
    if (!package_path || !g_initialized) return -1;

    vga_printf("[HPKG] installing: %s\n", package_path);

    /* 简化实现：从文件名提取包名和版本
     * 格式: name-version.hpkg */
    const char *base = package_path;
    for (const char *p = package_path; *p; p++) {
        if (*p == '/') base = p + 1;
    }

    /* 解析文件名 name-version.hpkg */
    char name[HPKG_MAX_NAME_LEN] = {0};
    char version[HPKG_MAX_VERSION_LEN] = {0};
    int name_len = 0;
    const char *dash = NULL;
    for (const char *p = base; *p && *p != '.'; p++) {
        if (*p == '-' && !dash) dash = p;
        if (!dash && name_len < HPKG_MAX_NAME_LEN - 1) {
            name[name_len++] = *p;
        }
    }
    name[name_len] = 0;
    if (dash) {
        int vlen = 0;
        const char *p = dash + 1;
        while (*p && *p != '.' && vlen < HPKG_MAX_VERSION_LEN - 1) {
            version[vlen++] = *p++;
        }
        version[vlen] = 0;
    }

    if (!name[0]) {
        vga_puts("[HPKG] error: invalid package name\n");
        return -1;
    }

    /* 检查是否已安装 */
    int idx = hpkg_find_installed(name);
    if (idx >= 0) {
        vga_printf("[HPKG] %s already installed (version %s)\n",
                   name, g_db.packages[idx].meta.version);
        return -2;  /* 已安装 */
    }

    /* 查找空槽位 */
    int slot = -1;
    for (int i = 0; i < HPKG_DB_MAX_PACKAGES; i++) {
        if (!g_db.packages[i].used) { slot = i; break; }
    }
    if (slot < 0) {
        vga_puts("[HPKG] error: database full\n");
        return -1;
    }

    /* 记录已安装包 */
    hpkg_installed_t *pkg = &g_db.packages[slot];
    memset(pkg, 0, sizeof(hpkg_installed_t));
    pkg->used = 1;
    hpkg_strncpy(pkg->meta.name, name, HPKG_MAX_NAME_LEN);
    hpkg_strncpy(pkg->meta.version, version[0] ? version : "1.0", HPKG_MAX_VERSION_LEN);
    snprintf(pkg->meta.description, sizeof(pkg->meta.description),
             "Package %s", name);
    pkg->status = HPKG_STATUS_INSTALLED;
    pkg->install_time = 0;  /* TODO: 获取当前时间 */
    pkg->file_count = 0;

    g_db.package_count++;

    vga_printf("[HPKG] installed: %s version %s\n", name, version[0] ? version : "1.0");

    /* 保存数据库 */
    hpkg_save_database();

    return 0;
}

/* 卸载包 */
int hpkg_remove(const char *package_name, int remove_configs) {
    if (!package_name || !g_initialized) return -1;

    int idx = hpkg_find_installed(package_name);
    if (idx < 0) {
        vga_printf("[HPKG] %s not installed\n", package_name);
        return -1;
    }

    vga_printf("[HPKG] removing: %s (version %s)\n",
               package_name, g_db.packages[idx].meta.version);

    /* 简化实现：只从数据库移除，实际文件删除由用户态工具完成 */
    memset(&g_db.packages[idx], 0, sizeof(hpkg_installed_t));
    g_db.package_count--;

    vga_printf("[HPKG] removed: %s\n", package_name);

    hpkg_save_database();
    return 0;
}

/* 更新包 */
int hpkg_update(const char *package_path) {
    if (!package_path) return -1;

    /* 简化实现：先卸载再安装 */
    const char *base = package_path;
    for (const char *p = package_path; *p; p++) {
        if (*p == '/') base = p + 1;
    }

    char name[HPKG_MAX_NAME_LEN] = {0};
    int name_len = 0;
    for (const char *p = base; *p && *p != '-' && *p != '.' && name_len < HPKG_MAX_NAME_LEN - 1; p++) {
        name[name_len++] = *p;
    }
    name[name_len] = 0;

    if (hpkg_is_installed(name)) {
        hpkg_remove(name, 1);
    }
    return hpkg_install(package_path);
}

/* 查询包信息 */
int hpkg_query(const char *package_name, hpkg_installed_t *out) {
    if (!package_name || !out) return -1;
    int idx = hpkg_find_installed(package_name);
    if (idx < 0) return -1;
    memcpy(out, &g_db.packages[idx], sizeof(hpkg_installed_t));
    return 0;
}

/* 列出所有已安装包 */
int hpkg_list(hpkg_installed_t *out_list, int max_count) {
    if (!out_list || max_count <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HPKG_DB_MAX_PACKAGES && count < max_count; i++) {
        if (g_db.packages[i].used) {
            memcpy(&out_list[count++], &g_db.packages[i], sizeof(hpkg_installed_t));
        }
    }
    return count;
}

/* 检查包是否已安装 */
int hpkg_is_installed(const char *package_name) {
    return hpkg_find_installed(package_name) >= 0;
}

/* 检查依赖是否满足 */
int hpkg_check_depends(const hpkg_meta_t *meta, char *missing_list, int list_size) {
    if (!meta || !missing_list || list_size <= 0) return -1;
    missing_list[0] = 0;
    int missing_count = 0;

    for (int i = 0; i < meta->depend_count; i++) {
        if (!hpkg_is_installed(meta->depends[i].name)) {
            if (missing_count > 0) {
                int len = hpkg_strlen(missing_list);
                if (len + 2 < list_size) {
                    missing_list[len++] = ',';
                    missing_list[len++] = ' ';
                    missing_list[len] = 0;
                }
            }
            hpkg_strncpy(missing_list + hpkg_strlen(missing_list),
                         meta->depends[i].name,
                         list_size - hpkg_strlen(missing_list));
            missing_count++;
        }
    }
    return missing_count;
}

/* 保存数据库到磁盘（简化：写入/var/lib/hpkg/db） */
int hpkg_save_database(void) {
    /* 简化实现：数据库保存在内存中，TODO: 写入文件系统 */
    return 0;
}

/* 加载数据库从磁盘 */
int hpkg_load_database(void) {
    /* 简化实现：从内存中恢复，TODO: 从文件系统读取 */
    return 0;
}

/* 获取状态字符串 */
const char *hpkg_status_str(hpkg_status_t status) {
    switch (status) {
        case HPKG_STATUS_NOT_INSTALLED: return "not installed";
        case HPKG_STATUS_INSTALLED: return "installed";
        case HPKG_STATUS_BROKEN: return "broken";
        case HPKG_STATUS_UPDATE_AVAILABLE: return "update available";
        default: return "unknown";
    }
}
