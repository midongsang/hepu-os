#ifndef HOTPLUG_H
#define HOTPLUG_H

#include <stdint.h>
#include "hal_device.h"

/* 总线类型 */
#define HOTPLUG_BUS_PCI     0
#define HOTPLUG_BUS_USB     1
#define HOTPLUG_BUS_MMC     2
#define HOTPLUG_BUS_SDIO    3
#define HOTPLUG_BUS_SATA    4
#define HOTPLUG_BUS_NVME    5
#define HOTPLUG_BUS_MAX     8

/* 事件类型 */
#define HOTPLUG_EVT_INSERT   0
#define HOTPLUG_EVT_REMOVE   1
#define HOTPLUG_EVT_SURPRISE 2

/* 热插拔回调：设备插入/移除通知 */
typedef void (*hotplug_cb_t)(uint8_t bus, const char *name, uint32_t dev_id,
                             uint8_t event_type, void *data);

/* 初始化 */
void hotplug_init(void);

/* 注册/注销回调 */
int hotplug_register_callback(hotplug_cb_t cb, void *data, uint8_t bus_mask);
int hotplug_unregister_callback(int handle);

/* 设备插入/移除事件 */
int hotplug_notify_insert(uint8_t bus, const char *name, uint32_t dev_id);
int hotplug_notify_remove(uint8_t bus, const char *name, uint32_t dev_id);

/* 总线扫描：presence[i]=1期望存在，0期望移除，-1跳过 */
int hotplug_scan_bus(uint8_t bus, const char *dev_names[], uint32_t count,
                     int *presence);

/* 统计/调试 */
void hotplug_stats(uint32_t *inserts, uint32_t *removes, uint32_t *pending);
void hotplug_dump_events(void);

/* 热插拔守护进程（V8.66新增） */
void hotplug_daemon_init(void);
void hotplug_daemon_poll(void);  /* 系统主循环调用，定时检测 */
void hotplug_daemon_set_interval(uint32_t ms);
uint32_t hotplug_daemon_get_interval(void);

/* uevent用户态通知（V8.66新增） */
typedef struct {
    uint8_t  action;      /* HOTPLUG_EVT_* */
    uint8_t  bus;
    uint16_t seqnum;
    uint32_t dev_id;
    char     dev_name[32];
    char     env[128];    /* 环境变量字符串，如 "ACTION=add\0DEVPATH=/usb/..." */
} hotplug_uevent_t;

int hotplug_uevent_read(hotplug_uevent_t *out);  /* 用户态读取下一个事件 */
int hotplug_uevent_pending(void);
void hotplug_uevent_flush(void);

#endif
