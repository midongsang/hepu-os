/* ============================================================================
 * 赫普OS - 通知/控制中心
 * ========================================================================== */

#include <stdint.h>
#include "notification_center.h"
#include "vga.h"
#include "string.h"

static notification_center_t nc;

void notification_center_init(void)
{
    memset(&nc, 0, sizeof(notification_center_t));
    nc.initialized = 1;
    nc.enabled = 1;
    nc.config.notification_history = 1;
    nc.config.notification_history_days = 7;
    nc.config.heads_up_enabled = 1;
    nc.config.lock_screen_notifications = 1;
    nc.config.badge_enabled = 1;
    nc.config.snooze_enabled = 1;
    nc.active_media = -1;

    /* 注册默认快捷开关 */
    const char *labels[] = {"WiFi"," BT "," airplane mode "," mobile data "," 定 bit "," flashlight ",
        " auto-rotate "," hotspot ","NFC"," 勿扰 "," 振动 "," 静音 "," 省电 "," dark mode "," 护眼 mode "};
    for (int i = 0; i < 15; i++) {
        nc.toggles[i].toggle = (nc_toggle_t)i;
        nc.toggles[i].enabled = 0;
        nc.toggles[i].available = 1;
        strncpy(nc.toggles[i].label, labels[i], 63);
        nc.toggle_count++;
    }

    vga_puts("[NOTIF] notif center init done \n");
    vga_printf(" notify: %d, quick toggles: %d \n", nc.notification_count, nc.toggle_count);
}

int nc_notify(const char *app, const char *package, const char *title,
              const char *content, nc_importance_t importance, nc_category_t category)
{
    for (int i = 0; i < NC_MAX_NOTIFICATIONS; i++) {
        if (!nc.notifications[i].used) {
            memset(&nc.notifications[i], 0, sizeof(nc_notification_t));
            nc.notifications[i].used = 1;
            nc.notifications[i].id = i;
            if (app) strncpy(nc.notifications[i].app_name, app, 127);
            if (package) strncpy(nc.notifications[i].package, package, 127);
            if (title) strncpy(nc.notifications[i].title, title, 255);
            if (content) strncpy(nc.notifications[i].content, content, 511);
            nc.notifications[i].importance = importance;
            nc.notifications[i].category = category;
            nc.notifications[i].post_time = 1;
            nc.notifications[i].is_clearable = 1;
            nc.notifications[i].is_shown = 1;
            nc.notification_count++;
            nc.unread_count++;
            nc.total_notifications++;
            return i;
        }
    }
    return -1;
}

int nc_notify_full(nc_notification_t *notif)
{
    if (!notif) return -1;
    for (int i = 0; i < NC_MAX_NOTIFICATIONS; i++) {
        if (!nc.notifications[i].used) {
            memcpy(&nc.notifications[i], notif, sizeof(nc_notification_t));
            nc.notifications[i].used = 1;
            nc.notifications[i].id = i;
            nc.notification_count++;
            nc.unread_count++;
            nc.total_notifications++;
            return i;
        }
    }
    return -1;
}

int nc_cancel(int notification_id)
{
    if (notification_id < 0 || notification_id >= NC_MAX_NOTIFICATIONS || !nc.notifications[notification_id].used) return -1;
    if (!nc.notifications[notification_id].is_clearable) return -1;
    nc.notifications[notification_id].used = 0;
    nc.notification_count--;
    if (!nc.notifications[notification_id].is_read) nc.unread_count--;
    nc.total_dismissed++;
    return 0;
}

int nc_cancel_all(const char *package)
{
    int cancelled = 0;
    for (int i = 0; i < NC_MAX_NOTIFICATIONS; i++) {
        if (nc.notifications[i].used && nc.notifications[i].is_clearable) {
            if (!package || strcmp(nc.notifications[i].package, package) == 0) {
                nc.notifications[i].used = 0;
                nc.notification_count--;
                cancelled++;
            }
        }
    }
    nc.total_dismissed += cancelled;
    return cancelled;
}

int nc_get(int notification_id, nc_notification_t *notif)
{
    if (notification_id < 0 || notification_id >= NC_MAX_NOTIFICATIONS || !nc.notifications[notification_id].used || !notif) return -1;
    memcpy(notif, &nc.notifications[notification_id], sizeof(nc_notification_t));
    return 0;
}

int nc_list(nc_notification_t *notifs, int max)
{
    if (!notifs || max <= 0) return 0;
    int count = 0;
    for (int i = NC_MAX_NOTIFICATIONS - 1; i >= 0 && count < max; i--) {
        if (nc.notifications[i].used) memcpy(&notifs[count++], &nc.notifications[i], sizeof(nc_notification_t));
    }
    return count;
}

int nc_list_by_app(const char *package, nc_notification_t *notifs, int max)
{
    if (!notifs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < NC_MAX_NOTIFICATIONS && count < max; i++) {
        if (nc.notifications[i].used && strcmp(nc.notifications[i].package, package) == 0)
            memcpy(&notifs[count++], &nc.notifications[i], sizeof(nc_notification_t));
    }
    return count;
}

int nc_mark_read(int notification_id)
{
    if (notification_id < 0 || notification_id >= NC_MAX_NOTIFICATIONS || !nc.notifications[notification_id].used) return -1;
    if (!nc.notifications[notification_id].is_read) {
        nc.notifications[notification_id].is_read = 1;
        nc.unread_count--;
    }
    return 0;
}

int nc_mark_all_read(void)
{
    for (int i = 0; i < NC_MAX_NOTIFICATIONS; i++) {
        if (nc.notifications[i].used && !nc.notifications[i].is_read) {
            nc.notifications[i].is_read = 1;
        }
    }
    nc.unread_count = 0;
    return 0;
}

int nc_snooze(int notification_id, int minutes)
{
    (void)minutes;
    if (notification_id < 0 || notification_id >= NC_MAX_NOTIFICATIONS || !nc.notifications[notification_id].used) return -1;
    nc.notifications[notification_id].is_shown = 0;
    return 0;
}

int nc_group_get(int group_id, nc_group_t *group)
{
    if (group_id < 0 || group_id >= NC_MAX_GROUPS || !nc.groups[group_id].used || !group) return -1;
    memcpy(group, &nc.groups[group_id], sizeof(nc_group_t));
    return 0;
}

int nc_group_list(nc_group_t *groups, int max)
{
    if (!groups || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < NC_MAX_GROUPS && count < max; i++) {
        if (nc.groups[i].used) memcpy(&groups[count++], &nc.groups[i], sizeof(nc_group_t));
    }
    return count;
}

int nc_group_dismiss(int group_id)
{
    if (group_id < 0 || group_id >= NC_MAX_GROUPS || !nc.groups[group_id].used) return -1;
    nc.groups[group_id].used = 0;
    return 0;
}

int nc_group_silence(int group_id, int silence)
{
    if (group_id < 0 || group_id >= NC_MAX_GROUPS || !nc.groups[group_id].used) return -1;
    nc.groups[group_id].is_silenced = silence ? 1 : 0;
    return 0;
}

int nc_toggle_set(nc_toggle_t toggle, int enabled)
{
    if (toggle < 0 || toggle >= NC_MAX_QUICK_TOGGLES) return -1;
    nc.toggles[toggle].enabled = enabled ? 1 : 0;
    return 0;
}

int nc_toggle_get(nc_toggle_t toggle, int *enabled)
{
    if (toggle < 0 || toggle >= NC_MAX_QUICK_TOGGLES || !enabled) return -1;
    *enabled = nc.toggles[toggle].enabled;
    return 0;
}

int nc_toggle_list(nc_quick_toggle_t *toggles, int max)
{
    if (!toggles || max <= 0) return 0;
    int count = nc.toggle_count < max ? nc.toggle_count : max;
    for (int i = 0; i < count; i++) memcpy(&toggles[i], &nc.toggles[i], sizeof(nc_quick_toggle_t));
    return count;
}

int nc_toggle_add(nc_toggle_t toggle, const char *label)
{
    if (toggle >= NC_MAX_QUICK_TOGGLES) return -1;
    nc.toggles[toggle].toggle = toggle;
    nc.toggles[toggle].available = 1;
    if (label) strncpy(nc.toggles[toggle].label, label, 63);
    if (nc.toggle_count <= (int)toggle) nc.toggle_count = toggle + 1;
    return 0;
}

int nc_toggle_remove(nc_toggle_t toggle)
{
    if (toggle < 0 || toggle >= NC_MAX_QUICK_TOGGLES) return -1;
    nc.toggles[toggle].available = 0;
    return 0;
}

int nc_media_register(const char *app, const char *title, const char *artist)
{
    for (int i = 0; i < NC_MAX_MEDIA_SESSIONS; i++) {
        if (!nc.media[i].used) {
            memset(&nc.media[i], 0, sizeof(nc_media_session_t));
            nc.media[i].used = 1;
            nc.media[i].id = i;
            if (app) strncpy(nc.media[i].app_name, app, 127);
            if (title) strncpy(nc.media[i].title, title, 255);
            if (artist) strncpy(nc.media[i].artist, artist, 255);
            nc.media[i].supports_play_pause = 1;
            nc.media[i].supports_skip = 1;
            nc.media[i].last_active = 1;
            nc.media_count++;
            nc.active_media = i;
            return i;
        }
    }
    return -1;
}

int nc_media_unregister(int session_id)
{
    if (session_id < 0 || session_id >= NC_MAX_MEDIA_SESSIONS || !nc.media[session_id].used) return -1;
    nc.media[session_id].used = 0;
    nc.media_count--;
    if (nc.active_media == session_id) nc.active_media = -1;
    return 0;
}

int nc_media_update(int session_id, int playing, int position, int duration)
{
    if (session_id < 0 || session_id >= NC_MAX_MEDIA_SESSIONS || !nc.media[session_id].used) return -1;
    nc.media[session_id].is_playing = playing;
    nc.media[session_id].position_ms = position;
    nc.media[session_id].duration_ms = duration;
    nc.media[session_id].last_active = 1;
    return 0;
}

int nc_media_play_pause(int session_id)
{
    if (session_id < 0 || session_id >= NC_MAX_MEDIA_SESSIONS || !nc.media[session_id].used) return -1;
    nc.media[session_id].is_playing = !nc.media[session_id].is_playing;
    nc.total_actions++;
    return 0;
}

int nc_media_next(int session_id)
{
    if (session_id < 0 || session_id >= NC_MAX_MEDIA_SESSIONS || !nc.media[session_id].used) return -1;
    nc.media[session_id].position_ms = 0;
    nc.total_actions++;
    return 0;
}

int nc_media_prev(int session_id)
{
    if (session_id < 0 || session_id >= NC_MAX_MEDIA_SESSIONS || !nc.media[session_id].used) return -1;
    nc.media[session_id].position_ms = 0;
    nc.total_actions++;
    return 0;
}

int nc_media_seek(int session_id, int position_ms)
{
    if (session_id < 0 || session_id >= NC_MAX_MEDIA_SESSIONS || !nc.media[session_id].used) return -1;
    nc.media[session_id].position_ms = position_ms;
    return 0;
}

int nc_media_get(int session_id, nc_media_session_t *session)
{
    if (session_id < 0 || session_id >= NC_MAX_MEDIA_SESSIONS || !nc.media[session_id].used || !session) return -1;
    memcpy(session, &nc.media[session_id], sizeof(nc_media_session_t));
    return 0;
}

int nc_media_list(nc_media_session_t *sessions, int max)
{
    if (!sessions || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < NC_MAX_MEDIA_SESSIONS && count < max; i++) {
        if (nc.media[i].used) memcpy(&sessions[count++], &nc.media[i], sizeof(nc_media_session_t));
    }
    return count;
}

int nc_media_get_active(nc_media_session_t *session)
{
    if (nc.active_media < 0 || !session) return -1;
    return nc_media_get(nc.active_media, session);
}

int nc_dnd_set(int enabled)
{
    nc.dnd.enabled = enabled ? 1 : 0;
    return 0;
}

int nc_dnd_get(int *enabled)
{
    if (!enabled) return -1;
    *enabled = nc.dnd.enabled;
    return 0;
}

int nc_dnd_set_config(const nc_dnd_config_t *config)
{
    if (!config) return -1;
    memcpy(&nc.dnd, config, sizeof(nc_dnd_config_t));
    return 0;
}

int nc_dnd_get_config(nc_dnd_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &nc.dnd, sizeof(nc_dnd_config_t));
    return 0;
}

int nc_set_config(const nc_config_t *config)
{
    if (!config) return -1;
    memcpy(&nc.config, config, sizeof(nc_config_t));
    return 0;
}

int nc_get_config(nc_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &nc.config, sizeof(nc_config_t));
    return 0;
}

int nc_get_stats(notification_center_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &nc, sizeof(notification_center_t));
    return 0;
}

int nc_get_unread_count(int *count)
{
    if (!count) return -1;
    *count = nc.unread_count;
    return 0;
}
