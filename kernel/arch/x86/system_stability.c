/* ============================================================================
 * 赫普OS - 系统稳定性/BUG修复
 * ========================================================================== */

#include <stdint.h>
#include "system_stability.h"
#include "vga.h"
#include "string.h"

static system_stability_t ss;

void system_stability_init(void)
{
    memset(&ss, 0, sizeof(system_stability_t));
    ss.initialized = 1;
    ss.enabled = 1;
    ss.health = SS_HEALTH_GOOD;
    ss.config.leak_detection = 1;
    ss.config.deadlock_detection = 1;
    ss.config.crash_recovery = 1;
    ss.config.health_monitoring = 1;
    ss.config.watchdog = 1;
    ss.config.watchdog_timeout_ms = 5000;
    ss.config.auto_reboot_on_crash = 0;
    ss.config.max_crashes_before_reboot = 5;
    ss.config.log_crashes = 1;

    /* 注册系统检查项 */
    const char *checks[] = {" memory used ","CPU 负载 "," proc count "," file 描述符 ",
        " disk empty 间 "," net connect "," irq 响应 "," sched 延迟 "," 锁竞争 "," 温度 "};
    for (int i = 0; i < 10 && i < SS_MAX_CHECKS; i++) {
        ss.checks[i].used = 1;
        ss.checks[i].id = i;
        strncpy(ss.checks[i].name, checks[i], 127);
        ss.checks[i].enabled = 1;
        ss.checks[i].interval_s = 60;
        ss.checks[i].last_result = 0;
        ss.check_count++;
    }

    vga_puts("[STABILITY] stability monitor init done \n");
    vga_printf(" leak detect: %s, deadlock detect: %s, crash recovery: %s\n",
               ss.config.leak_detection ? " on " : " close ",
               ss.config.deadlock_detection ? " on " : " close ",
               ss.config.crash_recovery ? " on " : " close ");
}

int ss_health_check(void)
{
    /* 简化：根据内存和崩溃数评估健康 */
    if (ss.crash_count > 10 || ss.memory.fragmentation_percent > 50)
        ss.health = SS_HEALTH_POOR;
    else if (ss.crash_count > 5 || ss.memory.fragmentation_percent > 30)
        ss.health = SS_HEALTH_FAIR;
    else if (ss.crash_count > 0)
        ss.health = SS_HEALTH_GOOD;
    else
        ss.health = SS_HEALTH_EXCELLENT;
    return ss.health;
}

int ss_get_health(ss_health_t *health)
{
    if (!health) return -1;
    *health = ss.health;
    return 0;
}

int ss_get_health_score(int *score)
{
    if (!score) return -1;
    switch (ss.health) {
        case SS_HEALTH_EXCELLENT: *score = 95; break;
        case SS_HEALTH_GOOD: *score = 80; break;
        case SS_HEALTH_FAIR: *score = 60; break;
        case SS_HEALTH_POOR: *score = 40; break;
        case SS_HEALTH_CRITICAL: *score = 20; break;
        default: *score = 50;
    }
    return 0;
}

int ss_run_all_checks(void)
{
    int failures = 0;
    for (int i = 0; i < SS_MAX_CHECKS; i++) {
        if (ss.checks[i].used && ss.checks[i].enabled) {
            ss.checks[i].last_result = 0;  /* pass */
            ss.checks[i].total_checks++;
        }
    }
    return failures;
}

int ss_run_check(int check_id)
{
    if (check_id < 0 || check_id >= SS_MAX_CHECKS || !ss.checks[check_id].used) return -1;
    ss.checks[check_id].last_result = 0;
    ss.checks[check_id].total_checks++;
    return 0;
}

int ss_register_check(const char *name, int interval_s)
{
    for (int i = 0; i < SS_MAX_CHECKS; i++) {
        if (!ss.checks[i].used) {
            memset(&ss.checks[i], 0, sizeof(ss_check_t));
            ss.checks[i].used = 1;
            ss.checks[i].id = i;
            if (name) strncpy(ss.checks[i].name, name, 127);
            ss.checks[i].interval_s = interval_s;
            ss.checks[i].enabled = 1;
            ss.check_count++;
            return i;
        }
    }
    return -1;
}

int ss_get_checks(ss_check_t *checks, int max)
{
    if (!checks || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < SS_MAX_CHECKS && count < max; i++) {
        if (ss.checks[i].used) memcpy(&checks[count++], &ss.checks[i], sizeof(ss_check_t));
    }
    return count;
}

int ss_track_alloc(uint32_t address, uint64_t size, const char *site)
{
    for (int i = 0; i < SS_MAX_LEAKS; i++) {
        if (!ss.leaks[i].used) {
            memset(&ss.leaks[i], 0, sizeof(ss_leak_t));
            ss.leaks[i].used = 1;
            ss.leaks[i].id = i;
            if (site) strncpy(ss.leaks[i].alloc_site, site, 127);
            ss.leaks[i].address = address;
            ss.leaks[i].size = size;
            ss.leaks[i].alloc_time = 1;
            ss.leak_count++;
            ss.memory.total_alloc += size;
            ss.memory.current_used += size;
            ss.memory.alloc_count++;
            if (ss.memory.current_used > ss.memory.peak_used)
                ss.memory.peak_used = ss.memory.current_used;
            return i;
        }
    }
    return -1;
}

int ss_track_free(uint32_t address)
{
    for (int i = 0; i < SS_MAX_LEAKS; i++) {
        if (ss.leaks[i].used && ss.leaks[i].address == address) {
            ss.memory.total_free += ss.leaks[i].size;
            ss.memory.current_used -= ss.leaks[i].size;
            ss.memory.free_count++;
            ss.leaks[i].used = 0;
            ss.leak_count--;
            return 0;
        }
    }
    return -1;
}

int ss_detect_leaks(void)
{
    int detected = 0;
    for (int i = 0; i < SS_MAX_LEAKS; i++) {
        if (ss.leaks[i].used && !ss.leaks[i].leak_detected) {
            ss.leaks[i].leak_detected = 1;
            ss.memory.leak_estimate += ss.leaks[i].size;
            detected++;
        }
    }
    return detected;
}

int ss_get_leaks(ss_leak_t *leaks, int max)
{
    if (!leaks || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < SS_MAX_LEAKS && count < max; i++) {
        if (ss.leaks[i].used) memcpy(&leaks[count++], &ss.leaks[i], sizeof(ss_leak_t));
    }
    return count;
}

int ss_get_memory_stats(ss_memory_stats_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &ss.memory, sizeof(ss_memory_stats_t));
    return 0;
}

int ss_lock_acquire(const char *name, int holder)
{
    for (int i = 0; i < SS_MAX_LOCKS; i++) {
        if (ss.locks[i].used && strcmp(ss.locks[i].name, name) == 0) {
            ss.locks[i].held_by = holder;
            ss.locks[i].acquire_time = 1;
            return 0;
        }
    }
    for (int i = 0; i < SS_MAX_LOCKS; i++) {
        if (!ss.locks[i].used) {
            memset(&ss.locks[i], 0, sizeof(ss_lock_t));
            ss.locks[i].used = 1;
            ss.locks[i].id = i;
            if (name) strncpy(ss.locks[i].name, name, 127);
            ss.locks[i].held_by = holder;
            ss.locks[i].acquire_time = 1;
            ss.lock_count++;
            return 0;
        }
    }
    return -1;
}

int ss_lock_release(const char *name)
{
    for (int i = 0; i < SS_MAX_LOCKS; i++) {
        if (ss.locks[i].used && strcmp(ss.locks[i].name, name) == 0) {
            ss.locks[i].held_by = -1;
            ss.locks[i].waiter_count = 0;
            return 0;
        }
    }
    return -1;
}

int ss_lock_wait(const char *name, int waiter)
{
    for (int i = 0; i < SS_MAX_LOCKS; i++) {
        if (ss.locks[i].used && strcmp(ss.locks[i].name, name) == 0) {
            if (ss.locks[i].waiter_count < 16)
                ss.locks[i].waiters[ss.locks[i].waiter_count++] = waiter;
            return 0;
        }
    }
    return -1;
}

int ss_detect_deadlocks(void)
{
    int detected = 0;
    for (int i = 0; i < SS_MAX_LOCKS; i++) {
        if (ss.locks[i].used && ss.locks[i].waiter_count > 0 && !ss.locks[i].deadlock_detected) {
            /* 简化检测：持有时间过长且有等待者 */
            ss.locks[i].deadlock_detected = 1;
            detected++;
        }
    }
    return detected;
}

int ss_get_locks(ss_lock_t *locks, int max)
{
    if (!locks || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < SS_MAX_LOCKS && count < max; i++) {
        if (ss.locks[i].used) memcpy(&locks[count++], &ss.locks[i], sizeof(ss_lock_t));
    }
    return count;
}

int ss_crash_report(const char *process, uint32_t fault_addr, int signal, int error)
{
    for (int i = 0; i < SS_MAX_CRASHES; i++) {
        if (!ss.crashes[i].used) {
            memset(&ss.crashes[i], 0, sizeof(ss_crash_t));
            ss.crashes[i].used = 1;
            ss.crashes[i].id = i;
            if (process) strncpy(ss.crashes[i].process, process, 127);
            ss.crashes[i].fault_address = fault_addr;
            ss.crashes[i].signal = signal;
            ss.crashes[i].error_code = error;
            ss.crashes[i].timestamp = 1;
            ss.crash_count++;
            ss.total_crashes++;
            ss.last_crash_time = 1;
            return i;
        }
    }
    return -1;
}

int ss_crash_recover(int crash_id)
{
    if (crash_id < 0 || crash_id >= SS_MAX_CRASHES || !ss.crashes[crash_id].used) return -1;
    ss.crashes[crash_id].recovered = 1;
    ss.total_recoveries++;
    return 0;
}

int ss_get_crashes(ss_crash_t *crashes, int max)
{
    if (!crashes || max <= 0) return 0;
    int count = 0;
    for (int i = SS_MAX_CRASHES - 1; i >= 0 && count < max; i--) {
        if (ss.crashes[i].used) memcpy(&crashes[count++], &ss.crashes[i], sizeof(ss_crash_t));
    }
    return count;
}

int ss_get_crash(int crash_id, ss_crash_t *crash)
{
    if (crash_id < 0 || crash_id >= SS_MAX_CRASHES || !ss.crashes[crash_id].used || !crash) return -1;
    memcpy(crash, &ss.crashes[crash_id], sizeof(ss_crash_t));
    return 0;
}

int ss_watchdog_start(uint64_t timeout_ms)
{
    ss.config.watchdog = 1;
    ss.config.watchdog_timeout_ms = timeout_ms;
    return 0;
}

int ss_watchdog_feed(void)
{
    return 0;
}

int ss_watchdog_stop(void)
{
    ss.config.watchdog = 0;
    return 0;
}

int ss_set_config(const ss_config_t *config)
{
    if (!config) return -1;
    memcpy(&ss.config, config, sizeof(ss_config_t));
    return 0;
}

int ss_get_config(ss_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &ss.config, sizeof(ss_config_t));
    return 0;
}

int ss_get_stats(system_stability_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &ss, sizeof(system_stability_t));
    return 0;
}

int ss_get_uptime(uint64_t *uptime_s)
{
    if (!uptime_s) return -1;
    *uptime_s = ss.uptime_s;
    return 0;
}
