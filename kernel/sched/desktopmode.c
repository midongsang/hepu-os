/* ============================================================================
 * 赫普OS - 桌面模式
 * 多屏管理 + 自由窗口 + 任务栏 + 工作区 + 键鼠优化 + 剪贴板
 * ========================================================================== */

#include <stdint.h>
#include "desktopmode.h"
#include "vga.h"
#include "string.h"

desktop_mode_t dm;

void desktopmode_init(void)
{
    memset(&dm, 0, sizeof(desktop_mode_t));
    dm.initialized = 1;
    dm.mode = DISPLAY_MODE_PHONE;
    dm.primary_display = 0;
    dm.focused_window = -1;
    dm.z_order_counter = 0;
    dm.taskbar_height = 40;
    dm.taskbar_position = 0;  /* 底部 */
    dm.taskbar_autohide = 0;
    dm.current_workspace = 0;
    dm.window_snapping = 1;
    dm.window_animations = 1;
    dm.show_desktop_icons = 1;
    dm.dark_mode = 0;
    dm.accent_color = 0x0066CC;

    /* 默认显示 */
    dm_add_display("Built-in", 320, 200, 160, 1);

    /* 默认工作区 */
    for (int i = 0; i < DM_MAX_WORKSPACES; i++) {
        dm.workspaces[i].id = i;
        {
            char tmp[32];
            tmp[0] = 'W'; tmp[1] = 'o'; tmp[2] = 'r'; tmp[3] = 'k'; tmp[4] = 's';
            tmp[5] = 'p'; tmp[6] = 'a'; tmp[7] = 'c'; tmp[8] = 'e'; tmp[9] = ' ';
            tmp[10] = '0' + (i + 1); tmp[11] = 0;
            strncpy(dm.workspaces[i].name, tmp, 31);
        }
        dm.workspaces[i].active = (i == 0);
    }

    /* 鼠标 */
    dm.mouse.x = 160;
    dm.mouse.y = 100;
    dm.mouse.cursor = CURSOR_ARROW;
    dm.mouse.cursor_visible = 1;

    vga_puts("[DM] desktop mode init done \n");
    vga_printf(" display: %d, workspace: %d, task bar: %dpx\n",
               dm.display_count, DM_MAX_WORKSPACES, dm.taskbar_height);
}

int dm_add_display(const char *name, int width, int height, int dpi, int primary)
{
    if (!name || !dm.initialized) return -1;
    for (int i = 0; i < DM_MAX_DISPLAYS; i++) {
        if (!dm.displays[i].used) {
            memset(&dm.displays[i], 0, sizeof(display_device_t));
            dm.displays[i].used = 1;
            dm.displays[i].id = i;
            strncpy(dm.displays[i].name, name, 63);
            dm.displays[i].width = width;
            dm.displays[i].height = height;
            dm.displays[i].dpi = dpi;
            dm.displays[i].refresh_rate = 60;
            dm.displays[i].is_primary = primary ? 1 : 0;
            dm.displays[i].connected = 1;
            dm.displays[i].rotation = 0;
            dm.displays[i].brightness = 100;
            dm.display_count++;
            if (primary) dm.primary_display = i;
            return i;
        }
    }
    return -1;
}

int dm_remove_display(int display_id)
{
    if (display_id < 0 || display_id >= DM_MAX_DISPLAYS || !dm.displays[display_id].used) return -1;
    dm.displays[display_id].used = 0;
    dm.display_count--;
    return 0;
}

int dm_set_primary(int display_id)
{
    if (display_id < 0 || display_id >= DM_MAX_DISPLAYS || !dm.displays[display_id].used) return -1;
    for (int i = 0; i < DM_MAX_DISPLAYS; i++) dm.displays[i].is_primary = 0;
    dm.displays[display_id].is_primary = 1;
    dm.primary_display = display_id;
    return 0;
}

int dm_get_display(int display_id, display_device_t *display)
{
    if (display_id < 0 || display_id >= DM_MAX_DISPLAYS || !dm.displays[display_id].used || !display) return -1;
    memcpy(display, &dm.displays[display_id], sizeof(display_device_t));
    return 0;
}

int dm_list_displays(display_device_t *displays, int max)
{
    if (!displays || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DM_MAX_DISPLAYS && count < max; i++) {
        if (dm.displays[i].used) memcpy(&displays[count++], &dm.displays[i], sizeof(display_device_t));
    }
    return count;
}

int dm_set_resolution(int display_id, int width, int height)
{
    if (display_id < 0 || display_id >= DM_MAX_DISPLAYS || !dm.displays[display_id].used) return -1;
    dm.displays[display_id].width = width;
    dm.displays[display_id].height = height;
    return 0;
}

int dm_set_rotation(int display_id, int rotation)
{
    if (display_id < 0 || display_id >= DM_MAX_DISPLAYS || !dm.displays[display_id].used) return -1;
    dm.displays[display_id].rotation = rotation;
    return 0;
}

int dm_set_brightness(int display_id, int brightness)
{
    if (display_id < 0 || display_id >= DM_MAX_DISPLAYS || !dm.displays[display_id].used) return -1;
    if (brightness < 0) brightness = 0;
    if (brightness > 100) brightness = 100;
    dm.displays[display_id].brightness = brightness;
    return 0;
}

int dm_set_mode(dm_display_mode_t mode)
{
    dm.mode = mode;
    vga_printf("[DM] switch display mode: %d\n", mode);
    return 0;
}

dm_display_mode_t dm_get_mode(void) { return dm.mode; }

int dm_enter_desktop_mode(void)
{
    dm.mode = DISPLAY_MODE_DESKTOP;
    vga_puts("[DM] enter desktop mode \n");
    return 0;
}

int dm_exit_desktop_mode(void)
{
    dm.mode = DISPLAY_MODE_PHONE;
    vga_puts("[DM] exit desktop mode \n");
    return 0;
}

int dm_mouse_wheel(int delta)
{
    dm.mouse.wheel_delta = delta;
    return 0;
}

int dm_key_press(int key, int pressed)
{
    if (key >= 0 && key < 256) dm.keyboard.keys[key] = pressed;
    return 0;
}

int dm_set_cursor(cursor_type_t cursor)
{
    dm.mouse.cursor = cursor;
    return 0;
}

int dm_get_mouse(mouse_state_t *mouse)
{
    if (!mouse) return -1;
    memcpy(mouse, &dm.mouse, sizeof(mouse_state_t));
    return 0;
}

int dm_get_keyboard(keyboard_state_t *keyboard)
{
    if (!keyboard) return -1;
    memcpy(keyboard, &dm.keyboard, sizeof(keyboard_state_t));
    return 0;
}

int dm_clipboard_set_text(const char *text)
{
    if (!text) return -1;
    strncpy(dm.clipboard.text, text, 4095);
    dm.clipboard.has_text = 1;
    dm.clipboard.has_image = 0;
    return 0;
}

int dm_clipboard_get_text(char *text, int len)
{
    if (!text || len <= 0 || !dm.clipboard.has_text) return -1;
    strncpy(text, dm.clipboard.text, len - 1);
    text[len - 1] = '\0';
    return 0;
}

int dm_clipboard_set_image(const uint8_t *data, int w, int h)
{
    if (!data) return -1;
    int size = w * h * 4;
    if (size > 65536) size = 65536;
    memcpy(dm.clipboard.image_data, data, size);
    dm.clipboard.image_width = w;
    dm.clipboard.image_height = h;
    dm.clipboard.has_image = 1;
    dm.clipboard.has_text = 0;
    return 0;
}

int dm_clipboard_has_text(void) { return dm.clipboard.has_text; }

int dm_clipboard_clear(void)
{
    dm.clipboard.has_text = 0;
    dm.clipboard.has_image = 0;
    dm.clipboard.text[0] = '\0';
    return 0;
}

int dm_snap_window(int window_id, int edge)
{
    if (window_id < 0 || window_id >= DM_MAX_WINDOWS || !dm.windows[window_id].used) return -1;
    if (!dm.window_snapping) return -1;
    dm_window_t *w = &dm.windows[window_id];
    int sw = dm.displays[dm.primary_display].width;
    int sh = dm.displays[dm.primary_display].height - dm.taskbar_height;
    switch (edge) {
        case 0: w->x = 0; w->y = 0; w->width = sw / 2; w->height = sh; break;  /* 左 */
        case 1: w->x = sw / 2; w->y = 0; w->width = sw / 2; w->height = sh; break;  /* 右 */
        case 2: w->x = 0; w->y = 0; w->width = sw; w->height = sh / 2; break;  /* 上 */
        case 3: w->x = 0; w->y = sh / 2; w->width = sw; w->height = sh / 2; break;  /* 下 */
        default: return -1;
    }
    return 0;
}

int dm_render(void) { return 0; }
void dm_tick(void) {}

int dm_window_count(void) { return dm.window_count; }
int dm_display_count(void) { return dm.display_count; }

