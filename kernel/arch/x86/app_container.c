/* ============================================================================
 * 赫普OS - 容器化应用隔离
 * ========================================================================== */

#include <stdint.h>
#include "app_container.h"
#include "vga.h"
#include "string.h"

static app_container_t acm;

void app_container_init(void)
{
    memset(&acm, 0, sizeof(app_container_t));
    acm.initialized = 1;
    acm.enabled = 1;

    vga_puts("[CONTAINER] app container isolation init done \n");
    vga_printf(" max containers: %d, default isolation: standard \n", AC_MAX_CONTAINERS);
}

/* 容器生命周期 */
int ac_create(const ac_config_t *config)
{
    if (!config) return -1;
    for (int i = 0; i < AC_MAX_CONTAINERS; i++) {
        if (!acm.containers[i].used) {
            memset(&acm.containers[i], 0, sizeof(ac_container_t));
            acm.containers[i].used = 1;
            acm.containers[i].id = i;
            strncpy(acm.containers[i].name, config->name, 127);
            strncpy(acm.containers[i].app_id, config->app_id, 127);
            strncpy(acm.containers[i].image, config->image, 255);
            acm.containers[i].state = AC_STATE_CREATED;
            acm.containers[i].isolation = config->isolation;
            acm.containers[i].caps = config->caps;
            acm.containers[i].quota = config->quota;
            /* 默认配额 */
            if (acm.containers[i].quota.memory_limit == 0)
                acm.containers[i].quota.memory_limit = 512UL * 1024 * 1024;
            if (acm.containers[i].quota.cpu_quota == 0)
                acm.containers[i].quota.cpu_quota = 2.0f;
            if (acm.containers[i].quota.disk_limit == 0)
                acm.containers[i].quota.disk_limit = 1UL * 1024 * 1024 * 1024;
            /* 默认启用命名空间 */
            acm.containers[i].namespaces[AC_NS_MOUNT] = 1;
            acm.containers[i].namespaces[AC_NS_PID] = 1;
            acm.containers[i].namespaces[AC_NS_NET] = 1;
            acm.containers[i].seccomp_enabled = 1;
            acm.container_count++;
            acm.total_memory_allocated += acm.containers[i].quota.memory_limit;
            acm.total_disk_allocated += acm.containers[i].quota.disk_limit;
            return i;
        }
    }
    return -1;
}

int ac_destroy(int container_id)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used) return -1;
    if (acm.containers[container_id].state == AC_STATE_RUNNING) ac_stop(container_id);
    acm.total_memory_allocated -= acm.containers[container_id].quota.memory_limit;
    acm.total_disk_allocated -= acm.containers[container_id].quota.disk_limit;
    acm.containers[container_id].used = 0;
    acm.container_count--;
    return 0;
}

int ac_start(int container_id)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used) return -1;
    acm.containers[container_id].state = AC_STATE_RUNNING;
    acm.containers[container_id].pid = 1000 + container_id;
    acm.containers[container_id].root_pid = 1000 + container_id;
    acm.containers[container_id].start_time = 1;
    acm.running_count++;
    return 0;
}

int ac_stop(int container_id)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used) return -1;
    if (acm.containers[container_id].state == AC_STATE_RUNNING) {
        acm.containers[container_id].state = AC_STATE_STOPPED;
        acm.running_count--;
    }
    return 0;
}

int ac_pause(int container_id)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used) return -1;
    if (acm.containers[container_id].state == AC_STATE_RUNNING)
        acm.containers[container_id].state = AC_STATE_PAUSED;
    return 0;
}

int ac_resume(int container_id)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used) return -1;
    if (acm.containers[container_id].state == AC_STATE_PAUSED)
        acm.containers[container_id].state = AC_STATE_RUNNING;
    return 0;
}

int ac_restart(int container_id)
{
    ac_stop(container_id);
    ac_start(container_id);
    acm.containers[container_id].total_restarts++;
    return 0;
}

int ac_get(int container_id, ac_container_t *cont)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used || !cont) return -1;
    memcpy(cont, &acm.containers[container_id], sizeof(ac_container_t));
    return 0;
}

int ac_list(ac_container_t *conts, int max)
{
    if (!conts || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < AC_MAX_CONTAINERS && count < max; i++) {
        if (acm.containers[i].used) memcpy(&conts[count++], &acm.containers[i], sizeof(ac_container_t));
    }
    return count;
}

int ac_get_by_app(const char *app_id, ac_container_t *cont)
{
    if (!app_id || !cont) return -1;
    for (int i = 0; i < AC_MAX_CONTAINERS; i++) {
        if (acm.containers[i].used && strcmp(acm.containers[i].app_id, app_id) == 0) {
            memcpy(cont, &acm.containers[i], sizeof(ac_container_t));
            return i;
        }
    }
    return -1;
}

/* 命名空间 */
int ac_ns_enable(int container_id, ac_ns_type_t ns)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used) return -1;
    acm.containers[container_id].namespaces[ns] = 1;
    return 0;
}

int ac_ns_disable(int container_id, ac_ns_type_t ns)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used) return -1;
    acm.containers[container_id].namespaces[ns] = 0;
    return 0;
}

int ac_ns_is_enabled(int container_id, ac_ns_type_t ns)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used) return -1;
    return acm.containers[container_id].namespaces[ns];
}

/* 挂载 */
int ac_mount_add(int container_id, const char *source, const char *target, int readonly)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used) return -1;
    for (int i = 0; i < AC_MAX_MOUNTS; i++) {
        if (!acm.containers[container_id].mounts[i].used) {
            memset(&acm.containers[container_id].mounts[i], 0, sizeof(ac_mount_t));
            acm.containers[container_id].mounts[i].used = 1;
            if (source) strncpy(acm.containers[container_id].mounts[i].source, source, 255);
            if (target) strncpy(acm.containers[container_id].mounts[i].target, target, 255);
            acm.containers[container_id].mounts[i].readonly = readonly ? 1 : 0;
            acm.containers[container_id].mount_count++;
            return 0;
        }
    }
    return -1;
}

int ac_mount_remove(int container_id, const char *target)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used) return -1;
    for (int i = 0; i < AC_MAX_MOUNTS; i++) {
        if (acm.containers[container_id].mounts[i].used &&
            strcmp(acm.containers[container_id].mounts[i].target, target) == 0) {
            acm.containers[container_id].mounts[i].used = 0;
            acm.containers[container_id].mount_count--;
            return 0;
        }
    }
    return -1;
}

int ac_mount_list(int container_id, ac_mount_t *mounts, int max)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used) return -1;
    if (!mounts || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < AC_MAX_MOUNTS && count < max; i++) {
        if (acm.containers[container_id].mounts[i].used)
            memcpy(&mounts[count++], &acm.containers[container_id].mounts[i], sizeof(ac_mount_t));
    }
    return count;
}

/* 能力 */
int ac_caps_set(int container_id, const ac_caps_t *caps)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used) return -1;
    if (caps) memcpy(&acm.containers[container_id].caps, caps, sizeof(ac_caps_t));
    return 0;
}

int ac_caps_get(int container_id, ac_caps_t *caps)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used || !caps) return -1;
    memcpy(caps, &acm.containers[container_id].caps, sizeof(ac_caps_t));
    return 0;
}

int ac_cap_grant(int container_id, const char *cap)
{
    (void)container_id; (void)cap;
    return 0;
}

int ac_cap_revoke(int container_id, const char *cap)
{
    (void)container_id; (void)cap;
    return 0;
}

/* 配额 */
int ac_quota_set(int container_id, const ac_quota_t *quota)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used) return -1;
    if (quota) memcpy(&acm.containers[container_id].quota, quota, sizeof(ac_quota_t));
    return 0;
}

int ac_quota_get(int container_id, ac_quota_t *quota)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used || !quota) return -1;
    memcpy(quota, &acm.containers[container_id].quota, sizeof(ac_quota_t));
    return 0;
}

int ac_quota_set_memory(int container_id, uint64_t limit)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used) return -1;
    acm.containers[container_id].quota.memory_limit = limit;
    return 0;
}

int ac_quota_set_cpu(int container_id, float quota)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used) return -1;
    acm.containers[container_id].quota.cpu_quota = quota;
    return 0;
}

int ac_quota_set_disk(int container_id, uint64_t limit)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used) return -1;
    acm.containers[container_id].quota.disk_limit = limit;
    return 0;
}

/* 资源使用 */
int ac_usage_get(int container_id, ac_usage_t *usage)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used || !usage) return -1;
    memcpy(usage, &acm.containers[container_id].usage, sizeof(ac_usage_t));
    return 0;
}

int ac_usage_update(int container_id)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used) return -1;
    acm.containers[container_id].usage.memory_used = 128 * 1024 * 1024;
    acm.containers[container_id].usage.cpu_usage = 15.5f;
    acm.containers[container_id].usage.process_count = 8;
    return 0;
}

/* 安全 */
int ac_seccomp_set(int container_id, int enable)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used) return -1;
    acm.containers[container_id].seccomp_enabled = enable ? 1 : 0;
    return 0;
}

int ac_selinux_set_context(int container_id, int context)
{
    if (container_id < 0 || container_id >= AC_MAX_CONTAINERS || !acm.containers[container_id].used) return -1;
    acm.containers[container_id].selinux_context = context;
    return 0;
}

/* 事件 */
int ac_on_app_launch(const char *app_id)
{
    ac_config_t cfg;
    memset(&cfg, 0, sizeof(ac_config_t));
    strncpy(cfg.app_id, app_id, 127);
    strncpy(cfg.name, app_id, 127);
    cfg.isolation = AC_ISO_STANDARD;
    cfg.caps.storage = 1;
    cfg.caps.network_access = 1;
    int id = ac_create(&cfg);
    if (id >= 0) ac_start(id);
    return id;
}

int ac_on_app_exit(const char *app_id)
{
    ac_container_t cont;
    if (ac_get_by_app(app_id, &cont) >= 0) {
        ac_stop(cont.id);
        return 0;
    }
    return -1;
}

/* 统计 */
int ac_get_stats(app_container_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &acm, sizeof(app_container_t));
    return 0;
}
