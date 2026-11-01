/* ============================================================================
 * 赫普OS - 功耗/续航优化
 * ========================================================================== */

#include <stdint.h>
#include "power_optimizer.h"
#include "vga.h"
#include "string.h"

static power_optimizer_t po;

void power_optimizer_init(void)
{
    memset(&po, 0, sizeof(power_optimizer_t));
    po.initialized = 1;
    po.enabled = 1;
    po.state = PO_STATE_ACTIVE;
    po.config.governor = PO_CPU_BALANCED;
    po.config.adaptive_brightness = 1;
    po.config.auto_screen_off = 30;
    po.config.background_limit = 1;
    po.config.wake_lock_control = 1;
    po.config.app_standby = 1;
    po.config.battery_saver_trigger = 20;
    /* 电池 */
    po.battery.present = 1;
    po.battery.level = 85;
    po.battery.health = 95;
    po.battery.status = 2;  /* 放电 */
    po.battery.plugged = 0;
    po.battery.technology = 1;
    po.battery.capacity_mah = 5000;
    po.battery.full_capacity_mah = 5000;
    po.battery.temperature = 320;  /* 32.0C */
    po.battery.voltage = 3850;
    po.battery.estimated_time_min = 720;

    vga_puts("[POWER] power optimizer init done \n");
    vga_printf(" battery: %d%%, sched: balanced, power-save trigger: %d%%\n",
               po.battery.level, po.config.battery_saver_trigger);
}

int po_battery_get(po_battery_t *battery)
{
    if (!battery) return -1;
    memcpy(battery, &po.battery, sizeof(po_battery_t));
    return 0;
}

int po_battery_update(int level, int status, int plugged, int temp, int voltage)
{
    po.battery.level = level;
    po.battery.status = status;
    po.battery.plugged = plugged;
    po.battery.temperature = temp;
    po.battery.voltage = voltage;
    /* 低电量自动省电 */
    if (level <= po.config.battery_saver_trigger && !po.config.power_save_mode)
        po.config.power_save_mode = 1;
    return 0;
}

int po_battery_get_time_remaining(int *minutes)
{
    if (!minutes) return -1;
    *minutes = po.battery.estimated_time_min;
    return 0;
}

int po_set_state(po_power_state_t state)
{
    po.state = state;
    return 0;
}

int po_get_state(po_power_state_t *state)
{
    if (!state) return -1;
    *state = po.state;
    return 0;
}

int po_suspend(void)
{
    po.state = PO_STATE_SUSPEND;
    /* 释放非必要唤醒锁 */
    for (int i = 0; i < PO_MAX_WAKELOCKS; i++) {
        if (po.wakelocks[i].used && po.wakelocks[i].type != PO_WAKE_NETWORK)
            po.wakelocks[i].held = 0;
    }
    return 0;
}

int po_resume(void)
{
    po.state = PO_STATE_ACTIVE;
    return 0;
}

int po_deep_sleep(void)
{
    po.state = PO_STATE_DEEP_SLEEP;
    /* 释放所有唤醒锁 */
    for (int i = 0; i < PO_MAX_WAKELOCKS; i++) {
        if (po.wakelocks[i].used) po.wakelocks[i].held = 0;
    }
    return 0;
}

int po_set_governor(po_cpu_governor_t governor)
{
    po.config.governor = governor;
    return 0;
}

int po_get_governor(po_cpu_governor_t *governor)
{
    if (!governor) return -1;
    *governor = po.config.governor;
    return 0;
}

int po_set_cpu_freq(int cpu, uint32_t freq_khz)
{
    (void)cpu; (void)freq_khz;
    return 0;
}

int po_get_cpu_freq(int cpu, uint32_t *freq_khz)
{
    (void)cpu;
    if (!freq_khz) return -1;
    if (po.config.governor == PO_CPU_PERFORMANCE) *freq_khz = 3200000;
    else if (po.config.governor == PO_CPU_POWERSAVE) *freq_khz = 1200000;
    else *freq_khz = 2400000;
    return 0;
}

int po_app_register(const char *name, const char *package)
{
    for (int i = 0; i < PO_MAX_APPS; i++) {
        if (!po.apps[i].used) {
            memset(&po.apps[i], 0, sizeof(po_app_power_t));
            po.apps[i].used = 1;
            po.apps[i].id = i;
            if (name) strncpy(po.apps[i].app_name, name, 127);
            if (package) strncpy(po.apps[i].package, package, 127);
            po.app_count++;
            return i;
        }
    }
    return -1;
}

int po_app_unregister(int app_id)
{
    if (app_id < 0 || app_id >= PO_MAX_APPS || !po.apps[app_id].used) return -1;
    po.apps[app_id].used = 0;
    po.app_count--;
    return 0;
}

int po_app_update_cpu(int app_id, uint64_t time_ms)
{
    if (app_id < 0 || app_id >= PO_MAX_APPS || !po.apps[app_id].used) return -1;
    po.apps[app_id].cpu_time_ms += time_ms;
    return 0;
}

int po_app_update_network(int app_id, uint64_t rx, uint64_t tx)
{
    if (app_id < 0 || app_id >= PO_MAX_APPS || !po.apps[app_id].used) return -1;
    po.apps[app_id].network_rx_bytes += rx;
    po.apps[app_id].network_tx_bytes += tx;
    return 0;
}

int po_app_update_sensor(int app_id, uint64_t time_ms)
{
    if (app_id < 0 || app_id >= PO_MAX_APPS || !po.apps[app_id].used) return -1;
    po.apps[app_id].sensor_time_ms += time_ms;
    return 0;
}

int po_app_get(int app_id, po_app_power_t *app)
{
    if (app_id < 0 || app_id >= PO_MAX_APPS || !po.apps[app_id].used || !app) return -1;
    memcpy(app, &po.apps[app_id], sizeof(po_app_power_t));
    return 0;
}

int po_app_list(po_app_power_t *apps, int max)
{
    if (!apps || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < PO_MAX_APPS && count < max; i++) {
        if (po.apps[i].used) memcpy(&apps[count++], &po.apps[i], sizeof(po_app_power_t));
    }
    return count;
}

int po_app_get_top(po_app_power_t *apps, int max)
{
    return po_app_list(apps, max);
}

int po_app_calculate_energy(int app_id, float *energy_mah)
{
    if (app_id < 0 || app_id >= PO_MAX_APPS || !po.apps[app_id].used || !energy_mah) return -1;
    /* 简化估算：CPU时间 + 网络 + 传感器 */
    float cpu_energy = (float)((uint32_t)po.apps[app_id].cpu_time_ms / 3600000u) * 200.0f;
    float net_energy = (float)((uint32_t)(po.apps[app_id].network_rx_bytes +
                        po.apps[app_id].network_tx_bytes) / 1048576u) * 5.0f;
    po.apps[app_id].estimated_energy_mah = cpu_energy + net_energy;
    *energy_mah = po.apps[app_id].estimated_energy_mah;
    return 0;
}

int po_wakelock_acquire(const char *name, po_wakelock_type_t type, int app_id)
{
    for (int i = 0; i < PO_MAX_WAKELOCKS; i++) {
        if (!po.wakelocks[i].used) {
            memset(&po.wakelocks[i], 0, sizeof(po_wakelock_t));
            po.wakelocks[i].used = 1;
            po.wakelocks[i].id = i;
            if (name) strncpy(po.wakelocks[i].name, name, 127);
            po.wakelocks[i].type = type;
            po.wakelocks[i].app_id = app_id;
            po.wakelocks[i].held = 1;
            po.wakelocks[i].acquire_time = 1;
            po.wakelocks[i].acquire_count = 1;
            po.wakelock_count++;
            if (app_id >= 0 && app_id < PO_MAX_APPS && po.apps[app_id].used)
                po.apps[app_id].wakelock_count++;
            return i;
        }
    }
    return -1;
}

int po_wakelock_release(int wakelock_id)
{
    if (wakelock_id < 0 || wakelock_id >= PO_MAX_WAKELOCKS || !po.wakelocks[wakelock_id].used) return -1;
    po.wakelocks[wakelock_id].held = 0;
    return 0;
}

int po_wakelock_get(int id, po_wakelock_t *wl)
{
    if (id < 0 || id >= PO_MAX_WAKELOCKS || !po.wakelocks[id].used || !wl) return -1;
    memcpy(wl, &po.wakelocks[id], sizeof(po_wakelock_t));
    return 0;
}

int po_wakelock_list(po_wakelock_t *wls, int max)
{
    if (!wls || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < PO_MAX_WAKELOCKS && count < max; i++) {
        if (po.wakelocks[i].used) memcpy(&wls[count++], &po.wakelocks[i], sizeof(po_wakelock_t));
    }
    return count;
}

int po_wakelock_force_release(int app_id)
{
    int released = 0;
    for (int i = 0; i < PO_MAX_WAKELOCKS; i++) {
        if (po.wakelocks[i].used && po.wakelocks[i].app_id == app_id && po.wakelocks[i].held) {
            po.wakelocks[i].held = 0;
            released++;
        }
    }
    return released;
}

int po_thermal_update(int cpu, int gpu, int battery, int skin)
{
    po.thermal.cpu_temp = cpu;
    po.thermal.gpu_temp = gpu;
    po.thermal.battery_temp = battery;
    po.thermal.skin_temp = skin;
    /* 温控等级 */
    if (skin > 450) { po.thermal.thermal_level = 3; po.thermal.throttling = 50; }
    else if (skin > 400) { po.thermal.thermal_level = 2; po.thermal.throttling = 30; }
    else if (skin > 350) { po.thermal.thermal_level = 1; po.thermal.throttling = 10; }
    else { po.thermal.thermal_level = 0; po.thermal.throttling = 0; }
    return 0;
}

int po_thermal_get(po_thermal_t *thermal)
{
    if (!thermal) return -1;
    memcpy(thermal, &po.thermal, sizeof(po_thermal_t));
    return 0;
}

int po_thermal_throttle(int level)
{
    po.thermal.throttling = level;
    return 0;
}

int po_set_config(const po_config_t *config)
{
    if (!config) return -1;
    memcpy(&po.config, config, sizeof(po_config_t));
    return 0;
}

int po_get_config(po_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &po.config, sizeof(po_config_t));
    return 0;
}

int po_set_power_save(int enable)
{
    po.config.power_save_mode = enable ? 1 : 0;
    if (enable) po.config.governor = PO_CPU_POWERSAVE;
    return 0;
}

int po_set_ultra_power_save(int enable)
{
    po.config.ultra_power_save = enable ? 1 : 0;
    if (enable) {
        po.config.governor = PO_CPU_POWERSAVE;
        po.config.background_limit = 1;
        /* 释放所有非系统唤醒锁 */
        for (int i = 0; i < PO_MAX_WAKELOCKS; i++) {
            if (po.wakelocks[i].used && po.wakelocks[i].app_id > 0)
                po.wakelocks[i].held = 0;
        }
    }
    return 0;
}

int po_get_stats(power_optimizer_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &po, sizeof(power_optimizer_t));
    return 0;
}

int po_get_remaining_battery_life(int *minutes)
{
    if (!minutes) return -1;
    *minutes = po.battery.level * 10;  /* 简化估算 */
    return 0;
}
