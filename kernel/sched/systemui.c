/* ============================================================================
 * 赫普OS - 系统UI组件
 * 状态栏 + 导航栏 + 通知中心 + 控制中心 + 锁屏
 * ========================================================================== */

#include <stdint.h>
#include "systemui.h"
#include "constants.h"
#include "vga.h"
#include "string.h"

static system_ui_t sui;

/* 默认控制开关回调 */
static void toggle_wifi(int enabled) { vga_printf("[SUI] WiFi %s\n", enabled ? " on " : " close "); }
static void toggle_bluetooth(int enabled) { vga_printf("[SUI] BT %s\n", enabled ? " on " : " close "); }
static void toggle_airplane(int enabled) { vga_printf("[SUI] airplane mode %s\n", enabled ? " on " : " close "); }
static void toggle_rotation(int enabled) { vga_printf("[SUI] auto-rotate %s\n", enabled ? " on " : " close "); }
static void toggle_flashlight(int enabled) { vga_printf("[SUI] flashlight %s\n", enabled ? " on " : " close "); }
static void toggle_gps(int enabled) { vga_printf("[SUI] GPS %s\n", enabled ? " on " : " close "); }
static void toggle_mobile_data(int enabled) { vga_printf("[SUI] mobile data %s\n", enabled ? " on " : " close "); }
static void toggle_hotspot(int enabled) { vga_printf("[SUI] hotspot %s\n", enabled ? " on " : " close "); }
static void toggle_nfc(int enabled) { vga_printf("[SUI] NFC %s\n", enabled ? " on " : " close "); }
static void toggle_dnd(int enabled) { vga_printf("[SUI] DND %s\n", enabled ? " on " : " close "); }

void systemui_init(void)
{
    memset(&sui, 0, sizeof(system_ui_t));
    sui.initialized = 1;
    sui.screen_width = 320;
    sui.screen_height = 200;
    sui.dpi = 160;
    sui.font_scale = 1.0f;
    sui.dark_mode = 0;

    /* 状态栏 */
    sui.status_bar.state = SUI_VISIBLE;
    sui.status_bar.height = 20;
    sui.status_bar.background_color = COLOR_BLACK;
    sui.status_bar.text_color = COLOR_WHITE;
    sui.status_bar.transparent = 0;
    sui.status_bar.show_clock = 1;
    sui.status_bar.show_battery = 1;
    sui.status_bar.show_signal = 1;
    sui.status_bar.show_wifi = 1;
    sui.status_bar.show_bluetooth = 0;
    sui.status_bar.show_notification_count = 1;

    /* 导航栏 */
    sui.nav_bar.state = SUI_VISIBLE;
    sui.nav_bar.height = 32;
    sui.nav_bar.background_color = COLOR_BLACK;
    sui.nav_bar.icon_color = COLOR_WHITE;
    sui.nav_bar.show_back = 1;
    sui.nav_bar.show_home = 1;
    sui.nav_bar.show_recent = 1;
    sui.nav_bar.show_menu = 0;
    sui.nav_bar.immersive_mode = 0;

    /* 通知中心 */
    sui.notification_shade.state = SUI_HIDDEN;
    sui.notification_shade.height = 150;
    sui.notification_shade.background_color = COLOR_TRANSLUCENT;
    sui.notification_shade.clear_all_enabled = 1;
    sui.notification_shade.quick_settings_visible = 1;

    /* 控制中心 */
    sui.control_center.state = SUI_HIDDEN;
    sui.control_center.height = 150;
    sui.control_center.background_color = COLOR_TRANSLUCENT;
    sui.control_center.brightness = 80;
    sui.control_center.volume = 70;
    sui.control_center.rotation_locked = 0;
    sui.control_center.flashlight_on = 0;

    /* 注册默认控制开关 */
    controlcenter_add_toggle("WiFi", 0, toggle_wifi);
    controlcenter_add_toggle("Bluetooth", 1, toggle_bluetooth);
    controlcenter_add_toggle("Airplane", 2, toggle_airplane);
    controlcenter_add_toggle("Rotation", 3, toggle_rotation);
    controlcenter_add_toggle("Flashlight", 4, toggle_flashlight);
    controlcenter_add_toggle("GPS", 5, toggle_gps);
    controlcenter_add_toggle("Mobile Data", 6, toggle_mobile_data);
    controlcenter_add_toggle("Hotspot", 7, toggle_hotspot);
    controlcenter_add_toggle("NFC", 8, toggle_nfc);
    controlcenter_add_toggle("Do Not Disturb", 9, toggle_dnd);

    /* 锁屏 */
    sui.lock_screen.state = LOCK_SCREEN_LOCKED;
    sui.lock_screen.lock_type = LOCK_TYPE_SWIPE;
    sui.lock_screen.background_color = COLOR_BLACK;
    sui.lock_screen.show_clock = 1;
    sui.lock_screen.show_date = 1;
    sui.lock_screen.show_notifications = 1;
    sui.lock_screen.show_camera_shortcut = 1;
    sui.lock_screen.show_emergency = 1;
    sui.lock_screen.pin_length = 4;
    sui.lock_screen.pattern_length = 5;
    sui.lock_screen.failed_attempts = 0;
    sui.lock_screen.max_attempts = 5;

    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[SUI] init done\n");
}

int statusbar_show(void)
{
    sui.status_bar.state = SUI_VISIBLE;
    return 0;
}

int statusbar_hide(void)
{
    sui.status_bar.state = SUI_HIDDEN;
    return 0;
}

int statusbar_add_icon(const char *name, int icon, int priority)
{
    if (!name) return -1;
    for (int i = 0; i < SUI_MAX_ICONS; i++) {
        if (!sui.status_bar.icons[i].used) {
            memset(&sui.status_bar.icons[i], 0, sizeof(status_icon_t));
            sui.status_bar.icons[i].used = 1;
            sui.status_bar.icons[i].id = i;
            sui.status_bar.icons[i].name = name;
            sui.status_bar.icons[i].icon = icon;
            sui.status_bar.icons[i].visible = 1;
            sui.status_bar.icons[i].priority = priority;
            sui.status_bar.icon_count++;
            return i;
        }
    }
    return -1;
}

int statusbar_remove_icon(int icon_id)
{
    if (icon_id < 0 || icon_id >= SUI_MAX_ICONS || !sui.status_bar.icons[icon_id].used) return -1;
    sui.status_bar.icons[icon_id].used = 0;
    sui.status_bar.icon_count--;
    return 0;
}

int statusbar_set_icon_text(int icon_id, const char *text)
{
    if (icon_id < 0 || icon_id >= SUI_MAX_ICONS || !sui.status_bar.icons[icon_id].used || !text) return -1;
    strncpy(sui.status_bar.icons[icon_id].text, text, 31);
    return 0;
}

int statusbar_set_transparent(int transparent)
{
    sui.status_bar.transparent = transparent ? 1 : 0;
    return 0;
}

int statusbar_get_height(void) { return sui.status_bar.height; }

int navbar_show(void)
{
    sui.nav_bar.state = SUI_VISIBLE;
    return 0;
}

int navbar_hide(void)
{
    sui.nav_bar.state = SUI_HIDDEN;
    return 0;
}

int navbar_set_immersive(int immersive)
{
    sui.nav_bar.immersive_mode = immersive ? 1 : 0;
    if (immersive) {
        sui.status_bar.state = SUI_HIDDEN;
        sui.nav_bar.state = SUI_HIDDEN;
    } else {
        sui.status_bar.state = SUI_VISIBLE;
        sui.nav_bar.state = SUI_VISIBLE;
    }
    return 0;
}

int navbar_get_height(void) { return sui.nav_bar.height; }

int notificationshade_expand(void)
{
    sui.notification_shade.state = SUI_EXPANDED;
    return 0;
}

int notificationshade_collapse(void)
{
    sui.notification_shade.state = SUI_HIDDEN;
    return 0;
}

int notificationshade_clear_all(void)
{
    if (!sui.notification_shade.clear_all_enabled) return -1;
    vga_puts("[SUI] clear all notifications \n");
    return 0;
}

int notificationshade_get_state(void) { return sui.notification_shade.state; }

int controlcenter_expand(void)
{
    sui.control_center.state = SUI_EXPANDED;
    return 0;
}

int controlcenter_collapse(void)
{
    sui.control_center.state = SUI_HIDDEN;
    return 0;
}

int controlcenter_toggle(int toggle_id)
{
    if (toggle_id < 0 || toggle_id >= SUI_MAX_TOGGLES || !sui.control_center.toggles[toggle_id].used) return -1;
    if (!sui.control_center.toggles[toggle_id].available) return -1;
    sui.control_center.toggles[toggle_id].enabled = !sui.control_center.toggles[toggle_id].enabled;
    if (sui.control_center.toggles[toggle_id].on_toggle) {
        sui.control_center.toggles[toggle_id].on_toggle(sui.control_center.toggles[toggle_id].enabled);
    }
    return sui.control_center.toggles[toggle_id].enabled;
}

int controlcenter_set_brightness(int value)
{
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    sui.control_center.brightness = value;
    return 0;
}

int controlcenter_set_volume(int value)
{
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    sui.control_center.volume = value;
    return 0;
}

int controlcenter_add_toggle(const char *name, int icon, void (*on_toggle)(int))
{
    if (!name) return -1;
    for (int i = 0; i < SUI_MAX_TOGGLES; i++) {
        if (!sui.control_center.toggles[i].used) {
            memset(&sui.control_center.toggles[i], 0, sizeof(control_toggle_t));
            sui.control_center.toggles[i].used = 1;
            sui.control_center.toggles[i].id = i;
            sui.control_center.toggles[i].name = name;
            sui.control_center.toggles[i].icon = icon;
            sui.control_center.toggles[i].enabled = 0;
            sui.control_center.toggles[i].available = 1;
            sui.control_center.toggles[i].on_toggle = on_toggle;
            sui.control_center.toggle_count++;
            return i;
        }
    }
    return -1;
}

int lockscreen_lock(void)
{
    sui.lock_screen.state = LOCK_SCREEN_LOCKED;
    vga_puts("[SUI] screen locked \n");
    return 0;
}

int lockscreen_unlock(void)
{
    if (sui.lock_screen.lock_type == LOCK_TYPE_NONE ||
        sui.lock_screen.lock_type == LOCK_TYPE_SWIPE) {
        sui.lock_screen.state = LOCK_SCREEN_UNLOCKED;
        sui.lock_screen.failed_attempts = 0;
        vga_puts("[SUI] screen unlocked \n");
        return 0;
    }
    return -1;
}

int lockscreen_set_type(lock_type_t type)
{
    sui.lock_screen.lock_type = type;
    return 0;
}

int lockscreen_verify_pin(const char *pin)
{
    if (!pin) return -1;
    /* 原型阶段: 任何4位PIN都通过 */
    if (strlen(pin) >= 4) {
        sui.lock_screen.state = LOCK_SCREEN_UNLOCKED;
        sui.lock_screen.failed_attempts = 0;
        return 0;
    }
    sui.lock_screen.failed_attempts++;
    if (sui.lock_screen.failed_attempts >= sui.lock_screen.max_attempts) {
        sui.lock_screen.lockout_until = 30000;  /* 30秒锁定 */
    }
    return -1;
}

int lockscreen_verify_pattern(const int *pattern, int length)
{
    if (!pattern || length < 4) return -1;
    /* 原型阶段: 任何长度>=4的图案都通过 */
    sui.lock_screen.state = LOCK_SCREEN_UNLOCKED;
    sui.lock_screen.failed_attempts = 0;
    return 0;
}

int lockscreen_get_state(void) { return sui.lock_screen.state; }

int lockscreen_is_locked(void)
{
    return (sui.lock_screen.state == LOCK_SCREEN_LOCKED) ? 1 : 0;
}

int lockscreen_show_emergency(void)
{
    vga_puts("[SUI] emergency call \n");
    return 0;
}

int systemui_set_dark_mode(int enable)
{
    sui.dark_mode = enable ? 1 : 0;
    if (enable) {
        sui.status_bar.background_color = COLOR_BLACK;
        sui.status_bar.text_color = COLOR_WHITE;
        sui.nav_bar.background_color = COLOR_BLACK;
        sui.nav_bar.icon_color = COLOR_WHITE;
    } else {
        sui.status_bar.background_color = COLOR_WHITE;
        sui.status_bar.text_color = COLOR_BLACK;
        sui.nav_bar.background_color = COLOR_WHITE;
        sui.nav_bar.icon_color = COLOR_BLACK;
    }
    vga_printf("[SUI] dark mode %s\n", enable ? " on " : " close ");
    return 0;
}

int systemui_get_dark_mode(void) { return sui.dark_mode; }

int systemui_set_font_scale(float scale)
{
    if (scale < 0.5f) scale = 0.5f;
    if (scale > 2.0f) scale = 2.0f;
    sui.font_scale = scale;
    return 0;
}

int systemui_get_font_scale(void) { return (int)(sui.font_scale * 100); }

int systemui_set_screen_brightness(int value)
{
    if (value < 0) value = 0;
    if (value > 255) value = 255;
    sui.control_center.brightness = value * 100 / 255;
    return 0;
}

int systemui_get_screen_brightness(void) { return sui.control_center.brightness * 255 / 100; }

int systemui_sleep(void)
{
    vga_puts("[SUI] screen sleep \n");
    return 0;
}

int systemui_wake(void)
{
    vga_puts("[SUI] screen wake \n");
    return 0;
}

int systemui_render(void)
{
    /* 原型阶段: 渲染状态栏+导航栏+通知中心+控制中心+锁屏 */
    return 0;
}

void systemui_tick(void)
{
    /* 更新时钟、电池、信号等状态 */
}

int systemui_get_status(system_ui_t *status)
{
    if (!status) return -1;
    memcpy(status, &sui, sizeof(system_ui_t));
    return 0;
}
