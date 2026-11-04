/* ============================================================================
 * 赫普OS - 设备热插拔 (V1.02新增)
 * 实现：热插拔事件检测、设备插入/移除通知、回调注册、总线扫描
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "hotplug.h"
#include "hal_device.h"
#include "vga.h"
#include "string.h"

#define HOTPLUG_MAX_CALLBACKS 16
#define HOTPLUG_MAX_EVENTS    32

/* 事件类型 */
#define HOTPLUG_EVT_INSERT   0
#define HOTPLUG_EVT_REMOVE   1
#define HOTPLUG_EVT_SURPRISE 2

/* 热插拔回调 */
typedef struct {
    uint8_t       used;
    hotplug_cb_t  callback;
    void         *data;
    uint8_t       bus_mask;    /* 监听的总线位图 */
} hotplug_cb_entry_t;

/* 事件队列 */
typedef struct {
    uint8_t       used;
    uint8_t       type;
    uint8_t       bus;
    char          dev_name[24];
    uint32_t      dev_id;
    uint32_t      timestamp;
} hotplug_event_t;

static hotplug_cb_entry_t hotplug_cbs[HOTPLUG_MAX_CALLBACKS];
static hotplug_event_t    hotplug_events[HOTPLUG_MAX_EVENTS];
static uint32_t hotplug_event_head = 0;
static uint32_t hotplug_event_count = 0;
static uint32_t hotplug_insert_count = 0;
static uint32_t hotplug_remove_count = 0;
static uint8_t  hotplug_initialized = 0;
static uint32_t hotplug_clock = 0;

void hotplug_init(void)
{
    memset(hotplug_cbs, 0, sizeof(hotplug_cbs));
    memset(hotplug_events, 0, sizeof(hotplug_events));
    hotplug_event_head = 0;
    hotplug_event_count = 0;
    hotplug_insert_count = 0;
    hotplug_remove_count = 0;
    hotplug_clock = 0;
    hotplug_initialized = 1;
    vga_puts("[HOTPLUG] hotplug subsystem init done \n");
}

/* 注册热插拔回调 */
int hotplug_register_callback(hotplug_cb_t cb, void *data, uint8_t bus_mask)
{
    if (!hotplug_initialized || !cb) return -1;
    for (int i = 0; i < HOTPLUG_MAX_CALLBACKS; i++) {
        if (!hotplug_cbs[i].used) {
            hotplug_cbs[i].used = 1;
            hotplug_cbs[i].callback = cb;
            hotplug_cbs[i].data = data;
            hotplug_cbs[i].bus_mask = bus_mask;
            return i;
        }
    }
    return -2;  /* 回调表满 */
}

/* 注销回调 */
int hotplug_unregister_callback(int handle)
{
    if (handle < 0 || handle >= HOTPLUG_MAX_CALLBACKS) return -1;
    if (!hotplug_cbs[handle].used) return -1;
    hotplug_cbs[handle].used = 0;
    hotplug_cbs[handle].callback = NULL;
    return 0;
}

/* 分发事件到匹配的回调 */
static void dispatch_event(hotplug_event_t *ev)
{
    for (int i = 0; i < HOTPLUG_MAX_CALLBACKS; i++) {
        if (hotplug_cbs[i].used &&
            (hotplug_cbs[i].bus_mask & (1u << ev->bus))) {
            hotplug_cbs[i].callback(ev->bus, ev->dev_name, ev->dev_id,
                                    ev->type, hotplug_cbs[i].data);
        }
    }
}

/* 设备插入事件 */
int hotplug_notify_insert(uint8_t bus, const char *name, uint32_t dev_id)
{
    if (!hotplug_initialized) return -1;
    if (hotplug_event_count >= HOTPLUG_MAX_EVENTS) return -2;  /* 队列满 */

    hotplug_event_t *ev = &hotplug_events[hotplug_event_head];
    ev->used = 1;
    ev->type = HOTPLUG_EVT_INSERT;
    ev->bus = bus;
    ev->dev_id = dev_id;
    ev->timestamp = hotplug_clock++;
    if (name) {
        strncpy(ev->dev_name, name, sizeof(ev->dev_name) - 1);
        ev->dev_name[sizeof(ev->dev_name) - 1] = 0;
    } else {
        ev->dev_name[0] = 0;
    }
    hotplug_event_head = (hotplug_event_head + 1) % HOTPLUG_MAX_EVENTS;
    hotplug_event_count++;
    hotplug_insert_count++;

    dispatch_event(ev);

    /* 同步到设备树：标记设备状态 */
    device_node_t *dev = hal_device_find(name);
    if (dev) hal_device_set_status(dev, 1);
    return 0;
}

/* 设备移除事件 */
int hotplug_notify_remove(uint8_t bus, const char *name, uint32_t dev_id)
{
    if (!hotplug_initialized) return -1;
    if (hotplug_event_count >= HOTPLUG_MAX_EVENTS) return -2;

    hotplug_event_t *ev = &hotplug_events[hotplug_event_head];
    ev->used = 1;
    ev->type = HOTPLUG_EVT_REMOVE;
    ev->bus = bus;
    ev->dev_id = dev_id;
    ev->timestamp = hotplug_clock++;
    if (name) {
        strncpy(ev->dev_name, name, sizeof(ev->dev_name) - 1);
        ev->dev_name[sizeof(ev->dev_name) - 1] = 0;
    } else {
        ev->dev_name[0] = 0;
    }
    hotplug_event_head = (hotplug_event_head + 1) % HOTPLUG_MAX_EVENTS;
    hotplug_event_count++;
    hotplug_remove_count++;

    dispatch_event(ev);

    device_node_t *dev = hal_device_find(name);
    if (dev) hal_device_set_status(dev, 0);
    return 0;
}

/* 总线扫描：探测总线上所有设备的插拔状态变化 */
int hotplug_scan_bus(uint8_t bus, const char *dev_names[], uint32_t count,
                     int *presence)
{
    if (!hotplug_initialized) return -1;
    int changes = 0;

    for (uint32_t i = 0; i < count; i++) {
        if (!dev_names[i]) continue;
        device_node_t *dev = hal_device_find(dev_names[i]);
        int present = 0;
        if (dev) {
            uint32_t status = 0;
            /* 读取设备状态 */
            if (hal_device_get_prop(dev, "status", &status) == 0) {
                present = (status != 0);
            }
        }
        /* 与期望存在状态比较，不同则触发事件 */
        if (presence[i] >= 0 && presence[i] != present) {
            if (presence[i]) {
                hotplug_notify_insert(bus, dev_names[i], i);
            } else {
                hotplug_notify_remove(bus, dev_names[i], i);
            }
            changes++;
        }
    }
    return changes;
}

/* 事件统计 */
void hotplug_stats(uint32_t *inserts, uint32_t *removes, uint32_t *pending)
{
    if (inserts) *inserts = hotplug_insert_count;
    if (removes) *removes = hotplug_remove_count;
    if (pending) *pending = hotplug_event_count;
}

/* 打印热插拔事件队列 */
void hotplug_dump_events(void)
{
    vga_printf("[HOTPLUG] events queue(%d):\n", hotplug_event_count);
    for (uint32_t i = 0; i < HOTPLUG_MAX_EVENTS; i++) {
        hotplug_event_t *ev = &hotplug_events[i];
        if (!ev->used) continue;
        vga_printf("  [%d] t=%d bus=%d id=%d %s %s\n",
                   i, ev->timestamp, ev->bus, ev->dev_id,
                   ev->type == HOTPLUG_EVT_INSERT ? "INSERT" :
                   ev->type == HOTPLUG_EVT_REMOVE ? "REMOVE" : "SURPRISE",
                   ev->dev_name);
    }
}
