/* ============================================================================
 * 赫普OS - APK 兼容层配置与性能优化
 * ========================================================================== */

#include <stdint.h>
#include "compat_layer.h"
#include "vga.h"
#include "string.h"

static compat_layer_t cl;

void compat_layer_init(void)
{
    memset(&cl, 0, sizeof(compat_layer_t));
    cl.initialized = 1;
    cl.enabled = 1;
    cl.default_translate_mode = CL_TRANSLATE_HYBRID;
    cl.default_perf_mode = CL_PERF_BALANCED;
    cl.default_sandbox_level = 1;
    cl.cache_size_kb = 65536;  /* V6.76: 64MB翻译缓存(原16MB) */

    /* 系统配置文件 */
    struct { const char *name; const char *desc; int gms; int web; int stor; int cam; int bt; int loc; int notif; } profiles[] = {
        {" 完全 compat ", " max compat 性 , all 功能 on ", 1,1,1,1,1,1,1},
        {" balanced mode ", " compat 性 & perf balanced ", 1,1,1,1,0,1,1},
        {" high perf ", " 优先 perf , close 部分 compat ", 1,1,0,0,0,1,1},
        {" 省电 mode ", " 优先省电 , 限制 next 台 ", 0,1,1,0,0,0,1},
        {" strict 沙箱 ", " 最 high security level 别 ", 1,1,1,1,1,1,1},
    };
    for (int i = 0; i < 5; i++) {
        int id = cl_profile_create(profiles[i].name, profiles[i].desc);
        if (id >= 0) {
            cl.profiles[id].is_system = 1;
            cl.profiles[id].gms_compat = profiles[i].gms;
            cl.profiles[id].webview_compat = profiles[i].web;
            cl.profiles[id].storage_compat = profiles[i].stor;
            cl.profiles[id].camera_compat = profiles[i].cam;
            cl.profiles[id].bluetooth_compat = profiles[i].bt;
            cl.profiles[id].location_compat = profiles[i].loc;
            cl.profiles[id].notification_compat = profiles[i].notif;
        }
    }

    vga_puts("[COMPAT] compat layer init done \n");
    vga_printf(" translation mode: hybrid, perf: balanced, config: %d \n", cl.profile_count);
}

/* 应用配置 */
int cl_app_register(const char *package, const char *name)
{
    for (int i = 0; i < CL_MAX_APPS; i++) {
        if (!cl.apps[i].used) {
            memset(&cl.apps[i], 0, sizeof(cl_app_config_t));
            cl.apps[i].used = 1;
            cl.apps[i].id = i;
            if (package) strncpy(cl.apps[i].package_name, package, 127);
            if (name) strncpy(cl.apps[i].app_name, name, 127);
            cl.apps[i].compat_level = CL_LEVEL_GOOD;
            cl.apps[i].translate_mode = cl.default_translate_mode;
            cl.apps[i].perf_mode = cl.default_perf_mode;
            cl.apps[i].gms_compat = 1;
            cl.apps[i].webview_compat = 1;
            cl.apps[i].storage_compat = 1;
            cl.apps[i].camera_compat = 1;
            cl.apps[i].bluetooth_compat = 1;
            cl.apps[i].location_compat = 1;
            cl.apps[i].notification_compat = 1;
            cl.apps[i].sandbox_level = cl.default_sandbox_level;
            cl.apps[i].heap_size_mb = 256;
            cl.apps[i].frame_rate = 60;
            cl.apps[i].render_quality = 80;
            cl.app_count++;
            return i;
        }
    }
    return -1;
}

int cl_app_unregister(const char *package)
{
    for (int i = 0; i < CL_MAX_APPS; i++) {
        if (cl.apps[i].used && strcmp(cl.apps[i].package_name, package) == 0) {
            cl.apps[i].used = 0;
            cl.app_count--;
            return 0;
        }
    }
    return -1;
}

int cl_app_get(const char *package, cl_app_config_t *config)
{
    if (!package || !config) return -1;
    for (int i = 0; i < CL_MAX_APPS; i++) {
        if (cl.apps[i].used && strcmp(cl.apps[i].package_name, package) == 0) {
            memcpy(config, &cl.apps[i], sizeof(cl_app_config_t));
            return 0;
        }
    }
    return -1;
}

int cl_app_list(cl_app_config_t *apps, int max)
{
    if (!apps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < CL_MAX_APPS && count < max; i++) {
        if (cl.apps[i].used) memcpy(&apps[count++], &cl.apps[i], sizeof(cl_app_config_t));
    }
    return count;
}

int cl_app_set_compat_level(const char *package, cl_compat_level_t level)
{
    for (int i = 0; i < CL_MAX_APPS; i++) {
        if (cl.apps[i].used && strcmp(cl.apps[i].package_name, package) == 0) {
            cl.apps[i].compat_level = level;
            return 0;
        }
    }
    return -1;
}

int cl_app_set_translate_mode(const char *package, cl_translate_mode_t mode)
{
    for (int i = 0; i < CL_MAX_APPS; i++) {
        if (cl.apps[i].used && strcmp(cl.apps[i].package_name, package) == 0) {
            cl.apps[i].translate_mode = mode;
            return 0;
        }
    }
    return -1;
}

int cl_app_set_perf_mode(const char *package, cl_perf_mode_t mode)
{
    for (int i = 0; i < CL_MAX_APPS; i++) {
        if (cl.apps[i].used && strcmp(cl.apps[i].package_name, package) == 0) {
            cl.apps[i].perf_mode = mode;
            return 0;
        }
    }
    return -1;
}

int cl_app_set_sandbox(const char *package, int level)
{
    for (int i = 0; i < CL_MAX_APPS; i++) {
        if (cl.apps[i].used && strcmp(cl.apps[i].package_name, package) == 0) {
            cl.apps[i].sandbox_level = level;
            return 0;
        }
    }
    return -1;
}

/* 兼容开关 */
int cl_app_enable_feature(const char *package, int feature, int enable)
{
    for (int i = 0; i < CL_MAX_APPS; i++) {
        if (cl.apps[i].used && strcmp(cl.apps[i].package_name, package) == 0) {
            switch (feature) {
                case 0: cl.apps[i].gms_compat = enable; break;
                case 1: cl.apps[i].webview_compat = enable; break;
                case 2: cl.apps[i].storage_compat = enable; break;
                case 3: cl.apps[i].camera_compat = enable; break;
                case 4: cl.apps[i].bluetooth_compat = enable; break;
                case 5: cl.apps[i].location_compat = enable; break;
                case 6: cl.apps[i].notification_compat = enable; break;
                default: return -1;
            }
            return 0;
        }
    }
    return -1;
}

int cl_app_get_feature(const char *package, int feature, int *enabled)
{
    if (!enabled) return -1;
    for (int i = 0; i < CL_MAX_APPS; i++) {
        if (cl.apps[i].used && strcmp(cl.apps[i].package_name, package) == 0) {
            switch (feature) {
                case 0: *enabled = cl.apps[i].gms_compat; break;
                case 1: *enabled = cl.apps[i].webview_compat; break;
                case 2: *enabled = cl.apps[i].storage_compat; break;
                case 3: *enabled = cl.apps[i].camera_compat; break;
                case 4: *enabled = cl.apps[i].bluetooth_compat; break;
                case 5: *enabled = cl.apps[i].location_compat; break;
                case 6: *enabled = cl.apps[i].notification_compat; break;
                default: return -1;
            }
            return 0;
        }
    }
    return -1;
}

/* 配置文件 */
int cl_profile_create(const char *name, const char *desc)
{
    for (int i = 0; i < CL_MAX_PROFILES; i++) {
        if (!cl.profiles[i].used) {
            memset(&cl.profiles[i], 0, sizeof(cl_profile_t));
            cl.profiles[i].used = 1;
            cl.profiles[i].id = i;
            if (name) strncpy(cl.profiles[i].name, name, 127);
            if (desc) strncpy(cl.profiles[i].description, desc, 255);
            cl.profile_count++;
            return i;
        }
    }
    return -1;
}

int cl_profile_delete(int id)
{
    if (id < 0 || id >= CL_MAX_PROFILES || !cl.profiles[id].used) return -1;
    if (cl.profiles[id].is_system) return -1;
    cl.profiles[id].used = 0;
    cl.profile_count--;
    return 0;
}

int cl_profile_apply(int id, const char *package)
{
    if (id < 0 || id >= CL_MAX_PROFILES || !cl.profiles[id].used) return -1;
    for (int i = 0; i < CL_MAX_APPS; i++) {
        if (cl.apps[i].used && strcmp(cl.apps[i].package_name, package) == 0) {
            cl.apps[i].gms_compat = cl.profiles[id].gms_compat;
            cl.apps[i].webview_compat = cl.profiles[id].webview_compat;
            cl.apps[i].storage_compat = cl.profiles[id].storage_compat;
            cl.apps[i].camera_compat = cl.profiles[id].camera_compat;
            cl.apps[i].bluetooth_compat = cl.profiles[id].bluetooth_compat;
            cl.apps[i].location_compat = cl.profiles[id].location_compat;
            cl.apps[i].notification_compat = cl.profiles[id].notification_compat;
            return 0;
        }
    }
    return -1;
}

int cl_profile_get(int id, cl_profile_t *profile)
{
    if (id < 0 || id >= CL_MAX_PROFILES || !cl.profiles[id].used || !profile) return -1;
    memcpy(profile, &cl.profiles[id], sizeof(cl_profile_t));
    return 0;
}

int cl_profile_list(cl_profile_t *profiles, int max)
{
    if (!profiles || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < CL_MAX_PROFILES && count < max; i++) {
        if (cl.profiles[i].used) memcpy(&profiles[count++], &cl.profiles[i], sizeof(cl_profile_t));
    }
    return count;
}

/* 翻译缓存 */
int cl_cache_add(const char *package, const char *method, uint32_t offset, uint32_t size)
{
    for (int i = 0; i < 1024; i++) {
        if (!cl.cache[i].used) {
            memset(&cl.cache[i], 0, sizeof(cl_translate_cache_t));
            cl.cache[i].used = 1;
            cl.cache[i].id = i;
            if (package) strncpy(cl.cache[i].package_name, package, 127);
            if (method) strncpy(cl.cache[i].method_signature, method, 255);
            cl.cache[i].code_offset = offset;
            cl.cache[i].code_size = size;
            cl.cache[i].compiled = 1;
            cl.cache[i].last_used = 1;
            cl.cache_count++;
            cl.stats.total_translations++;
            return i;
        }
    }
    return -1;
}

int cl_cache_lookup(const char *package, const char *method, cl_translate_cache_t *entry)
{
    if (!package || !method) return -1;
    for (int i = 0; i < 1024; i++) {
        if (cl.cache[i].used &&
            strcmp(cl.cache[i].package_name, package) == 0 &&
            strcmp(cl.cache[i].method_signature, method) == 0) {
            cl.cache[i].use_count++;
            cl.cache[i].last_used = 1;
            cl.stats.cache_hits++;
            if (entry) memcpy(entry, &cl.cache[i], sizeof(cl_translate_cache_t));
            return i;
        }
    }
    cl.stats.cache_misses++;
    return -1;
}

int cl_cache_remove(const char *package, const char *method)
{
    for (int i = 0; i < 1024; i++) {
        if (cl.cache[i].used &&
            strcmp(cl.cache[i].package_name, package) == 0 &&
            strcmp(cl.cache[i].method_signature, method) == 0) {
            cl.cache[i].used = 0;
            cl.cache_count--;
            return 0;
        }
    }
    return -1;
}

int cl_cache_clear(void)
{
    for (int i = 0; i < 1024; i++) cl.cache[i].used = 0;
    cl.cache_count = 0;
    return 0;
}

int cl_cache_get_stats(cl_perf_stats_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &cl.stats, sizeof(cl_perf_stats_t));
    return 0;
}

/* 性能优化 */
int cl_optimize_app(const char *package)
{
    for (int i = 0; i < CL_MAX_APPS; i++) {
        if (cl.apps[i].used && strcmp(cl.apps[i].package_name, package) == 0) {
            cl.apps[i].compat_level = CL_LEVEL_EXCELLENT;
            cl.apps[i].translate_mode = CL_TRANSLATE_AOT;
            return 0;
        }
    }
    return -1;
}

int cl_optimize_all(void)
{
    int count = 0;
    for (int i = 0; i < CL_MAX_APPS; i++) {
        if (cl.apps[i].used) {
            cl_optimize_app(cl.apps[i].package_name);
            count++;
        }
    }
    return count;
}

int cl_precompile_app(const char *package)
{
    cl_cache_add(package, "precompiled", 0, 4096);
    return 0;
}

int cl_clear_cache(const char *package)
{
    for (int i = 0; i < 1024; i++) {
        if (cl.cache[i].used && strcmp(cl.cache[i].package_name, package) == 0) {
            cl.cache[i].used = 0;
            cl.cache_count--;
        }
    }
    return 0;
}

/* 全局配置 */
int cl_set_enabled(int enable)
{
    cl.enabled = enable ? 1 : 0;
    return 0;
}

int cl_set_default_translate_mode(cl_translate_mode_t mode)
{
    cl.default_translate_mode = mode;
    return 0;
}

int cl_set_default_perf_mode(cl_perf_mode_t mode)
{
    cl.default_perf_mode = mode;
    return 0;
}

int cl_get_stats(compat_layer_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &cl, sizeof(compat_layer_t));
    return 0;
}
