/* ============================================================================
 * 赫普OS - 性能分析
 * perf + 火焰图 + 系统调用跟踪 + 函数级profiling
 * ========================================================================== */

#include <stdint.h>
#include "kperf.h"
#include "vga.h"
#include "string.h"

static kperf_t perf;

void kperf_init(void)
{
    memset(&perf, 0, sizeof(kperf_t));
    perf.initialized = 1;
    perf.enabled = 1;
    perf.sampling_frequency = 1000;  /* 1kHz */
    perf.callchain_enabled = 1;
    perf.userspace_callchain = 1;
    perf.kernel_callchain = 1;

    /* 默认事件 */
    kperf_event_create(KPERF_EVENT_CPU_CYCLES, -1, -1);
    kperf_event_create(KPERF_EVENT_INSTRUCTIONS, -1, -1);
    kperf_event_create(KPERF_EVENT_CACHE_MISSES, -1, -1);
    kperf_event_create(KPERF_EVENT_BRANCH_MISSES, -1, -1);
    kperf_event_create(KPERF_EVENT_CONTEXT_SWITCHES, -1, -1);
    kperf_event_create(KPERF_EVENT_PAGE_FAULTS, -1, -1);

    /* 默认计数器 */
    kperf_counter_add(KPERF_EVENT_CPU_CYCLES, 0);
    kperf_counter_add(KPERF_EVENT_INSTRUCTIONS, 0);
    kperf_counter_add(KPERF_EVENT_CACHE_MISSES, 0);

    /* 内核符号 */
    const char *sym_names[] = {"kernel_main", "kdebug_init", "kperf_init", "scheduler_tick",
                               "syscall_handler", "irq_handler", "memory_alloc", "memory_free",
                               "process_create", "process_switch", "ipc_send", "ipc_recv",
                               "vfs_read", "vfs_write", "net_send", "net_recv"};
    for (int i = 0; i < 16; i++) {
        kperf_symbol_add(0x100000 + i * 0x1000, 0x100000 + (i + 1) * 0x1000, sym_names[i], "kernel");
    }

    vga_puts("[KPERF] perf profile init done \n");
    vga_printf(" events: %d, counter: %d, sym: %d, sample freq: %dHz\n",
               perf.event_count, perf.counter_count, perf.symbol_count, perf.sampling_frequency);
}

/* 事件管理 */
int kperf_event_create(kkperf_event_type_t type, int pid, int cpu)
{
    for (int i = 0; i < KPERF_MAX_EVENTS; i++) {
        if (!perf.events[i].used) {
            memset(&perf.events[i], 0, sizeof(kperf_event_t));
            perf.events[i].used = 1;
            perf.events[i].id = i;
            perf.events[i].type = type;
            perf.events[i].pid = pid;
            perf.events[i].cpu = cpu;
            perf.events[i].enabled = 1;
            perf.events[i].frequency = 1000;
            perf.event_count++;
            return i;
        }
    }
    return -1;
}

int kperf_event_enable(int event_id)
{
    if (event_id < 0 || event_id >= KPERF_MAX_EVENTS || !perf.events[event_id].used) return -1;
    perf.events[event_id].enabled = 1;
    return 0;
}

int kperf_event_disable(int event_id)
{
    if (event_id < 0 || event_id >= KPERF_MAX_EVENTS || !perf.events[event_id].used) return -1;
    perf.events[event_id].enabled = 0;
    return 0;
}

int kperf_event_read(int event_id, uint64_t *value)
{
    if (event_id < 0 || event_id >= KPERF_MAX_EVENTS || !perf.events[event_id].used || !value) return -1;
    *value = perf.events[event_id].count;
    return 0;
}

int kperf_event_reset(int event_id)
{
    if (event_id < 0 || event_id >= KPERF_MAX_EVENTS || !perf.events[event_id].used) return -1;
    perf.events[event_id].count = 0;
    return 0;
}

int kperf_event_list(kperf_event_t *events, int max)
{
    if (!events || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < KPERF_MAX_EVENTS && count < max; i++) {
        if (perf.events[i].used) memcpy(&events[count++], &perf.events[i], sizeof(kperf_event_t));
    }
    return count;
}

int kperf_event_get(int event_id, kperf_event_t *event)
{
    if (event_id < 0 || event_id >= KPERF_MAX_EVENTS || !perf.events[event_id].used || !event) return -1;
    memcpy(event, &perf.events[event_id], sizeof(kperf_event_t));
    return 0;
}

/* 计数器 */
int kperf_counter_add(kkperf_event_type_t type, int cpu)
{
    for (int i = 0; i < KPERF_MAX_COUNTERS; i++) {
        if (!perf.counters[i].used) {
            memset(&perf.counters[i], 0, sizeof(kperf_counter_t));
            perf.counters[i].used = 1;
            perf.counters[i].id = i;
            perf.counters[i].type = type;
            perf.counters[i].cpu = cpu;
            perf.counters[i].active = 1;
            perf.counter_count++;
            return i;
        }
    }
    return -1;
}

int kperf_counter_read(int counter_id, uint64_t *value)
{
    if (counter_id < 0 || counter_id >= KPERF_MAX_COUNTERS || !perf.counters[counter_id].used || !value) return -1;
    *value = perf.counters[counter_id].value;
    return 0;
}

int kperf_counter_start(int counter_id)
{
    if (counter_id < 0 || counter_id >= KPERF_MAX_COUNTERS || !perf.counters[counter_id].used) return -1;
    perf.counters[counter_id].active = 1;
    return 0;
}

int kperf_counter_stop(int counter_id)
{
    if (counter_id < 0 || counter_id >= KPERF_MAX_COUNTERS || !perf.counters[counter_id].used) return -1;
    perf.counters[counter_id].active = 0;
    return 0;
}

int kperf_counter_list(kperf_counter_t *counters, int max)
{
    if (!counters || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < KPERF_MAX_COUNTERS && count < max; i++) {
        if (perf.counters[i].used) memcpy(&counters[count++], &perf.counters[i], sizeof(kperf_counter_t));
    }
    return count;
}

/* 采样 */
int kperf_sample_record(perf_sample_t *sample)
{
    if (!sample) return -1;
    memcpy(&perf.samples[perf.sample_index], sample, sizeof(perf_sample_t));
    perf.sample_index = (perf.sample_index + 1) % KPERF_MAX_SAMPLES;
    if (perf.sample_count < KPERF_MAX_SAMPLES) perf.sample_count++;
    perf.total_samples++;
    return 0;
}

int kperf_sample_get(int index, perf_sample_t *sample)
{
    if (index < 0 || index >= KPERF_MAX_SAMPLES || !sample) return -1;
    memcpy(sample, &perf.samples[index], sizeof(perf_sample_t));
    return 0;
}

int kperf_sample_list(perf_sample_t *samples, int max)
{
    if (!samples || max <= 0) return 0;
    int count = perf.sample_count < max ? perf.sample_count : max;
    for (int i = 0; i < count; i++) memcpy(&samples[i], &perf.samples[i], sizeof(perf_sample_t));
    return count;
}

int kperf_sample_flush(void)
{
    perf.sample_count = 0;
    perf.sample_index = 0;
    return 0;
}

/* 符号 */
int kperf_symbol_add(uint64_t start, uint64_t end, const char *name, const char *module)
{
    for (int i = 0; i < KPERF_MAX_SYMBOLS; i++) {
        if (perf.symbols[i].start == 0 && perf.symbols[i].name[0] == 0) {
            perf.symbols[i].start = start;
            perf.symbols[i].end = end;
            if (name) strncpy(perf.symbols[i].name, name, 63);
            if (module) strncpy(perf.symbols[i].module, module, 63);
            perf.symbol_count++;
            return i;
        }
    }
    return -1;
}

int kperf_symbol_lookup(uint64_t addr, perf_symbol_t *symbol)
{
    if (!symbol) return -1;
    for (int i = 0; i < KPERF_MAX_SYMBOLS; i++) {
        if (addr >= perf.symbols[i].start && addr < perf.symbols[i].end) {
            memcpy(symbol, &perf.symbols[i], sizeof(perf_symbol_t));
            return i;
        }
    }
    return -1;
}

int kperf_symbol_list(perf_symbol_t *symbols, int max)
{
    if (!symbols || max <= 0) return 0;
    int count = perf.symbol_count < max ? perf.symbol_count : max;
    for (int i = 0; i < count; i++) memcpy(&symbols[i], &perf.symbols[i], sizeof(perf_symbol_t));
    return count;
}

int kperf_symbol_hot_list(perf_symbol_t *symbols, int max)
{
    if (!symbols || max <= 0) return 0;
    /* 简单排序: 按hot计数 */
    perf_symbol_t sorted[KPERF_MAX_SYMBOLS];
    int count = perf.symbol_count < max ? perf.symbol_count : max;
    for (int i = 0; i < count; i++) memcpy(&sorted[i], &perf.symbols[i], sizeof(perf_symbol_t));
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (sorted[j].hot > sorted[i].hot) {
                perf_symbol_t tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }
    for (int i = 0; i < count; i++) memcpy(&symbols[i], &sorted[i], sizeof(perf_symbol_t));
    return count;
}

/* 进程统计 */
int kperf_process_update(int pid, const char *name, perf_process_t *stats)
{
    for (int i = 0; i < KPERF_MAX_PROCESSES; i++) {
        if (perf.processes[i].pid == pid) {
            if (name) strncpy(perf.processes[i].name, name, 63);
            if (stats) memcpy(&perf.processes[i], stats, sizeof(perf_process_t));
            perf.processes[i].pid = pid;
            return i;
        }
    }
    for (int i = 0; i < KPERF_MAX_PROCESSES; i++) {
        if (perf.processes[i].pid == 0) {
            perf.processes[i].pid = pid;
            if (name) strncpy(perf.processes[i].name, name, 63);
            if (stats) memcpy(&perf.processes[i], stats, sizeof(perf_process_t));
            perf.processes[i].pid = pid;
            perf.process_count++;
            return i;
        }
    }
    return -1;
}

int kperf_process_get(int pid, perf_process_t *process)
{
    if (!process) return -1;
    for (int i = 0; i < KPERF_MAX_PROCESSES; i++) {
        if (perf.processes[i].pid == pid) {
            memcpy(process, &perf.processes[i], sizeof(perf_process_t));
            return i;
        }
    }
    return -1;
}

int kperf_process_list(perf_process_t *processes, int max)
{
    if (!processes || max <= 0) return 0;
    int count = perf.process_count < max ? perf.process_count : max;
    for (int i = 0; i < count; i++) memcpy(&processes[i], &perf.processes[i], sizeof(perf_process_t));
    return count;
}

int kperf_process_top(perf_process_t *processes, int max, int sort_by)
{
    (void)sort_by;
    return kperf_process_list(processes, max);
}

/* 火焰图 */
int kperf_flame_add(const char *stack[], int depth, uint64_t count)
{
    (void)stack; (void)depth; (void)count;
    return 0;
}

int kperf_flame_get(flame_node_t *nodes, int max)
{
    if (!nodes || max <= 0) return 0;
    int count = perf.flame_node_count < max ? perf.flame_node_count : max;
    for (int i = 0; i < count; i++) memcpy(&nodes[i], &perf.flame[i], sizeof(flame_node_t));
    return count;
}

int kperf_flame_generate(void)
{
    perf.flame_node_count = 0;
    /* 根节点 */
    strncpy(perf.flame[0].name, "all", 63);
    perf.flame[0].count = perf.total_samples;
    perf.flame[0].depth = 0;
    perf.flame[0].parent = -1;
    perf.flame_node_count = 1;
    return 0;
}

int kperf_flame_print(void)
{
    vga_puts(" flamegraph:\n");
    for (int i = 0; i < perf.flame_node_count; i++) {
        for (int d = 0; d < perf.flame[i].depth; d++) vga_puts("  ");
        vga_printf("%s (%u)\n", perf.flame[i].name, (uint32_t)perf.flame[i].count);
    }
    return perf.flame_node_count;
}

/* 系统调用跟踪 */
int kperf_syscall_trace_start(void)
{
    perf.syscall_tracing_enabled = 1;
    return 0;
}

int kperf_syscall_trace_stop(void)
{
    perf.syscall_tracing_enabled = 0;
    return 0;
}

int kperf_syscall_trace_record(int syscall, uint64_t duration_ns, int error)
{
    if (syscall < 0 || syscall >= 256) return -1;
    if (!perf.syscall_traces[syscall].name[0]) {
        perf.syscall_traces[syscall].syscall_number = syscall;
        perf.syscall_trace_count++;
    }
    perf.syscall_traces[syscall].count++;
    perf.syscall_traces[syscall].total_time_ns += duration_ns;
    if (duration_ns > perf.syscall_traces[syscall].max_time_ns)
        perf.syscall_traces[syscall].max_time_ns = duration_ns;
    if (perf.syscall_traces[syscall].min_time_ns == 0 || duration_ns < perf.syscall_traces[syscall].min_time_ns)
        perf.syscall_traces[syscall].min_time_ns = duration_ns;
    if (error) perf.syscall_traces[syscall].errors++;
    return 0;
}

int kperf_syscall_trace_get(int syscall, syscall_trace_t *trace)
{
    if (syscall < 0 || syscall >= 256 || !trace) return -1;
    memcpy(trace, &perf.syscall_traces[syscall], sizeof(syscall_trace_t));
    return 0;
}

int kperf_syscall_trace_list(syscall_trace_t *traces, int max)
{
    if (!traces || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < 256 && count < max; i++) {
        if (perf.syscall_traces[i].count > 0) memcpy(&traces[count++], &perf.syscall_traces[i], sizeof(syscall_trace_t));
    }
    return count;
}

int kperf_syscall_trace_top(syscall_trace_t *traces, int max, int sort_by)
{
    (void)sort_by;
    return kperf_syscall_trace_list(traces, max);
}

/* 性能分析控制 */
int kperf_start(int frequency, int callchain)
{
    perf.sampling_frequency = frequency;
    perf.callchain_enabled = callchain;
    perf.enabled = 1;
    perf.profiling_start_time = 0;
    perf.total_samples = 0;
    vga_printf("[KPERF] perf profile start, freq: %dHz, callchain: %s\n",
               frequency, callchain ? " on " : " close ");
    return 0;
}

int kperf_stop(void)
{
    perf.enabled = 0;
    perf.profiling_end_time = 0;
    perf.profiling_duration_ms = 1000;
    return 0;
}

int kperf_report(void)
{
    vga_puts("=== perf profile report ===\n");
    vga_printf(" samples: %u\n", (uint32_t)perf.total_samples);
    vga_printf(" event count: %d\n", perf.event_count);
    vga_printf(" sym count: %d\n", perf.symbol_count);
    vga_printf(" proc count: %d\n", perf.process_count);
    vga_printf(" syscall trace: %d \n", perf.syscall_trace_count);
    vga_printf(" duration: %u ms\n", (uint32_t)perf.profiling_duration_ms);
    /* 热点符号 */
    vga_puts(" hot functions:\n");
    perf_symbol_t hot[10];
    int hot_count = kperf_symbol_hot_list(hot, 10);
    for (int i = 0; i < hot_count; i++) {
        vga_printf(" %-20s %u \n", hot[i].name, (uint32_t)hot[i].hot);
    }
    return 0;
}

int kperf_get_stats(kperf_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &perf, sizeof(kperf_t));
    return 0;
}
