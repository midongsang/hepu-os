/* ============================================================================
 * 赫普OS - 高级开发工具（从devtools.c拆分）
 * 性能分析器 / 日志 / 符号表 / 热重载
 * ========================================================================== */

#include <stdint.h>
#include "devtools.h"
#include "vga.h"
#include "string.h"

extern devtools_t dt;

int profiler_start(void)
{
    dt.profiler.profiling = 1;
    dt.profiler.event_count = 0;
    dt.profiler.event_head = 0;
    vga_puts("[DEV] profiling started \n");
    return 0;
}

int profiler_stop(void)
{
    dt.profiler.profiling = 0;
    vga_puts("[DEV] profiling stopped \n");
    return 0;
}

int profiler_is_running(void) { return dt.profiler.profiling; }

int profiler_get_events(prof_event_t *events, int max)
{
    if (!events || max <= 0) return 0;
    int count = (dt.profiler.event_count < max) ? dt.profiler.event_count : max;
    for (int i = 0; i < count; i++) {
        memcpy(&events[i], &dt.profiler.events[(dt.profiler.event_head + i) % DT_MAX_PROFILE_EVENTS], sizeof(prof_event_t));
    }
    return count;
}

int profiler_get_functions(func_profile_t *funcs, int max)
{
    if (!funcs || max <= 0) return 0;
    int count = (dt.profiler.func_count < max) ? dt.profiler.func_count : max;
    memcpy(funcs, dt.profiler.functions, sizeof(func_profile_t) * count);
    return count;
}

int profiler_get_stats(int *cpu, int *mem_kb, int *faults, int *switches)
{
    if (cpu) *cpu = dt.profiler.cpu_usage;
    if (mem_kb) *mem_kb = dt.profiler.memory_usage_kb;
    if (faults) *faults = dt.profiler.page_faults;
    if (switches) *switches = dt.profiler.context_switches;
    return 0;
}

int profiler_reset(void)
{
    dt.profiler.event_count = 0;
    dt.profiler.event_head = 0;
    dt.profiler.func_count = 0;
    dt.profiler.total_samples = 0;
    return 0;
}

int dt_log_write(dt_log_level_t level, const char *tag, const char *message)
{
    if (level < dt.logger.min_level) return -1;
    dt_log_entry_t *entry = &dt.logger.entries[dt.logger.entry_head];
    entry->timestamp = 0;
    entry->level = level;
    if (tag) strncpy(entry->tag, tag, 31);
    if (message) strncpy(entry->message, message, 255);
    entry->pid = 0;
    entry->tid = 0;
    dt.logger.entry_head = (dt.logger.entry_head + 1) % DT_MAX_LOG_ENTRIES;
    if (dt.logger.entry_count < DT_MAX_LOG_ENTRIES) dt.logger.entry_count++;
    if (dt.logger.log_to_vga) {
        vga_printf("[%s] %s: %s\n",
                   level == DT_LOG_VERBOSE ? "V" : level == DT_LOG_DEBUG ? "D" :
                   level == DT_LOG_INFO ? "I" : level == DT_LOG_WARNING ? "W" :
                   level == DT_LOG_ERROR ? "E" : "F",
                   tag ? tag : "?", message ? message : "");
    }
    return 0;
}

int dt_log_read(dt_log_entry_t *entries, int max)
{
    if (!entries || max <= 0) return 0;
    int count = (dt.logger.entry_count < max) ? dt.logger.entry_count : max;
    for (int i = 0; i < count; i++) {
        int idx = (dt.logger.entry_head - count + i + DT_MAX_LOG_ENTRIES) % DT_MAX_LOG_ENTRIES;
        memcpy(&entries[i], &dt.logger.entries[idx], sizeof(dt_log_entry_t));
    }
    return count;
}

int dt_log_set_level(dt_log_level_t level)
{
    dt.logger.min_level = level;
    return 0;
}

int dt_log_clear(void)
{
    dt.logger.entry_count = 0;
    dt.logger.entry_head = 0;
    return 0;
}

int dt_log_count(void) { return dt.logger.entry_count; }

int symbol_add(uint32_t address, const char *name, int size, char type)
{
    if (!name) return -1;
    for (int i = 0; i < DT_MAX_SYMBOLS; i++) {
        if (!dt.symbols[i].name[0]) {
            dt.symbols[i].address = address;
            strncpy(dt.symbols[i].name, name, 63);
            dt.symbols[i].size = size;
            dt.symbols[i].type = type;
            dt.symbol_count++;
            return i;
        }
    }
    return -1;
}

int symbol_lookup(uint32_t address, symbol_t *sym)
{
    if (!sym) return -1;
    for (int i = 0; i < DT_MAX_SYMBOLS; i++) {
        if (dt.symbols[i].name[0] && dt.symbols[i].address == address) {
            memcpy(sym, &dt.symbols[i], sizeof(symbol_t));
            return i;
        }
    }
    return -1;
}

int symbol_find(const char *name, symbol_t *sym)
{
    if (!name || !sym) return -1;
    for (int i = 0; i < DT_MAX_SYMBOLS; i++) {
        if (dt.symbols[i].name[0] && strcmp(dt.symbols[i].name, name) == 0) {
            memcpy(sym, &dt.symbols[i], sizeof(symbol_t));
            return i;
        }
    }
    return -1;
}

int symbol_list(symbol_t *syms, int max)
{
    if (!syms || max <= 0) return 0;
    int count = (dt.symbol_count < max) ? dt.symbol_count : max;
    memcpy(syms, dt.symbols, sizeof(symbol_t) * count);
    return count;
}

int hotreload_enable(int enable)
{
    dt.hot_reload.enabled = enable ? 1 : 0;
    vga_printf("[DEV] hot reload %s\n", enable ? " enabled " : " disabled ");
    return 0;
}

int hotreload_add_watch(const char *path)
{
    if (!path) return -1;
    for (int i = 0; i < 16; i++) {
        if (!dt.hot_reload.watch_paths[i][0]) {
            strncpy(dt.hot_reload.watch_paths[i], path, 255);
            dt.hot_reload.watch_count++;
            return i;
        }
    }
    return -1;
}

int hotreload_remove_watch(const char *path)
{
    if (!path) return -1;
    for (int i = 0; i < 16; i++) {
        if (dt.hot_reload.watch_paths[i][0] && strcmp(dt.hot_reload.watch_paths[i], path) == 0) {
            dt.hot_reload.watch_paths[i][0] = '\0';
            dt.hot_reload.watch_count--;
            return 0;
        }
    }
    return -1;
}

int hotreload_trigger(void)
{
    dt.hot_reload.reload_count++;
    vga_puts("[DEV] trigger hot reload \n");
    return 0;
}

int hotreload_get_status(hot_reload_t *status)
{
    if (!status) return -1;
    memcpy(status, &dt.hot_reload, sizeof(hot_reload_t));
    return 0;
}

