/* ============================================================================
 * 赫普OS - 调试与性能分析
 * ftrace式函数跟踪 + perf性能计数器 + 内核环形日志
 * ========================================================================== */

#include <stdint.h>
#include "trace.h"
#include "vga.h"
#include "string.h"

/* 跟踪缓冲区 */
static trace_event_t trace_buffer[TRACE_MAX_EVENTS];
static int trace_head = 0;
static int trace_tail = 0;
static int trace_enabled = 0;
static int trace_initialized = 0;
static uint64_t trace_time = 0;

/* 探针 */
static trace_probe_t probes[TRACE_MAX_PROBES];
static int probe_count = 0;

/* 性能计数器 */
static perf_counters_t perf_counters;
static int perf_running = 0;
static perf_event_t perf_events[16];
static int perf_event_count = 0;

/* 内核日志环形缓冲区 */
static log_entry_t log_buffer[256];
static int log_head = 0;
static int log_tail = 0;
static log_level_t log_max_level = LOG_INFO;
static int log_initialized = 0;

void trace_init(void)
{
    trace_head = trace_tail = 0;
    trace_enabled = 0;
    trace_initialized = 1;
    trace_time = 0;
    for (int i = 0; i < TRACE_MAX_PROBES; i++) probes[i].used = 0;
    probe_count = 0;

    vga_puts("[TRACE] trace subsystem init done \n");
    vga_printf(" buffer region: %d events, probe: %d \n", TRACE_MAX_EVENTS, TRACE_MAX_PROBES);
}

int trace_enable(int enable)
{
    if (!trace_initialized) return -1;
    trace_enabled = enable ? 1 : 0;
    vga_printf("[TRACE] trace %s\n", enable ? " enabled " : " disabled ");
    return 0;
}

static void trace_add(trace_event_type_t type, const char *name,
                      uint32_t arg1, uint32_t arg2, uint32_t duration)
{
    if (!trace_enabled || !trace_initialized) return;

    trace_event_t *ev = &trace_buffer[trace_tail];
    ev->timestamp = trace_time++;
    ev->type = type;
    ev->cpu = 0;
    ev->pid = 0;
    ev->arg1 = arg1;
    ev->arg2 = arg2;
    ev->duration = duration;
    if (name) {
        strncpy(ev->name, name, TRACE_NAME_LEN - 1);
        ev->name[TRACE_NAME_LEN - 1] = '\0';
    } else {
        ev->name[0] = '\0';
    }

    trace_tail = (trace_tail + 1) % TRACE_MAX_EVENTS;
    if (trace_tail == trace_head) {
        trace_head = (trace_head + 1) % TRACE_MAX_EVENTS;
    }
}

void trace_enter(const char *name, uint32_t arg1, uint32_t arg2)
{
    trace_add(TRACE_EVENT_ENTER, name, arg1, arg2, 0);
}

void trace_exit(const char *name, uint32_t retval)
{
    trace_add(TRACE_EVENT_EXIT, name, retval, 0, 0);
}

void trace_event(trace_event_type_t type, const char *name, uint32_t arg1, uint32_t arg2)
{
    trace_add(type, name, arg1, arg2, 0);
}

int trace_read(trace_event_t *events, int max)
{
    if (!events || max <= 0) return 0;
    int count = 0;
    while (trace_head != trace_tail && count < max) {
        events[count++] = trace_buffer[trace_head];
        trace_head = (trace_head + 1) % TRACE_MAX_EVENTS;
    }
    return count;
}

int trace_count(void)
{
    if (trace_tail >= trace_head) return trace_tail - trace_head;
    return TRACE_MAX_EVENTS - trace_head + trace_tail;
}

void trace_clear(void)
{
    trace_head = trace_tail = 0;
}

int trace_probe_register(const char *name, void *address)
{
    if (!name || !trace_initialized) return -1;
    for (int i = 0; i < TRACE_MAX_PROBES; i++) {
        if (!probes[i].used) {
            memset(&probes[i], 0, sizeof(trace_probe_t));
            probes[i].used = 1;
            probes[i].id = i;
            strncpy(probes[i].name, name, TRACE_NAME_LEN - 1);
            probes[i].address = address;
            probes[i].min_time = 0xFFFFFFFF;
            probes[i].enabled = 1;
            probe_count++;
            return i;
        }
    }
    return -1;
}

int trace_probe_enable(int id, int enable)
{
    if (id < 0 || id >= TRACE_MAX_PROBES || !probes[id].used) return -1;
    probes[id].enabled = enable ? 1 : 0;
    return 0;
}

int trace_probe_get_stats(int id, trace_probe_t *stats)
{
    if (id < 0 || id >= TRACE_MAX_PROBES || !probes[id].used || !stats) return -1;
    memcpy(stats, &probes[id], sizeof(trace_probe_t));
    return 0;
}

int trace_probe_list(trace_probe_t *probes_out, int max)
{
    if (!probes_out || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < TRACE_MAX_PROBES && count < max; i++) {
        if (probes[i].used) {
            memcpy(&probes_out[count++], &probes[i], sizeof(trace_probe_t));
        }
    }
    return count;
}

void perf_init(void)
{
    memset(&perf_counters, 0, sizeof(perf_counters_t));
    perf_running = 0;
    perf_event_count = 0;
    vga_puts("[PERF] perf counters init done \n");
}

int perf_start(void)
{
    if (perf_running) return -1;
    perf_running = 1;
    return 0;
}

int perf_stop(void)
{
    if (!perf_running) return -1;
    perf_running = 0;
    return 0;
}

int perf_read(perf_counters_t *counters)
{
    if (!counters) return -1;
    memcpy(counters, &perf_counters, sizeof(perf_counters_t));
    return 0;
}

int perf_reset(void)
{
    memset(&perf_counters, 0, sizeof(perf_counters_t));
    return 0;
}

int perf_event_create(const char *name)
{
    if (!name || perf_event_count >= 16) return -1;
    perf_events[perf_event_count].name = name;
    perf_events[perf_event_count].count = 0;
    perf_events[perf_event_count].time_enabled = 0;
    perf_events[perf_event_count].time_running = 0;
    return perf_event_count++;
}

int perf_event_read(int id, perf_event_t *event)
{
    if (id < 0 || id >= perf_event_count || !event) return -1;
    memcpy(event, &perf_events[id], sizeof(perf_event_t));
    return 0;
}

int perf_event_destroy(int id)
{
    if (id < 0 || id >= perf_event_count) return -1;
    perf_events[id].count = 0;
    return 0;
}

void log_init(void)
{
    log_head = log_tail = 0;
    log_max_level = LOG_INFO;
    log_initialized = 1;
    vga_puts("[LOG] kernel log system init done \n");
}

void log_write(log_level_t level, const char *tag, const char *fmt, ...)
{
    if (!log_initialized || level > log_max_level) return;

    log_entry_t *entry = &log_buffer[log_tail];
    entry->timestamp = 0;
    entry->level = level;
    entry->tag = tag;
    /* 简化: 直接复制格式字符串 */
    if (fmt) {
        strncpy(entry->message, fmt, 127);
        entry->message[127] = '\0';
    } else {
        entry->message[0] = '\0';
    }

    log_tail = (log_tail + 1) % 256;
    if (log_tail == log_head) {
        log_head = (log_head + 1) % 256;
    }
}

int log_read(log_entry_t *entries, int max)
{
    if (!entries || max <= 0) return 0;
    int count = 0;
    while (log_head != log_tail && count < max) {
        entries[count++] = log_buffer[log_head];
        log_head = (log_head + 1) % 256;
    }
    return count;
}

int log_count(void)
{
    if (log_tail >= log_head) return log_tail - log_head;
    return 256 - log_head + log_tail;
}

void log_clear(void)
{
    log_head = log_tail = 0;
}

void log_set_level(log_level_t level)
{
    log_max_level = level;
}

void trace_print_stats(void)
{
    vga_printf("[TRACE] event count: %d, enabled: %s\n",
               trace_count(), trace_enabled ? " is " : " 否 ");
    vga_printf("[TRACE] probe count: %d\n", probe_count);
    for (int i = 0; i < TRACE_MAX_PROBES; i++) {
        if (probes[i].used) {
            vga_printf(" [%d] %s: hits =%d, total time =%uus\n",
                       probes[i].id, probes[i].name,
                       probes[i].hit_count, (uint32_t)probes[i].total_time);
        }
    }
}

void perf_print_stats(void)
{
    vga_printf("[PERF] CPU cycles: %u, instructions: %u\n",
               (uint32_t)perf_counters.cpu_cycles, (uint32_t)perf_counters.instructions);
    vga_printf("[PERF] cache refs: %u, cache misses: %u\n",
               (uint32_t)perf_counters.cache_refs, (uint32_t)perf_counters.cache_misses);
    vga_printf("[PERF] branch misses: %u, page faults: %u\n",
               (uint32_t)perf_counters.branch_misses, (uint32_t)perf_counters.page_faults);
    vga_printf("[PERF] ctx switches: %u, CPU migrate: %u\n",
               (uint32_t)perf_counters.context_switches,
               (uint32_t)perf_counters.cpu_migrations);
}

int trace_export(void *buf, uint32_t size)
{
    if (!buf || size == 0) return -1;
    int count = trace_count();
    uint32_t copy_size = count * sizeof(trace_event_t);
    if (copy_size > size) copy_size = size;
    /* 复制事件到缓冲区 */
    int idx = trace_head;
    uint32_t copied = 0;
    while (idx != trace_tail && copied < copy_size) {
        memcpy((uint8_t *)buf + copied, &trace_buffer[idx], sizeof(trace_event_t));
        copied += sizeof(trace_event_t);
        idx = (idx + 1) % TRACE_MAX_EVENTS;
    }
    return copied;
}
