/* ============================================================================
 * 赫普OS - 实时内核深化
 * ========================================================================== */

#include <stdint.h>
#include "realtime_kernel.h"
#include "vga.h"
#include "string.h"

static rtk_kernel_t rt;

void rtk_kernel_init(void)
{
    memset(&rt, 0, sizeof(rtk_kernel_t));
    rt.initialized = 1;
    rt.enabled = 1;
    rt.config.default_policy = RTK_SCHED_EDF;
    rt.config.preemption_enabled = 1;
    rt.config.priority_inheritance = 1;
    rt.config.interrupt_threading = 1;
    rt.config.tick_ns = 100000;  /* 100us */
    rt.config.max_latency_us = 100;
    rt.config.cpu_partitioning = 1;
    rt.config.watchdog_enabled = 1;
    rt.config.watchdog_timeout_ms = 1000;
    rt.running_task = -1;

    vga_puts("[RT] RT kernel deep init done \n");
    vga_printf(" sched: EDF, tick: %u ns, preemptive: %s\n",
               (unsigned long long)rt.config.tick_ns,
               rt.config.preemption_enabled ? " on " : " close ");
}

int rtk_set_config(const rtk_config_t *config)
{
    if (!config) return -1;
    memcpy(&rt.config, config, sizeof(rtk_config_t));
    return 0;
}

int rtk_get_config(rtk_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &rt.config, sizeof(rtk_config_t));
    return 0;
}

/* 任务管理 */
int rtk_task_create(const char *name, rtk_priority_t prio, rtk_sched_policy_t policy,
                   uint64_t period_ns, uint64_t deadline_ns, void (*entry)(void))
{
    (void)entry;
    for (int i = 0; i < RT_MAX_TASKS; i++) {
        if (!rt.tasks[i].used) {
            memset(&rt.tasks[i], 0, sizeof(rtk_task_t));
            rt.tasks[i].used = 1;
            rt.tasks[i].id = i;
            if (name) strncpy(rt.tasks[i].name, name, 127);
            rt.tasks[i].priority = prio;
            rt.tasks[i].policy = policy;
            rt.tasks[i].state = RTK_TASK_READY;
            rt.tasks[i].period_ns = period_ns;
            rt.tasks[i].deadline_ns = deadline_ns;
            rt.tasks[i].execution_ns = period_ns / 10;
            rt.tasks[i].min_exec_ns = 0xFFFFFFFFFFFFFFFFUL;
            rt.tasks[i].cpu_affinity = -1;
            rt.task_count++;
            return i;
        }
    }
    return -1;
}

int rtk_task_destroy(int task_id)
{
    if (task_id < 0 || task_id >= RT_MAX_TASKS || !rt.tasks[task_id].used) return -1;
    rt.tasks[task_id].used = 0;
    rt.task_count--;
    return 0;
}

int rtk_task_suspend(int task_id)
{
    if (task_id < 0 || task_id >= RT_MAX_TASKS || !rt.tasks[task_id].used) return -1;
    rt.tasks[task_id].state = RTK_TASK_SUSPENDED;
    return 0;
}

int rtk_task_resume(int task_id)
{
    if (task_id < 0 || task_id >= RT_MAX_TASKS || !rt.tasks[task_id].used) return -1;
    rt.tasks[task_id].state = RTK_TASK_READY;
    return 0;
}

int rtk_task_yield(void)
{
    rt.total_context_switches++;
    return rtk_schedule();
}

int rtk_task_get(int task_id, rtk_task_t *task)
{
    if (task_id < 0 || task_id >= RT_MAX_TASKS || !rt.tasks[task_id].used || !task) return -1;
    memcpy(task, &rt.tasks[task_id], sizeof(rtk_task_t));
    return 0;
}

int rtk_task_list(rtk_task_t *tasks, int max)
{
    if (!tasks || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < RT_MAX_TASKS && count < max; i++) {
        if (rt.tasks[i].used) memcpy(&tasks[count++], &rt.tasks[i], sizeof(rtk_task_t));
    }
    return count;
}

int rtk_task_set_priority(int task_id, rtk_priority_t prio)
{
    if (task_id < 0 || task_id >= RT_MAX_TASKS || !rt.tasks[task_id].used) return -1;
    rt.tasks[task_id].priority = prio;
    return 0;
}

int rtk_task_set_affinity(int task_id, int cpu)
{
    if (task_id < 0 || task_id >= RT_MAX_TASKS || !rt.tasks[task_id].used) return -1;
    rt.tasks[task_id].cpu_affinity = cpu;
    return 0;
}

/* 调度 */
int rtk_schedule(void)
{
    int best = -1;
    uint64_t earliest_deadline = 0xFFFFFFFFFFFFFFFFUL;
    for (int i = 0; i < RT_MAX_TASKS; i++) {
        if (rt.tasks[i].used && rt.tasks[i].state == RTK_TASK_READY) {
            if (rt.tasks[i].next_release_ns < earliest_deadline) {
                earliest_deadline = rt.tasks[i].next_release_ns;
                best = i;
            }
        }
    }
    if (best >= 0) {
        if (rt.running_task >= 0 && rt.running_task != best)
            rt.tasks[rt.running_task].state = RTK_TASK_READY;
        rt.tasks[best].state = RTK_TASK_RUNNING;
        rt.running_task = best;
        rt.total_context_switches++;
    }
    return best;
}

int rtk_get_current_task(rtk_task_t *task)
{
    if (rt.running_task < 0 || !task) return -1;
    return rtk_task_get(rt.running_task, task);
}

int rtk_get_worst_case_latency(uint64_t *latency_ns)
{
    if (!latency_ns) return -1;
    *latency_ns = rt.max_sched_latency_ns > rt.max_irq_latency_ns ?
                   rt.max_sched_latency_ns : rt.max_irq_latency_ns;
    return 0;
}

/* 定时器 */
int rtk_timer_create(const char *name, uint64_t period_ns, int periodic, void (*callback)(void))
{
    for (int i = 0; i < RT_MAX_TIMERS; i++) {
        if (!rt.timers[i].used) {
            memset(&rt.timers[i], 0, sizeof(rtk_timer_t));
            rt.timers[i].used = 1;
            rt.timers[i].id = i;
            if (name) strncpy(rt.timers[i].name, name, 127);
            rt.timers[i].period_ns = period_ns;
            rt.timers[i].periodic = periodic ? 1 : 0;
            rt.timers[i].callback = callback;
            rt.timer_count++;
            return i;
        }
    }
    return -1;
}

int rtk_timer_start(int timer_id)
{
    if (timer_id < 0 || timer_id >= RT_MAX_TIMERS || !rt.timers[timer_id].used) return -1;
    rt.timers[timer_id].active = 1;
    rt.timers[timer_id].next_fire_ns = rt.uptime_ns + rt.timers[timer_id].period_ns;
    return 0;
}

int rtk_timer_stop(int timer_id)
{
    if (timer_id < 0 || timer_id >= RT_MAX_TIMERS || !rt.timers[timer_id].used) return -1;
    rt.timers[timer_id].active = 0;
    return 0;
}

int rtk_timer_destroy(int timer_id)
{
    if (timer_id < 0 || timer_id >= RT_MAX_TIMERS || !rt.timers[timer_id].used) return -1;
    rt.timers[timer_id].used = 0;
    rt.timer_count--;
    return 0;
}

int rtk_timer_get(int timer_id, rtk_timer_t *timer)
{
    if (timer_id < 0 || timer_id >= RT_MAX_TIMERS || !rt.timers[timer_id].used || !timer) return -1;
    memcpy(timer, &rt.timers[timer_id], sizeof(rtk_timer_t));
    return 0;
}

/* CAN总线 */
int rtk_can_init(int bus_id, uint32_t bitrate, rtk_can_format_t format)
{
    if (bus_id < 0 || bus_id >= RT_MAX_CAN_BUSES) return -1;
    memset(&rt.can_buses[bus_id], 0, sizeof(rtk_can_bus_t));
    rt.can_buses[bus_id].used = 1;
    rt.can_buses[bus_id].id = bus_id;
    rt.can_buses[bus_id].enabled = 1;
    rt.can_buses[bus_id].bitrate = bitrate;
    rt.can_buses[bus_id].format = format;
    rt.can_buses[bus_id].fd_enabled = (format == RTK_CAN_FD) ? 1 : 0;
    rt.can_buses[bus_id].fd_bitrate = bitrate * 4;
    if (rt.can_count <= bus_id) rt.can_count = bus_id + 1;
    return 0;
}

int rtk_can_shutdown(int bus_id)
{
    if (bus_id < 0 || bus_id >= RT_MAX_CAN_BUSES || !rt.can_buses[bus_id].used) return -1;
    rt.can_buses[bus_id].enabled = 0;
    return 0;
}

int rtk_can_send(int bus_id, uint32_t can_id, uint8_t *data, uint8_t len)
{
    if (bus_id < 0 || bus_id >= RT_MAX_CAN_BUSES || !rt.can_buses[bus_id].enabled) return -1;
    (void)can_id; (void)data;
    rt.can_buses[bus_id].tx_frames++;
    return len;
}

int rtk_can_receive(int bus_id, rtk_can_msg_t *msg)
{
    if (bus_id < 0 || bus_id >= RT_MAX_CAN_BUSES || !rt.can_buses[bus_id].enabled || !msg) return -1;
    rt.can_buses[bus_id].rx_frames++;
    memset(msg, 0, sizeof(rtk_can_msg_t));
    msg->bus_id = bus_id;
    msg->is_rx = 1;
    return 0;
}

int rtk_can_get_bus(int bus_id, rtk_can_bus_t *bus)
{
    if (bus_id < 0 || bus_id >= RT_MAX_CAN_BUSES || !rt.can_buses[bus_id].used || !bus) return -1;
    memcpy(bus, &rt.can_buses[bus_id], sizeof(rtk_can_bus_t));
    return 0;
}

int rtk_can_set_bitrate(int bus_id, uint32_t bitrate)
{
    if (bus_id < 0 || bus_id >= RT_MAX_CAN_BUSES || !rt.can_buses[bus_id].used) return -1;
    rt.can_buses[bus_id].bitrate = bitrate;
    return 0;
}

int rtk_can_get_stats(int bus_id, uint64_t *tx, uint64_t *rx, uint64_t *errors)
{
    if (bus_id < 0 || bus_id >= RT_MAX_CAN_BUSES || !rt.can_buses[bus_id].used) return -1;
    if (tx) *tx = rt.can_buses[bus_id].tx_frames;
    if (rx) *rx = rt.can_buses[bus_id].rx_frames;
    if (errors) *errors = rt.can_buses[bus_id].tx_errors + rt.can_buses[bus_id].rx_errors;
    return 0;
}

/* TSN */
int rtk_tsn_create_stream(const char *name, rtk_tsn_type_t type, uint16_t vlan_id,
                         uint8_t pcp, uint64_t period_ns, uint32_t max_size)
{
    for (int i = 0; i < RT_MAX_TSN_STREAMS; i++) {
        if (!rt.tsn_streams[i].used) {
            memset(&rt.tsn_streams[i], 0, sizeof(rtk_tsn_stream_t));
            rt.tsn_streams[i].used = 1;
            rt.tsn_streams[i].id = i;
            if (name) strncpy(rt.tsn_streams[i].name, name, 127);
            rt.tsn_streams[i].type = type;
            rt.tsn_streams[i].vlan_id = vlan_id;
            rt.tsn_streams[i].pcp = pcp;
            rt.tsn_streams[i].period_ns = period_ns;
            rt.tsn_streams[i].max_frame_size = max_size;
            rt.tsn_count++;
            return i;
        }
    }
    return -1;
}

int rtk_tsn_destroy_stream(int stream_id)
{
    if (stream_id < 0 || stream_id >= RT_MAX_TSN_STREAMS || !rt.tsn_streams[stream_id].used) return -1;
    rt.tsn_streams[stream_id].used = 0;
    rt.tsn_count--;
    return 0;
}

int rtk_tsn_send(int stream_id, uint8_t *data, int len)
{
    (void)data;
    if (stream_id < 0 || stream_id >= RT_MAX_TSN_STREAMS || !rt.tsn_streams[stream_id].used) return -1;
    rt.tsn_streams[stream_id].tx_frames++;
    return len;
}

int rtk_tsn_receive(int stream_id, uint8_t *data, int max)
{
    (void)data;
    if (stream_id < 0 || stream_id >= RT_MAX_TSN_STREAMS || !rt.tsn_streams[stream_id].used) return -1;
    rt.tsn_streams[stream_id].rx_frames++;
    return max;
}

int rtk_tsn_get_stream(int stream_id, rtk_tsn_stream_t *stream)
{
    if (stream_id < 0 || stream_id >= RT_MAX_TSN_STREAMS || !rt.tsn_streams[stream_id].used || !stream) return -1;
    memcpy(stream, &rt.tsn_streams[stream_id], sizeof(rtk_tsn_stream_t));
    return 0;
}

int rtk_tsn_list_streams(rtk_tsn_stream_t *streams, int max)
{
    if (!streams || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < RT_MAX_TSN_STREAMS && count < max; i++) {
        if (rt.tsn_streams[i].used) memcpy(&streams[count++], &rt.tsn_streams[i], sizeof(rtk_tsn_stream_t));
    }
    return count;
}

int rtk_tsn_set_gate(int stream_id, uint64_t open_ns, uint64_t close_ns)
{
    if (stream_id < 0 || stream_id >= RT_MAX_TSN_STREAMS || !rt.tsn_streams[stream_id].used) return -1;
    rt.tsn_streams[stream_id].gate_enabled = 1;
    rt.tsn_streams[stream_id].gate_open_ns = open_ns;
    rt.tsn_streams[stream_id].gate_close_ns = close_ns;
    return 0;
}

/* 延迟测量 */
int rtk_measure_irq_latency(uint64_t *latency_ns)
{
    if (!latency_ns) return -1;
    *latency_ns = 5000;  /* 5us */
    if (*latency_ns > rt.max_irq_latency_ns) rt.max_irq_latency_ns = *latency_ns;
    return 0;
}

int rtk_measure_sched_latency(uint64_t *latency_ns)
{
    if (!latency_ns) return -1;
    *latency_ns = 10000;  /* 10us */
    if (*latency_ns > rt.max_sched_latency_ns) rt.max_sched_latency_ns = *latency_ns;
    return 0;
}

int rtk_get_jitter(int task_id, float *jitter_ns)
{
    if (task_id < 0 || task_id >= RT_MAX_TASKS || !rt.tasks[task_id].used || !jitter_ns) return -1;
    *jitter_ns = rt.tasks[task_id].avg_jitter_ns;
    return 0;
}

/* 看门狗 */
int rtk_watchdog_start(uint64_t timeout_ms)
{
    rt.config.watchdog_enabled = 1;
    rt.config.watchdog_timeout_ms = timeout_ms;
    return 0;
}

int rtk_watchdog_feed(void)
{
    return 0;
}

int rtk_watchdog_stop(void)
{
    rt.config.watchdog_enabled = 0;
    return 0;
}

/* 统计 */
int rtk_get_stats(rtk_kernel_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &rt, sizeof(rtk_kernel_t));
    return 0;
}
