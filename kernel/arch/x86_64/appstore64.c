/* ============================================================================
 * 赫普OS 64位 - 应用商店框架
 * 支持应用浏览、分类、搜索、安装、更新
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);

#define APPSTORE_MAX_APPS 64
#define APPSTORE_NAME_LEN 64
#define APPSTORE_DESC_LEN 128
#define APPSTORE_VERSION_LEN 16

typedef enum {
    CATEGORY_SYSTEM = 0,
    CATEGORY_TOOLS,
    CATEGORY_PRODUCTIVITY,
    CATEGORY_GAMES,
    CATEGORY_MEDIA,
    CATEGORY_NETWORK,
    CATEGORY_DEVELOPMENT,
    CATEGORY_EDUCATION,
    CATEGORY_COUNT
} store_category_t;

typedef enum {
    APP_STATUS_AVAILABLE = 0,
    APP_STATUS_INSTALLED,
    APP_STATUS_INSTALLING,
    APP_STATUS_UPDATE_AVAILABLE,
    APP_STATUS_ERROR
} app_status_t;

typedef struct {
    int          id;
    char         name[APPSTORE_NAME_LEN];
    char         description[APPSTORE_DESC_LEN];
    char         version[APPSTORE_VERSION_LEN];
    store_category_t category;
    uint32_t     size_kb;
    app_status_t status;
    int          installed;
    int          rating;       /* 1-5 */
    int          downloads;
} appstore_app_t;

typedef struct {
    int          app_id;
    int          progress;     /* 0-100 */
    int          stage;        /* 0=download,1=verify,2=extract,3=register */
    int          active;
} install_task_t;

static appstore_app_t appstore_apps[APPSTORE_MAX_APPS];
static int appstore_count = 0;
static install_task_t install_tasks[8] __attribute__((unused));   /* 预留: 安装任务队列尚未实现 */
static int appstore_initialized = 0;

/* 字符串复制 */
static void store_strcpy(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

/* 字符串比较 */

/* 字符串包含 */
static int store_strstr(const char *haystack, const char *needle)
{
    if (!needle || !needle[0]) return 1;
    for (int i = 0; haystack[i]; i++) {
        int j = 0;
        while (needle[j] && haystack[i+j] == needle[j]) j++;
        if (!needle[j]) return 1;
    }
    return 0;
}

/* 添加应用 */
static void add_app(int id, const char *name, const char *desc,
                    const char *ver, store_category_t cat,
                    uint32_t size_kb, int rating, int downloads)
{
    if (appstore_count >= APPSTORE_MAX_APPS) return;
    appstore_app_t *app = &appstore_apps[appstore_count];
    app->id = id;
    store_strcpy(app->name, name, APPSTORE_NAME_LEN);
    store_strcpy(app->description, desc, APPSTORE_DESC_LEN);
    store_strcpy(app->version, ver, APPSTORE_VERSION_LEN);
    app->category = cat;
    app->size_kb = size_kb;
    app->status = APP_STATUS_AVAILABLE;
    app->installed = 0;
    app->rating = rating;
    app->downloads = downloads;
    appstore_count++;
}

/* 初始化应用商店 */
void appstore64_init(void)
{
    appstore_count = 0;
    appstore_initialized = 1;

    /* 系统工具类 */
    add_app(1, "File Manager", "Browse and manage files", "1.0", CATEGORY_SYSTEM, 128, 5, 10000);
    add_app(2, "Task Manager", "Monitor system processes", "1.0", CATEGORY_SYSTEM, 96, 4, 8500);
    add_app(3, "System Settings", "Configure system options", "1.0", CATEGORY_SYSTEM, 64, 5, 12000);
    add_app(4, "Terminal", "Command line interface", "1.0", CATEGORY_SYSTEM, 48, 5, 15000);

    /* 工具类 */
    add_app(10, "Notepad", "Simple text editor", "1.0", CATEGORY_TOOLS, 32, 4, 20000);
    add_app(11, "Calculator", "Basic and scientific calculator", "1.0", CATEGORY_TOOLS, 16, 5, 18000);
    add_app(12, "Clock", "World clock and timer", "1.0", CATEGORY_TOOLS, 24, 4, 9000);

    /* 生产力类 */
    add_app(20, "Text Editor", "Advanced text editor", "1.0", CATEGORY_PRODUCTIVITY, 128, 4, 7500);
    add_app(21, "Calendar", "Calendar and events", "1.0", CATEGORY_PRODUCTIVITY, 64, 4, 6000);

    /* 媒体类 */
    add_app(30, "Image Viewer", "View and manage images", "1.0", CATEGORY_MEDIA, 96, 4, 11000);
    add_app(31, "Music Player", "Play audio files", "1.0", CATEGORY_MEDIA, 128, 3, 8000);

    /* 网络类 */
    add_app(40, "Web Browser", "Browse the internet", "1.0", CATEGORY_NETWORK, 512, 4, 25000);
    add_app(41, "Email Client", "Manage email", "1.0", CATEGORY_NETWORK, 256, 3, 5000);

    /* 开发类 */
    add_app(50, "Code Editor", "Programming editor", "1.0", CATEGORY_DEVELOPMENT, 192, 5, 4000);
    add_app(51, "Hex Viewer", "View binary files", "1.0", CATEGORY_DEVELOPMENT, 32, 4, 2000);

    /* 教育类 */
    add_app(60, "Dictionary", "Word definitions", "1.0", CATEGORY_EDUCATION, 1024, 5, 3000);

    /* 标记预装应用 */
    appstore_apps[0].installed = 1;  /* File Manager */
    appstore_apps[0].status = APP_STATUS_INSTALLED;
    appstore_apps[1].installed = 1;  /* Task Manager */
    appstore_apps[1].status = APP_STATUS_INSTALLED;
    appstore_apps[2].installed = 1;  /* System Settings */
    appstore_apps[2].status = APP_STATUS_INSTALLED;
    appstore_apps[4].installed = 1;  /* Notepad */
    appstore_apps[4].status = APP_STATUS_INSTALLED;

    vga_puts("[APPSTORE] Initialized with ");
    char buf[8];
    int n = appstore_count, i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
    vga_puts(buf);
    vga_puts(" apps (4 preinstalled)\n");
}

/* 获取应用 */
int appstore64_get_app(int id, appstore_app_t *app)
{
    for (int i = 0; i < appstore_count; i++) {
        if (appstore_apps[i].id == id) {
            if (app) *app = appstore_apps[i];
            return 0;
        }
    }
    return -1;
}

/* 列出所有应用 */
int appstore64_list(appstore_app_t *apps, int max)
{
    int count = 0;
    for (int i = 0; i < appstore_count && count < max; i++) {
        apps[count++] = appstore_apps[i];
    }
    return count;
}

/* 按分类列出 */
int appstore64_list_category(store_category_t cat,
                             appstore_app_t *apps, int max)
{
    int count = 0;
    for (int i = 0; i < appstore_count && count < max; i++) {
        if (appstore_apps[i].category == cat) {
            apps[count++] = appstore_apps[i];
        }
    }
    return count;
}

/* 列出已安装应用 */
int appstore64_list_installed(appstore_app_t *apps, int max)
{
    int count = 0;
    for (int i = 0; i < appstore_count && count < max; i++) {
        if (appstore_apps[i].installed) {
            apps[count++] = appstore_apps[i];
        }
    }
    return count;
}

/* 搜索应用 */
int appstore64_search(const char *query, appstore_app_t *results, int max)
{
    int count = 0;
    for (int i = 0; i < appstore_count && count < max; i++) {
        if (store_strstr(appstore_apps[i].name, query) ||
            store_strstr(appstore_apps[i].description, query)) {
            results[count++] = appstore_apps[i];
        }
    }
    return count;
}

/* 安装应用 */
int appstore64_install(int app_id)
{
    for (int i = 0; i < appstore_count; i++) {
        if (appstore_apps[i].id == app_id) {
            if (appstore_apps[i].installed) {
                vga_puts("[APPSTORE] Already installed\n");
                return -1;
            }
            /* 模拟安装 */
            appstore_apps[i].status = APP_STATUS_INSTALLING;
            appstore_apps[i].installed = 1;
            appstore_apps[i].status = APP_STATUS_INSTALLED;
            vga_puts("[APPSTORE] Installed: ");
            vga_puts(appstore_apps[i].name);
            vga_puts("\n");
            return 0;
        }
    }
    return -1;
}

/* 卸载应用 */
int appstore64_uninstall(int app_id)
{
    for (int i = 0; i < appstore_count; i++) {
        if (appstore_apps[i].id == app_id) {
            if (!appstore_apps[i].installed) {
                vga_puts("[APPSTORE] Not installed\n");
                return -1;
            }
            appstore_apps[i].installed = 0;
            appstore_apps[i].status = APP_STATUS_AVAILABLE;
            vga_puts("[APPSTORE] Uninstalled: ");
            vga_puts(appstore_apps[i].name);
            vga_puts("\n");
            return 0;
        }
    }
    return -1;
}

/* 文本模式显示应用列表 */
void appstore64_show_list(void)
{
    const char *cat_names[] = {
        "System", "Tools", "Productivity", "Games",
        "Media", "Network", "Development", "Education"
    };

    vga_puts("\n=== App Store ===\n");
    for (int i = 0; i < appstore_count; i++) {
        appstore_app_t *app = &appstore_apps[i];
        vga_puts("  [");
        char idbuf[8];
        int n = app->id, j = 0;
        if (n == 0) idbuf[j++] = '0';
        while (n > 0) { idbuf[j++] = '0' + n % 10; n /= 10; }
        idbuf[j] = 0;
        for (int k = 0; k < j / 2; k++) {
            char t = idbuf[k]; idbuf[k] = idbuf[j-1-k]; idbuf[j-1-k] = t;
        }
        vga_puts(idbuf);
        vga_puts("] ");
        vga_puts(app->name);
        vga_puts(" (");
        vga_puts(cat_names[app->category]);
        vga_puts(") ");
        if (app->installed) vga_puts("[INSTALLED]");
        vga_puts("\n    ");
        vga_puts(app->description);
        vga_puts("\n");
    }
    vga_puts("==================\n");
}

/* 获取分类名称 */
const char* appstore64_category_name(store_category_t cat)
{
    static const char *names[] = {
        "System", "Tools", "Productivity", "Games",
        "Media", "Network", "Development", "Education"
    };
    if (cat >= 0 && cat < CATEGORY_COUNT) return names[cat];
    return "Unknown";
}

/* 获取应用数量 */
int appstore64_get_count(void)
{
    return appstore_count;
}

/* 获取已安装数量 */
int appstore64_get_installed_count(void)
{
    int count = 0;
    for (int i = 0; i < appstore_count; i++) {
        if (appstore_apps[i].installed) count++;
    }
    return count;
}
