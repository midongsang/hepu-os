/* ============================================================================
 * 赫普OS - 窗口系统深化
 * 多窗口/分屏/画中画 + 窗口动画 + 转场效果
 * ========================================================================== */

#include <stdint.h>
#include "windowsystem.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"

static window_manager_t wm;

/* 动画插值 */
static float interpolate(float t, anim_interpolator_t interp)
{
    switch (interp) {
        case INTERP_LINEAR:
            return t;
        case INTERP_ACCELERATE:
            return t * t;
        case INTERP_DECELERATE:
            return 1 - (1 - t) * (1 - t);
        case INTERP_ACCELERATE_DECELERATE:
            return (t < 0.5f) ? 2*t*t : 1 - 2*(1-t)*(1-t);
        case INTERP_BOUNCE:
            if (t < 1/2.75f) return 7.5625f*t*t;
            if (t < 2/2.75f) { t -= 1.5f/2.75f; return 7.5625f*t*t + 0.75f; }
            if (t < 2.5f/2.75f) { t -= 2.25f/2.75f; return 7.5625f*t*t + 0.9375f; }
            t -= 2.625f/2.75f; return 7.5625f*t*t + 0.984375f;
        case INTERP_OVERSHOOT:
            t -= 1; return t*t*(2.70158f*t + 1.70158f) + 1;
        default:
            return t;
    }
}

void windowsystem_init(void)
{
    memset(&wm, 0, sizeof(window_manager_t));
    wm.initialized = 1;
    wm.screen_width = 320;
    wm.screen_height = 200;
    wm.focus_window = -1;
    wm.split_mode = SPLIT_NONE;
    wm.split_window_a = -1;
    wm.split_window_b = -1;

    /* 创建桌面窗口 */
    wm.desktop_window = ws_create_window("Desktop", 0, 0, 320, 200,
                                          WINDOW_FLAG_TOUCHABLE | WINDOW_FLAG_SYSTEM);
    ws_show_window(wm.desktop_window);

    vga_puts("[WS] window system deep init done \n");
    vga_printf(" screen: %dx%d, max window: %d\n", wm.screen_width, wm.screen_height, WS_MAX_WINDOWS);
}

int ws_create_window(const char *title, int x, int y, int w, int h, uint32_t flags)
{
    if (!wm.initialized) return -1;
    for (int i = 0; i < WS_MAX_WINDOWS; i++) {
        if (!wm.windows[i].used) {
            memset(&wm.windows[i], 0, sizeof(window_t));
            wm.windows[i].used = 1;
            wm.windows[i].id = i;
            if (title) strncpy(wm.windows[i].title, title, WS_TITLE_LEN - 1);
            wm.windows[i].type = WINDOW_TYPE_NORMAL;
            wm.windows[i].state = WINDOW_STATE_CREATED;
            wm.windows[i].flags = flags;
            wm.windows[i].x = x;
            wm.windows[i].y = y;
            wm.windows[i].width = w;
            wm.windows[i].height = h;
            wm.windows[i].z_order = wm.window_count;
            wm.windows[i].alpha = 1.0f;
            wm.windows[i].scale_x = 1.0f;
            wm.windows[i].scale_y = 1.0f;
            wm.windows[i].parent_id = -1;
            wm.window_count++;
            return i;
        }
    }
    return -1;
}

int ws_destroy_window(int win_id)
{
    if (win_id < 0 || win_id >= WS_MAX_WINDOWS || !wm.windows[win_id].used) return -1;
    if (wm.windows[win_id].on_close) wm.windows[win_id].on_close(win_id);
    wm.windows[win_id].used = 0;
    wm.windows[win_id].state = WINDOW_STATE_DESTROYED;
    wm.window_count--;
    if (wm.focus_window == win_id) wm.focus_window = -1;
    return 0;
}

int ws_show_window(int win_id)
{
    if (win_id < 0 || win_id >= WS_MAX_WINDOWS || !wm.windows[win_id].used) return -1;
    wm.windows[win_id].state = WINDOW_STATE_SHOWN;
    return 0;
}

int ws_hide_window(int win_id)
{
    if (win_id < 0 || win_id >= WS_MAX_WINDOWS || !wm.windows[win_id].used) return -1;
    wm.windows[win_id].state = WINDOW_STATE_HIDDEN;
    return 0;
}

int ws_minimize_window(int win_id)
{
    if (win_id < 0 || win_id >= WS_MAX_WINDOWS || !wm.windows[win_id].used) return -1;
    wm.windows[win_id].saved_x = wm.windows[win_id].x;
    wm.windows[win_id].saved_y = wm.windows[win_id].y;
    wm.windows[win_id].saved_w = wm.windows[win_id].width;
    wm.windows[win_id].saved_h = wm.windows[win_id].height;
    wm.windows[win_id].state = WINDOW_STATE_MINIMIZED;
    return 0;
}

int ws_maximize_window(int win_id)
{
    if (win_id < 0 || win_id >= WS_MAX_WINDOWS || !wm.windows[win_id].used) return -1;
    wm.windows[win_id].saved_x = wm.windows[win_id].x;
    wm.windows[win_id].saved_y = wm.windows[win_id].y;
    wm.windows[win_id].saved_w = wm.windows[win_id].width;
    wm.windows[win_id].saved_h = wm.windows[win_id].height;
    wm.windows[win_id].x = 0;
    wm.windows[win_id].y = 0;
    wm.windows[win_id].width = wm.screen_width;
    wm.windows[win_id].height = wm.screen_height;
    wm.windows[win_id].state = WINDOW_STATE_MAXIMIZED;
    return 0;
}

int ws_restore_window(int win_id)
{
    if (win_id < 0 || win_id >= WS_MAX_WINDOWS || !wm.windows[win_id].used) return -1;
    wm.windows[win_id].x = wm.windows[win_id].saved_x;
    wm.windows[win_id].y = wm.windows[win_id].saved_y;
    wm.windows[win_id].width = wm.windows[win_id].saved_w;
    wm.windows[win_id].height = wm.windows[win_id].saved_h;
    wm.windows[win_id].state = WINDOW_STATE_SHOWN;
    return 0;
}

int ws_fullscreen_window(int win_id)
{
    if (win_id < 0 || win_id >= WS_MAX_WINDOWS || !wm.windows[win_id].used) return -1;
    ws_maximize_window(win_id);
    wm.windows[win_id].state = WINDOW_STATE_FULLSCREEN;
    wm.windows[win_id].flags &= ~WINDOW_FLAG_TITLE_BAR;
    wm.windows[win_id].flags &= ~WINDOW_FLAG_BORDER;
    return 0;
}

int ws_close_window(int win_id)
{
    return ws_destroy_window(win_id);
}

int ws_move_window(int win_id, int x, int y)
{
    if (win_id < 0 || win_id >= WS_MAX_WINDOWS || !wm.windows[win_id].used) return -1;
    wm.windows[win_id].x = x;
    wm.windows[win_id].y = y;
    return 0;
}

int ws_resize_window(int win_id, int w, int h)
{
    if (win_id < 0 || win_id >= WS_MAX_WINDOWS || !wm.windows[win_id].used) return -1;
    if (wm.windows[win_id].on_resize) wm.windows[win_id].on_resize(win_id, w, h);
    wm.windows[win_id].width = w;
    wm.windows[win_id].height = h;
    return 0;
}

int ws_set_focus(int win_id)
{
    if (win_id < 0 || win_id >= WS_MAX_WINDOWS || !wm.windows[win_id].used) return -1;
    int old = wm.focus_window;
    if (old >= 0 && wm.windows[old].used) {
        wm.windows[old].has_focus = 0;
        if (wm.windows[old].on_focus) wm.windows[old].on_focus(old, 0);
    }
    wm.focus_window = win_id;
    wm.windows[win_id].has_focus = 1;
    if (wm.windows[win_id].on_focus) wm.windows[win_id].on_focus(win_id, 1);
    ws_raise_window(win_id);
    return 0;
}

int ws_raise_window(int win_id)
{
    if (win_id < 0 || win_id >= WS_MAX_WINDOWS || !wm.windows[win_id].used) return -1;
    int max_z = 0;
    for (int i = 0; i < WS_MAX_WINDOWS; i++) {
        if (wm.windows[i].used && wm.windows[i].z_order > max_z) max_z = wm.windows[i].z_order;
    }
    wm.windows[win_id].z_order = max_z + 1;
    return 0;
}

int ws_lower_window(int win_id)
{
    if (win_id < 0 || win_id >= WS_MAX_WINDOWS || !wm.windows[win_id].used) return -1;
    wm.windows[win_id].z_order = 0;
    return 0;
}

int ws_set_alpha(int win_id, float alpha)
{
    if (win_id < 0 || win_id >= WS_MAX_WINDOWS || !wm.windows[win_id].used) return -1;
    if (alpha < 0) alpha = 0;
    if (alpha > 1) alpha = 1;
    wm.windows[win_id].alpha = alpha;
    return 0;
}

int ws_set_rotation(int win_id, float angle)
{
    if (win_id < 0 || win_id >= WS_MAX_WINDOWS || !wm.windows[win_id].used) return -1;
    wm.windows[win_id].rotation = angle;
    return 0;
}

int ws_set_scale(int win_id, float sx, float sy)
{
    if (win_id < 0 || win_id >= WS_MAX_WINDOWS || !wm.windows[win_id].used) return -1;
    wm.windows[win_id].scale_x = sx;
    wm.windows[win_id].scale_y = sy;
    return 0;
}

int ws_set_title(int win_id, const char *title)
{
    if (win_id < 0 || win_id >= WS_MAX_WINDOWS || !wm.windows[win_id].used || !title) return -1;
    strncpy(wm.windows[win_id].title, title, WS_TITLE_LEN - 1);
    return 0;
}

int ws_set_buffer(int win_id, void *buffer, uint32_t size)
{
    if (win_id < 0 || win_id >= WS_MAX_WINDOWS || !wm.windows[win_id].used) return -1;
    wm.windows[win_id].buffer = buffer;
    wm.windows[win_id].buffer_size = size;
    return 0;
}

int ws_enter_split_mode(int win_a, int win_b, split_mode_t mode)
{
    if (!wm.initialized) return -1;
    wm.split_mode = mode;
    wm.split_window_a = win_a;
    wm.split_window_b = win_b;

    if (mode == SPLIT_LEFT_RIGHT) {
        ws_move_window(win_a, 0, 0);
        ws_resize_window(win_a, wm.screen_width / 2, wm.screen_height);
        ws_move_window(win_b, wm.screen_width / 2, 0);
        ws_resize_window(win_b, wm.screen_width / 2, wm.screen_height);
    } else if (mode == SPLIT_TOP_BOTTOM) {
        ws_move_window(win_a, 0, 0);
        ws_resize_window(win_a, wm.screen_width, wm.screen_height / 2);
        ws_move_window(win_b, 0, wm.screen_height / 2);
        ws_resize_window(win_b, wm.screen_width, wm.screen_height / 2);
    }
    vga_printf("[WS] split screen: %s\n", mode == SPLIT_LEFT_RIGHT ? " 左右 " : " 下 ");
    return 0;
}

int ws_exit_split_mode(void)
{
    wm.split_mode = SPLIT_NONE;
    wm.split_window_a = -1;
    wm.split_window_b = -1;
    return 0;
}

int ws_get_split_mode(void) { return wm.split_mode; }

int ws_enter_pip(int win_id, int x, int y, int w, int h)
{
    if (win_id < 0 || win_id >= WS_MAX_WINDOWS || !wm.windows[win_id].used) return -1;
    wm.windows[win_id].type = WINDOW_TYPE_PIP;
    ws_move_window(win_id, x, y);
    ws_resize_window(win_id, w, h);
    ws_raise_window(win_id);
    vga_printf("[WS] PiP: window #%d\n", win_id);
    return 0;
}

int ws_exit_pip(int win_id)
{
    if (win_id < 0 || win_id >= WS_MAX_WINDOWS || !wm.windows[win_id].used) return -1;
    wm.windows[win_id].type = WINDOW_TYPE_NORMAL;
    return 0;
}

int ws_start_animation(int win_id, anim_type_t type, int duration,
                       anim_interpolator_t interp, void (*on_complete)(int))
{
    if (win_id < 0 || win_id >= WS_MAX_WINDOWS || !wm.windows[win_id].used) return -1;
    for (int i = 0; i < WS_MAX_ANIMATIONS; i++) {
        if (!wm.animations[i].used) {
            memset(&wm.animations[i], 0, sizeof(window_animation_t));
            wm.animations[i].used = 1;
            wm.animations[i].id = i;
            wm.animations[i].window_id = win_id;
            wm.animations[i].type = type;
            wm.animations[i].interpolator = interp;
            wm.animations[i].duration = duration;
            wm.animations[i].from_x = wm.windows[win_id].x;
            wm.animations[i].from_y = wm.windows[win_id].y;
            wm.animations[i].from_alpha = wm.windows[win_id].alpha;
            wm.animations[i].on_complete = on_complete;
            wm.animation_count++;
            wm.windows[win_id].anim_id = i;
            return i;
        }
    }
    return -1;
}

int ws_cancel_animation(int win_id)
{
    if (win_id < 0 || win_id >= WS_MAX_WINDOWS) return -1;
    int anim_id = wm.windows[win_id].anim_id;
    if (anim_id >= 0 && wm.animations[anim_id].used) {
        wm.animations[anim_id].used = 0;
        wm.animation_count--;
        wm.windows[win_id].anim_id = -1;
    }
    return 0;
}

int ws_animate_window(int win_id, float to_x, float to_y, float to_w, float to_h,
                      float to_alpha, int duration, anim_interpolator_t interp)
{
    int anim_id = ws_start_animation(win_id, ANIM_CUSTOM, duration, interp, NULL);
    if (anim_id < 0) return -1;
    wm.animations[anim_id].to_x = to_x;
    wm.animations[anim_id].to_y = to_y;
    wm.animations[anim_id].to_w = to_w;
    wm.animations[anim_id].to_h = to_h;
    wm.animations[anim_id].to_alpha = to_alpha;
    return anim_id;
}

int ws_transition(int from_win, int to_win, anim_type_t type, int duration)
{
    (void)type;
    if (from_win >= 0) ws_start_animation(from_win, ANIM_FADE_OUT, duration / 2, INTERP_LINEAR, NULL);
    if (to_win >= 0) {
        ws_show_window(to_win);
        ws_set_alpha(to_win, 0);
        ws_start_animation(to_win, ANIM_FADE_IN, duration, INTERP_LINEAR, NULL);
    }
    return 0;
}

int ws_get_window(int win_id, window_t *win)
{
    if (win_id < 0 || win_id >= WS_MAX_WINDOWS || !wm.windows[win_id].used || !win) return -1;
    memcpy(win, &wm.windows[win_id], sizeof(window_t));
    return 0;
}

int ws_get_focus_window(void) { return wm.focus_window; }

int ws_list_windows(window_t *list, int max)
{
    if (!list || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < WS_MAX_WINDOWS && count < max; i++) {
        if (wm.windows[i].used) memcpy(&list[count++], &wm.windows[i], sizeof(window_t));
    }
    return count;
}

int ws_window_at(int x, int y)
{
    int top = -1;
    int top_z = -1;
    for (int i = 0; i < WS_MAX_WINDOWS; i++) {
        if (wm.windows[i].used && wm.windows[i].state == WINDOW_STATE_SHOWN) {
            if (x >= wm.windows[i].x && x < wm.windows[i].x + wm.windows[i].width &&
                y >= wm.windows[i].y && y < wm.windows[i].y + wm.windows[i].height) {
                if (wm.windows[i].z_order > top_z) {
                    top_z = wm.windows[i].z_order;
                    top = i;
                }
            }
        }
    }
    return top;
}

int ws_render(void)
{
    /* 原型阶段: 按Z序渲染所有可见窗口 */
    return wm.window_count;
}

void ws_tick(void)
{
    /* 更新动画 */
    for (int i = 0; i < WS_MAX_ANIMATIONS; i++) {
        if (!wm.animations[i].used) continue;
        wm.animations[i].current_time += 16;  /* ~60fps */
        float t = (float)wm.animations[i].current_time / wm.animations[i].duration;
        if (t >= 1.0f) t = 1.0f;
        float v = interpolate(t, wm.animations[i].interpolator);

        int wid = wm.animations[i].window_id;
        if (wid >= 0 && wm.windows[wid].used) {
            if (wm.animations[i].type == ANIM_FADE_IN) {
                wm.windows[wid].alpha = v;
            } else if (wm.animations[i].type == ANIM_FADE_OUT) {
                wm.windows[wid].alpha = 1 - v;
            } else if (wm.animations[i].type == ANIM_CUSTOM) {
                wm.windows[wid].x = (int)(wm.animations[i].from_x + (wm.animations[i].to_x - wm.animations[i].from_x) * v);
                wm.windows[wid].alpha = wm.animations[i].from_alpha + (wm.animations[i].to_alpha - wm.animations[i].from_alpha) * v;
            }
        }

        if (t >= 1.0f) {
            if (wm.animations[i].on_complete) wm.animations[i].on_complete(wid);
            wm.animations[i].used = 0;
            wm.animation_count--;
        }
    }
}

int ws_window_count(void) { return wm.window_count; }
