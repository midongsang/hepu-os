/* ============================================================================
 * 赫普OS - 通知系统
 * NotificationManager + 通知渠道 + 锁屏通知 + 监听服务
 * ========================================================================== */

#include <stdint.h>
#include "notification.h"
#include "vga.h"
#include "string.h"

static notification_t notifications[NOTIF_MAX_NOTIFICATIONS];
static notif_channel_t channels[NOTIF_MAX_CHANNELS];
static notif_listener_t listeners[NOTIF_MAX_LISTENERS];
static int notif_total = 0;
static int channel_count = 0;
static int listener_count = 0;
static int dnd_enabled = 0;
static int notif_initialized = 0;

void notification_init(void)
{
    /* V0.10.5-BIOS: 极简版本，避免在BIOS环境下卡住 */
    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[NOTIF] init (minimal BIOS version)\n");
    for (int i = 0; i < NOTIF_MAX_NOTIFICATIONS; i++) notifications[i].used = 0;
    for (int i = 0; i < NOTIF_MAX_CHANNELS; i++) channels[i].used = 0;
    for (int i = 0; i < NOTIF_MAX_LISTENERS; i++) listeners[i].used = 0;
    notif_total = channel_count = listener_count = 0;
    dnd_enabled = 0;
    notif_initialized = 1;
    serial_puts(0x3F8, "[NOTIF] init done\n");
}

int notif_create_channel(notif_channel_t *channel)
{
    if (!channel || !notif_initialized) return -1;
    for (int i = 0; i < NOTIF_MAX_CHANNELS; i++) {
        if (!channels[i].used) {
            memcpy(&channels[i], channel, sizeof(notif_channel_t));
            channels[i].used = 1;
            channels[i].id = i;
            channel_count++;
            return i;
        }
    }
    return -1;
}

int notif_delete_channel(const char *channel_id)
{
    if (!channel_id || !notif_initialized) return -1;
    for (int i = 0; i < NOTIF_MAX_CHANNELS; i++) {
        if (channels[i].used && strcmp(channels[i].name, channel_id) == 0) {
            if (channels[i].locked) return -1;
            channels[i].used = 0;
            channel_count--;
            return 0;
        }
    }
    return -1;
}

int notif_get_channel(const char *channel_id, notif_channel_t *channel)
{
    if (!channel_id || !channel) return -1;
    for (int i = 0; i < NOTIF_MAX_CHANNELS; i++) {
        if (channels[i].used && strcmp(channels[i].name, channel_id) == 0) {
            memcpy(channel, &channels[i], sizeof(notif_channel_t));
            return 0;
        }
    }
    return -1;
}

int notif_list_channels(notif_channel_t *ch_list, int max)
{
    if (!ch_list || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < NOTIF_MAX_CHANNELS && count < max; i++) {
        if (channels[i].used) memcpy(&ch_list[count++], &channels[i], sizeof(notif_channel_t));
    }
    return count;
}

int notif_post(const char *package, const char *channel_id, notification_t *notif)
{
    if (!package || !notif || !notif_initialized) return -1;

    /* 勿扰模式检查 */
    if (dnd_enabled) {
        /* 检查渠道是否绕过DND */
        int bypass = 0;
        for (int i = 0; i < NOTIF_MAX_CHANNELS; i++) {
            if (channels[i].used && strcmp(channels[i].name, channel_id) == 0) {
                bypass = channels[i].bypass_dnd;
                break;
            }
        }
        if (!bypass && notif->category != NOTIF_CATEGORY_CALL &&
            notif->category != NOTIF_CATEGORY_ALARM) {
            vga_printf("[NOTIF] DND blocked: %s\n", package);
            return -1;
        }
    }

    for (int i = 0; i < NOTIF_MAX_NOTIFICATIONS; i++) {
        if (!notifications[i].used) {
            memcpy(&notifications[i], notif, sizeof(notification_t));
            notifications[i].used = 1;
            notifications[i].id = i;
            strncpy(notifications[i].package_name, package, 63);
            strncpy(notifications[i].channel_id, channel_id ? channel_id : "Default", 63);
            notifications[i].post_time = 0;
            notifications[i].read = 0;
            notifications[i].shown = 1;
            notif_total++;

            vga_printf("[NOTIF] publish notify #%d: %s - %s\n",
                       i, notif->title, package);

            /* 通知监听器 */
            for (int j = 0; j < NOTIF_MAX_LISTENERS; j++) {
                if (listeners[j].used && listeners[j].on_notification_posted) {
                    listeners[j].on_notification_posted(&notifications[i]);
                }
            }
            return i;
        }
    }
    return -1;
}

int notif_cancel(const char *package, int notif_id)
{
    if (notif_id < 0 || notif_id >= NOTIF_MAX_NOTIFICATIONS) return -1;
    if (!notifications[notif_id].used) return -1;
    if (package && strcmp(notifications[notif_id].package_name, package) != 0) return -1;

    notification_t copy = notifications[notif_id];
    notifications[notif_id].used = 0;
    notif_total--;

    for (int j = 0; j < NOTIF_MAX_LISTENERS; j++) {
        if (listeners[j].used && listeners[j].on_notification_removed) {
            listeners[j].on_notification_removed(&copy);
        }
    }
    return 0;
}

int notif_cancel_all(const char *package)
{
    int cancelled = 0;
    for (int i = 0; i < NOTIF_MAX_NOTIFICATIONS; i++) {
        if (notifications[i].used &&
            (!package || strcmp(notifications[i].package_name, package) == 0) &&
            !notifications[i].ongoing) {
            notifications[i].used = 0;
            notif_total--;
            cancelled++;
        }
    }
    return cancelled;
}

int notif_get_notification(int id, notification_t *notif)
{
    if (id < 0 || id >= NOTIF_MAX_NOTIFICATIONS || !notifications[id].used || !notif) return -1;
    memcpy(notif, &notifications[id], sizeof(notification_t));
    return 0;
}

int notif_list_notifications(notification_t *list, int max)
{
    if (!list || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < NOTIF_MAX_NOTIFICATIONS && count < max; i++) {
        if (notifications[i].used) memcpy(&list[count++], &notifications[i], sizeof(notification_t));
    }
    return count;
}

int notif_list_by_package(const char *package, notification_t *list, int max)
{
    if (!package || !list || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < NOTIF_MAX_NOTIFICATIONS && count < max; i++) {
        if (notifications[i].used && strcmp(notifications[i].package_name, package) == 0) {
            memcpy(&list[count++], &notifications[i], sizeof(notification_t));
        }
    }
    return count;
}

int notif_mark_read(int id)
{
    if (id < 0 || id >= NOTIF_MAX_NOTIFICATIONS || !notifications[id].used) return -1;
    notifications[id].read = 1;
    return 0;
}

int notif_tap(int id, int action_index)
{
    if (id < 0 || id >= NOTIF_MAX_NOTIFICATIONS || !notifications[id].used) return -1;
    notifications[id].read = 1;
    if (notifications[id].auto_cancel && action_index < 0) {
        notif_cancel(notifications[id].package_name, id);
    }
    return 0;
}

int notif_get_lockscreen_notifications(notification_t *list, int max)
{
    if (!list || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < NOTIF_MAX_NOTIFICATIONS && count < max; i++) {
        if (notifications[i].used &&
            notifications[i].visibility != NOTIF_VISIBILITY_SECRET) {
            memcpy(&list[count++], &notifications[i], sizeof(notification_t));
        }
    }
    return count;
}

int notif_register_listener(notif_listener_t *listener)
{
    if (!listener || !notif_initialized) return -1;
    for (int i = 0; i < NOTIF_MAX_LISTENERS; i++) {
        if (!listeners[i].used) {
            memcpy(&listeners[i], listener, sizeof(notif_listener_t));
            listeners[i].used = 1;
            listeners[i].id = i;
            listener_count++;
            return i;
        }
    }
    return -1;
}

int notif_unregister_listener(int listener_id)
{
    if (listener_id < 0 || listener_id >= NOTIF_MAX_LISTENERS || !listeners[listener_id].used) return -1;
    listeners[listener_id].used = 0;
    listener_count--;
    return 0;
}

int notif_set_dnd(int enable)
{
    dnd_enabled = enable ? 1 : 0;
    vga_printf("[NOTIF] DND %s\n", enable ? " on " : " close ");
    return 0;
}

int notif_get_dnd(void) { return dnd_enabled; }

int notif_count(void) { return notif_total; }

int notif_unread_count(void)
{
    int count = 0;
    for (int i = 0; i < NOTIF_MAX_NOTIFICATIONS; i++) {
        if (notifications[i].used && !notifications[i].read) count++;
    }
    return count;
}

int notif_channel_count(void) { return channel_count; }

int notif_set_light(int color, int on_ms, int off_ms)
{
    (void)color; (void)on_ms; (void)off_ms;
    /* 原型阶段: 控制通知LED */
    return 0;
}

int notif_cancel_light(void) { return 0; }

int notif_vibrate(int *pattern, int length, int repeat)
{
    (void)pattern; (void)length; (void)repeat;
    /* 原型阶段: 控制震动马达 */
    return 0;
}

int notif_cancel_vibrate(void) { return 0; }
