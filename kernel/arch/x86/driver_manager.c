/* ============================================================================
 * 赫普OS - 驱动管理器
 * 统一驱动生命周期管理：注册/探测/初始化/挂起/恢复/关闭
 * ========================================================================== */

#include <stdint.h>
#include "driver_manager.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"

#define MAX_DRIVERS 64

typedef struct {
    int used;
    dm_state_t state;
    dm_desc_t desc;
    void *dev;
} driver_entry_t;

static driver_entry_t drivers[MAX_DRIVERS];
static int driver_count = 0;
static int initialized = 0;

void driver_manager_init(void)
{
    memset(drivers, 0, sizeof(drivers));
    driver_count = 0;
    initialized = 1;
    vga_puts("[DRVMGR] driver manager init done(max 64 driver)\n");
}

int driver_register(dm_desc_t *desc)
{
    if (!desc || !initialized) return -1;
    for (int i = 0; i < MAX_DRIVERS; i++) {
        if (!drivers[i].used) {
            drivers[i].used = 1;
            drivers[i].state = DM_STATE_LOADED;
            memcpy(&drivers[i].desc, desc, sizeof(dm_desc_t));
            drivers[i].dev = NULL;
            driver_count++;
            vga_printf("[DRVMGR] register driver: %s v%s\n", desc->name, desc->version);
            return i;
        }
    }
    return -1;
}

int driver_unregister(int driver_id)
{
    if (driver_id < 0 || driver_id >= MAX_DRIVERS || !drivers[driver_id].used)
        return -1;
    if (drivers[driver_id].state == DM_STATE_READY &&
        drivers[driver_id].desc.ops && drivers[driver_id].desc.ops->shutdown) {
        drivers[driver_id].desc.ops->shutdown(drivers[driver_id].dev);
    }
    drivers[driver_id].used = 0;
    drivers[driver_id].state = DM_STATE_UNLOADED;
    driver_count--;
    return 0;
}

int driver_probe(int driver_id, void *dev)
{
    if (driver_id < 0 || driver_id >= MAX_DRIVERS || !drivers[driver_id].used)
        return -1;
    driver_entry_t *e = &drivers[driver_id];
    if (!e->desc.ops || !e->desc.ops->probe) return -1;
    e->state = DM_STATE_PROBING;
    e->dev = dev;
    int ret = e->desc.ops->probe(dev);
    e->state = ret == 0 ? DM_STATE_LOADED : DM_STATE_ERROR;
    return ret;
}

int driver_init(int driver_id, void *dev)
{
    if (driver_id < 0 || driver_id >= MAX_DRIVERS || !drivers[driver_id].used)
        return -1;
    driver_entry_t *e = &drivers[driver_id];
    if (!e->desc.ops || !e->desc.ops->init) return -1;
    e->dev = dev;
    int ret = e->desc.ops->init(dev);
    e->state = ret == 0 ? DM_STATE_READY : DM_STATE_ERROR;
    return ret;
}

int driver_suspend(int driver_id)
{
    if (driver_id < 0 || driver_id >= MAX_DRIVERS || !drivers[driver_id].used)
        return -1;
    driver_entry_t *e = &drivers[driver_id];
    if (e->state != DM_STATE_READY) return -1;
    if (e->desc.ops && e->desc.ops->suspend) {
        int ret = e->desc.ops->suspend(e->dev);
        if (ret == 0) e->state = DM_STATE_SUSPENDED;
        return ret;
    }
    e->state = DM_STATE_SUSPENDED;
    return 0;
}

int driver_resume(int driver_id)
{
    if (driver_id < 0 || driver_id >= MAX_DRIVERS || !drivers[driver_id].used)
        return -1;
    driver_entry_t *e = &drivers[driver_id];
    if (e->state != DM_STATE_SUSPENDED) return -1;
    if (e->desc.ops && e->desc.ops->resume) {
        int ret = e->desc.ops->resume(e->dev);
        if (ret == 0) e->state = DM_STATE_READY;
        return ret;
    }
    e->state = DM_STATE_READY;
    return 0;
}

int driver_shutdown(int driver_id)
{
    if (driver_id < 0 || driver_id >= MAX_DRIVERS || !drivers[driver_id].used)
        return -1;
    driver_entry_t *e = &drivers[driver_id];
    if (e->desc.ops && e->desc.ops->shutdown) {
        e->desc.ops->shutdown(e->dev);
    }
    e->state = DM_STATE_UNLOADED;
    return 0;
}

int driver_ioctl(int driver_id, uint32_t cmd, void *arg)
{
    if (driver_id < 0 || driver_id >= MAX_DRIVERS || !drivers[driver_id].used)
        return -1;
    driver_entry_t *e = &drivers[driver_id];
    if (e->state != DM_STATE_READY) return -1;
    if (e->desc.ops && e->desc.ops->ioctl) {
        return e->desc.ops->ioctl(e->dev, cmd, arg);
    }
    return -1;
}

int driver_get_state(int driver_id)
{
    if (driver_id < 0 || driver_id >= MAX_DRIVERS || !drivers[driver_id].used)
        return -1;
    return drivers[driver_id].state;
}

int driver_list(dm_desc_t *descs, int max)
{
    int count = 0;
    for (int i = 0; i < MAX_DRIVERS && count < max; i++) {
        if (drivers[i].used) {
            memcpy(&descs[count], &drivers[i].desc, sizeof(dm_desc_t));
            count++;
        }
    }
    return count;
}

int driver_find_by_name(const char *name)
{
    for (int i = 0; i < MAX_DRIVERS; i++) {
        if (drivers[i].used && strcmp(drivers[i].desc.name, name) == 0)
            return i;
    }
    return -1;
}

int driver_find_by_category(uint32_t category, dm_desc_t *descs, int max)
{
    int count = 0;
    for (int i = 0; i < MAX_DRIVERS && count < max; i++) {
        if (drivers[i].used && drivers[i].desc.category == category) {
            memcpy(&descs[count], &drivers[i].desc, sizeof(dm_desc_t));
            count++;
        }
    }
    return count;
}

int driver_suspend_all(void)
{
    int count = 0;
    for (int i = 0; i < MAX_DRIVERS; i++) {
        if (drivers[i].used && drivers[i].state == DM_STATE_READY) {
            if (driver_suspend(i) == 0) count++;
        }
    }
    vga_printf("[DRVMGR] suspend %d driver \n", count);
    return count;
}

int driver_resume_all(void)
{
    int count = 0;
    for (int i = 0; i < MAX_DRIVERS; i++) {
        if (drivers[i].used && drivers[i].state == DM_STATE_SUSPENDED) {
            if (driver_resume(i) == 0) count++;
        }
    }
    vga_printf("[DRVMGR] resume %d driver \n", count);
    return count;
}

int driver_shutdown_all(void)
{
    int count = 0;
    for (int i = 0; i < MAX_DRIVERS; i++) {
        if (drivers[i].used && drivers[i].state == DM_STATE_READY) {
            driver_shutdown(i);
            count++;
        }
    }
    vga_printf("[DRVMGR] close %d driver \n", count);
    return count;
}

int driver_get_count(void) { return driver_count; }

int driver_get_ready_count(void)
{
    int count = 0;
    for (int i = 0; i < MAX_DRIVERS; i++) {
        if (drivers[i].used && drivers[i].state == DM_STATE_READY) count++;
    }
    return count;
}
