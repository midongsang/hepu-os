#ifndef WIDGETS_H
#define WIDGETS_H

#include "wm.h"

/*
 * 赫普OS 控件库 (V2.35)
 * 负责所有控件(wm_widget_t)的绘制与命中, 与窗口管理器解耦。
 * 绘制颜色全部来自 hepu_theme。
 */

/* 绘制单个控件 (is_focused: 是否为键盘焦点控件) */
void widget_draw(wm_window_t *win, wm_widget_t *wgt, int is_focused);

/* 控件命中测试: 坐标在控件内返回 1 */
int widget_hit(wm_widget_t *wgt, int wx, int wy, int x, int y);

/* 列表点击: 根据鼠标 y 设置选中项, 返回选中条目索引或 -1 */
int widget_list_click(wm_widget_t *wgt, int wx, int wy, int x, int y);

/* 滚动条点击: 设置滑块位置 */
void widget_scrollbar_click(wm_widget_t *wgt, int wx, int wy, int x, int y);

#endif /* WIDGETS_H */
