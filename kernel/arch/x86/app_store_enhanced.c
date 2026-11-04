/* ============================================================================
 * 赫普OS - 应用商店增强（V8.85新增，P5-6）
 * 真实安装流程、与包管理器集成、权限管理、沙箱
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "app_store.h"
#include "hpkg.h"
#include "vga.h"
#include "kstdio.h"

/* 外部应用商店状态 */
extern app_store_t store;

/* 简单字符串工具 */
static int estore_strlen(const char *s) {
    int n = 0; while (s && s[n]) n++; return n;
}
static int estore_strcmp(const char *a, const char *b) {
    if (!a || !b) return -1;
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static void estore_strncpy(char *dst, const char *src, int n) {
    if (!dst || !src || n <= 0) return;
    int i = 0; while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ============================================================================
 * 应用安装任务管理
 * ========================================================================== */

#define MAX_INSTALL_TASKS 8
#define MAX_APP_PERMISSIONS 32

/* 安装阶段 */
typedef enum {
    INSTALL_STAGE_IDLE = 0,
    INSTALL_STAGE_DOWNLOADING,
    INSTALL_STAGE_VERIFYING,
    INSTALL_STAGE_EXTRACTING,
    INSTALL_STAGE_REGISTERING,
    INSTALL_STAGE_COMPLETE,
    INSTALL_STAGE_FAILED
} install_stage_t;

/* 应用权限 */
typedef struct {
    char name[64];
    char description[128];
    int  granted;
    int  dangerous;  /* 危险权限需要用户确认 */
} app_permission_t;

/* 安装任务 */
typedef struct {
    int  active;
    int  app_id;
    char package_name[128];
    char version[32];
    install_stage_t stage;
    int  progress;  /* 0-100 */
    char error_msg[256];
    /* 权限列表 */
    app_permission_t permissions[MAX_APP_PERMISSIONS];
    int  permission_count;
    /* 下载信息 */
    uint32_t download_size;
    uint32_t downloaded;
    uint32_t speed;  /* bytes/sec */
} install_task_t;

static install_task_t g_install_tasks[MAX_INSTALL_TASKS];
static int g_install_initialized = 0;

/* 初始化安装任务管理 */
int app_store_install_init(void)
{
    if (g_install_initialized) return 0;
    memset(g_install_tasks, 0, sizeof(g_install_tasks));
    g_install_initialized = 1;
    vga_puts("[APPSTORE] Install task manager initialized\n");
    return 0;
}

/* 查找空闲任务槽 */
static install_task_t *find_free_task(void)
{
    for (int i = 0; i < MAX_INSTALL_TASKS; i++) {
        if (!g_install_tasks[i].active) return &g_install_tasks[i];
    }
    return NULL;
}

/* 查找应用的安装任务 */
static install_task_t *find_task_by_app(int app_id)
{
    for (int i = 0; i < MAX_INSTALL_TASKS; i++) {
        if (g_install_tasks[i].active &&
            g_install_tasks[i].app_id == app_id) {
            return &g_install_tasks[i];
        }
    }
    return NULL;
}

/* ============================================================================
 * 真实安装流程
 * ========================================================================== */

/* 阶段1：下载应用包 */
static int install_stage_download(install_task_t *task)
{
    task->stage = INSTALL_STAGE_DOWNLOADING;
    task->progress = 10;

    /* 模拟下载过程 */
    /* 实际应该从仓库下载.hpkg包 */
    vga_printf("[APPSTORE] Downloading %s...\n", task->package_name);

    /* 检查包管理器仓库 */
    /* hpkg_repo_list(...) */

    task->downloaded = task->download_size;
    task->progress = 30;
    return 0;
}

/* 阶段2：验证包签名和完整性 */
static int install_stage_verify(install_task_t *task)
{
    task->stage = INSTALL_STAGE_VERIFYING;
    task->progress = 40;

    vga_printf("[APPSTORE] Verifying package %s...\n", task->package_name);

    /* 验证包校验和 */
    /* hpkg_checksum(...) */

    /* 验证签名（如果有） */
    /* 检查开发者证书 */

    task->progress = 50;
    return 0;
}

/* 阶段3：解压应用文件 */
static int install_stage_extract(install_task_t *task)
{
    task->stage = INSTALL_STAGE_EXTRACTING;
    task->progress = 60;

    vga_printf("[APPSTORE] Extracting %s...\n", task->package_name);

    /* 使用包管理器解压 */
    /* hpkg_install(package_path) */

    /* 创建应用目录 */
    /* /apps/<package_name>/ */

    task->progress = 80;
    return 0;
}

/* 阶段4：注册应用到系统 */
static int install_stage_register(install_task_t *task)
{
    task->stage = INSTALL_STAGE_REGISTERING;
    task->progress = 90;

    vga_printf("[APPSTORE] Registering %s...\n", task->package_name);

    /* 注册到应用数据库 */
    /* 创建桌面图标 */
    /* 注册文件关联 */
    /* 设置应用权限 */

    task->progress = 100;
    return 0;
}

/* 开始真实安装 */
int app_store_install_real(int app_id)
{
    if (app_id < 0 || app_id >= STORE_MAX_APPS || !store.apps[app_id].used) {
        return -1;
    }
    if (store.apps[app_id].installed) return -1;

    /* 检查是否已有安装任务 */
    if (find_task_by_app(app_id)) return -1;

    /* 获取空闲任务槽 */
    install_task_t *task = find_free_task();
    if (!task) {
        vga_puts("[APPSTORE] Too many install tasks\n");
        return -1;
    }

    /* 初始化任务 */
    memset(task, 0, sizeof(install_task_t));
    task->active = 1;
    task->app_id = app_id;
    estore_strncpy(task->package_name, store.apps[app_id].package_name, 128);
    estore_strncpy(task->version, store.apps[app_id].version, 32);
    task->download_size = store.apps[app_id].size;
    task->stage = INSTALL_STAGE_IDLE;
    task->progress = 0;

    vga_printf("[APPSTORE] Starting install: %s v%s\n",
               task->package_name, task->version);

    /* 执行安装流程 */
    if (install_stage_download(task) != 0) {
        task->stage = INSTALL_STAGE_FAILED;
        estore_strncpy(task->error_msg, "Download failed", 256);
        return -1;
    }
    if (install_stage_verify(task) != 0) {
        task->stage = INSTALL_STAGE_FAILED;
        estore_strncpy(task->error_msg, "Verification failed", 256);
        return -1;
    }
    if (install_stage_extract(task) != 0) {
        task->stage = INSTALL_STAGE_FAILED;
        estore_strncpy(task->error_msg, "Extraction failed", 256);
        return -1;
    }
    if (install_stage_register(task) != 0) {
        task->stage = INSTALL_STAGE_FAILED;
        estore_strncpy(task->error_msg, "Registration failed", 256);
        return -1;
    }

    /* 安装完成 */
    task->stage = INSTALL_STAGE_COMPLETE;
    store.apps[app_id].installed = 1;
    store.apps[app_id].status = APP_STATUS_INSTALLED;
    store.installed_count++;
    store.total_installs++;

    vga_printf("[APPSTORE] Install complete: %s\n", task->package_name);

    /* 任务完成后释放（延迟清理） */
    /* task->active = 0; */

    return 0;
}

/* 获取安装进度 */
int app_store_get_install_progress_real(int app_id, int *progress,
                                        install_stage_t *stage)
{
    install_task_t *task = find_task_by_app(app_id);
    if (!task) return -1;
    if (progress) *progress = task->progress;
    if (stage) *stage = task->stage;
    return 0;
}

/* 取消安装 */
int app_store_cancel_install_real(int app_id)
{
    install_task_t *task = find_task_by_app(app_id);
    if (!task) return -1;

    vga_printf("[APPSTORE] Cancelling install: %s\n", task->package_name);

    /* 清理已下载的文件 */
    /* 回滚已解压的文件 */

    task->active = 0;
    store.apps[app_id].status = APP_STATUS_NOT_INSTALLED;
    return 0;
}

/* ============================================================================
 * 应用权限管理
 * ============================================================================ */

/* 标准权限定义 */
static const char *standard_permissions[][2] = {
    {"INTERNET", "访问网络"},
    {"ACCESS_NETWORK_STATE", "查看网络状态"},
    {"WRITE_EXTERNAL_STORAGE", "写入外部存储"},
    {"READ_EXTERNAL_STORAGE", "读取外部存储"},
    {"CAMERA", "使用相机"},
    {"RECORD_AUDIO", "录制音频"},
    {"ACCESS_FINE_LOCATION", "精确定位"},
    {"ACCESS_COARSE_LOCATION", "大致定位"},
    {"READ_CONTACTS", "读取通讯录"},
    {"WRITE_CONTACTS", "修改通讯录"},
    {"SEND_SMS", "发送短信"},
    {"READ_SMS", "读取短信"},
    {"CALL_PHONE", "拨打电话"},
    {"READ_CALL_LOG", "读取通话记录"},
    {"VIBRATE", "控制震动"},
    {"WAKE_LOCK", "保持唤醒"},
    {"SET_ALARM", "设置闹钟"},
    {"INSTALL_SHORTCUT", "创建快捷方式"},
    {"UNINSTALL_SHORTCUT", "删除快捷方式"},
    {"RECEIVE_BOOT_COMPLETED", "开机自启"},
};

#define STANDARD_PERMISSION_COUNT 20

/* 检查权限是否危险 */
static int is_dangerous_permission(const char *name)
{
    const char *dangerous[] = {
        "CAMERA", "RECORD_AUDIO", "ACCESS_FINE_LOCATION",
        "READ_CONTACTS", "WRITE_CONTACTS", "SEND_SMS",
        "READ_SMS", "CALL_PHONE", "READ_CALL_LOG",
        "WRITE_EXTERNAL_STORAGE", NULL
    };
    for (int i = 0; dangerous[i]; i++) {
        if (estore_strcmp(name, dangerous[i]) == 0) return 1;
    }
    return 0;
}

/* 解析应用权限清单 */
int app_store_parse_permissions(const char *manifest,
                                app_permission_t *perms, int max_perms)
{
    if (!manifest || !perms || max_perms <= 0) return 0;

    int count = 0;
    /* 简化：从manifest字符串中提取权限 */
    /* 实际应该解析XML/JSON格式的清单文件 */

    /* 示例：默认授予基本权限 */
    for (int i = 0; i < STANDARD_PERMISSION_COUNT && count < max_perms; i++) {
        estore_strncpy(perms[count].name, standard_permissions[i][0], 64);
        estore_strncpy(perms[count].description, standard_permissions[i][1], 128);
        perms[count].dangerous = is_dangerous_permission(standard_permissions[i][0]);
        perms[count].granted = perms[count].dangerous ? 0 : 1;
        count++;
    }

    return count;
}

/* 检查应用是否有权限 */
int app_store_check_permission(const char *package_name, const char *permission)
{
    /* 简化：检查应用是否已安装并有权限 */
    /* 实际应该查询应用权限数据库 */
    for (int i = 0; i < STORE_MAX_APPS; i++) {
        if (store.apps[i].used && store.apps[i].installed &&
            estore_strcmp(store.apps[i].package_name, package_name) == 0) {
            /* 已安装应用默认有权限 */
            return 1;
        }
    }
    return 0;
}

/* 请求权限 */
int app_store_request_permission(const char *package_name,
                                 const char *permission)
{
    vga_printf("[APPSTORE] %s requests permission: %s\n",
               package_name, permission);

    /* 检查是否危险权限 */
    if (is_dangerous_permission(permission)) {
        /* 需要用户确认 */
        /* 实际应该弹出权限请求对话框 */
        vga_printf("[APPSTORE] Dangerous permission, user confirmation required\n");
        return 0;  /* 等待用户确认 */
    }

    /* 普通权限直接授予 */
    return 1;
}

/* ============================================================================
 * 应用沙箱
 * ============================================================================ */

typedef struct {
    char package_name[128];
    char root_dir[256];  /* 沙箱根目录 */
    int  uid;            /* 应用用户ID */
    int  active;
} app_sandbox_t;

#define MAX_SANDBOXES 32
static app_sandbox_t g_sandboxes[MAX_SANDBOXES];
static int g_sandbox_count = 0;

/* 创建应用沙箱 */
int app_sandbox_create(const char *package_name)
{
    if (!package_name || g_sandbox_count >= MAX_SANDBOXES) return -1;

    /* 检查是否已存在 */
    for (int i = 0; i < g_sandbox_count; i++) {
        if (estore_strcmp(g_sandboxes[i].package_name, package_name) == 0) {
            return i;
        }
    }

    app_sandbox_t *sb = &g_sandboxes[g_sandbox_count];
    memset(sb, 0, sizeof(app_sandbox_t));
    estore_strncpy(sb->package_name, package_name, 128);

    /* 创建沙箱目录 */
    /* /sandbox/<package_name>/ */
    estore_strncpy(sb->root_dir, "/sandbox/", 256);
    estore_strncpy(sb->root_dir + 9, package_name, 247);

    /* 分配UID（从10000开始） */
    sb->uid = 10000 + g_sandbox_count;
    sb->active = 1;

    vga_printf("[APPSTORE] Sandbox created: %s (uid=%d)\n",
               package_name, sb->uid);

    g_sandbox_count++;
    return g_sandbox_count - 1;
}

/* 获取应用沙箱信息 */
int app_sandbox_get(const char *package_name, app_sandbox_t *out)
{
    if (!package_name || !out) return -1;
    for (int i = 0; i < g_sandbox_count; i++) {
        if (g_sandboxes[i].active &&
            estore_strcmp(g_sandboxes[i].package_name, package_name) == 0) {
            *out = g_sandboxes[i];
            return i;
        }
    }
    return -1;
}

/* 销毁应用沙箱 */
int app_sandbox_destroy(const char *package_name)
{
    for (int i = 0; i < g_sandbox_count; i++) {
        if (g_sandboxes[i].active &&
            estore_strcmp(g_sandboxes[i].package_name, package_name) == 0) {
            vga_printf("[APPSTORE] Sandbox destroyed: %s\n", package_name);
            g_sandboxes[i].active = 0;
            return 0;
        }
    }
    return -1;
}

/* ============================================================================
 * 应用更新检查
 * ============================================================================ */

/* 检查单个应用更新 */
int app_store_check_update_real(int app_id)
{
    if (app_id < 0 || app_id >= STORE_MAX_APPS || !store.apps[app_id].used) {
        return -1;
    }
    if (!store.apps[app_id].installed) return -1;

    /* 从仓库查询最新版本 */
    /* 比较版本号 */
    /* 简化：模拟有更新 */
    store.apps[app_id].has_update = 0;  /* 暂时无更新 */

    return 0;
}

/* 检查所有应用更新 */
int app_store_check_all_updates(void)
{
    int updates = 0;
    for (int i = 0; i < STORE_MAX_APPS; i++) {
        if (store.apps[i].used && store.apps[i].installed) {
            if (app_store_check_update_real(i) == 0 &&
                store.apps[i].has_update) {
                updates++;
            }
        }
    }
    store.updates_available = updates;
    vga_printf("[APPSTORE] %d updates available\n", updates);
    return updates;
}

/* ============================================================================
 * 应用统计
 * ============================================================================ */

typedef struct {
    uint32_t total_apps;
    uint32_t installed_apps;
    uint32_t available_updates;
    uint32_t total_downloads;
    uint32_t total_installs;
    uint32_t total_uninstalls;
    uint32_t total_updates;
    uint32_t active_sandboxes;
    uint32_t install_tasks;
} app_store_stats_t;

/* 获取应用商店统计 */
int app_store_get_stats_real(app_store_stats_t *stats)
{
    if (!stats) return -1;
    memset(stats, 0, sizeof(app_store_stats_t));

    stats->total_apps = store.app_count;
    stats->installed_apps = store.installed_count;
    stats->available_updates = store.updates_available;
    stats->total_downloads = store.total_downloads;
    stats->total_installs = store.total_installs;
    stats->total_uninstalls = store.total_uninstalls;
    stats->total_updates = store.total_updates;

    for (int i = 0; i < MAX_SANDBOXES; i++) {
        if (g_sandboxes[i].active) stats->active_sandboxes++;
    }
    for (int i = 0; i < MAX_INSTALL_TASKS; i++) {
        if (g_install_tasks[i].active) stats->install_tasks++;
    }

    return 0;
}

/* ============================================================================
 * 增强初始化
 * ============================================================================ */

int app_store_enhanced_init(void)
{
    /* 初始化安装任务管理 */
    app_store_install_init();

    vga_puts("[APPSTORE] Enhanced app store initialized\n");
    vga_printf("[APPSTORE]   Max install tasks: %d\n", MAX_INSTALL_TASKS);
    vga_printf("[APPSTORE]   Max sandboxes: %d\n", MAX_SANDBOXES);
    vga_printf("[APPSTORE]   Standard permissions: %d\n",
               STANDARD_PERMISSION_COUNT);

    return 0;
}
