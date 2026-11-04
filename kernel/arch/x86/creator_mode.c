/* ============================================================================
 * 赫普OS - 创变自由模式
 * ========================================================================== */

#include <stdint.h>
#include "creator_mode.h"
#include "vga.h"
#include "string.h"

static creator_mode_t cm;

void creator_mode_init(void)
{
    memset(&cm, 0, sizeof(creator_mode_t));
    cm.initialized = 1;
    cm.current_mode = CM_MODE_NORMAL;
    cm.config.enabled = 1;
    cm.config.require_auth = 1;
    cm.config.auto_snapshot = 1;
    cm.config.allow_system_modify = 1;
    cm.config.allow_kernel_param = 1;
    cm.config.allow_replace_app = 1;
    cm.config.allow_custom_rom = 0;  /* 默认不允许自定义ROM */
    cm.config.audit_enabled = 1;
    cm.config.auto_rollback_on_fail = 1;

    /* 默认受保护项（创变模式下也不可修改的核心） */
    const char *protected_paths[][2] = {
        {"/kernel", " kernel 镜像 , 修改可能导致 cannot start "},
        {"/system/bin/init", " system init process "},
        {"/system/lib/libc.so", " system C 库 "},
        {"/system/etc/fingerprint", " kernel fingerprint (GPLv3 要求 not 可 remove )"},
        {"/boot", " start 引导 partition "},
    };
    for (int i = 0; i < 5; i++) {
        cm.protected[i].used = 1;
        strncpy(cm.protected[i].path, protected_paths[i][0], CM_PATH_LEN - 1);
        strncpy(cm.protected[i].reason, protected_paths[i][1], 127);
        cm.protected[i].level = 0;  /* 核心不可变 */
        cm.protected_count++;
    }

    vga_puts("[CREATOR] creator mode init done \n");
    vga_printf(" current mode: normal secure, protected items: %d, auto snapshot: %s\n",
               cm.protected_count, cm.config.auto_snapshot ? " on " : " close ");
}

int creator_mode_enter(void)
{
    if (cm.current_mode == CM_MODE_CREATOR) return 0;
    /* 开启前自动创建快照 */
    if (cm.config.auto_snapshot) {
        creator_mode_snapshot_create(" enter 创变 mode prev auto backup ", " system auto create ");
    }
    cm_mode_t old = cm.current_mode;
    cm.current_mode = CM_MODE_CREATOR;
    cm.total_mode_switches++;
    creator_mode_audit_log("enter_creator_mode", "system", 0);
    (void)old;
    return 0;
}

int creator_mode_exit(void)
{
    if (cm.current_mode != CM_MODE_CREATOR) return 0;
    cm.current_mode = CM_MODE_NORMAL;
    cm.total_mode_switches++;
    creator_mode_audit_log("exit_creator_mode", "system", 0);
    return 0;
}

int creator_mode_switch(cm_mode_t mode)
{
    if (mode == cm.current_mode) return 0;
    if (mode == CM_MODE_CREATOR) return creator_mode_enter();
    cm_mode_t old = cm.current_mode;
    cm.current_mode = mode;
    cm.total_mode_switches++;
    creator_mode_audit_log("switch_mode", "system", 0);
    (void)old;
    return 0;
}

int creator_mode_get(cm_mode_t *mode)
{
    if (!mode) return -1;
    *mode = cm.current_mode;
    return 0;
}

int creator_mode_is_creator(int *is_creator)
{
    if (!is_creator) return -1;
    *is_creator = (cm.current_mode == CM_MODE_CREATOR) ? 1 : 0;
    return 0;
}

int creator_mode_snapshot_create(const char *name, const char *desc)
{
    for (int i = 0; i < CM_MAX_SNAPSHOTS; i++) {
        if (!cm.snapshots[i].used) {
            memset(&cm.snapshots[i], 0, sizeof(cm_snapshot_t));
            cm.snapshots[i].used = 1;
            cm.snapshots[i].id = i;
            if (name) strncpy(cm.snapshots[i].name, name, CM_NAME_LEN - 1);
            if (desc) strncpy(cm.snapshots[i].description, desc, 255);
            cm.snapshots[i].state = CM_SNAPSHOT_CREATING;
            cm.snapshots[i].create_time = 1;
            cm.snapshots[i].size = 1024 * 1024 * 256;  /* 256MB估算 */
            cm.snapshots[i].is_auto = (name && strstr(name, " auto ")) ? 1 : 0;
            cm.snapshots[i].state = CM_SNAPSHOT_READY;
            cm.snapshots[i].verified = 1;
            cm.snapshot_count++;
            cm.total_snapshots++;
            return i;
        }
    }
    /* 快照已满，删除最旧的自动快照 */
    for (int i = 0; i < CM_MAX_SNAPSHOTS; i++) {
        if (cm.snapshots[i].used && cm.snapshots[i].is_auto) {
            cm.snapshots[i].used = 0;
            cm.snapshot_count--;
            return creator_mode_snapshot_create(name, desc);
        }
    }
    return -1;
}

int creator_mode_snapshot_restore(int snapshot_id)
{
    if (snapshot_id < 0 || snapshot_id >= CM_MAX_SNAPSHOTS ||
        !cm.snapshots[snapshot_id].used) return -1;
    if (cm.snapshots[snapshot_id].state != CM_SNAPSHOT_READY) return -1;
    cm.snapshots[snapshot_id].state = CM_SNAPSHOT_RESTORING;
    /* 执行回滚（简化） */
    cm.snapshots[snapshot_id].state = CM_SNAPSHOT_READY;
    cm.total_rollbacks++;
    creator_mode_audit_log("restore_snapshot", cm.snapshots[snapshot_id].name, 0);
    return 0;
}

int creator_mode_snapshot_delete(int snapshot_id)
{
    if (snapshot_id < 0 || snapshot_id >= CM_MAX_SNAPSHOTS ||
        !cm.snapshots[snapshot_id].used) return -1;
    cm.snapshots[snapshot_id].used = 0;
    cm.snapshot_count--;
    return 0;
}

int creator_mode_snapshot_list(cm_snapshot_t *snaps, int max)
{
    if (!snaps || max <= 0) return 0;
    int count = 0;
    for (int i = CM_MAX_SNAPSHOTS - 1; i >= 0 && count < max; i--) {
        if (cm.snapshots[i].used) memcpy(&snaps[count++], &cm.snapshots[i], sizeof(cm_snapshot_t));
    }
    return count;
}

int creator_mode_snapshot_get(int id, cm_snapshot_t *snap)
{
    if (id < 0 || id >= CM_MAX_SNAPSHOTS || !cm.snapshots[id].used || !snap) return -1;
    memcpy(snap, &cm.snapshots[id], sizeof(cm_snapshot_t));
    return 0;
}

int creator_mode_snapshot_verify(int id)
{
    if (id < 0 || id >= CM_MAX_SNAPSHOTS || !cm.snapshots[id].used) return -1;
    cm.snapshots[id].verified = 1;
    return 0;
}

int creator_mode_modify_param(cm_param_t param, const char *value)
{
    if (cm.current_mode != CM_MODE_CREATOR) {
        cm.total_blocked++;
        creator_mode_audit_log("modify_param", "system", 1);
        return -1;  /* 非创变模式拒绝 */
    }
    if (param < 0 || param >= CM_PARAM_MAX) return -1;
    cm.total_modifications++;
    creator_mode_audit_log("modify_param", value ? value : "null", 0);
    return 0;
}

int creator_mode_replace_system_app(const char *package, const char *new_path)
{
    if (cm.current_mode != CM_MODE_CREATOR || !cm.config.allow_replace_app) {
        cm.total_blocked++;
        return -1;
    }
    if (!package || !new_path) return -1;
    cm.total_modifications++;
    creator_mode_audit_log("replace_app", package, 0);
    return 0;
}

int creator_mode_modify_system_file(const char *path, const char *content)
{
    if (cm.current_mode != CM_MODE_CREATOR || !cm.config.allow_system_modify) {
        cm.total_blocked++;
        return -1;
    }
    if (!path) return -1;
    /* 检查受保护项 */
    int level;
    if (creator_mode_check_protected(path, &level) == 0 && level == 0) {
        cm.total_blocked++;
        creator_mode_audit_log("modify_file_blocked", path, 1);
        return -2;  /* 受核心保护 */
    }
    (void)content;
    cm.total_modifications++;
    creator_mode_audit_log("modify_file", path, 0);
    return 0;
}

int creator_mode_check_protected(const char *path, int *level)
{
    if (!path || !level) return -1;
    *level = 2;  /* 默认可修改 */
    for (int i = 0; i < CM_MAX_PROTECTED; i++) {
        if (cm.protected[i].used && strncmp(path, cm.protected[i].path, strlen(cm.protected[i].path)) == 0) {
            *level = cm.protected[i].level;
            return 0;
        }
    }
    return -1;  /* 不在保护列表 */
}

int creator_mode_add_protected(const char *path, const char *reason, int level)
{
    if (!path) return -1;
    for (int i = 0; i < CM_MAX_PROTECTED; i++) {
        if (!cm.protected[i].used) {
            memset(&cm.protected[i], 0, sizeof(cm_protected_t));
            cm.protected[i].used = 1;
            strncpy(cm.protected[i].path, path, CM_PATH_LEN - 1);
            if (reason) strncpy(cm.protected[i].reason, reason, 127);
            cm.protected[i].level = level;
            cm.protected_count++;
            return 0;
        }
    }
    return -1;
}

int creator_mode_remove_protected(const char *path)
{
    if (!path) return -1;
    for (int i = 0; i < CM_MAX_PROTECTED; i++) {
        if (cm.protected[i].used && strcmp(cm.protected[i].path, path) == 0) {
            cm.protected[i].used = 0;
            cm.protected_count--;
            return 0;
        }
    }
    return -1;
}

int creator_mode_list_protected(cm_protected_t *items, int max)
{
    if (!items || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < CM_MAX_PROTECTED && count < max; i++) {
        if (cm.protected[i].used) memcpy(&items[count++], &cm.protected[i], sizeof(cm_protected_t));
    }
    return count;
}

int creator_mode_audit_log(const char *action, const char *target, int result)
{
    if (!cm.config.audit_enabled) return 0;
    for (int i = 0; i < CM_MAX_AUDIT; i++) {
        if (!cm.audit[i].used) {
            memset(&cm.audit[i], 0, sizeof(cm_audit_t));
            cm.audit[i].used = 1;
            cm.audit[i].id = i;
            cm.audit[i].timestamp = 1;
            cm.audit[i].mode_before = cm.current_mode;
            cm.audit[i].mode_after = cm.current_mode;
            if (action) strncpy(cm.audit[i].action, action, CM_NAME_LEN - 1);
            if (target) strncpy(cm.audit[i].target, target, CM_PATH_LEN - 1);
            cm.audit[i].result = result;
            strncpy(cm.audit[i].user, "owner", 63);
            cm.audit_count++;
            return 0;
        }
    }
    /* 审计日志满，覆盖最旧的 */
    cm.audit[0].used = 0;
    return creator_mode_audit_log(action, target, result);
}

int creator_mode_audit_list(cm_audit_t *logs, int max)
{
    if (!logs || max <= 0) return 0;
    int count = 0;
    for (int i = CM_MAX_AUDIT - 1; i >= 0 && count < max; i--) {
        if (cm.audit[i].used) memcpy(&logs[count++], &cm.audit[i], sizeof(cm_audit_t));
    }
    return count;
}

int creator_mode_set_config(const cm_config_t *config)
{
    if (!config) return -1;
    memcpy(&cm.config, config, sizeof(cm_config_t));
    return 0;
}

int creator_mode_get_config(cm_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &cm.config, sizeof(cm_config_t));
    return 0;
}

int creator_mode_get_stats(creator_mode_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &cm, sizeof(creator_mode_t));
    return 0;
}
