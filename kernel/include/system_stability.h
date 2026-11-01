#ifndef SYSTEM_STABILITY_H
#define SYSTEM_STABILITY_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 系统稳定性/BUG修复
 * 内存泄漏检测 / 死锁检测 / 崩溃恢复 / 系统健康监控
 * ========================================================================== */

#define SS_MAX_LEAKS 256
#define SS_MAX_LOCKS 128
#define SS_MAX_CRASHES 64
#define SS_MAX_CHECKS 64
#define SS_NAME_LEN 128

/* 健康等级 */
typedef enum {
    SS_HEALTH_EXCELLENT = 0,
    SS_HEALTH_GOOD,
    SS_HEALTH_FAIR,
    SS_HEALTH_POOR,
    SS_HEALTH_CRITICAL,
} ss_health_t;

/* 泄漏记录 */
typedef struct {
    int used;
    int id;
    char alloc_site[SS_NAME_LEN];
    uint32_t address;
    uint64_t size;
    uint64_t alloc_time;
    int leak_detected;
    int stack_trace[8];
} ss_leak_t;

/* 锁记录 */
typedef struct {
    int used;
    int id;
    char name[SS_NAME_LEN];
    int held_by;
    uint64_t acquire_time;
    int waiters[16];
    int waiter_count;
    int deadlock_detected;
    uint64_t hold_time;
} ss_lock_t;

/* 崩溃记录 */
typedef struct {
    int used;
    int id;
    char process[SS_NAME_LEN];
    uint32_t fault_address;
    int signal;
    int error_code;
    uint64_t timestamp;
    int stack_trace[16];
    int stack_depth;
    int recovered;
    char reason[256];
} ss_crash_t;

/* 系统检查项 */
typedef struct {
    int used;
    int id;
    char name[SS_NAME_LEN];
    int enabled;
    int interval_s;
    uint64_t last_check;
    int last_result;  /* 0=pass,1=warn,2=fail */
    char last_message[256];
    uint64_t total_checks;
    uint64_t total_failures;
} ss_check_t;

/* 内存统计 */
typedef struct {
    uint64_t total_alloc;
    uint64_t total_free;
    uint64_t current_used;
    uint64_t peak_used;
    uint64_t leak_estimate;
    int alloc_count;
    int free_count;
    int fragmentation_percent;
} ss_memory_stats_t;

/* 稳定性配置 */
typedef struct {
    int enabled;
    int leak_detection;
    int deadlock_detection;
    int crash_recovery;
    int health_monitoring;
    int watchdog;
    uint64_t watchdog_timeout_ms;
    int auto_reboot_on_crash;
    int max_crashes_before_reboot;
    int log_crashes;
} ss_config_t;

/* 稳定性管理器状态 */
typedef struct {
    int initialized;
    int enabled;
    ss_config_t config;
    ss_health_t health;
    /* 泄漏 */
    ss_leak_t leaks[SS_MAX_LEAKS];
    int leak_count;
    /* 锁 */
    ss_lock_t locks[SS_MAX_LOCKS];
    int lock_count;
    /* 崩溃 */
    ss_crash_t crashes[SS_MAX_CRASHES];
    int crash_count;
    /* 检查 */
    ss_check_t checks[SS_MAX_CHECKS];
    int check_count;
    /* 内存 */
    ss_memory_stats_t memory;
    /* 统计 */
    uint64_t uptime_s;
    uint64_t total_reboots;
    uint64_t total_crashes;
    uint64_t total_recoveries;
    uint64_t last_crash_time;
} system_stability_t;

/* 初始化 */
void system_stability_init(void);

/* 健康监控 */
int ss_health_check(void);
int ss_get_health(ss_health_t *health);
int ss_get_health_score(int *score);
int ss_run_all_checks(void);
int ss_run_check(int check_id);
int ss_register_check(const char *name, int interval_s);
int ss_get_checks(ss_check_t *checks, int max);

/* 内存泄漏检测 */
int ss_track_alloc(uint32_t address, uint64_t size, const char *site);
int ss_track_free(uint32_t address);
int ss_detect_leaks(void);
int ss_get_leaks(ss_leak_t *leaks, int max);
int ss_get_memory_stats(ss_memory_stats_t *stats);

/* 死锁检测 */
int ss_lock_acquire(const char *name, int holder);
int ss_lock_release(const char *name);
int ss_lock_wait(const char *name, int waiter);
int ss_detect_deadlocks(void);
int ss_get_locks(ss_lock_t *locks, int max);

/* 崩溃处理 */
int ss_crash_report(const char *process, uint32_t fault_addr, int signal, int error);
int ss_crash_recover(int crash_id);
int ss_get_crashes(ss_crash_t *crashes, int max);
int ss_get_crash(int crash_id, ss_crash_t *crash);

/* 看门狗 */
int ss_watchdog_start(uint64_t timeout_ms);
int ss_watchdog_feed(void);
int ss_watchdog_stop(void);

/* 配置 */
int ss_set_config(const ss_config_t *config);
int ss_get_config(ss_config_t *config);

/* 统计 */
int ss_get_stats(system_stability_t *stats);
int ss_get_uptime(uint64_t *uptime_s);

#endif /* SYSTEM_STABILITY_H */
