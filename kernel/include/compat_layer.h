#ifndef COMPAT_LAYER_H
#define COMPAT_LAYER_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS APK 兼容层配置与性能优化
 * 兼容配置 / 性能调优 / 沙箱 / 翻译缓存
 * ========================================================================== */

#define CL_MAX_APPS 256
#define CL_MAX_PROFILES 32
#define CL_NAME_LEN 128

/* 兼容级别 */
typedef enum {
    CL_LEVEL_NONE = 0,         /* 不兼容 */
    CL_LEVEL_BASIC,            /* 基础兼容 */
    CL_LEVEL_GOOD,             /* 良好兼容 */
    CL_LEVEL_EXCELLENT,        /* 优秀兼容 */
    CL_LEVEL_NATIVE,           /* 原生级兼容 */
} cl_compat_level_t;

/* 性能模式 */
typedef enum {
    CL_PERF_POWER_SAVE = 0,    /* 省电 */
    CL_PERF_BALANCED,          /* 平衡 */
    CL_PERF_PERFORMANCE,       /* 性能 */
    CL_PERF_GAME,              /* 游戏 */
} cl_perf_mode_t;

/* 翻译模式 */
typedef enum {
    CL_TRANSLATE_NONE = 0,     /* 不翻译(原生) */
    CL_TRANSLATE_INTERPRET,    /* 解释执行 */
    CL_TRANSLATE_JIT,          /* JIT翻译 */
    CL_TRANSLATE_AOT,          /* AOT预翻译 */
    CL_TRANSLATE_HYBRID,       /* 混合翻译 */
} cl_translate_mode_t;

/* 应用兼容配置 */
typedef struct {
    int used;
    int id;
    char package_name[CL_NAME_LEN];
    char app_name[CL_NAME_LEN];
    cl_compat_level_t compat_level;
    cl_translate_mode_t translate_mode;
    cl_perf_mode_t perf_mode;
    /* 兼容开关 */
    int gms_compat;       /* GMS兼容 */
    int webview_compat;   /* WebView兼容 */
    int storage_compat;   /* 存储兼容 */
    int camera_compat;    /* 相机兼容 */
    int bluetooth_compat; /* 蓝牙兼容 */
    int location_compat;  /* 位置兼容 */
    int notification_compat; /* 通知兼容 */
    /* 性能参数 */
    int cpu_affinity;     /* CPU亲和性 */
    int priority;         /* 进程优先级 */
    int heap_size_mb;     /* 堆大小 */
    int render_quality;   /* 渲染质量 0-100 */
    int frame_rate;       /* 目标帧率 */
    /* 沙箱 */
    int sandbox_level;    /* 0=宽松,1=标准,2=严格 */
    int network_isolation;
    int file_isolation;
    /* 统计 */
    uint64_t launch_count;
    uint64_t total_runtime;
    uint64_t crash_count;
    uint64_t anr_count;
    int last_launch_time;
} cl_app_config_t;

/* 兼容配置文件 */
typedef struct {
    int used;
    int id;
    char name[CL_NAME_LEN];
    char description[256];
    int gms_compat;
    int webview_compat;
    int storage_compat;
    int camera_compat;
    int bluetooth_compat;
    int location_compat;
    int notification_compat;
    int is_system;
} cl_profile_t;

/* 翻译缓存 */
typedef struct {
    int used;
    int id;
    char package_name[CL_NAME_LEN];
    char method_signature[256];
    uint32_t code_offset;
    uint32_t code_size;
    int compiled;
    uint64_t last_used;
    uint64_t use_count;
} cl_translate_cache_t;

/* 性能统计 */
typedef struct {
    uint64_t total_translations;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t total_compile_time;
    uint64_t total_app_launches;
    uint64_t avg_cold_start_ms;
    uint64_t avg_warm_start_ms;
    uint64_t total_crashes;
    uint64_t total_anrs;
} cl_perf_stats_t;

/* 兼容层状态 */
typedef struct {
    int initialized;
    int enabled;
    cl_translate_mode_t default_translate_mode;
    cl_perf_mode_t default_perf_mode;
    int default_sandbox_level;
    /* 应用配置 */
    cl_app_config_t apps[CL_MAX_APPS];
    int app_count;
    /* 配置文件 */
    cl_profile_t profiles[CL_MAX_PROFILES];
    int profile_count;
    /* 翻译缓存 */
    cl_translate_cache_t cache[1024];
    int cache_count;
    int cache_size_kb;
    /* 统计 */
    cl_perf_stats_t stats;
} compat_layer_t;

/* 初始化 */
void compat_layer_init(void);

/* 应用配置 */
int cl_app_register(const char *package, const char *name);
int cl_app_unregister(const char *package);
int cl_app_get(const char *package, cl_app_config_t *config);
int cl_app_list(cl_app_config_t *apps, int max);
int cl_app_set_compat_level(const char *package, cl_compat_level_t level);
int cl_app_set_translate_mode(const char *package, cl_translate_mode_t mode);
int cl_app_set_perf_mode(const char *package, cl_perf_mode_t mode);
int cl_app_set_sandbox(const char *package, int level);

/* 兼容开关 */
int cl_app_enable_feature(const char *package, int feature, int enable);
int cl_app_get_feature(const char *package, int feature, int *enabled);

/* 配置文件 */
int cl_profile_create(const char *name, const char *desc);
int cl_profile_delete(int id);
int cl_profile_apply(int id, const char *package);
int cl_profile_get(int id, cl_profile_t *profile);
int cl_profile_list(cl_profile_t *profiles, int max);

/* 翻译缓存 */
int cl_cache_add(const char *package, const char *method, uint32_t offset, uint32_t size);
int cl_cache_lookup(const char *package, const char *method, cl_translate_cache_t *entry);
int cl_cache_remove(const char *package, const char *method);
int cl_cache_clear(void);
int cl_cache_get_stats(cl_perf_stats_t *stats);

/* 性能优化 */
int cl_optimize_app(const char *package);
int cl_optimize_all(void);
int cl_precompile_app(const char *package);
int cl_clear_cache(const char *package);

/* 全局配置 */
int cl_set_enabled(int enable);
int cl_set_default_translate_mode(cl_translate_mode_t mode);
int cl_set_default_perf_mode(cl_perf_mode_t mode);
int cl_get_stats(compat_layer_t *stats);

#endif /* COMPAT_LAYER_H */
