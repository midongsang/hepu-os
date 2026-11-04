/* ============================================================================
 * 赫普OS - ART 运行时骨架
 * ========================================================================== */

#include <stdint.h>
#include "art_runtime.h"
#include "vga.h"
#include "string.h"

static art_runtime_t art;

void art_runtime_init(void)
{
    memset(&art, 0, sizeof(art_runtime_t));
    art.initialized = 1;
    art.state = ART_STATE_STOPPED;
    art.compiler_mode = ART_MODE_HYBRID;
    art.gc_type = ART_GC_CONCURRENT;
    art.heap_size = 256 * 1024 * 1024;  /* 256MB */
    art.heap_used = 0;

    vga_puts("[ART] ART run runtime init done \n");
    vga_printf(" mode: hybrid(JIT+AOT), GC: concurrent, heap: %dMB\n",
               (int)(art.heap_size / 1024 / 1024));
}

/* 运行时控制 */
int art_start(void)
{
    if (art.state == ART_STATE_RUNNING) return -1;
    art.state = ART_STATE_RUNNING;
    art.total_app_starts++;
    return 0;
}

int art_stop(void)
{
    art.state = ART_STATE_STOPPED;
    return 0;
}

int art_suspend(void)
{
    if (art.state != ART_STATE_RUNNING) return -1;
    art.state = ART_STATE_SUSPENDED;
    return 0;
}

int art_resume(void)
{
    if (art.state != ART_STATE_SUSPENDED) return -1;
    art.state = ART_STATE_RUNNING;
    return 0;
}

int art_set_compiler_mode(art_compiler_mode_t mode)
{
    art.compiler_mode = mode;
    return 0;
}

int art_set_gc_type(art_gc_type_t type)
{
    art.gc_type = type;
    return 0;
}

/* DEX管理 */
int art_load_dex(const char *path)
{
    for (int i = 0; i < ART_MAX_DEX_FILES; i++) {
        if (!art.dex_files[i].used) {
            memset(&art.dex_files[i], 0, sizeof(art_dex_file_t));
            art.dex_files[i].used = 1;
            art.dex_files[i].id = i;
            if (path) strncpy(art.dex_files[i].path, path, 255);
            art.dex_files[i].size = 5 * 1024 * 1024;
            art.dex_files[i].class_count = 100;
            art.dex_files[i].method_count = 1000;
            art.dex_files[i].loaded = 1;
            art.dex_count++;
            return i;
        }
    }
    return -1;
}

int art_unload_dex(int id)
{
    if (id < 0 || id >= ART_MAX_DEX_FILES || !art.dex_files[id].used) return -1;
    art.dex_files[id].used = 0;
    art.dex_count--;
    return 0;
}

int art_get_dex(int id, art_dex_file_t *dex)
{
    if (id < 0 || id >= ART_MAX_DEX_FILES || !art.dex_files[id].used || !dex) return -1;
    memcpy(dex, &art.dex_files[id], sizeof(art_dex_file_t));
    return 0;
}

int art_list_dex(art_dex_file_t *dexes, int max)
{
    if (!dexes || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < ART_MAX_DEX_FILES && count < max; i++) {
        if (art.dex_files[i].used) memcpy(&dexes[count++], &art.dex_files[i], sizeof(art_dex_file_t));
    }
    return count;
}

/* 类加载 */
int art_load_class(const char *name)
{
    for (int i = 0; i < ART_MAX_CLASSES; i++) {
        if (!art.classes[i].used) {
            memset(&art.classes[i], 0, sizeof(art_class_t));
            art.classes[i].used = 1;
            art.classes[i].id = i;
            if (name) strncpy(art.classes[i].name, name, 255);
            art.classes[i].initialized = 1;
            art.class_count++;
            return i;
        }
    }
    return -1;
}

int art_get_class(const char *name, art_class_t *cls)
{
    if (!name || !cls) return -1;
    for (int i = 0; i < ART_MAX_CLASSES; i++) {
        if (art.classes[i].used && strcmp(art.classes[i].name, name) == 0) {
            memcpy(cls, &art.classes[i], sizeof(art_class_t));
            return i;
        }
    }
    return -1;
}

int art_list_classes(art_class_t *classes, int max)
{
    if (!classes || max <= 0) return 0;
    int count = art.class_count < max ? art.class_count : max;
    for (int i = 0; i < count; i++)
        memcpy(&classes[i], &art.classes[i], sizeof(art_class_t));
    return count;
}

int art_initialize_class(int class_id)
{
    if (class_id < 0 || class_id >= ART_MAX_CLASSES || !art.classes[class_id].used) return -1;
    art.classes[class_id].initialized = 1;
    return 0;
}

/* 方法执行 */
int art_invoke_method(int class_id, const char *method_name, void *args, void *result)
{
    (void)class_id; (void)method_name; (void)args; (void)result;
    art.total_method_invocations++;
    return 0;
}

int art_get_method(int class_id, const char *name, art_method_t *method)
{
    (void)class_id;
    if (!name || !method) return -1;
    for (int i = 0; i < ART_MAX_METHODS; i++) {
        if (art.methods[i].used && strcmp(art.methods[i].name, name) == 0) {
            memcpy(method, &art.methods[i], sizeof(art_method_t));
            return i;
        }
    }
    return -1;
}

int art_compile_method(int method_id)
{
    if (method_id < 0 || method_id >= ART_MAX_METHODS || !art.methods[method_id].used) return -1;
    art.methods[method_id].compiled = 1;
    art.jit_stats.compiled_methods++;
    return 0;
}

int art_list_methods(int class_id, art_method_t *methods, int max)
{
    (void)class_id;
    if (!methods || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < ART_MAX_METHODS && count < max; i++) {
        if (art.methods[i].used) memcpy(&methods[count++], &art.methods[i], sizeof(art_method_t));
    }
    return count;
}

/* 线程管理 */
int art_thread_create(const char *name, void (*entry)(void *), void *arg)
{
    (void)entry; (void)arg;
    for (int i = 0; i < ART_MAX_THREADS; i++) {
        if (!art.threads[i].used) {
            memset(&art.threads[i], 0, sizeof(art_thread_t));
            art.threads[i].used = 1;
            art.threads[i].id = i;
            art.threads[i].tid = 1000 + i;
            if (name) strncpy(art.threads[i].name, name, 63);
            art.threads[i].state = ART_STATE_RUNNING;
            art.threads[i].stack_size = 1024 * 1024;
            art.threads[i].priority = 5;
            art.thread_count++;
            return i;
        }
    }
    return -1;
}

int art_thread_suspend(int tid)
{
    for (int i = 0; i < ART_MAX_THREADS; i++) {
        if (art.threads[i].used && art.threads[i].tid == tid) {
            art.threads[i].state = ART_STATE_SUSPENDED;
            return 0;
        }
    }
    return -1;
}

int art_thread_resume(int tid)
{
    for (int i = 0; i < ART_MAX_THREADS; i++) {
        if (art.threads[i].used && art.threads[i].tid == tid) {
            art.threads[i].state = ART_STATE_RUNNING;
            return 0;
        }
    }
    return -1;
}

int art_thread_get(int tid, art_thread_t *thread)
{
    if (!thread) return -1;
    for (int i = 0; i < ART_MAX_THREADS; i++) {
        if (art.threads[i].used && art.threads[i].tid == tid) {
            memcpy(thread, &art.threads[i], sizeof(art_thread_t));
            return 0;
        }
    }
    return -1;
}

int art_list_threads(art_thread_t *threads, int max)
{
    if (!threads || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < ART_MAX_THREADS && count < max; i++) {
        if (art.threads[i].used) memcpy(&threads[count++], &art.threads[i], sizeof(art_thread_t));
    }
    return count;
}

/* GC */
int art_gc_collect(void)
{
    art.gc_stats.total_collections++;
    art.gc_stats.last_pause_time = 5000000;  /* 5ms */
    art.gc_stats.total_pause_time += art.gc_stats.last_pause_time;
    art.gc_stats.freed_objects += 1000;
    art.gc_stats.freed_bytes += 4 * 1024 * 1024;
    art.heap_used = art.heap_used > 4 * 1024 * 1024 ?
                    art.heap_used - 4 * 1024 * 1024 : 0;
    art.gc_stats.heap_used = art.heap_used;
    return 0;
}

int art_gc_get_stats(art_gc_stats_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &art.gc_stats, sizeof(art_gc_stats_t));
    return 0;
}

int art_gc_set_heap_size(uint64_t size)
{
    art.heap_size = size;
    art.gc_stats.heap_max = size;
    return 0;
}

/* JIT/AOT */
int art_jit_compile(int method_id)
{
    return art_compile_method(method_id);
}

int art_jit_get_stats(art_jit_stats_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &art.jit_stats, sizeof(art_jit_stats_t));
    return 0;
}

int art_aot_compile_dex(int dex_id, const char *output_path)
{
    (void)output_path;
    if (dex_id < 0 || dex_id >= ART_MAX_DEX_FILES || !art.dex_files[dex_id].used) return -1;
    art.dex_files[dex_id].odex_available = 1;
    return 0;
}

/* 状态查询 */
int art_get_state(art_state_t *state)
{
    if (!state) return -1;
    *state = art.state;
    return 0;
}

int art_get_stats(art_runtime_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &art, sizeof(art_runtime_t));
    return 0;
}
