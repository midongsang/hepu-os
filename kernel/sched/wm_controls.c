/* ============================================================================
 * 赫普OS - 窗口管理器：高级控件
 * 从 wm.c 拆分（V6.46 代码重构，大文件拆分目标<500行）
 * 包含：wm_add_list/wm_add_checkbox/wm_add_progress/wm_add_scrollbar
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "wm.h"
#include "widgets.h"

/* 依赖 wm.c 中的全局变量 */
extern wm_window_t windows[];
extern int wm_strlen(const char *s);

wm_widget_t *wm_add_list(wm_window_t *win, int x, int y, int w, int h, int item_h)
{
    x = wm_sx(x); y = wm_sy(y); w = wm_sx(w); h = wm_sy(h);
    item_h = wm_sy(item_h);
    if (!win || win->widget_count >= WM_MAX_WIDGETS) return NULL;
    wm_widget_t *wgt = &win->widgets[win->widget_count++];
    wgt->type = WM_WIDGET_LIST;
    wgt->x = x; wgt->y = y; wgt->w = w; wgt->h = h;
    wgt->text[0] = 0;
    wgt->u.list.count = 0;
    wgt->u.list.selected = -1;
    wgt->u.list.scroll = 0;
    wgt->u.list.item_h = (item_h > 0) ? item_h : 10;
    return wgt;
}

wm_widget_t *wm_add_checkbox(wm_window_t *win, int x, int y, const char *text, int checked)
{
    x = wm_sx(x); y = wm_sy(y);
    if (!win || win->widget_count >= WM_MAX_WIDGETS) return NULL;
    wm_widget_t *wgt = &win->widgets[win->widget_count++];
    wgt->type = WM_WIDGET_CHECKBOX;
    wgt->x = x; wgt->y = y; wgt->w = 14 + (int)wm_strlen(text) * 8; wgt->h = 10;
    strncpy(wgt->text, text, 31);
    wgt->u.checked = checked ? 1 : 0;
    return wgt;
}

wm_widget_t *wm_add_progress(wm_window_t *win, int x, int y, int w, int h, int percent)
{
    x = wm_sx(x); y = wm_sy(y); w = wm_sx(w); h = wm_sy(h);
    if (!win || win->widget_count >= WM_MAX_WIDGETS) return NULL;
    wm_widget_t *wgt = &win->widgets[win->widget_count++];
    wgt->type = WM_WIDGET_PROGRESS;
    wgt->x = x; wgt->y = y; wgt->w = w; wgt->h = h;
    wgt->u.progress = percent;
    return wgt;
}

wm_widget_t *wm_add_scrollbar(wm_window_t *win, int x, int y, int w, int h, int max, int pos)
{
    x = wm_sx(x); y = wm_sy(y); w = wm_sx(w); h = wm_sy(h);
    if (!win || win->widget_count >= WM_MAX_WIDGETS) return NULL;
    wm_widget_t *wgt = &win->widgets[win->widget_count++];
    wgt->type = WM_WIDGET_SCROLLBAR;
    wgt->x = x; wgt->y = y; wgt->w = w; wgt->h = h;
    /* 复用 u.list.selected 存 max, u.scroll_pos 存位置 */
    wgt->u.list.selected = (max > 0) ? max : 1;
    wgt->u.scroll_pos = pos;
    return wgt;
}


