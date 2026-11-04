/* ============================================================================
 * 赫普OS - 多用户/隐私空间
 * ========================================================================== */

#include <stdint.h>
#include "multi_user.h"
#include "vga.h"
#include "string.h"

static multi_user_t mu;

void multi_user_init(void)
{
    memset(&mu, 0, sizeof(multi_user_t));
    mu.initialized = 1;
    mu.enabled = 1;
    /* 创建机主用户 */
    mu.users[0].used = 1;
    mu.users[0].id = 0;
    strncpy(mu.users[0].name, " owner ", 127);
    mu.users[0].type = MU_USER_OWNER;
    mu.users[0].state = MU_USER_STATE_RUNNING;
    mu.users[0].can_install_apps = 1;
    mu.users[0].can_uninstall_apps = 1;
    mu.users[0].can_make_calls = 1;
    mu.users[0].can_send_sms = 1;
    mu.users[0].can_use_camera = 1;
    mu.users[0].can_use_location = 1;
    mu.users[0].can_access_storage = 1;
    mu.users[0].can_change_settings = 1;
    mu.user_count = 1;
    mu.current_user = 0;
    /* 隐私空间默认关闭 */
    mu.private_config.hidden = 1;
    mu.private_config.password_protected = 1;
    mu.private_config.fingerprint_unlock = 1;
    mu.private_config.separate_notifications = 1;

    vga_puts("[MULTIUSER] multi-user / privacy space init done \n");
    vga_printf(" user: %d, current: owner, private space: %s\n",
               mu.user_count, mu.private_config.enabled ? " on " : " close ");
}

int mu_user_create(const char *name, mu_user_type_t type)
{
    for (int i = 0; i < MU_MAX_USERS; i++) {
        if (!mu.users[i].used) {
            memset(&mu.users[i], 0, sizeof(mu_user_t));
            mu.users[i].used = 1;
            mu.users[i].id = i;
            if (name) strncpy(mu.users[i].name, name, 127);
            mu.users[i].type = type;
            mu.users[i].state = MU_USER_STATE_ACTIVE;
            mu.users[i].create_time = 1;
            /* 默认权限 */
            mu.users[i].can_install_apps = (type == MU_USER_OWNER);
            mu.users[i].can_use_camera = 1;
            mu.users[i].can_use_location = 1;
            mu.users[i].can_access_storage = 1;
            mu.user_count++;
            return i;
        }
    }
    return -1;
}

int mu_user_remove(int user_id)
{
    if (user_id <= 0 || user_id >= MU_MAX_USERS || !mu.users[user_id].used) return -1;
    if (mu.users[user_id].type == MU_USER_OWNER) return -1;  /* 不能删除机主 */
    mu.users[user_id].used = 0;
    mu.user_count--;
    return 0;
}

int mu_user_switch(int user_id)
{
    if (user_id < 0 || user_id >= MU_MAX_USERS || !mu.users[user_id].used) return -1;
    mu.users[mu.current_user].state = MU_USER_STATE_ACTIVE;
    mu.current_user = user_id;
    mu.users[user_id].state = MU_USER_STATE_RUNNING;
    mu.users[user_id].last_login = 1;
    mu.total_switches++;
    return 0;
}

int mu_user_get(int user_id, mu_user_t *user)
{
    if (user_id < 0 || user_id >= MU_MAX_USERS || !mu.users[user_id].used || !user) return -1;
    memcpy(user, &mu.users[user_id], sizeof(mu_user_t));
    return 0;
}

int mu_user_list(mu_user_t *users, int max)
{
    if (!users || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < MU_MAX_USERS && count < max; i++) {
        if (mu.users[i].used) memcpy(&users[count++], &mu.users[i], sizeof(mu_user_t));
    }
    return count;
}

int mu_user_get_current(mu_user_t *user)
{
    return mu_user_get(mu.current_user, user);
}

int mu_user_set_name(int user_id, const char *name)
{
    if (user_id < 0 || user_id >= MU_MAX_USERS || !mu.users[user_id].used) return -1;
    if (name) strncpy(mu.users[user_id].name, name, 127);
    return 0;
}

int mu_user_set_permission(int user_id, const char *perm, int value)
{
    if (user_id < 0 || user_id >= MU_MAX_USERS || !mu.users[user_id].used) return -1;
    if (!perm) return -1;
    if (strcmp(perm, "install") == 0) mu.users[user_id].can_install_apps = value;
    else if (strcmp(perm, "uninstall") == 0) mu.users[user_id].can_uninstall_apps = value;
    else if (strcmp(perm, "calls") == 0) mu.users[user_id].can_make_calls = value;
    else if (strcmp(perm, "sms") == 0) mu.users[user_id].can_send_sms = value;
    else if (strcmp(perm, "camera") == 0) mu.users[user_id].can_use_camera = value;
    else if (strcmp(perm, "location") == 0) mu.users[user_id].can_use_location = value;
    else if (strcmp(perm, "storage") == 0) mu.users[user_id].can_access_storage = value;
    else if (strcmp(perm, "settings") == 0) mu.users[user_id].can_change_settings = value;
    else return -1;
    return 0;
}

int mu_user_lock(int user_id)
{
    if (user_id < 0 || user_id >= MU_MAX_USERS || !mu.users[user_id].used) return -1;
    mu.users[user_id].state = MU_USER_STATE_LOCKED;
    return 0;
}

int mu_user_unlock(int user_id)
{
    if (user_id < 0 || user_id >= MU_MAX_USERS || !mu.users[user_id].used) return -1;
    mu.users[user_id].state = MU_USER_STATE_ACTIVE;
    return 0;
}

int mu_app_clone(const char *package, int user_id)
{
    for (int i = 0; i < MU_MAX_APPS; i++) {
        if (!mu.apps[i].used) {
            memset(&mu.apps[i], 0, sizeof(mu_app_instance_t));
            mu.apps[i].used = 1;
            mu.apps[i].id = i;
            if (package) strncpy(mu.apps[i].package, package, 127);
            mu.apps[i].user_id = user_id;
            mu.apps[i].clone_id = 1;
            mu.apps[i].data_isolated = 1;
            mu.app_count++;
            mu.total_clones++;
            return i;
        }
    }
    return -1;
}

int mu_app_unclone(int clone_id)
{
    if (clone_id < 0 || clone_id >= MU_MAX_APPS || !mu.apps[clone_id].used) return -1;
    mu.apps[clone_id].used = 0;
    mu.app_count--;
    return 0;
}

int mu_app_get_instance(int clone_id, mu_app_instance_t *inst)
{
    if (clone_id < 0 || clone_id >= MU_MAX_APPS || !mu.apps[clone_id].used || !inst) return -1;
    memcpy(inst, &mu.apps[clone_id], sizeof(mu_app_instance_t));
    return 0;
}

int mu_app_list_instances(mu_app_instance_t *insts, int max)
{
    if (!insts || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < MU_MAX_APPS && count < max; i++) {
        if (mu.apps[i].used) memcpy(&insts[count++], &mu.apps[i], sizeof(mu_app_instance_t));
    }
    return count;
}

int mu_app_launch(int clone_id)
{
    if (clone_id < 0 || clone_id >= MU_MAX_APPS || !mu.apps[clone_id].used) return -1;
    mu.apps[clone_id].running = 1;
    return 0;
}

int mu_app_close(int clone_id)
{
    if (clone_id < 0 || clone_id >= MU_MAX_APPS || !mu.apps[clone_id].used) return -1;
    mu.apps[clone_id].running = 0;
    return 0;
}

int mu_private_enable(void)
{
    mu.private_config.enabled = 1;
    return mu_user_create(" private space ", MU_USER_PRIVATE);
}

int mu_private_disable(void)
{
    mu.private_config.enabled = 0;
    return 0;
}

int mu_private_get_config(mu_private_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &mu.private_config, sizeof(mu_private_config_t));
    return 0;
}

int mu_private_set_config(const mu_private_config_t *config)
{
    if (!config) return -1;
    memcpy(&mu.private_config, config, sizeof(mu_private_config_t));
    return 0;
}

int mu_private_unlock(const char *password)
{
    (void)password;
    return 0;
}

int mu_private_lock(void)
{
    return 0;
}

int mu_private_is_unlocked(int *unlocked)
{
    if (!unlocked) return -1;
    *unlocked = 1;
    return 0;
}

int mu_work_enable(void)
{
    mu.work_config.enabled = 1;
    return mu_user_create(" 工作资料 ", MU_USER_WORK);
}

int mu_work_disable(void)
{
    mu.work_config.enabled = 0;
    return 0;
}

int mu_work_get_config(mu_work_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &mu.work_config, sizeof(mu_work_config_t));
    return 0;
}

int mu_work_set_config(const mu_work_config_t *config)
{
    if (!config) return -1;
    memcpy(&mu.work_config, config, sizeof(mu_work_config_t));
    return 0;
}

int mu_guest_enter(void)
{
    int gid = mu_user_create(" 访客 ", MU_USER_GUEST);
    if (gid >= 0) mu_user_switch(gid);
    return gid;
}

int mu_guest_exit(void)
{
    return mu_user_switch(0);
}

int mu_guest_get_config(mu_guest_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &mu.guest_config, sizeof(mu_guest_config_t));
    return 0;
}

int mu_guest_set_config(const mu_guest_config_t *config)
{
    if (!config) return -1;
    memcpy(&mu.guest_config, config, sizeof(mu_guest_config_t));
    return 0;
}

int mu_kids_enter(void)
{
    int kid = mu_user_create(" 儿童 empty 间 ", MU_USER_KIDS);
    if (kid >= 0) mu_user_switch(kid);
    return kid;
}

int mu_kids_exit(void)
{
    return mu_user_switch(0);
}

int mu_kids_get_config(mu_kids_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &mu.kids_config, sizeof(mu_kids_config_t));
    return 0;
}

int mu_kids_set_config(const mu_kids_config_t *config)
{
    if (!config) return -1;
    memcpy(&mu.kids_config, config, sizeof(mu_kids_config_t));
    return 0;
}

int mu_kids_add_allowed_app(int app_id)
{
    if (mu.kids_config.allowed_app_count < 64) {
        mu.kids_config.allowed_apps[mu.kids_config.allowed_app_count++] = app_id;
        return 0;
    }
    return -1;
}

int mu_kids_remove_allowed_app(int app_id)
{
    for (int i = 0; i < mu.kids_config.allowed_app_count; i++) {
        if (mu.kids_config.allowed_apps[i] == app_id) {
            for (int j = i; j < mu.kids_config.allowed_app_count - 1; j++)
                mu.kids_config.allowed_apps[j] = mu.kids_config.allowed_apps[j + 1];
            mu.kids_config.allowed_app_count--;
            return 0;
        }
    }
    return -1;
}

int mu_user_get_used_time(int user_id, int *minutes)
{
    if (user_id < 0 || user_id >= MU_MAX_USERS || !mu.users[user_id].used || !minutes) return -1;
    *minutes = mu.users[user_id].used_time_min;
    return 0;
}

int mu_user_reset_time(int user_id)
{
    if (user_id < 0 || user_id >= MU_MAX_USERS || !mu.users[user_id].used) return -1;
    mu.users[user_id].used_time_min = 0;
    return 0;
}

int mu_user_set_time_limit(int user_id, int minutes)
{
    if (user_id < 0 || user_id >= MU_MAX_USERS || !mu.users[user_id].used) return -1;
    mu.users[user_id].time_limit_min = minutes;
    return 0;
}

int mu_get_stats(multi_user_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &mu, sizeof(multi_user_t));
    return 0;
}
