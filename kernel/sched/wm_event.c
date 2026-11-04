#include <stdint.h>
#include "wm.h"
#include "widgets.h"
#include "theme.h"
#include "vga_gfx.h"
#include "mouse.h"
#include "vga.h"
#include "string.h"
#include "pit.h"
#include "hepu.h"
#include "keyboard.h"
#include "input.h"
/* 依赖 wm.c 中的全局变量 */
extern wm_window_t windows[];
extern wm_window_t *desktop;
extern int next_z;
extern int wm_initialized;
extern wm_window_t *last_down_win;
extern wm_widget_t *last_down_widget;
extern wm_window_t *focus_win;
extern wm_widget_t *focus_widget;
extern wm_window_t *wm_modal;
extern uint32_t title_down_tick;
extern int title_down_x, title_down_y;
extern int selected_icon;
extern uint32_t icon_click_tick;
extern int icon_click_idx;
extern const char *wm_menu_items[];
extern int wm_menu_open;
extern int wm_menu_x, wm_menu_y;
extern const int wm_menu_item_h;
extern int wm_sx(int v);
extern int wm_sy(int v);

/* 依赖 wm.c 中的全局函数 */
extern void cursor_save_bg(int x, int y);
extern void cursor_restore_bg(int x, int y);
extern void wm_draw_cursor(int x, int y);
extern int wm_icon_at(int x, int y);
extern wm_widget_t *widget_at(wm_window_t *win, int x, int y);
extern void wm_focus_next(void);
extern void wm_activate_focus(void);
extern void wm_draw_all(void);
extern void wm_draw_window(wm_window_t *win);
extern void draw_widget(wm_window_t *win, wm_widget_t *wgt);

void wm_handle_event(wm_event_t *event)
{
    if (!event) return;

    if (event->type == WM_EVENT_MOUSE_DOWN) {
        /* V2.36: 桌面右键菜单——优先处理 */
        if (event->button == WM_MOUSE_RIGHT) {
            /* 桌面区域右键 → 打开菜单 */
            wm_window_t *w = wm_window_at(event->x, event->y);
            if (w == desktop || w == NULL) {
                wm_menu_x = event->x;
                wm_menu_y = event->y;
                wm_menu_open = 1;
                return;
            }
            return;
        }
        if (wm_menu_open) {
            int mw = 96;
            int mh = WM_MENU_ITEMS * wm_menu_item_h + 4;
            int mx = wm_menu_x, my = wm_menu_y;
            if (mx + mw > gfx_width) mx = gfx_width - mw;
            if (my + mh > gfx_height - 12) my = gfx_height - 12 - mh;
            if (event->x >= mx && event->x < mx + mw &&
                event->y >= my && event->y < my + mh) {
                int item = (event->y - (my + 2)) / wm_menu_item_h;
                wm_menu_open = 0;
                if (item >= 0 && item < WM_MENU_ITEMS) {
                    extern void desktop_menu_action(int);
                    desktop_menu_action(item);
                }
                return;
            }
            wm_menu_open = 0;  /* 点菜单外: 关闭 */
        }

        /* V2.35: 模态窗口阻塞——外部点击被吞掉 */
        if (wm_modal && wm_modal->used) {
            wm_window_t *m = wm_modal;
            if (!(event->x >= m->x && event->x < m->x + m->w &&
                  event->y >= m->y && event->y < m->y + m->h)) {
                return;
            }
            wm_bring_to_front(m);
        }

        /* V2.31: 任务栏点击——还原/置顶对应窗口 (tx 与 wm_draw_all 的 38 一致) */
        if (event->y >= gfx_height - 12 && event->y < gfx_height) {
            int tx = 38;
            const int tb_bw = 72;
            for (int i = 0; i < WM_MAX_WINDOWS; i++) {
                if (windows[i].used && &windows[i] != desktop) {
                    if (event->x >= tx && event->x < tx + tb_bw) {
                        windows[i].visible = 1;
                        wm_bring_to_front(&windows[i]);
                        return;
                    }
                    tx += tb_bw + 2;
                }
            }
            return;  /* 点任务栏空白区 */
        }

        wm_window_t *win = wm_window_at(event->x, event->y);

        /* V2.31: 桌面图标点击 (点在桌面区域)
         * V2.36: 单击选中, 双击(250ms内同图标)启动 */
        if (win == desktop || win == NULL) {
            int ic = wm_icon_at(event->x, event->y);
            if (ic >= 0) {
                extern void desktop_launch_icon(int);
                extern uint32_t pit_get_ticks(void);
                uint32_t now = pit_get_ticks();
                if (ic == icon_click_idx && now - icon_click_tick < 25) {
                    icon_click_tick = 0;
                    icon_click_idx = -1;
                    desktop_launch_icon(ic);
                } else {
                    selected_icon = ic;
                    icon_click_tick = now;
                    icon_click_idx = ic;
                }
                return;
            }
            /* 点击桌面空白: 取消选中 */
            selected_icon = -1;
            icon_click_idx = -1;
            return;
        }

        if (win && win != desktop) {
            wm_bring_to_front(win);

            /* 检查是否点击标题栏 (拖动) */
            if (event->y < win->y + 12) {
                /* 检查关闭按钮 */
                if (event->x >= win->x + win->w - 12 && event->x < win->x + win->w - 2) {
                    wm_destroy_window(win);
                    return;
                }
                /* V2.31: 最小化按钮 (_ 在 X 左侧) */
                if (event->x >= win->x + win->w - 24 && event->x < win->x + win->w - 14) {
                    win->visible = 0;
                    win->minimized = 1;
                    /* V2.39: 最小化后焦点转移到下一个可见窗口 */
                    wm_window_t *next = NULL;
                    for (int k = 0; k < WM_MAX_WINDOWS; k++) {
                        if (windows[k].used && windows[k].visible &&
                            &windows[k] != desktop && &windows[k] != win) {
                            if (!next || windows[k].z_order > next->z_order)
                                next = &windows[k];
                        }
                    }
                    if (next) { focus_win = next; focus_widget = NULL; }
                    else { focus_win = NULL; focus_widget = NULL; }
                    return;
                }
                /* V2.35: 最大化按钮 (□) */
                if (event->x >= win->x + win->w - 36 && event->x < win->x + win->w - 26) {
                    wm_maximize_toggle(win);
                    return;
                }
                /* V2.35: 标题栏双击 → 最大化/还原 */
                {
                    extern uint32_t pit_get_ticks(void);
                    uint32_t now = pit_get_ticks();
                    if (title_down_tick != 0 &&
                        now - title_down_tick < 25 &&
                        event->x >= title_down_x - 8 && event->x <= title_down_x + 8 &&
                        event->y >= title_down_y - 8 && event->y <= title_down_y + 8) {
                        title_down_tick = 0;
                        wm_maximize_toggle(win);
                        return;
                    }
                    title_down_tick = now;
                    title_down_x = event->x;
                    title_down_y = event->y;
                }
                /* V2.39: 点击标题栏置顶窗口 */
                wm_bring_to_front(win);
                win->dragging = 1;
                win->drag_offset_x = event->x - win->x;
                win->drag_offset_y = event->y - win->y;
            } else {
                /* V2.39: 点击窗口内容区置顶 */
                wm_bring_to_front(win);
                /* V2.35: 控件点击——按钮/输入/列表/复选/滚动条 */
                wm_widget_t *wgt = widget_at(win, event->x, event->y);
                if (wgt) {
                    switch (wgt->type) {
                        case WM_WIDGET_BUTTON:
                            wgt->pressed = 1;
                            last_down_win = win;
                            last_down_widget = wgt;
                            break;
                        case WM_WIDGET_INPUT:
                            wgt->focused = 1;
                            if (wgt->on_click) wgt->on_click(win, wgt);
                            break;
                        case WM_WIDGET_LIST: {
                            int idx = widget_list_click(wgt, win->x, win->y,
                                                        event->x, event->y);
                            if (idx >= 0 && wgt->on_click) wgt->on_click(win, wgt);
                            break;
                        }
                        case WM_WIDGET_CHECKBOX:
                            wgt->u.checked = wgt->u.checked ? 0 : 1;
                            if (wgt->on_click) wgt->on_click(win, wgt);
                            break;
                        case WM_WIDGET_SCROLLBAR:
                            widget_scrollbar_click(wgt, win->x, win->y,
                                                   event->x, event->y);
                            if (wgt->on_click) wgt->on_click(win, wgt);
                            break;
                        default:
                            break;
                    }
                }
            }
        }
    }
    else if (event->type == WM_EVENT_MOUSE_UP) {
        for (int i = 0; i < WM_MAX_WINDOWS; i++) {
            wm_window_t *win = &windows[i];
            if (!win->used) continue;

            if (win->dragging) {
                win->dragging = 0;
            }

            /* 按钮释放 (V2.28: 按下时记录的按钮, 释放时同窗口内即触发) */
            if (last_down_widget && last_down_win && last_down_win->used) {
                last_down_widget->pressed = 0;
                /* 释放点在按下按钮所在窗口内即触发——兼容 QEMU 鼠标重置 */
                wm_window_t *up_win = wm_window_at(event->x, event->y);
                if (up_win == last_down_win || up_win == NULL) {
                    if (last_down_widget->on_click) {
                        last_down_widget->on_click(last_down_win, last_down_widget);
                    }
                }
            }
            last_down_win = NULL;
            last_down_widget = NULL;
        }
    }
    else if (event->type == WM_EVENT_MOUSE_MOVE) {
        for (int i = 0; i < WM_MAX_WINDOWS; i++) {
            wm_window_t *win = &windows[i];
            if (!win->used) continue;

            if (win->dragging) {
                win->x = event->x - win->drag_offset_x;
                win->y = event->y - win->drag_offset_y;
                /* 限制在屏幕内 */
                if (win->x < 0) win->x = 0;
                if (win->y < 0) win->y = 0;
                if (win->x + win->w > gfx_width) win->x = gfx_width - win->w;
                if (win->y + win->h > gfx_height - 12) win->y = gfx_height - 12 - win->h;
            }
        }
    }

    /* 调用窗口自定义事件回调 */
    wm_window_t *front = wm_window_at(event->x, event->y);
    if (front && front->event_cb) {
        front->event_cb(front, event);
    }
}

void wm_handle_gesture(gesture_event_t *ge)
{
    extern void serial_puts(unsigned short, const char *);
    if (!ge) return;
    switch (ge->type) {
        case GESTURE_SWIPE_LEFT:
        case GESTURE_SWIPE_RIGHT:
            wm_focus_next();
            wm_draw_all();
            vga_printf("[WM] SWIPE -> focus next\n");
            serial_puts(0x3F8, "[WM] focus next (gesture swipe)\n");
            break;
        case GESTURE_DOUBLE_TAP:
            wm_activate_focus();
            vga_printf("[WM] DOUBLE_TAP -> activate\n");
            serial_puts(0x3F8, "[WM] activate (gesture double-tap)\n");
            break;
        case GESTURE_PINCH:
            if (focus_win && focus_win != desktop) {
                wm_resize_window(focus_win, -30, -20);
                vga_printf("[WM] PINCH -> shrink focus win (%dx%d)\n", focus_win->w, focus_win->h);
                serial_puts(0x3F8, "[WM] PINCH -> shrink focus win\n");
            } else {
                vga_printf("[WM] PINCH: no focus window (resize needs GUI focus)\n");
                serial_puts(0x3F8, "[WM] PINCH: no focus window\n");
            }
            break;
        case GESTURE_ZOOM:
            if (focus_win && focus_win != desktop) {
                wm_resize_window(focus_win, 30, 20);
                vga_printf("[WM] ZOOM -> grow focus win (%dx%d)\n", focus_win->w, focus_win->h);
                serial_puts(0x3F8, "[WM] ZOOM -> grow focus win\n");
            } else {
                vga_printf("[WM] ZOOM: no focus window (resize needs GUI focus)\n");
                serial_puts(0x3F8, "[WM] ZOOM: no focus window\n");
            }
            break;
        case GESTURE_LONG_PRESS:
            vga_printf("[WM] LONG_PRESS -> context menu slot\n");
            serial_puts(0x3F8, "[WM] LONG_PRESS -> context menu slot\n");
            break;
        default:
            break;
    }
}

void wm_main_loop(void)
{
    extern void serial_puts(unsigned short, const char *);
    serial_puts(0x3F8, "[WM] enter WM loop\n");
    vga_puts("[WM] enter WM ...\n");

    static int last_buttons = 0;

    while (1) {
        /* V2.32: 确保中断开启 (VGA 图形模式切换后, 保证键盘/鼠标中断可到达) */
        __asm__ volatile ("sti");
        /* V2.29: 键盘导航——Tab 切焦点, Enter 激活焦点按钮, 'x' 关焦点窗口
         * V2.38: 可打印字符路由到焦点窗口 event_cb (Terminal 输入) */
        {
            char kc;
            while (keyboard_try_getchar(&kc)) {
                if (kc == 0x09) {           /* Tab */
                    wm_focus_next();
                } else if (kc == '\n' || kc == '\r') {  /* Enter */
                    wm_window_t *fw = focus_win;
                    wm_widget_t *f = focus_widget;
                    if (f && f->type == WM_WIDGET_BUTTON && f->on_click)
                        f->on_click(fw, f);
                    else if (fw && fw->event_cb) {
                        wm_event_t evt = { WM_EVENT_KEY_DOWN, 0, 0, 0, kc, 0 };
                        fw->event_cb(fw, &evt);
                    } else wm_activate_focus();
                } else if (kc == 'x' || kc == 'X') {
                    wm_window_t *top = NULL;
                    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
                        if (windows[i].used && windows[i].visible && &windows[i] != desktop) {
                            if (!top || windows[i].z_order > top->z_order) top = &windows[i];
                        }
                    }
                    if (top) wm_destroy_window(top);
                    focus_win = NULL; focus_widget = NULL;
                } else if (kc == '1') {
                    /* V4.40-E85: 调试键——注入右滑手势 */
                    extern int touch_sim_inject(int, int, int);
                    touch_sim_inject(100, 100, 1);
                    touch_sim_inject(450, 100, 2);
                    touch_sim_inject(450, 100, 0);
                } else if (kc == '2') {
                    extern int touch_sim_inject(int, int, int);
                    touch_sim_inject(160, 120, 1);
                    touch_sim_inject(160, 120, 0);
                    touch_sim_inject(160, 120, 1);
                    touch_sim_inject(160, 120, 0);
                } else if (kc == '3') {
                    extern int touch_sim_inject_slot(int, int, int, int);
                    touch_sim_inject_slot(0, 200, 200, 1);
                    touch_sim_inject_slot(1, 300, 200, 1);
                    touch_sim_inject_slot(1, 400, 200, 1);
                    touch_sim_inject_slot(0, -1, 0, 0);
                    touch_sim_inject_slot(1, -1, 0, 0);
                } else if (kc >= 0x20 && kc < 0x7F) {  /* 可打印字符 */
                    wm_window_t *fw = focus_win;
                    if (fw && fw->event_cb) {
                        wm_event_t evt = { WM_EVENT_KEY_DOWN, 0, 0, 0, kc, 0 };
                        fw->event_cb(fw, &evt);
                    }
                } else if (kc == 0x08) {  /* Backspace */
                    wm_window_t *fw = focus_win;
                    if (fw && fw->event_cb) {
                        wm_event_t evt = { WM_EVENT_KEY_DOWN, 0, 0, 0, kc, 0 };
                        fw->event_cb(fw, &evt);
                    }
                } else if (kc == '1') {
                    /* V4.40-E85: 调试键——注入右滑手势 */
                    extern int touch_sim_inject(int, int, int);
                    touch_sim_inject(100, 100, 1);
                    touch_sim_inject(450, 100, 2);
                    touch_sim_inject(450, 100, 0);
                } else if (kc == '2') {
                    extern int touch_sim_inject(int, int, int);
                    touch_sim_inject(160, 120, 1);
                    touch_sim_inject(160, 120, 0);
                    touch_sim_inject(160, 120, 1);
                    touch_sim_inject(160, 120, 0);
                } else if (kc == '3') {
                    extern int touch_sim_inject_slot(int, int, int, int);
                    touch_sim_inject_slot(0, 200, 200, 1);
                    touch_sim_inject_slot(1, 300, 200, 1);
                    touch_sim_inject_slot(1, 400, 200, 1);
                    touch_sim_inject_slot(0, -1, 0, 0);
                    touch_sim_inject_slot(1, -1, 0, 0);
                }
            }
        }

        /* V4.40-E85: 触屏手势轮询——滑动切焦点/双击激活/双指缩放演示 */
        {
            extern int input_gesture_poll(gesture_event_t *);
            gesture_event_t ge;
            while (input_gesture_poll(&ge))
                wm_handle_gesture(&ge);
        }

        int mx = mouse_get_x();
        int my = mouse_get_y();
        uint8_t buttons = mouse_get_buttons();

        /* 鼠标按下 */
        if ((buttons & WM_MOUSE_LEFT) && !(last_buttons & WM_MOUSE_LEFT)) {
            wm_event_t evt = { WM_EVENT_MOUSE_DOWN, mx, my, WM_MOUSE_LEFT, 0, 0 };
            wm_handle_event(&evt);
        }

        /* 鼠标释放 */
        if (!(buttons & WM_MOUSE_LEFT) && (last_buttons & WM_MOUSE_LEFT)) {
            wm_event_t evt = { WM_EVENT_MOUSE_UP, mx, my, WM_MOUSE_LEFT, 0, 0 };
            wm_handle_event(&evt);
        }

        /* V2.36: 鼠标右键按下 (桌面菜单) */
        if ((buttons & WM_MOUSE_RIGHT) && !(last_buttons & WM_MOUSE_RIGHT)) {
            wm_event_t evt = { WM_EVENT_MOUSE_DOWN, mx, my, WM_MOUSE_RIGHT, 0, 0 };
            wm_handle_event(&evt);
        }

        /* 鼠标移动 */
        wm_event_t move_evt = { WM_EVENT_MOUSE_MOVE, mx, my, buttons, 0, 0 };
        wm_handle_event(&move_evt);

        last_buttons = buttons;

        /* 重绘 */
        wm_draw_all();

        /* V2.31: 真正的 16x16 箭头光标 (带背景保存/恢复) */
        wm_draw_cursor(mx, my);

        /* V2.28修复: 每帧把 back_buffer 提交到显存(gfx_flip 带垂直同步).
         * 修复前从不 flip——屏幕显示的是进入图形模式后的显存残留垃圾,
         * 表现为"界面一直闪、无法操作". 双缓冲必须 flip 才可见. */
        gfx_flip();

        /* 简单延时 */
        for (volatile int i = 0; i < 500; i++);
    }
}
