/* ============================================================================
 * 赫普OS - 内核级后台冻结
 * 后台应用完全挂起，零调度、零CPU、零后台刷新
 * ========================================================================== */

#include <stdint.h>
#include "app_freeze.h"
#include "process.h"
extern process_t *current;
#include "vga.h"
#include "string.h"

static app_freeze_t af;

/* 外部进程表（定义在process.c） */

void app_freeze_init(void)
{
    memset(&af, 0, sizeof(app_freeze_t));
    af.initialized = 1;
    af.enabled = 1;
    af.screen_on = 1;
    af.battery_level = 100;
    af.policy.enabled = 1;
    af.policy.auto_freeze_on_screen_off = 1;
    af.policy.auto_freeze_background = 1;
    af.policy.background_timeout_s = 300;  /* 5分钟 */
    af.policy.low_battery_threshold = 20;
    af.policy.low_battery_freeze = 1;
    af.policy.preserve_music = 1;
    af.policy.preserve_navigation = 1;
    af.policy.preserve_download = 1;
    af.policy.preserve_voip = 1;

    /* 系统白名单 */
    const char *sys_whitelist[] = {"music_player", "video_player", "navigation",
        "download", "phone", "sms", "voip", "alarm", "hepu_desktop", "systemui"};
    for (int i = 0; i < 10 && i < AF_MAX_WHITELIST; i++) {
        af.whitelist[i].used = 1;
        strncpy(af.whitelist[i].name, sys_whitelist[i], AF_NAME_LEN - 1);
        af.whitelist[i].reason = 1;  /* 系统 */
        af.whitelist_count++;
    }

    vga_puts("[FREEZE] kernel level background freeze init done \n");
    vga_printf(" auto freeze: %s, bg timeout: %ds, whitelist: %d \n",
               af.policy.auto_freeze_background ? " on " : " close ",
               af.policy.background_timeout_s, af.whitelist_count);
}

int app_freeze_process(uint32_t pid, af_reason_t reason)
{
    if (!af.enabled) return -1;
    if (pid == 0) return -1;  /* 不冻结内核进程 */

    /* 查找进程 */
    process_t *proc = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].pid == pid && proc_table[i].state != PROC_UNUSED) {
            proc = &proc_table[i];
            break;
        }
    }
    if (!proc) return -1;

    /* 检查白名单 */
    for (int i = 0; i < AF_MAX_WHITELIST; i++) {
        if (af.whitelist[i].used && strcmp(af.whitelist[i].name, proc->name) == 0)
            return -2;  /* 在白名单中，拒绝冻结 */
    }

    /* 已经冻结 */
    if (proc->state == PROC_FROZEN) return 0;

    /* 执行冻结 */
    proc->state = PROC_FROZEN;

    /* 记录 */
    for (int i = 0; i < AF_MAX_FROZEN; i++) {
        if (!af.records[i].used) {
            memset(&af.records[i], 0, sizeof(af_record_t));
            af.records[i].used = 1;
            af.records[i].pid = pid;
            strncpy(af.records[i].name, proc->name, AF_NAME_LEN - 1);
            af.records[i].reason = reason;
            af.records[i].freeze_time = 1;
            af.records[i].is_frozen = 1;
            af.records[i].freeze_count = 1;
            af.record_count++;
            break;
        } else if (af.records[i].pid == pid) {
            af.records[i].is_frozen = 1;
            af.records[i].reason = reason;
            af.records[i].freeze_count++;
            af.records[i].freeze_time = 1;
            break;
        }
    }

    af.stats.total_freeze_count++;
    af.stats.currently_frozen++;
    return 0;
}

int app_unfreeze_process(uint32_t pid)
{
    process_t *proc = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].pid == pid && proc_table[i].state != PROC_UNUSED) {
            proc = &proc_table[i];
            break;
        }
    }
    if (!proc) return -1;
    if (proc->state != PROC_FROZEN) return 0;

    proc->state = PROC_READY;

    for (int i = 0; i < AF_MAX_FROZEN; i++) {
        if (af.records[i].used && af.records[i].pid == pid) {
            af.records[i].is_frozen = 0;
            af.records[i].total_frozen_ms += 1000;  /* 估算 */
            break;
        }
    }

    af.stats.total_unfreeze_count++;
    if (af.stats.currently_frozen > 0) af.stats.currently_frozen--;
    return 0;
}

int app_freeze_all_background(af_reason_t reason)
{
    int frozen = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_READY ||
            proc_table[i].state == PROC_BLOCKED) {
            /* 跳过当前进程和内核进程 */
            if (current && proc_table[i].pid == current->pid) continue;
            if (proc_table[i].privilege == PRIV_KERNEL) continue;
            if (app_freeze_process(proc_table[i].pid, reason) == 0)
                frozen++;
        }
    }
    return frozen;
}

int app_unfreeze_all(void)
{
    int unfrozen = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_FROZEN) {
            proc_table[i].state = PROC_READY;
            unfrozen++;
        }
    }
    for (int i = 0; i < AF_MAX_FROZEN; i++) {
        if (af.records[i].used) af.records[i].is_frozen = 0;
    }
    af.stats.currently_frozen = 0;
    af.stats.total_unfreeze_count += unfrozen;
    return unfrozen;
}

int app_is_frozen(uint32_t pid, int *frozen)
{
    if (!frozen) return -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].pid == pid) {
            *frozen = (proc_table[i].state == PROC_FROZEN) ? 1 : 0;
            return 0;
        }
    }
    return -1;
}

int app_freeze_whitelist_add(const char *name, int reason)
{
    if (!name) return -1;
    for (int i = 0; i < AF_MAX_WHITELIST; i++) {
        if (!af.whitelist[i].used) {
            memset(&af.whitelist[i], 0, sizeof(af_whitelist_t));
            af.whitelist[i].used = 1;
            strncpy(af.whitelist[i].name, name, AF_NAME_LEN - 1);
            af.whitelist[i].reason = reason;
            af.whitelist_count++;
            return 0;
        }
    }
    return -1;
}

int app_freeze_whitelist_remove(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < AF_MAX_WHITELIST; i++) {
        if (af.whitelist[i].used && strcmp(af.whitelist[i].name, name) == 0) {
            af.whitelist[i].used = 0;
            af.whitelist_count--;
            return 0;
        }
    }
    return -1;
}

int app_freeze_whitelist_check(const char *name, int *whitelisted)
{
    if (!name || !whitelisted) return -1;
    *whitelisted = 0;
    for (int i = 0; i < AF_MAX_WHITELIST; i++) {
        if (af.whitelist[i].used && strcmp(af.whitelist[i].name, name) == 0) {
            *whitelisted = 1;
            return 0;
        }
    }
    return 0;
}

int app_freeze_whitelist_list(af_whitelist_t *list, int max)
{
    if (!list || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < AF_MAX_WHITELIST && count < max; i++) {
        if (af.whitelist[i].used) memcpy(&list[count++], &af.whitelist[i], sizeof(af_whitelist_t));
    }
    return count;
}

int app_freeze_set_policy(const af_policy_t *policy)
{
    if (!policy) return -1;
    memcpy(&af.policy, policy, sizeof(af_policy_t));
    return 0;
}

int app_freeze_get_policy(af_policy_t *policy)
{
    if (!policy) return -1;
    memcpy(policy, &af.policy, sizeof(af_policy_t));
    return 0;
}

int app_freeze_set_enabled(int enable)
{
    af.enabled = enable ? 1 : 0;
    if (!enable) app_unfreeze_all();
    return 0;
}

int app_freeze_screen_state(int on)
{
    int old = af.screen_on;
    af.screen_on = on ? 1 : 0;
    if (old && !on && af.policy.auto_freeze_on_screen_off)
        app_freeze_on_screen_off();
    else if (!old && on)
        app_freeze_on_screen_on();
    return 0;
}

int app_freeze_battery_update(int level)
{
    af.battery_level = level;
    if (level <= af.policy.low_battery_threshold && af.policy.low_battery_freeze)
        app_freeze_on_low_battery();
    return 0;
}

int app_freeze_on_screen_off(void)
{
    return app_freeze_all_background(AF_REASON_SCREEN_OFF);
}

int app_freeze_on_screen_on(void)
{
    return app_unfreeze_all();
}

int app_freeze_on_app_background(uint32_t pid)
{
    if (!af.policy.auto_freeze_background) return 0;
    return app_freeze_process(pid, AF_REASON_BACKGROUND);
}

int app_freeze_on_low_battery(void)
{
    return app_freeze_all_background(AF_REASON_LOW_BATTERY);
}

int app_freeze_get_frozen_list(af_record_t *records, int max)
{
    if (!records || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < AF_MAX_FROZEN && count < max; i++) {
        if (af.records[i].used && af.records[i].is_frozen)
            memcpy(&records[count++], &af.records[i], sizeof(af_record_t));
    }
    return count;
}

int app_freeze_get_stats(af_stats_t *stats)
{
    if (!stats) return -1;
    /* 估算节省电量：每个冻结进程节省约0.5%/小时 */
    af.stats.battery_saved_pct = (uint64_t)af.stats.currently_frozen * 5 / 10;
    memcpy(stats, &af.stats, sizeof(af_stats_t));
    return 0;
}

int app_freeze_get_record(uint32_t pid, af_record_t *record)
{
    if (!record) return -1;
    for (int i = 0; i < AF_MAX_FROZEN; i++) {
        if (af.records[i].used && af.records[i].pid == pid) {
            memcpy(record, &af.records[i], sizeof(af_record_t));
            return 0;
        }
    }
    return -1;
}
