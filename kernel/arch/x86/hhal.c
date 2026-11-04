/* ============================================================================
 * 赫普OS - H-HAL 强化硬件抽象层
 * ========================================================================== */

#include <stdint.h>
#include "hhal.h"
#include "vga.h"
#include "string.h"

static hhal_t hal;

void hhal_init(void)
{
    memset(&hal, 0, sizeof(hhal_t));
    hal.initialized = 1;
    hal.adapter_mode = 0;  /* 自动检测 */
    strncpy(hal.board_name, "HEPU-Generic", 63);
    strncpy(hal.chipset, "generic-x86", 63);
    strncpy(hal.soc_vendor, "HEPU", 63);
    hal.soc_id = 0x0001;

    /* 注册通用适配器 */
    hhal_adapter_register("generic-x86", ADAPTER_GENERIC, "x86-pc");
    hhal_adapter_register("modern-android", ADAPTER_NEW, "qualcomm-mediatek");
    hhal_adapter_register("legacy-bridge", ADAPTER_LEGACY, "legacy-android");

    /* 注册通用驱动 */
    hhal_driver_register("vga-display", HW_CATEGORY_DISPLAY, ADAPTER_GENERIC, 0x1234, NULL, 0);
    hhal_driver_register("ps2-keyboard", HW_CATEGORY_KEYBOARD, ADAPTER_GENERIC, 0x5678, NULL, 0);
    hhal_driver_register("ps2-mouse", HW_CATEGORY_TOUCH, ADAPTER_GENERIC, 0x9ABC, NULL, 0);
    hhal_driver_register("ide-storage", HW_CATEGORY_STORAGE, ADAPTER_GENERIC, 0xDEF0, NULL, 0);

    vga_puts("[HHAL] H-HAL enhanced HAL init done \n");
    vga_printf(" driver: %d, device: %d, adapters: %d, mode: %s\n",
               hal.driver_count, hal.device_count, hal.adapter_count,
               hal.adapter_mode == 0 ? " auto detect " : hal.adapter_mode == 1 ? " 新 device " : " compat mode ");
}

/* 驱动管理 */
int hhal_driver_register(const char *name, hhal_hw_category_t category,
                         hhal_adapter_type_t adapter, uint32_t hw_id,
                         const hhal_driver_ops_t *ops, int priority)
{
    for (int i = 0; i < HHAL_MAX_DRIVERS; i++) {
        if (!hal.drivers[i].used) {
            memset(&hal.drivers[i], 0, sizeof(hhal_driver_t));
            hal.drivers[i].used = 1;
            hal.drivers[i].id = i;
            if (name) strncpy(hal.drivers[i].name, name, 63);
            hal.drivers[i].category = category;
            hal.drivers[i].adapter_type = adapter;
            hal.drivers[i].hw_id = hw_id;
            hal.drivers[i].priority = priority;
            hal.drivers[i].state = DRV_STATE_LOADED;
            if (ops) memcpy(&hal.drivers[i].ops, ops, sizeof(hhal_driver_ops_t));
            strncpy(hal.drivers[i].version, "1.0.0", 31);
            strncpy(hal.drivers[i].author, "HEPU Community", 63);
            strncpy(hal.drivers[i].license, "GPLv3", 31);
            hal.driver_count++;
            return i;
        }
    }
    return -1;
}

int hhal_driver_unregister(int driver_id)
{
    if (driver_id < 0 || driver_id >= HHAL_MAX_DRIVERS || !hal.drivers[driver_id].used) return -1;
    hal.drivers[driver_id].used = 0;
    hal.drivers[driver_id].state = DRV_STATE_UNLOADED;
    hal.driver_count--;
    return 0;
}

int hhal_driver_get(int driver_id, hhal_driver_t *drv)
{
    if (driver_id < 0 || driver_id >= HHAL_MAX_DRIVERS || !hal.drivers[driver_id].used || !drv) return -1;
    memcpy(drv, &hal.drivers[driver_id], sizeof(hhal_driver_t));
    return 0;
}

int hhal_driver_list(hhal_driver_t *drivers, int max)
{
    if (!drivers || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HHAL_MAX_DRIVERS && count < max; i++) {
        if (hal.drivers[i].used) memcpy(&drivers[count++], &hal.drivers[i], sizeof(hhal_driver_t));
    }
    return count;
}

int hhal_driver_find_by_category(hhal_hw_category_t category, hhal_driver_t *drivers, int max)
{
    if (!drivers || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HHAL_MAX_DRIVERS && count < max; i++) {
        if (hal.drivers[i].used && hal.drivers[i].category == category)
            memcpy(&drivers[count++], &hal.drivers[i], sizeof(hhal_driver_t));
    }
    return count;
}

int hhal_driver_probe(int driver_id, void *dev)
{
    if (driver_id < 0 || driver_id >= HHAL_MAX_DRIVERS || !hal.drivers[driver_id].used) return -1;
    hal.drivers[driver_id].state = DRV_STATE_INITIALIZING;
    if (hal.drivers[driver_id].ops.probe) {
        int ret = hal.drivers[driver_id].ops.probe(dev);
        if (ret == 0) hal.drivers[driver_id].state = DRV_STATE_READY;
        else hal.drivers[driver_id].state = DRV_STATE_ERROR;
        return ret;
    }
    hal.drivers[driver_id].state = DRV_STATE_READY;
    return 0;
}

int hhal_driver_ioctl(int driver_id, uint32_t cmd, void *arg)
{
    if (driver_id < 0 || driver_id >= HHAL_MAX_DRIVERS || !hal.drivers[driver_id].used) return -1;
    hal.drivers[driver_id].call_count++;
    hal.total_driver_calls++;
    if (hal.drivers[driver_id].ops.ioctl)
        return hal.drivers[driver_id].ops.ioctl(NULL, cmd, arg);
    return 0;
}

/* 设备管理 */
int hhal_device_register(const char *name, hhal_hw_category_t category,
                         uint32_t hw_id, uint32_t reg_base, uint32_t irq)
{
    for (int i = 0; i < HHAL_MAX_DEVICES; i++) {
        if (!hal.devices[i].used) {
            memset(&hal.devices[i], 0, sizeof(hhal_device_t));
            hal.devices[i].used = 1;
            hal.devices[i].id = i;
            if (name) strncpy(hal.devices[i].name, name, 63);
            hal.devices[i].category = category;
            hal.devices[i].hw_id = hw_id;
            hal.devices[i].reg_base = reg_base;
            hal.devices[i].irq = irq;
            hal.devices[i].present = 1;
            hal.devices[i].working = 1;
            hal.device_count++;
            return i;
        }
    }
    return -1;
}

int hhal_device_get(int device_id, hhal_device_t *dev)
{
    if (device_id < 0 || device_id >= HHAL_MAX_DEVICES || !hal.devices[device_id].used || !dev) return -1;
    memcpy(dev, &hal.devices[device_id], sizeof(hhal_device_t));
    return 0;
}

int hhal_device_list(hhal_device_t *devices, int max)
{
    if (!devices || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HHAL_MAX_DEVICES && count < max; i++) {
        if (hal.devices[i].used) memcpy(&devices[count++], &hal.devices[i], sizeof(hhal_device_t));
    }
    return count;
}

int hhal_device_bind_driver(int device_id, int driver_id)
{
    if (device_id < 0 || device_id >= HHAL_MAX_DEVICES || !hal.devices[device_id].used) return -1;
    if (driver_id < 0 || driver_id >= HHAL_MAX_DRIVERS || !hal.drivers[driver_id].used) return -1;
    hal.devices[device_id].driver_id = driver_id;
    return 0;
}

/* 适配器管理 */
int hhal_adapter_register(const char *name, hhal_adapter_type_t type, const char *chipset)
{
    for (int i = 0; i < HHAL_MAX_ADAPTERS; i++) {
        if (!hal.adapters[i].used) {
            memset(&hal.adapters[i], 0, sizeof(hhal_adapter_t));
            hal.adapters[i].used = 1;
            hal.adapters[i].id = i;
            if (name) strncpy(hal.adapters[i].name, name, 63);
            hal.adapters[i].type = type;
            if (chipset) strncpy(hal.adapters[i].chipset, chipset, 63);
            hal.adapters[i].supported = 1;
            hal.adapter_count++;
            return i;
        }
    }
    return -1;
}

int hhal_adapter_get(int adapter_id, hhal_adapter_t *adapter)
{
    if (adapter_id < 0 || adapter_id >= HHAL_MAX_ADAPTERS || !hal.adapters[adapter_id].used || !adapter) return -1;
    memcpy(adapter, &hal.adapters[adapter_id], sizeof(hhal_adapter_t));
    return 0;
}

int hhal_adapter_list(hhal_adapter_t *adapters, int max)
{
    if (!adapters || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < HHAL_MAX_ADAPTERS && count < max; i++) {
        if (hal.adapters[i].used) memcpy(&adapters[count++], &hal.adapters[i], sizeof(hhal_adapter_t));
    }
    return count;
}

int hhal_adapter_detect(void)
{
    /* 自动检测硬件平台, 选择合适的适配器 */
    hal.active_adapter = 0;  /* 默认通用x86 */
    return 0;
}

int hhal_adapter_set_mode(int mode)
{
    hal.adapter_mode = mode;
    return 0;
}

/* 驱动隔离与故障恢复 */
int hhal_driver_isolate(int driver_id)
{
    if (driver_id < 0 || driver_id >= HHAL_MAX_DRIVERS || !hal.drivers[driver_id].used) return -1;
    hal.drivers[driver_id].isolated = 1;
    hal.drivers[driver_id].state = DRV_STATE_ERROR;
    return 0;
}

int hhal_driver_recover(int driver_id)
{
    if (driver_id < 0 || driver_id >= HHAL_MAX_DRIVERS || !hal.drivers[driver_id].used) return -1;
    hal.drivers[driver_id].crash_count++;
    hal.drivers[driver_id].state = DRV_STATE_READY;
    hal.drivers[driver_id].isolated = 0;
    hal.driver_crash_recoveries++;
    return 0;
}

int hhal_get_crash_count(int driver_id)
{
    if (driver_id < 0 || driver_id >= HHAL_MAX_DRIVERS || !hal.drivers[driver_id].used) return -1;
    return hal.drivers[driver_id].crash_count;
}

/* 统计 */
int hhal_get_stats(hhal_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &hal, sizeof(hhal_t));
    return 0;
}
