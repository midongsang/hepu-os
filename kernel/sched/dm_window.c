/* ============================================================================
 * 赫普OS - 桌面模式窗口管理（从desktopmode.c拆分）
 * ========================================================================== */

#include <stdint.h>
#include "desktopmode.h"
#include "vga.h"
#include "string.h"

extern desktop_mode_t dm;

int dm_window_create(const char *title, int x, int y, int w, int h)
{
    if (!title || !dm.initialized) return -1;
    for (int i = 0; i < DM_MAX_WINDOWS; i++) {
        if (!dm.windows[i].used) {
            memset(&dm.windows[i], 0, sizeof(dm_window_t));
            dm.windows[i].used = 1;
            dm.windows[i].id = i;
            strncpy(dm.windows[i].title, title, 127);
            dm.windows[i].x = x;
            dm.windows[i].y = y;
            dm.windows[i].width = w;
            dm.windows[i].height = h;
            dm.windows[i].minimized = 0;
            dm.windows[i].maximized = 0;
            dm.windows[i].fullscreen = 0;
            dm.windows[i].always_on_top = 0;
            dm.windows[i].resizable = 1;
            dm.windows[i].focusable = 1;
            dm.windows[i].visible = 1;
            dm.windows[i].workspace = dm.current_workspace;
            dm.windows[i].z_order = dm.z_order_counter++;
            dm.windows[i].opacity = 255;
            dm.window_count++;
            dm.focused_window = i;
            /* 添加到任务栏 */
            dm_taskbar_add(title, i, 0);
            return i;
        }
    }
    return -1;
}

int dm_window_destroy(int window_id)
{
    if (window_id < 0 || window_id >= DM_MAX_WINDOWS || !dm.windows[window_id].used) return -1;
    dm.windows[window_id].used = 0;
    dm.window_count--;
    if (dm.focused_window == window_id) dm.focused_window = -1;
    return 0;
}

int dm_window_move(int window_id, int x, int y)
{
    if (window_id < 0 || window_id >= DM_MAX_WINDOWS || !dm.windows[window_id].used) return -1;
    if (dm.windows[window_id].maximized) return -1;
    dm.windows[window_id].x = x;
    dm.windows[window_id].y = y;
    return 0;
}

int dm_window_resize(int window_id, int w, int h)
{
    if (window_id < 0 || window_id >= DM_MAX_WINDOWS || !dm.windows[window_id].used) return -1;
    if (!dm.windows[window_id].resizable || dm.windows[window_id].maximized) return -1;
    dm.windows[window_id].width = w;
    dm.windows[window_id].height = h;
    return 0;
}

int dm_window_focus(int window_id)
{
    if (window_id < 0 || window_id >= DM_MAX_WINDOWS || !dm.windows[window_id].used) return -1;
    if (!dm.windows[window_id].focusable) return -1;
    dm.focused_window = window_id;
    dm.windows[window_id].z_order = dm.z_order_counter++;
    dm.windows[window_id].minimized = 0;
    return 0;
}

int dm_window_minimize(int window_id)
{
    if (window_id < 0 || window_id >= DM_MAX_WINDOWS || !dm.windows[window_id].used) return -1;
    dm.windows[window_id].minimized = 1;
    if (dm.focused_window == window_id) dm.focused_window = -1;
    return 0;
}

int dm_window_maximize(int window_id)
{
    if (window_id < 0 || window_id >= DM_MAX_WINDOWS || !dm.windows[window_id].used) return -1;
    dm_window_t *w = &dm.windows[window_id];
    if (!w->maximized) {
        w->prev_x = w->x; w->prev_y = w->y;
        w->prev_w = w->width; w->prev_h = w->height;
        w->x = 0; w->y = 0;
        w->width = dm.displays[dm.primary_display].width;
        w->height = dm.displays[dm.primary_display].height - dm.taskbar_height;
        w->maximized = 1;
    }
    return 0;
}

int dm_window_restore(int window_id)
{
    if (window_id < 0 || window_id >= DM_MAX_WINDOWS || !dm.windows[window_id].used) return -1;
    dm_window_t *w = &dm.windows[window_id];
    if (w->maximized) {
        w->x = w->prev_x; w->y = w->prev_y;
        w->width = w->prev_w; w->height = w->prev_h;
        w->maximized = 0;
    }
    w->minimized = 0;
    return 0;
}

int dm_window_close(int window_id)
{
    return dm_window_destroy(window_id);
}

int dm_window_set_always_on_top(int window_id, int on_top)
{
    if (window_id < 0 || window_id >= DM_MAX_WINDOWS || !dm.windows[window_id].used) return -1;
    dm.windows[window_id].always_on_top = on_top ? 1 : 0;
    return 0;
}

int dm_window_set_opacity(int window_id, int opacity)
{
    if (window_id < 0 || window_id >= DM_MAX_WINDOWS || !dm.windows[window_id].used) return -1;
    if (opacity < 0) opacity = 0;
    if (opacity > 255) opacity = 255;
    dm.windows[window_id].opacity = opacity;
    return 0;
}

int dm_window_get(int window_id, dm_window_t *window)
{
    if (window_id < 0 || window_id >= DM_MAX_WINDOWS || !dm.windows[window_id].used || !window) return -1;
    memcpy(window, &dm.windows[window_id], sizeof(dm_window_t));
    return 0;
}

int dm_list_windows(dm_window_t *windows, int max)
{
    if (!windows || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DM_MAX_WINDOWS && count < max; i++) {
        if (dm.windows[i].used) memcpy(&windows[count++], &dm.windows[i], sizeof(dm_window_t));
    }
    return count;
}

int dm_get_focused_window(void) { return dm.focused_window; }

int dm_taskbar_add(const char *title, int window_id, int pinned)
{
    if (!title) return -1;
    for (int i = 0; i < DM_MAX_TASKBAR_ITEMS; i++) {
        if (!dm.taskbar[i].used) {
            memset(&dm.taskbar[i], 0, sizeof(taskbar_item_t));
            dm.taskbar[i].used = 1;
            dm.taskbar[i].id = i;
            strncpy(dm.taskbar[i].title, title, 127);
            dm.taskbar[i].window_id = window_id;
            dm.taskbar[i].pinned = pinned ? 1 : 0;
            dm.taskbar[i].running = 1;
            dm.taskbar_count++;
            return i;
        }
    }
    return -1;
}

int dm_taskbar_remove(int item_id)
{
    if (item_id < 0 || item_id >= DM_MAX_TASKBAR_ITEMS || !dm.taskbar[item_id].used) return -1;
    dm.taskbar[item_id].used = 0;
    dm.taskbar_count--;
    return 0;
}

int dm_taskbar_set_position(int position)
{
    dm.taskbar_position = position;
    return 0;
}

int dm_taskbar_set_autohide(int autohide)
{
    dm.taskbar_autohide = autohide ? 1 : 0;
    return 0;
}

int dm_taskbar_get(taskbar_item_t *items, int max)
{
    if (!items || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DM_MAX_TASKBAR_ITEMS && count < max; i++) {
        if (dm.taskbar[i].used) memcpy(&items[count++], &dm.taskbar[i], sizeof(taskbar_item_t));
    }
    return count;
}

int dm_desktop_add_icon(const char *name, const char *app_id, int x, int y)
{
    if (!name) return -1;
    for (int i = 0; i < DM_MAX_DESKTOP_ICONS; i++) {
        if (!dm.desktop_icons[i].used) {
            memset(&dm.desktop_icons[i], 0, sizeof(desktop_icon_t));
            dm.desktop_icons[i].used = 1;
            dm.desktop_icons[i].id = i;
            strncpy(dm.desktop_icons[i].name, name, 63);
            if (app_id) strncpy(dm.desktop_icons[i].app_id, app_id, 63);
            dm.desktop_icons[i].x = x;
            dm.desktop_icons[i].y = y;
            dm.desktop_icon_count++;
            return i;
        }
    }
    return -1;
}

int dm_desktop_remove_icon(int icon_id)
{
    if (icon_id < 0 || icon_id >= DM_MAX_DESKTOP_ICONS || !dm.desktop_icons[icon_id].used) return -1;
    dm.desktop_icons[icon_id].used = 0;
    dm.desktop_icon_count--;
    return 0;
}

int dm_desktop_get_icons(desktop_icon_t *icons, int max)
{
    if (!icons || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DM_MAX_DESKTOP_ICONS && count < max; i++) {
        if (dm.desktop_icons[i].used) memcpy(&icons[count++], &dm.desktop_icons[i], sizeof(desktop_icon_t));
    }
    return count;
}

int dm_workspace_switch(int workspace_id)
{
    if (workspace_id < 0 || workspace_id >= DM_MAX_WORKSPACES) return -1;
    for (int i = 0; i < DM_MAX_WORKSPACES; i++) dm.workspaces[i].active = 0;
    dm.workspaces[workspace_id].active = 1;
    dm.current_workspace = workspace_id;
    return 0;
}

int dm_workspace_move_window(int window_id, int workspace_id)
{
    if (window_id < 0 || window_id >= DM_MAX_WINDOWS || !dm.windows[window_id].used) return -1;
    if (workspace_id < 0 || workspace_id >= DM_MAX_WORKSPACES) return -1;
    dm.windows[window_id].workspace = workspace_id;
    return 0;
}

int dm_get_current_workspace(void) { return dm.current_workspace; }

int dm_list_workspaces(workspace_t *workspaces, int max)
{
    if (!workspaces || max <= 0) return 0;
    int count = (DM_MAX_WORKSPACES < max) ? DM_MAX_WORKSPACES : max;
    memcpy(workspaces, dm.workspaces, sizeof(workspace_t) * count);
    return count;
}

int dm_mouse_move(int x, int y)
{
    dm.mouse.prev_x = dm.mouse.x;
    dm.mouse.prev_y = dm.mouse.y;
    dm.mouse.x = x;
    dm.mouse.y = y;
    return 0;
}

int dm_mouse_button(int button, int pressed)
{
    if (button == 0) dm.mouse.left_button = pressed;
    else if (button == 1) dm.mouse.right_button = pressed;
    else if (button == 2) dm.mouse.middle_button = pressed;
    return 0;
}

