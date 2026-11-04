/* ============================================================================
 * 赫普OS - 应用生态
 * 原生应用管理 + 应用商店 + SDK + 评论 + 权限
 * ========================================================================== */

#include <stdint.h>
#include "appeco.h"
#include "vga.h"
#include "string.h"

static appeco_t eco;

void appeco_init(void)
{
    memset(&eco, 0, sizeof(appeco_t));
    eco.initialized = 1;
    eco.auto_update = 1;
    eco.update_over_wifi_only = 1;

    /* 系统应用 */
    const char *sys_apps[] = {" set ", " 电话 ", " SMS ", " camera ", " gallery ", " 音乐 ", " video ", " 浏览器 ",
                              " file manager ", " clock ", " calculator ", " calendar ", " 邮件 ", " 地 map ", " app store ", " Hepu assistant "};
    const char *pkg_names[] = {"com.hepu.settings", "com.hepu.phone", "com.hepu.sms", "com.hepu.camera",
                               "com.hepu.gallery", "com.hepu.music", "com.hepu.video", "com.hepu.browser",
                               "com.hepu.files", "com.hepu.clock", "com.hepu.calculator", "com.hepu.calendar",
                               "com.hepu.mail", "com.hepu.maps", "com.hepu.store", "com.hepu.assistant"};
    for (int i = 0; i < 16; i++) {
        eco.apps[i].used = 1;
        eco.apps[i].id = i;
        strncpy(eco.apps[i].name, sys_apps[i], 63);
        strncpy(eco.apps[i].package_name, pkg_names[i], 127);
        strncpy(eco.apps[i].version, "1.0.0", 31);
        eco.apps[i].version_code = 1;
        eco.apps[i].category = i < 4 ? APP_CAT_SYSTEM : APP_CAT_TOOLS;
        eco.apps[i].state = APP_STATE_INSTALLED;
        strncpy(eco.apps[i].developer, " Hepu 官方 ", 63);
        eco.apps[i].install_size = 1024 * 1024 * (i + 1);
        eco.apps[i].installed = 1;
        eco.apps[i].system_app = 1;
        eco.apps[i].removable = 0;
        eco.apps[i].signature_verified = 1;
    }
    eco.app_count = 16;

    /* 应用商店应用 */
    const char *store_apps[] = {" 微信 ", " payment 宝 ", " 抖音 ", " 淘宝 ", " 美团 ", " high 德地 map ", " 网易云音乐 ", " 哔哩哔哩 "};
    for (int i = 0; i < 8; i++) {
        eco.store_apps[i].used = 1;
        eco.store_apps[i].id = i;
        strncpy(eco.store_apps[i].name, store_apps[i], 63);
        eco.store_apps[i].category = APP_CAT_SOCIAL;
        eco.store_apps[i].rating = 4;
        eco.store_apps[i].rating_count = 1000000;
        eco.store_apps[i].downloads = 10000000;
        eco.store_apps[i].price = 0;
        eco.store_apps[i].installed = 0;
    }
    eco.store_count = 8;

    /* SDK模块 */
    const char *sdk_names[] = {"UI components 库 ", " net 库 ", " map 像库 ", " audio 库 ", " video 库 ", " 地 map SDK", " payment SDK", " push SDK",
                               " stats SDK", " 广告 SDK", " 分享 SDK", " 登录 SDK", " 定 bit SDK", " sensors SDK", " BT SDK", "AI SDK"};
    for (int i = 0; i < 16; i++) {
        eco.sdk_modules[i].used = 1;
        eco.sdk_modules[i].id = i;
        strncpy(eco.sdk_modules[i].name, sdk_names[i], 63);
        eco.sdk_modules[i].version = 1;
        eco.sdk_modules[i].installed = 1;
        eco.sdk_modules[i].api_count = 20 + i * 5;
    }
    eco.sdk_count = 16;

    vga_puts("[APPECO] app ecosystem init done \n");
    vga_printf(" system apps: %d, store apps: %d, SDK module: %d \n",
               eco.app_count, eco.store_count, eco.sdk_count);
}

/* 应用管理 */
int appeco_install(const char *package_path)
{
    (void)package_path;
    for (int i = 0; i < APPECO_MAX_APPS; i++) {
        if (!eco.apps[i].used) {
            memset(&eco.apps[i], 0, sizeof(app_info_t));
            eco.apps[i].used = 1;
            eco.apps[i].id = i;
            strncpy(eco.apps[i].name, " 新 app ", 63);
            eco.apps[i].state = APP_STATE_INSTALLED;
            eco.apps[i].installed = 1;
            eco.apps[i].signature_verified = 1;
            eco.app_count++;
            eco.total_installs++;
            vga_puts("[APPECO] app installed \n");
            return i;
        }
    }
    return -1;
}

int appeco_uninstall(int app_id)
{
    if (app_id < 0 || app_id >= APPECO_MAX_APPS || !eco.apps[app_id].used) return -1;
    if (eco.apps[app_id].system_app && !eco.apps[app_id].removable) return -1;
    eco.apps[app_id].used = 0;
    eco.apps[app_id].state = APP_STATE_NOT_INSTALLED;
    eco.apps[app_id].installed = 0;
    eco.app_count--;
    eco.total_uninstalls++;
    return 0;
}

int appeco_update(int app_id)
{
    if (app_id < 0 || app_id >= APPECO_MAX_APPS || !eco.apps[app_id].used) return -1;
    eco.apps[app_id].version_code++;
    eco.total_updates++;
    vga_printf("[APPECO] app update: %s\n", eco.apps[app_id].name);
    return 0;
}

int appeco_launch(int app_id)
{
    if (app_id < 0 || app_id >= APPECO_MAX_APPS || !eco.apps[app_id].used) return -1;
    eco.apps[app_id].state = APP_STATE_RUNNING;
    eco.apps[app_id].launch_count++;
    eco.apps[app_id].last_launch = 0;
    eco.running_apps++;
    eco.total_launches++;
    return 0;
}

int appeco_close(int app_id)
{
    if (app_id < 0 || app_id >= APPECO_MAX_APPS || !eco.apps[app_id].used) return -1;
    eco.apps[app_id].state = APP_STATE_STOPPED;
    if (eco.running_apps > 0) eco.running_apps--;
    return 0;
}

int appeco_get_app(int app_id, app_info_t *app)
{
    if (app_id < 0 || app_id >= APPECO_MAX_APPS || !eco.apps[app_id].used || !app) return -1;
    memcpy(app, &eco.apps[app_id], sizeof(app_info_t));
    return 0;
}

int appeco_list_apps(app_info_t *apps, int max)
{
    if (!apps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < APPECO_MAX_APPS && count < max; i++) {
        if (eco.apps[i].used) memcpy(&apps[count++], &eco.apps[i], sizeof(app_info_t));
    }
    return count;
}

int appeco_list_by_category(app_category_t category, app_info_t *apps, int max)
{
    if (!apps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < APPECO_MAX_APPS && count < max; i++) {
        if (eco.apps[i].used && eco.apps[i].category == category)
            memcpy(&apps[count++], &eco.apps[i], sizeof(app_info_t));
    }
    return count;
}

int appeco_get_by_package(const char *package, app_info_t *app)
{
    if (!package || !app) return -1;
    for (int i = 0; i < APPECO_MAX_APPS; i++) {
        if (eco.apps[i].used && strcmp(eco.apps[i].package_name, package) == 0) {
            memcpy(app, &eco.apps[i], sizeof(app_info_t));
            return i;
        }
    }
    return -1;
}

int appeco_set_default(int app_id, int is_default)
{
    if (app_id < 0 || app_id >= APPECO_MAX_APPS || !eco.apps[app_id].used) return -1;
    eco.apps[app_id].default_app = is_default;
    return 0;
}

int appeco_disable(int app_id)
{
    if (app_id < 0 || app_id >= APPECO_MAX_APPS || !eco.apps[app_id].used) return -1;
    eco.apps[app_id].state = APP_STATE_DISABLED;
    return 0;
}

int appeco_enable(int app_id)
{
    if (app_id < 0 || app_id >= APPECO_MAX_APPS || !eco.apps[app_id].used) return -1;
    eco.apps[app_id].state = APP_STATE_INSTALLED;
    return 0;
}

int appeco_clear_data(int app_id)
{
    if (app_id < 0 || app_id >= APPECO_MAX_APPS || !eco.apps[app_id].used) return -1;
    eco.apps[app_id].data_size = 0;
    return 0;
}

int appeco_clear_cache(int app_id)
{
    if (app_id < 0 || app_id >= APPECO_MAX_APPS || !eco.apps[app_id].used) return -1;
    eco.apps[app_id].cache_size = 0;
    return 0;
}

int appeco_grant_permission(int app_id, const char *permission)
{
    (void)permission;
    if (app_id < 0 || app_id >= APPECO_MAX_APPS || !eco.apps[app_id].used) return -1;
    return 0;
}

int appeco_revoke_permission(int app_id, const char *permission)
{
    (void)permission;
    if (app_id < 0 || app_id >= APPECO_MAX_APPS || !eco.apps[app_id].used) return -1;
    return 0;
}

/* 应用商店 */
int appeco_store_search(const char *query, store_app_t *results, int max)
{
    (void)query;
    if (!results || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < APPECO_MAX_APPS && count < max; i++) {
        if (eco.store_apps[i].used) memcpy(&results[count++], &eco.store_apps[i], sizeof(store_app_t));
    }
    return count;
}

int appeco_store_list(store_app_t *apps, int max)
{
    if (!apps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < APPECO_MAX_APPS && count < max; i++) {
        if (eco.store_apps[i].used) memcpy(&apps[count++], &eco.store_apps[i], sizeof(store_app_t));
    }
    return count;
}

int appeco_store_list_by_category(app_category_t category, store_app_t *apps, int max)
{
    if (!apps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < APPECO_MAX_APPS && count < max; i++) {
        if (eco.store_apps[i].used && eco.store_apps[i].category == category)
            memcpy(&apps[count++], &eco.store_apps[i], sizeof(store_app_t));
    }
    return count;
}

int appeco_store_get_app(int store_id, store_app_t *app)
{
    if (store_id < 0 || store_id >= APPECO_MAX_APPS || !eco.store_apps[store_id].used || !app) return -1;
    memcpy(app, &eco.store_apps[store_id], sizeof(store_app_t));
    return 0;
}

int appeco_store_install(int store_id)
{
    if (store_id < 0 || store_id >= APPECO_MAX_APPS || !eco.store_apps[store_id].used) return -1;
    eco.store_apps[store_id].installed = 1;
    vga_printf("[APPECO] install from store: %s\n", eco.store_apps[store_id].name);
    return appeco_install(NULL);
}

int appeco_store_get_updates(store_app_t *apps, int max)
{
    if (!apps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < APPECO_MAX_APPS && count < max; i++) {
        if (eco.store_apps[i].used && eco.store_apps[i].has_update)
            memcpy(&apps[count++], &eco.store_apps[i], sizeof(store_app_t));
    }
    return count;
}

int appeco_store_update_all(void)
{
    int count = 0;
    for (int i = 0; i < APPECO_MAX_APPS; i++) {
        if (eco.store_apps[i].used && eco.store_apps[i].has_update) {
            eco.store_apps[i].has_update = 0;
            count++;
        }
    }
    return count;
}

/* 评论 */
int appeco_review_add(int app_id, int rating, const char *title, const char *content)
{
    for (int i = 0; i < APPECO_MAX_REVIEWS; i++) {
        if (!eco.reviews[i].used) {
            memset(&eco.reviews[i], 0, sizeof(app_review_t));
            eco.reviews[i].used = 1;
            eco.reviews[i].id = i;
            eco.reviews[i].app_id = app_id;
            eco.reviews[i].rating = rating;
            if (title) strncpy(eco.reviews[i].title, title, 127);
            if (content) strncpy(eco.reviews[i].content, content, 511);
            eco.review_count++;
            return i;
        }
    }
    return -1;
}

int appeco_review_list(int app_id, app_review_t *reviews, int max)
{
    if (!reviews || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < APPECO_MAX_REVIEWS && count < max; i++) {
        if (eco.reviews[i].used && eco.reviews[i].app_id == app_id)
            memcpy(&reviews[count++], &eco.reviews[i], sizeof(app_review_t));
    }
    return count;
}

int appeco_review_get(int review_id, app_review_t *review)
{
    if (review_id < 0 || review_id >= APPECO_MAX_REVIEWS || !eco.reviews[review_id].used || !review) return -1;
    memcpy(review, &eco.reviews[review_id], sizeof(app_review_t));
    return 0;
}

/* SDK */
int appeco_sdk_install(int module_id)
{
    if (module_id < 0 || module_id >= APPECO_MAX_SDK_MODULES || !eco.sdk_modules[module_id].used) return -1;
    eco.sdk_modules[module_id].installed = 1;
    return 0;
}

int appeco_sdk_uninstall(int module_id)
{
    if (module_id < 0 || module_id >= APPECO_MAX_SDK_MODULES || !eco.sdk_modules[module_id].used) return -1;
    eco.sdk_modules[module_id].installed = 0;
    return 0;
}

int appeco_sdk_list(sdk_module_t *modules, int max)
{
    if (!modules || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < APPECO_MAX_SDK_MODULES && count < max; i++) {
        if (eco.sdk_modules[i].used) memcpy(&modules[count++], &eco.sdk_modules[i], sizeof(sdk_module_t));
    }
    return count;
}

int appeco_sdk_get(int module_id, sdk_module_t *module)
{
    if (module_id < 0 || module_id >= APPECO_MAX_SDK_MODULES || !eco.sdk_modules[module_id].used || !module) return -1;
    memcpy(module, &eco.sdk_modules[module_id], sizeof(sdk_module_t));
    return 0;
}

/* 应用商店设置 */
int appeco_set_auto_update(int enable)
{
    eco.auto_update = enable ? 1 : 0;
    return 0;
}

int appeco_set_update_wifi_only(int enable)
{
    eco.update_over_wifi_only = enable ? 1 : 0;
    return 0;
}

int appeco_set_parental_control(int enable, int content_rating)
{
    eco.parental_control = enable ? 1 : 0;
    eco.content_rating_limit = content_rating;
    return 0;
}

/* 统计 */
int appeco_get_stats(int *installed, int *running, int *store_apps, uint64_t *launches)
{
    if (installed) *installed = eco.app_count;
    if (running) *running = eco.running_apps;
    if (store_apps) *store_apps = eco.store_count;
    if (launches) *launches = eco.total_launches;
    return 0;
}
