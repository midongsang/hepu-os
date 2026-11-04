#ifndef ART_RUNTIME_H
#define ART_RUNTIME_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS ART 运行时骨架
 * DEX加载 / 类加载 / JIT/AOT / GC / 线程管理
 * ========================================================================== */

#define ART_MAX_CLASSES 1024
#define ART_MAX_METHODS 4096
#define ART_MAX_THREADS 64
#define ART_MAX_DEX_FILES 32
#define ART_NAME_LEN 256

/* 运行时状态 */
typedef enum {
    ART_STATE_STOPPED = 0,
    ART_STATE_STARTING,
    ART_STATE_RUNNING,
    ART_STATE_SUSPENDED,
    ART_STATE_ERROR,
} art_state_t;

/* 编译器模式 */
typedef enum {
    ART_MODE_INTERPRETER = 0,   /* 纯解释执行 */
    ART_MODE_JIT,               /* 即时编译 */
    ART_MODE_AOT,               /* 预编译 */
    ART_MODE_HYBRID,            /* 混合模式 */
} art_compiler_mode_t;

/* GC类型 */
typedef enum {
    ART_GC_GENERATIONAL = 0,
    ART_GC_CONCURRENT,
    ART_GC_INCREMENTAL,
    ART_GC_BATCH,
} art_gc_type_t;

/* DEX文件 */
typedef struct {
    int used;
    int id;
    char path[ART_NAME_LEN];
    uint64_t size;
    uint32_t checksum;
    int class_count;
    int method_count;
    int loaded;
    int odex_available;  /* 预编译可用 */
} art_dex_file_t;

/* 类信息 */
typedef struct {
    int used;
    int id;
    char name[ART_NAME_LEN];
    char super_class[ART_NAME_LEN];
    int access_flags;
    int method_count;
    int field_count;
    int initialized;
    int loader_id;
} art_class_t;

/* 方法信息 */
typedef struct {
    int used;
    int id;
    char name[ART_NAME_LEN];
    char signature[256];
    int class_id;
    int access_flags;
    int compiled;  /* 是否已JIT编译 */
    uint64_t invocation_count;
    uint64_t total_time_ns;
} art_method_t;

/* 线程 */
typedef struct {
    int used;
    int id;
    char name[64];
    int tid;
    art_state_t state;
    uint32_t stack_top;
    uint32_t stack_size;
    int priority;
    int daemon;
    uint64_t cpu_time;
} art_thread_t;

/* GC统计 */
typedef struct {
    uint64_t total_collections;
    uint64_t total_pause_time;
    uint64_t last_pause_time;
    uint64_t heap_used;
    uint64_t heap_max;
    uint64_t freed_objects;
    uint64_t freed_bytes;
} art_gc_stats_t;

/* JIT统计 */
typedef struct {
    uint64_t compiled_methods;
    uint64_t compilation_failures;
    uint64_t total_compilation_time;
    int code_cache_size;
    int code_cache_used;
} art_jit_stats_t;

/* ART运行时状态 */
typedef struct {
    int initialized;
    art_state_t state;
    art_compiler_mode_t compiler_mode;
    art_gc_type_t gc_type;
    /* DEX */
    art_dex_file_t dex_files[ART_MAX_DEX_FILES];
    int dex_count;
    /* 类/方法 */
    art_class_t classes[ART_MAX_CLASSES];
    int class_count;
    art_method_t methods[ART_MAX_METHODS];
    int method_count;
    /* 线程 */
    art_thread_t threads[ART_MAX_THREADS];
    int thread_count;
    /* 堆 */
    uint64_t heap_start;
    uint64_t heap_size;
    uint64_t heap_used;
    /* 统计 */
    art_gc_stats_t gc_stats;
    art_jit_stats_t jit_stats;
    uint64_t total_app_starts;
    uint64_t total_method_invocations;
} art_runtime_t;

/* 初始化 */
void art_runtime_init(void);

/* 运行时控制 */
int art_start(void);
int art_stop(void);
int art_suspend(void);
int art_resume(void);
int art_set_compiler_mode(art_compiler_mode_t mode);
int art_set_gc_type(art_gc_type_t type);

/* DEX管理 */
int art_load_dex(const char *path);
int art_unload_dex(int id);
int art_get_dex(int id, art_dex_file_t *dex);
int art_list_dex(art_dex_file_t *dexes, int max);

/* 类加载 */
int art_load_class(const char *name);
int art_get_class(const char *name, art_class_t *cls);
int art_list_classes(art_class_t *classes, int max);
int art_initialize_class(int class_id);

/* 方法执行 */
int art_invoke_method(int class_id, const char *method_name, void *args, void *result);
int art_get_method(int class_id, const char *name, art_method_t *method);
int art_compile_method(int method_id);
int art_list_methods(int class_id, art_method_t *methods, int max);

/* 线程管理 */
int art_thread_create(const char *name, void (*entry)(void *), void *arg);
int art_thread_suspend(int tid);
int art_thread_resume(int tid);
int art_thread_get(int tid, art_thread_t *thread);
int art_list_threads(art_thread_t *threads, int max);

/* GC */
int art_gc_collect(void);
int art_gc_get_stats(art_gc_stats_t *stats);
int art_gc_set_heap_size(uint64_t size);

/* JIT/AOT */
int art_jit_compile(int method_id);
int art_jit_get_stats(art_jit_stats_t *stats);
int art_aot_compile_dex(int dex_id, const char *output_path);

/* 状态查询 */
int art_get_state(art_state_t *state);
int art_get_stats(art_runtime_t *stats);

#endif /* ART_RUNTIME_H */
