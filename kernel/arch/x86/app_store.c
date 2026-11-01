/* ============================================================================
 * 赫普OS - 应用商店
 * ========================================================================== */

#include <stdint.h>
#include "app_store.h"
#include "vga.h"
#include "string.h"

static void tools_strcat(char *dst, const char *src, int max) {
    int dlen = 0; while (dst[dlen] && dlen < max-1) dlen++;
    int i = 0;
    while (src[i] && dlen < max-1) { dst[dlen++] = src[i++]; }
    dst[dlen] = 0;
}


static app_store_t store;

/* 分类名称（V7.43 完全中文化） */
static const char *cat_names[] = {
    "工具", "社交", "效率", "游戏", "娱乐", "新闻", "财经", "健康",
    "教育", "购物", "旅行", "摄影", "音乐", "视频", "地图", "商务",
    "天气", "实用工具", "通讯", "其他"
};

void app_store_init(void)
{
    memset(&store, 0, sizeof(app_store_t));
    store.initialized = 1;
    store.enabled = 1;
    store.auto_update = 1;
    store.update_on_wifi_only = 0;
    store.install_unknown_sources = 0;
    store.parental_control = 0;

    /* 注册分类 */
    for (int i = 0; i < 20; i++) {
        store.categories[i].id = (store_category_t)i;
        strncpy(store.categories[i].name, cat_names[i], 63);
        store.category_count++;
    }

    /* ============================================================
     * 预装系统应用（由赫普OS团队开发，直接预装在系统中）
     * 不出现在应用商店的可安装列表中，用户开箱即用
     * ============================================================ */
    const char *preinstalled[][5] = {
        /* 名称, 描述, 包名, 分类, 系统核心 */
        {"赫普计算器", "科学计算器", "com.hepu.calculator", "17", "1"},
        {"赫普日历", "智能日历", "com.hepu.calendar", "2", "1"},
        {"赫普时钟", "闹钟/世界时钟/秒表", "com.hepu.clock", "17", "1"},
        {"赫普记事本", "简洁文本编辑", "com.hepu.notepad", "2", "1"},
        {"赫普文件", "文件管理器", "com.hepu.filemanager", "17", "1"},
        {"赫普设置", "系统设置", "com.hepu.settings", "17", "1"},
        {"赫普终端", "命令行终端", "com.hepu.terminal", "0", "1"},
        {"赫普任务管理器", "进程/资源监控", "com.hepu.taskmanager", "0", "1"},
        {"赫普系统监控", "实时系统状态", "com.hepu.sysmon", "0", "1"},
        {"赫普浏览器", "安全快速浏览器", "com.hepu.browser", "0", "0"},
        {"赫普相机", "相机应用", "com.hepu.camera", "11", "0"},
        {"赫普音乐", "本地音乐播放器", "com.hepu.music", "12", "0"},
        {"赫普视频", "视频播放器", "com.hepu.video", "13", "0"},
        {"贪吃蛇", "经典游戏", "com.hepu.snake", "3", "0"},
        {"俄罗斯方块", "经典游戏", "com.hepu.tetris", "3", "0"},
        {"扫雷", "经典游戏", "com.hepu.minesweeper", "3", "0"},
    };

    for (int i = 0; i < 16; i++) {
        store.preinstalled[i].used = 1;
        strncpy(store.preinstalled[i].name, preinstalled[i][0], 127);
        strncpy(store.preinstalled[i].description, preinstalled[i][1], 511);
        strncpy(store.preinstalled[i].package_name, preinstalled[i][2], 127);
        strncpy(store.preinstalled[i].version, "1.0.0", 31);
        store.preinstalled[i].category = (store_category_t)(preinstalled[i][3][0] - '0');
        store.preinstalled[i].system_app = (preinstalled[i][4][0] == '1') ? 1 : 0;
        store.preinstalled[i].removable = store.preinstalled[i].system_app ? 0 : 1;
        store.preinstalled_count++;
    }

    /* ============================================================
     * 第三方应用商店（示例，实际由开发者上传）
     * 应用商店只作为第三方应用分发平台
     * ============================================================ */
    const char *third_party[][4] = {
        /* 名称, 描述, 开发者, 分类 */
        {"微信", "即时通讯", "腾讯", "1"},
        {"支付宝", "移动支付", "蚂蚁集团", "6"},
        {"抖音", "短视频", "字节跳动", "4"},
        {"淘宝", "购物", "阿里巴巴", "9"},
    };

    for (int i = 0; i < 4; i++) {
        store.apps[i].used = 1;
        store.apps[i].id = i;
        strncpy(store.apps[i].name, third_party[i][0], 127);
        strncpy(store.apps[i].description, third_party[i][1], 511);
        strncpy(store.apps[i].author, third_party[i][2], 63);
        strncpy(store.apps[i].package_name, "com.thirdparty.", 127);
        tools_strcat(store.apps[i].package_name, third_party[i][0], 256);
        strncpy(store.apps[i].version, "1.0.0", 31);
        store.apps[i].size = (i + 1) * 1024 * 1024;
        store.apps[i].category = (store_category_t)(third_party[i][3][0] - '0');
        store.apps[i].rating = 4.0f + (i % 10) * 0.1f;
        store.apps[i].rating_count = 1000 + i * 100;
        store.apps[i].download_count = 10000 + i * 5000;
        store.apps[i].free = 1;
        store.apps[i].age_rating = 0;
        store.apps[i].installed = 0;  /* 第三方应用默认未安装 */
        store.apps[i].status = APP_STATUS_NOT_INSTALLED;
        store.apps[i].source = APP_SOURCE_THIRD_PARTY;
        store.apps[i].removable = 1;  /* 第三方应用可卸载 */
        store.app_count++;
    }

    /* 注册示例开发者 */
    const char *devs[][3] = {
        {"腾讯", "dev@tencent.com", "https://dev.tencent.com"},
        {"蚂蚁集团", "dev@antgroup.com", "https://dev.antgroup.com"},
        {"字节跳动", "dev@bytedance.com", "https://dev.bytedance.com"},
        {"阿里巴巴", "dev@alibaba.com", "https://dev.alibaba.com"},
    };

    for (int i = 0; i < 4; i++) {
        store.developers[i].used = 1;
        store.developers[i].id = i;
        strncpy(store.developers[i].name, devs[i][0], 63);
        strncpy(store.developers[i].email, devs[i][1], 127);
        strncpy(store.developers[i].website, devs[i][2], 255);
        store.developers[i].verified = 1;
        store.developers[i].app_count = 1;
        store.developer_count++;
    }

    vga_puts("[STORE] app store init done (third-party platform)\n");
    vga_printf(" preinstalled: %d, third-party: %d, developers: %d\n",
               store.preinstalled_count, store.app_count, store.developer_count);
}

/* 应用浏览 */
int app_store_get_app(int id, appstore_app_t *app)
{
    if (id < 0 || id >= STORE_MAX_APPS || !store.apps[id].used || !app) return -1;
    memcpy(app, &store.apps[id], sizeof(appstore_app_t));
    return 0;
}

int app_store_list_apps(appstore_app_t *apps, int max)
{
    if (!apps || max <= 0) return 0;
    int count = store.app_count < max ? store.app_count : max;
    for (int i = 0; i < count; i++)
        memcpy(&apps[i], &store.apps[i], sizeof(appstore_app_t));
    return count;
}

int app_store_list_by_category(store_category_t cat, appstore_app_t *apps, int max)
{
    if (!apps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < STORE_MAX_APPS && count < max; i++) {
        if (store.apps[i].used && store.apps[i].category == cat)
            memcpy(&apps[count++], &store.apps[i], sizeof(appstore_app_t));
    }
    return count;
}

int app_store_list_installed(appstore_app_t *apps, int max)
{
    if (!apps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < STORE_MAX_APPS && count < max; i++) {
        if (store.apps[i].used && store.apps[i].installed)
            memcpy(&apps[count++], &store.apps[i], sizeof(appstore_app_t));
    }
    return count;
}

int app_store_list_updates(appstore_app_t *apps, int max)
{
    if (!apps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < STORE_MAX_APPS && count < max; i++) {
        if (store.apps[i].used && store.apps[i].has_update)
            memcpy(&apps[count++], &store.apps[i], sizeof(appstore_app_t));
    }
    return count;
}

int app_store_list_featured(appstore_app_t *apps, int max)
{
    if (!apps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < STORE_MAX_APPS && count < max && i < 6; i++) {
        if (store.apps[i].used) memcpy(&apps[count++], &store.apps[i], sizeof(appstore_app_t));
    }
    return count;
}

int app_store_search(const char *query, appstore_app_t *results, int max)
{
    if (!query || !results || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < STORE_MAX_APPS && count < max; i++) {
        if (store.apps[i].used) {
            int match = 1;
            int qlen = 0; while (query[qlen]) qlen++;
            for (int j = 0; j < qlen; j++) {
                int found = 0;
                for (int k = 0; store.apps[i].name[k]; k++) {
                    if (store.apps[i].name[k] == query[j]) { found = 1; break; }
                }
                if (!found) { match = 0; break; }
            }
            if (match) memcpy(&results[count++], &store.apps[i], sizeof(appstore_app_t));
        }
    }
    return count;
}

/* 安装/卸载 */
int app_store_install(int app_id)
{
    if (app_id < 0 || app_id >= STORE_MAX_APPS || !store.apps[app_id].used) return -1;
    if (store.apps[app_id].installed) return -1;
    store.apps[app_id].status = APP_STATUS_INSTALLING;
    store.apps[app_id].installed = 1;
    store.apps[app_id].status = APP_STATUS_INSTALLED;
    store.installed_count++;
    store.total_installs++;
    return 0;
}

int app_store_uninstall(int app_id)
{
    if (app_id < 0 || app_id >= STORE_MAX_APPS || !store.apps[app_id].used) return -1;
    if (!store.apps[app_id].installed) return -1;
    store.apps[app_id].status = APP_STATUS_UNINSTALLING;
    store.apps[app_id].installed = 0;
    store.apps[app_id].status = APP_STATUS_NOT_INSTALLED;
    store.installed_count--;
    store.total_uninstalls++;
    return 0;
}

int app_store_update(int app_id)
{
    if (app_id < 0 || app_id >= STORE_MAX_APPS || !store.apps[app_id].used) return -1;
    if (!store.apps[app_id].has_update) return -1;
    store.apps[app_id].status = APP_STATUS_UPDATING;
    strncpy(store.apps[app_id].version, store.apps[app_id].new_version, 31);
    store.apps[app_id].has_update = 0;
    store.apps[app_id].status = APP_STATUS_INSTALLED;
    if (store.updates_available > 0) store.updates_available--;
    store.total_updates++;
    return 0;
}

int app_store_update_all(void)
{
    int count = 0;
    for (int i = 0; i < STORE_MAX_APPS; i++) {
        if (store.apps[i].used && store.apps[i].has_update) {
            app_store_update(i);
            count++;
        }
    }
    return count;
}

int app_store_cancel_install(int app_id)
{
    if (app_id < 0 || app_id >= STORE_MAX_APPS || !store.apps[app_id].used) return -1;
    if (store.apps[app_id].status == APP_STATUS_INSTALLING) {
        store.apps[app_id].status = APP_STATUS_NOT_INSTALLED;
        return 0;
    }
    return -1;
}

int app_store_get_install_progress(int app_id, int *progress)
{
    if (app_id < 0 || app_id >= STORE_MAX_APPS || !store.apps[app_id].used || !progress) return -1;
    *progress = (store.apps[app_id].status == APP_STATUS_INSTALLED) ? 100 : 50;
    return 0;
}

/* 评论 */
int app_store_add_review(int app_id, float rating, const char *title, const char *content)
{
    for (int i = 0; i < STORE_MAX_REVIEWS; i++) {
        if (!store.reviews[i].used) {
            memset(&store.reviews[i], 0, sizeof(store_review_t));
            store.reviews[i].used = 1;
            store.reviews[i].id = i;
            store.reviews[i].app_id = app_id;
            strncpy(store.reviews[i].author, " user ", 63);
            store.reviews[i].rating = rating;
            if (title) strncpy(store.reviews[i].title, title, 127);
            if (content) strncpy(store.reviews[i].content, content, 511);
            store.reviews[i].timestamp = 1;
            store.review_count++;
            return i;
        }
    }
    return -1;
}

int app_store_list_reviews(int app_id, store_review_t *reviews, int max)
{
    if (!reviews || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < STORE_MAX_REVIEWS && count < max; i++) {
        if (store.reviews[i].used && store.reviews[i].app_id == app_id)
            memcpy(&reviews[count++], &store.reviews[i], sizeof(store_review_t));
    }
    return count;
}

int app_store_get_review(int id, store_review_t *review)
{
    if (id < 0 || id >= STORE_MAX_REVIEWS || !store.reviews[id].used || !review) return -1;
    memcpy(review, &store.reviews[id], sizeof(store_review_t));
    return 0;
}

/* 分类 */
int app_store_list_categories(store_category_info_t *categories, int max)
{
    if (!categories || max <= 0) return 0;
    int count = store.category_count < max ? store.category_count : max;
    for (int i = 0; i < count; i++)
        memcpy(&categories[i], &store.categories[i], sizeof(store_category_info_t));
    return count;
}

int app_store_get_category(store_category_t id, store_category_info_t *cat)
{
    if (id >= STORE_MAX_CATEGORIES || !cat) return -1;
    memcpy(cat, &store.categories[id], sizeof(store_category_info_t));
    return 0;
}

/* 设置 */
int app_store_set_auto_update(int enable)
{
    store.auto_update = enable ? 1 : 0;
    return 0;
}

int app_store_set_wifi_only(int enable)
{
    store.update_on_wifi_only = enable ? 1 : 0;
    return 0;
}

int app_store_set_unknown_sources(int allow)
{
    store.install_unknown_sources = allow ? 1 : 0;
    return 0;
}

int app_store_set_parental_control(int enable, int age_limit)
{
    (void)age_limit;
    store.parental_control = enable ? 1 : 0;
    return 0;
}

/* 统计 */
int app_store_get_stats(app_store_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &store, sizeof(app_store_t));
    return 0;
}

/* ============================================================
 * 预装应用管理（系统软件）
 * ============================================================ */

/* 列出所有预装应用 */
int app_store_list_preinstalled(preinstalled_app_t *apps, int max)
{
    if (!apps || max <= 0) return 0;
    int count = store.preinstalled_count < max ? store.preinstalled_count : max;
    for (int i = 0; i < count; i++) {
        if (store.preinstalled[i].used) {
            memcpy(&apps[i], &store.preinstalled[i], sizeof(preinstalled_app_t));
        }
    }
    return count;
}

/* 获取指定预装应用 */
int app_store_get_preinstalled(const char *package_name, preinstalled_app_t *app)
{
    if (!package_name || !app) return -1;
    for (int i = 0; i < STORE_MAX_PREINSTALLED; i++) {
        if (store.preinstalled[i].used &&
            strncmp(store.preinstalled[i].package_name, package_name, 127) == 0) {
            memcpy(app, &store.preinstalled[i], sizeof(preinstalled_app_t));
            return 0;
        }
    }
    return -1;
}

/* 检查是否为预装应用 */
int app_store_is_preinstalled(const char *package_name)
{
    if (!package_name) return 0;
    for (int i = 0; i < STORE_MAX_PREINSTALLED; i++) {
        if (store.preinstalled[i].used &&
            strncmp(store.preinstalled[i].package_name, package_name, 127) == 0) {
            return 1;
        }
    }
    return 0;
}

/* ============================================================
 * 第三方开发者平台
 * ============================================================ */

/* 注册开发者 */
int app_store_register_developer(const char *name, const char *email, const char *website)
{
    if (!name || !email) return -1;

    /* 查找空闲槽位 */
    int dev_id = -1;
    for (int i = 0; i < STORE_MAX_DEVELOPERS; i++) {
        if (!store.developers[i].used) {
            dev_id = i;
            break;
        }
    }
    if (dev_id < 0) return -1;

    store.developers[dev_id].used = 1;
    store.developers[dev_id].id = dev_id;
    strncpy(store.developers[dev_id].name, name, 63);
    strncpy(store.developers[dev_id].email, email, 127);
    if (website) {
        strncpy(store.developers[dev_id].website, website, 255);
    }
    store.developers[dev_id].verified = 0;  /* 待审核 */
    store.developers[dev_id].app_count = 0;
    store.developer_count++;

    vga_printf("[STORE] Developer registered: %s (id=%d)\n", name, dev_id);
    return dev_id;
}

/* 列出所有开发者 */
int app_store_list_developers(store_developer_t *devs, int max)
{
    if (!devs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < STORE_MAX_DEVELOPERS && count < max; i++) {
        if (store.developers[i].used) {
            memcpy(&devs[count++], &store.developers[i], sizeof(store_developer_t));
        }
    }
    return count;
}

/* 获取开发者信息 */
int app_store_get_developer(int id, store_developer_t *dev)
{
    if (id < 0 || id >= STORE_MAX_DEVELOPERS || !store.developers[id].used || !dev) {
        return -1;
    }
    memcpy(dev, &store.developers[id], sizeof(store_developer_t));
    return 0;
}

/* 开发者上传应用 */
int app_store_upload_app(int developer_id, const char *name, const char *desc,
                          const char *version, uint64_t size,
                          store_category_t category)
{
    if (developer_id < 0 || developer_id >= STORE_MAX_DEVELOPERS ||
        !store.developers[developer_id].used || !name) {
        return -1;
    }

    /* 查找空闲应用槽位 */
    int app_id = -1;
    for (int i = 0; i < STORE_MAX_APPS; i++) {
        if (!store.apps[i].used) {
            app_id = i;
            break;
        }
    }
    if (app_id < 0) return -1;

    store.apps[app_id].used = 1;
    store.apps[app_id].id = app_id;
    strncpy(store.apps[app_id].name, name, 127);
    if (desc) strncpy(store.apps[app_id].description, desc, 511);
    strncpy(store.apps[app_id].author, store.developers[developer_id].name, 63);
    strncpy(store.apps[app_id].package_name, "com.thirdparty.", 127);
    tools_strcat(store.apps[app_id].package_name, name, 256);
    if (version) strncpy(store.apps[app_id].version, version, 31);
    store.apps[app_id].size = size;
    store.apps[app_id].category = category;
    store.apps[app_id].rating = 0;
    store.apps[app_id].rating_count = 0;
    store.apps[app_id].download_count = 0;
    store.apps[app_id].free = 1;
    store.apps[app_id].installed = 0;
    store.apps[app_id].status = APP_STATUS_NOT_INSTALLED;
    store.apps[app_id].source = APP_SOURCE_THIRD_PARTY;
    store.apps[app_id].removable = 1;
    store.app_count++;
    store.developers[developer_id].app_count++;

    vga_printf("[STORE] App uploaded: %s by %s (id=%d)\n",
               name, store.developers[developer_id].name, app_id);
    return app_id;
}

/* 审核应用 */
int app_store_review_app(int app_id, int approved, const char *reason)
{
    if (app_id < 0 || app_id >= STORE_MAX_APPS || !store.apps[app_id].used) {
        return -1;
    }

    if (approved) {
        vga_printf("[STORE] App approved: %s\n", store.apps[app_id].name);
    } else {
        vga_printf("[STORE] App rejected: %s, reason: %s\n",
                   store.apps[app_id].name, reason ? reason : "unknown");
        /* 下架应用 */
        store.apps[app_id].used = 0;
        store.app_count--;
    }

    return 0;
}
