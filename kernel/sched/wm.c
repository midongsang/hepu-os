/* ============================================================================
 * 赫普OS - 窗口管理器
 * ========================================================================== */

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

wm_window_t windows[WM_MAX_WINDOWS];
wm_window_t *desktop = NULL;
int next_z = 0;
int wm_initialized = 0;
/* V2.28: 记录"按下"的窗口和按钮——释放时在同一窗口内即触发回调.
 * 兼容 QEMU headless 鼠标注入(按下后指针会被重置到别处), 真机体验也更宽容. */
wm_window_t *last_down_win = NULL;
wm_widget_t *last_down_widget = NULL;
/* V2.29: 键盘导航焦点——Tab 切换焦点按钮, Enter 激活 */
wm_window_t *focus_win = NULL;
wm_widget_t *focus_widget = NULL;
/* V2.35: 模态窗口 (非空时阻塞底层窗口事件) */
wm_window_t *wm_modal = NULL;
/* V2.35: 标题栏双击检测 (PIT 100Hz) */
uint32_t title_down_tick = 0;
int title_down_x = 0, title_down_y = 0;
/* V2.36: 桌面图标选中/双击 */
int selected_icon = -1;
uint32_t icon_click_tick = 0;
int icon_click_idx = -1;
/* V2.36: 桌面右键菜单 */
const char *wm_menu_items[WM_MENU_ITEMS] =
    {"Terminal", "Files", "Settings", "About", "Shutdown"};
int wm_menu_open = 0;
int wm_menu_x = 0, wm_menu_y = 0;
const int wm_menu_item_h = 14;

/* ============ V2.31: 鼠标箭头光标 + 桌面图标 + 任务栏时钟 ============ */
static uint8_t cursor_backup[16 * 16];
static int last_cx = -1, last_cy = -1;
/* 16x16 箭头指针 (每行一位图, 高位在前) */
static const uint8_t cursor_arrow[16] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF,
    0xFF, 0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01
};

/* 桌面图标定义 (与 wm_draw_all 中的绘制一致) */
/* V2.41: UI 坐标按分辨率缩放 (需 gfx_scale_x/y, 声明见 vga_gfx.h) */
int wm_sx(int v) { extern int gfx_scale_x; return (v * gfx_scale_x) / 100; }
int wm_sy(int v) { extern int gfx_scale_y; return (v * gfx_scale_y) / 100; }

const char *wm_icon_names[WM_ICON_COUNT] = {"Apps", "Files", "Term", "About"};

/* 保存/恢复/绘制鼠标指针 */
void cursor_save_bg(int x, int y)
{
    for (int r = 0; r < 16; r++)
        for (int c = 0; c < 16; c++)
            cursor_backup[r * 16 + c] = gfx_getpixel(x + c, y + r);
}
void cursor_restore_bg(int x, int y)
{
    for (int r = 0; r < 16; r++)
        for (int c = 0; c < 16; c++)
            gfx_putpixel(x + c, y + r, cursor_backup[r * 16 + c]);
}
void wm_draw_cursor(int x, int y)
{
    if (last_cx >= 0) cursor_restore_bg(last_cx, last_cy);
    if (x < 0) x = 0;
    if (x > gfx_width - 16) x = gfx_width - 16;
    if (y < 0) y = 0;
    if (y > gfx_height - 16) y = gfx_height - 16;
    cursor_save_bg(x, y);
    /* 白色描边 + 黑色主体 */
    for (int r = 0; r < 16; r++) {
        uint8_t line = cursor_arrow[r];
        for (int c = 0; c < 8; c++) {
            if (line & (0x80 >> c)) {
                gfx_putpixel(x + c, y + r, GFX_COLOR_BLACK);
                if (c + 1 < 8 && !(line & (0x80 >> (c + 1))))
                    gfx_putpixel(x + c + 1, y + r, GFX_COLOR_WHITE);
            }
        }
    }
    last_cx = x; last_cy = y;
}

/* 返回点击的桌面图标索引, 未点中返回 -1 */
int wm_icon_at(int x, int y)
{
    int y0 = wm_sy(WM_ICON_Y0);
    int iw = wm_sx(WM_ICON_W);
    int ih = wm_sy(WM_ICON_H);
    int gap = wm_sx(WM_ICON_GAP);
    if (y < y0 - 6 || y > y0 + ih + 8) return -1;
    for (int i = 0; i < WM_ICON_COUNT; i++) {
        int ix = wm_sx(WM_ICON_X0) + i * (iw + gap);
        if (x >= ix && x < ix + iw && y >= y0 && y < y0 + ih)
            return i;
    }
    return -1;
}

/* 颜色定义 (V2.35: 全部收口到 hepu_theme) */

/* strlen */
int wm_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

void wm_init(void)
{
    /* V2.35: 主题初始化 (幂等) */
    theme_init();

    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        windows[i].used = 0;
    }
    next_z = 0;
    wm_initialized = 1;

    /* 创建桌面窗口 (全屏) */
    desktop = wm_create_window("Desktop", 0, 0, gfx_width, gfx_height - 12);
    if (desktop) {
        desktop->bg_color = WM_COLOR_DESKTOP;
        desktop->z_order = -1;  /* 桌面在最底层 */
    }

    vga_puts("[WM] WM init done \n");
}

wm_window_t *wm_create_window(const char *title, int x, int y, int w, int h)
{
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (!windows[i].used) {
            wm_window_t *win = &windows[i];
            memset(win, 0, sizeof(wm_window_t));
            win->used = 1;
            win->x = x;
            win->y = y;
            win->w = w;
            win->h = h;
            win->bg_color = WM_COLOR_WINDOW_BG;
            win->title_color = WM_COLOR_TITLEBAR;
            win->title_text_color = WM_COLOR_TITLE_TX;
            win->z_order = next_z++;
            win->visible = 1;
            win->widget_count = 0;
            strncpy(win->title, title, WM_MAX_TITLE - 1);
            return win;
        }
    }
    return NULL;
}

void wm_destroy_window(wm_window_t *win)
{
    if (!win) return;
    win->used = 0;
}

wm_widget_t *wm_add_button(wm_window_t *win, int x, int y, int w, int h,
                            const char *label,
                            void (*on_click)(wm_window_t*, wm_widget_t*))
{
    x = wm_sx(x); y = wm_sy(y); w = wm_sx(w); h = wm_sy(h);
    if (!win || win->widget_count >= WM_MAX_WIDGETS) return NULL;
    wm_widget_t *wgt = &win->widgets[win->widget_count++];
    memset(wgt, 0, sizeof(wm_widget_t));
    wgt->type = WM_WIDGET_BUTTON;
    wgt->x = x;
    wgt->y = y;
    wgt->w = w;
    wgt->h = h;
    strncpy(wgt->text, label, 31);
    wgt->on_click = on_click;
    return wgt;
}

wm_widget_t *wm_add_label(wm_window_t *win, int x, int y, const char *text)
{
    x = wm_sx(x); y = wm_sy(y);
    if (!win || win->widget_count >= WM_MAX_WIDGETS) return NULL;
    wm_widget_t *wgt = &win->widgets[win->widget_count++];
    memset(wgt, 0, sizeof(wm_widget_t));
    wgt->type = WM_WIDGET_LABEL;
    wgt->x = x;
    wgt->y = y;
    strncpy(wgt->text, text, 31);
    return wgt;
}

wm_widget_t *wm_add_input(wm_window_t *win, int x, int y, int w, const char *text)
{
    x = wm_sx(x); y = wm_sy(y); w = wm_sx(w);
    if (!win || win->widget_count >= WM_MAX_WIDGETS) return NULL;
    wm_widget_t *wgt = &win->widgets[win->widget_count++];
    memset(wgt, 0, sizeof(wm_widget_t));
    wgt->type = WM_WIDGET_INPUT;
    wgt->x = x;
    wgt->y = y;
    wgt->w = w;
    wgt->h = 10;
    strncpy(wgt->text, text, 31);
    return wgt;
}

/* 绘制单个控件 */
wm_window_t *wm_window_at(int x, int y)
{
    /* 从高Z-order到低查找 */
    for (int z = next_z - 1; z >= -1; z--) {
        for (int i = 0; i < WM_MAX_WINDOWS; i++) {
            if (windows[i].used && windows[i].visible && windows[i].z_order == z) {
                wm_window_t *win = &windows[i];
                if (x >= win->x && x < win->x + win->w &&
                    y >= win->y && y < win->y + win->h) {
                    return win;
                }
            }
        }
    }
    return NULL;
}

/* 查找窗口内的控件 */
wm_widget_t *widget_at(wm_window_t *win, int x, int y)
{
    int rel_x = x - win->x;
    int rel_y = y - win->y;
    for (int i = 0; i < win->widget_count; i++) {
        wm_widget_t *wgt = &win->widgets[i];
        if (rel_x >= wgt->x && rel_x < wgt->x + wgt->w &&
            rel_y >= wgt->y && rel_y < wgt->y + wgt->h) {
            return wgt;
        }
    }
    return NULL;
}

void wm_bring_to_front(wm_window_t *win)
{
    if (!win) return;
    win->z_order = next_z++;
    /* V2.38: 带到前台的窗口获得键盘焦点 */
    focus_win = win;
    focus_widget = NULL;
}



/* 高级控件已拆分到 wm_controls.c */
wm_window_t *wm_get_desktop(void)
{
    return desktop;
}

/* V2.35: 最大化/还原切换 (工作区 320x188, 不含任务栏) */
void wm_maximize_toggle(wm_window_t *win)
{
    if (!win || !win->used) return;
    if (!win->maximized) {
        win->saved_x = win->x;
        win->saved_y = win->y;
        win->saved_w = win->w;
        win->saved_h = win->h;
        win->x = 0;
        win->y = 0;
        win->w = gfx_width;
        win->h = gfx_height - 12;
        win->maximized = 1;
    } else {
        win->x = win->saved_x;
        win->y = win->saved_y;
        win->w = win->saved_w;
        win->h = win->saved_h;
        win->maximized = 0;
    }
}

/* V2.35: 模态窗口设置——阻塞底层窗口交互 */
void wm_set_modal(wm_window_t *win, int modal)
{
    if (win && win->used) {
        win->modal = modal ? 1 : 0;
    }
    if (modal) {
        wm_modal = win;
        if (win) wm_bring_to_front(win);
    } else if (wm_modal == win) {
        wm_modal = NULL;
    }
}

wm_window_t *wm_get_modal(void)
{
    if (wm_modal && wm_modal->used) return wm_modal;
    return NULL;
}

/* V2.29: Tab——把焦点移到下一个可见按钮(遍历所有可见窗口) */
void wm_focus_next(void)
{
    /* 收集所有可见窗口的按钮, 找当前焦点的下一个 */
    wm_widget_t *first = NULL;
    wm_window_t *first_win = NULL;
    int found_after = 0;
    wm_widget_t *next = NULL;
    wm_window_t *next_win = NULL;

    for (int zi = 0; zi < next_z; zi++) {
        for (int i = 0; i < WM_MAX_WINDOWS; i++) {
            wm_window_t *win = &windows[i];
            if (!win->used || !win->visible || win == desktop) continue;
            for (int j = 0; j < win->widget_count; j++) {
                wm_widget_t *wgt = &win->widgets[j];
                if (wgt->type != WM_WIDGET_BUTTON) continue;
                if (!first) { first = wgt; first_win = win; }
                if (found_after) { next = wgt; next_win = win; break; }
                if (wgt == focus_widget && win == focus_win) found_after = 1;
            }
            if (next) break;
        }
        if (next) break;
    }
    if (next) { focus_widget = next; focus_win = next_win; }
    else if (first) { focus_widget = first; focus_win = first_win; }
}

/* V2.29: Enter——激活当前焦点按钮 */
void wm_activate_focus(void)
{
    if (focus_widget && focus_win && focus_win->used && focus_widget->on_click) {
        focus_widget->on_click(focus_win, focus_widget);
    }
}

/* V4.41-E86: 窗口缩放——PINCH 缩小 / ZOOM 放大 (带边界夹取) */
void wm_resize_window(wm_window_t *win, int dw, int dh)
{
    if (!win || !win->used) return;
    int nw = win->w + dw, nh = win->h + dh;
    if (nw < 80) nw = 80;          /* 最小 80x40 */
    if (nh < 40) nh = 40;
    if (nw > 640) nw = 640;        /* 最大 640x480 (桌面画布) */
    if (nh > 480) nh = 480;
    if (win->x + nw > 640) nw = 640 - win->x;   /* 不越右/下边界 */
    if (win->y + nh > 480) nh = 480 - win->y;
    win->w = nw; win->h = nh;
    if (win->event_cb) {
        wm_event_t evt = { WM_EVENT_RESIZE, 0, 0, 0, 0, 0 };
        win->event_cb(win, &evt);
    }
    wm_draw_all();
}

/* 事件处理已拆分到 wm_event.c */
