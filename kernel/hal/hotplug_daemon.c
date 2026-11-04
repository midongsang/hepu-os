/* ============================================================================
 * 赫普OS - 热插拔守护进程（V8.66新增）
 * 实现：定时自动轮询、设备断开清理、uevent用户态通知
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "hotplug.h"
#include "vga.h"
#include "kheap.h"

/* uevent队列大小 */
#define HOTPLUG_UEVENT_MAX 64

/* uevent事件队列 */
typedef struct {
    uint8_t  used;
    uint8_t  action;
    uint8_t  bus;
    uint16_t seqnum;
    uint32_t dev_id;
    char     dev_name[32];
    char     env[128];
} uevent_entry_t;

static uevent_entry_t uevent_queue[HOTPLUG_UEVENT_MAX];
static uint32_t uevent_head = 0;
static uint32_t uevent_tail = 0;
static uint32_t uevent_count = 0;
static uint16_t uevent_seqnum = 0;

/* 守护进程状态 */
static uint32_t poll_interval_ms = 500;  /* 默认500ms轮询一次 */
static uint32_t last_poll_tick = 0;
static uint32_t total_polls = 0;
static uint32_t total_changes = 0;
static uint8_t  daemon_initialized = 0;

/* 外部函数 */
extern int hepu_usb_hotplug_poll(void);

/* 简化的系统tick获取（实际应从定时器获取） */
static uint32_t get_system_tick(void)
{
    /* 这里使用简单的计数器，实际应读取系统定时器 */
    static uint32_t tick = 0;
    return tick++;
}

/* 入队uevent */
static void uevent_enqueue(uint8_t action, uint8_t bus, const char *name,
                           uint32_t dev_id)
{
    if (uevent_count >= HOTPLUG_UEVENT_MAX) {
        /* 队列满，丢弃最旧的 */
        uevent_tail = (uevent_tail + 1) % HOTPLUG_UEVENT_MAX;
        uevent_count--;
    }

    uevent_entry_t *ev = &uevent_queue[uevent_head];
    ev->used = 1;
    ev->action = action;
    ev->bus = bus;
    ev->seqnum = uevent_seqnum++;
    ev->dev_id = dev_id;
    if (name) {
        strncpy(ev->dev_name, name, sizeof(ev->dev_name) - 1);
        ev->dev_name[sizeof(ev->dev_name) - 1] = 0;
    } else {
        ev->dev_name[0] = 0;
    }

    /* 构建环境变量字符串 */
    const char *action_str = (action == HOTPLUG_EVT_INSERT) ? "add" :
                             (action == HOTPLUG_EVT_REMOVE) ? "remove" : "change";
    snprintf(ev->env, sizeof(ev->env),
             "ACTION=%s\0DEVPATH=/bus/%d/dev/%d\0DEVNAME=%s\0",
             action_str, bus, dev_id, name ? name : "unknown");

    uevent_head = (uevent_head + 1) % HOTPLUG_UEVENT_MAX;
    uevent_count++;
}

/* 热插拔回调：将事件转发到uevent队列 */
static void hotplug_uevent_callback(uint8_t bus, const char *name,
                                    uint32_t dev_id, uint8_t event_type,
                                    void *data)
{
    uevent_enqueue(event_type, bus, name, dev_id);
}

/* 初始化热插拔守护进程 */
void hotplug_daemon_init(void)
{
    if (daemon_initialized) return;

    memset(uevent_queue, 0, sizeof(uevent_queue));
    uevent_head = 0;
    uevent_tail = 0;
    uevent_count = 0;
    uevent_seqnum = 0;
    total_polls = 0;
    total_changes = 0;
    last_poll_tick = 0;

    /* 注册uevent转发回调 */
    hotplug_register_callback(hotplug_uevent_callback, NULL, 0xFF);

    daemon_initialized = 1;
    vga_puts("[HOTPLUG] Daemon initialized (500ms poll interval)\n");
}

/* 定时轮询（系统主循环调用） */
void hotplug_daemon_poll(void)
{
    if (!daemon_initialized) return;

    uint32_t now = get_system_tick();
    /* 简化：每次调用都执行轮询（实际应检查时间间隔） */
    if (now - last_poll_tick < poll_interval_ms / 10) {
        return;  /* 间隔未到，跳过 */
    }
    last_poll_tick = now;

    total_polls++;

    /* USB热插拔检测 */
    int changes = hepu_usb_hotplug_poll();
    if (changes > 0) {
        total_changes += changes;
    }

    /* 未来可添加：PCI热插拔、MMC/SD热插拔、SATA热插拔 */
}

/* 设置轮询间隔 */
void hotplug_daemon_set_interval(uint32_t ms)
{
    if (ms < 10) ms = 10;
    if (ms > 10000) ms = 10000;
    poll_interval_ms = ms;
}

uint32_t hotplug_daemon_get_interval(void)
{
    return poll_interval_ms;
}

/* 用户态读取uevent */
int hotplug_uevent_read(hotplug_uevent_t *out)
{
    if (!out || uevent_count == 0) return -1;

    uevent_entry_t *ev = &uevent_queue[uevent_tail];
    if (!ev->used) return -1;

    out->action = ev->action;
    out->bus = ev->bus;
    out->seqnum = ev->seqnum;
    out->dev_id = ev->dev_id;
    strncpy(out->dev_name, ev->dev_name, sizeof(out->dev_name) - 1);
    out->dev_name[sizeof(out->dev_name) - 1] = 0;
    strncpy(out->env, ev->env, sizeof(out->env) - 1);
    out->env[sizeof(out->env) - 1] = 0;

    ev->used = 0;
    uevent_tail = (uevent_tail + 1) % HOTPLUG_UEVENT_MAX;
    uevent_count--;

    return 0;
}

/* 待处理uevent数量 */
int hotplug_uevent_pending(void)
{
    return uevent_count;
}

/* 清空uevent队列 */
void hotplug_uevent_flush(void)
{
    memset(uevent_queue, 0, sizeof(uevent_queue));
    uevent_head = 0;
    uevent_tail = 0;
    uevent_count = 0;
}

/* 守护进程统计 */
void hotplug_daemon_stats(uint32_t *polls, uint32_t *changes,
                          uint32_t *uevents)
{
    if (polls) *polls = total_polls;
    if (changes) *changes = total_changes;
    if (uevents) *uevents = uevent_count;
}
