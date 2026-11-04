/* ============================================================================
 * 赫普OS - 窗口管理器：绘制
 * 从 wm.c 拆分（V6.46 代码重构，大文件拆分目标<500行）
 * 包含：draw_widget/wm_draw_window/wm_draw_all
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "wm.h"
#include "widgets.h"
#include "theme.h"
#include "vga_gfx.h"
#include "vga.h"
#include "hepu.h"

/* 依赖 wm.c 中的全局变量和函数 */
extern wm_window_t windows[];
extern wm_window_t *desktop;
extern int next_z;
extern int selected_icon;
extern int wm_sx(int v);
extern int wm_sy(int v);
extern int wm_strlen(const char *s);
extern const char *wm_menu_items[];
extern int wm_menu_open;
extern int wm_menu_x, wm_menu_y;
extern const int wm_menu_item_h;
extern const char *wm_icon_names[];
extern wm_window_t *focus_win;
extern wm_widget_t *focus_widget;
extern void cursor_save_bg(int x, int y);
extern void cursor_restore_bg(int x, int y);
extern void wm_draw_cursor(int x, int y);

void draw_widget(wm_window_t *win, wm_widget_t *wgt)
{
    /* V2.35: 绘制委托给控件库 widgets.c, 颜色统一走 hepu_theme */
    widget_draw(win, wgt, (wgt == focus_widget));
}

void wm_draw_window(wm_window_t *win)
{
    if (!win || !win->visible) return;

    /* 窗口背景 */
    gfx_fillrect(win->x, win->y, win->w, win->h, win->bg_color);

    /* V2.31: 判断是否为最前(焦点)窗口——标题栏用亮蓝高亮 */
    int is_front = 1;
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (windows[i].used && windows[i].visible &&
            &windows[i] != desktop && windows[i].z_order > win->z_order) {
            is_front = 0; break;
        }
    }
    uint8_t title_col = is_front ? 0x09 : win->title_color;  /* 亮蓝 vs 深蓝 */

    /* 标题栏 (高度12) */
    gfx_fillrect(win->x, win->y, win->w, 12, title_col);
    gfx_puts(win->x + 4, win->y + 2, win->title, win->title_text_color);

    /* 关闭按钮 (X, 红色) */
    gfx_fillrect(win->x + win->w - 12, win->y + 1, 10, 10, hepu_theme.close_btn);
    gfx_puts(win->x + win->w - 10, win->y + 2, "X", 0x0F);

    /* V2.31: 最小化按钮 (_ 在 X 左侧, 灰色) */
    gfx_fillrect(win->x + win->w - 24, win->y + 1, 10, 10, hepu_theme.min_btn);
    gfx_puts(win->x + win->w - 22, win->y + 2, "_", 0x0F);

    /* V2.35: 最大化按钮 (□, 在最小化左侧) */
    {
        uint8_t mbg = win->maximized ? hepu_theme.title_active : hepu_theme.min_btn;
        gfx_fillrect(win->x + win->w - 36, win->y + 1, 10, 10, mbg);
        gfx_rect(win->x + win->w - 34, win->y + 3, 6, 6, 0x0F);
    }

    /* 窗口边框 */
    gfx_rect(win->x, win->y, win->w, win->h, WM_COLOR_BORDER);

    /* 绘制控件 */
    for (int i = 0; i < win->widget_count; i++) {
        draw_widget(win, &win->widgets[i]);
    }
    /* V2.38: 窗口自定义内容绘制 (如 Terminal) */
    if (win->draw_cb) win->draw_cb(win);
}

/* 按Z-order排序绘制 */
void wm_draw_all(void)
{
    /* V2.36: 壁纸渐变 (顶部深紫蓝 0x11 -> 底部深蓝 0x01), 替代纯色清屏 */
    for (int y = 0; y < gfx_height - 12; y++) {
        uint8_t col = (y < 94) ? 0x11 : 0x01;
        gfx_hline(0, y, gfx_width, col);
    }
    /* 壁纸格点装饰 (每 20px 淡色横线) */
    for (int y = 20; y < gfx_height - 12; y += 20)
        gfx_hline(0, y, 320, 0x02);

    /* V2.31: 桌面图标——彩色渐变 + 反光 + 内嵌符号 */
    {
        static const uint8_t icon_colors[WM_ICON_COUNT] = {0x09, 0x0A, 0x0C, 0x0D}; /* 蓝/绿/红/紫 */
        int iw = wm_sx(WM_ICON_W);
        int ih = wm_sy(WM_ICON_H);
        int igap = wm_sx(WM_ICON_GAP);
        int iy0 = wm_sy(WM_ICON_Y0);
        for (int i = 0; i < WM_ICON_COUNT; i++) {
            int ix = wm_sx(WM_ICON_X0) + i * (iw + igap);
            int iy = iy0;
            /* 渐变: 上半亮色, 下半暗色 */
            for (int r = 0; r < ih; r++) {
                uint8_t col = icon_colors[i];
                if (r >= ih / 2 && col >= 8) col = (uint8_t)(col - 8);
                gfx_hline(ix, iy + r, iw, col);
            }
            /* 反光 (左上白条) + 边框 */
            gfx_hline(ix + 2, iy + 2, 9, 0x0F);
            gfx_vline(ix + 2, iy + 2, 6, 0x0F);
            gfx_rect(ix, iy, WM_ICON_W, WM_ICON_H, 0x0F);
            /* 内嵌符号 */
            if (i == 0) {           /* Apps: 网格点 */
                gfx_putpixel(ix + 14, iy + 12, 0x0F); gfx_putpixel(ix + 19, iy + 12, 0x0F);
                gfx_putpixel(ix + 14, iy + 18, 0x0F); gfx_putpixel(ix + 19, iy + 18, 0x0F);
            } else if (i == 1) {    /* Files: 文件角标 */
                gfx_fillrect(ix + 12, iy + 10, 9, 12, 0x0F);
                gfx_rect(ix + 12, iy + 10, 9, 12, 0x08);
            } else if (i == 2) {    /* Term: 终端提示符 */
                gfx_puts(ix + 11, iy + 12, "$", 0x0F);
            } else {                /* About: 问号/感叹号 */
                gfx_puts(ix + 13, iy + 10, "i", 0x0F);
            }
            gfx_puts(ix + 4, iy + ih + 4, wm_icon_names[i], 0x0F);
            /* V2.36: 选中图标高亮外框 */
            if (i == selected_icon) {
                gfx_rect(ix - 2, iy - 2, iw + 4, ih + 4, 0x0E);
            }
        }
    }

    /* 按Z-order从低到高绘制窗口 */
    for (int z = -1; z < next_z; z++) {
        for (int i = 0; i < WM_MAX_WINDOWS; i++) {
            if (windows[i].used && windows[i].visible && windows[i].z_order == z) {
                if (&windows[i] != desktop) {
                    wm_draw_window(&windows[i]);
                }
            }
        }
    }

    /* V2.36: 桌面右键菜单 */
    if (wm_menu_open) {
        int mw = 96;
        int mh = WM_MENU_ITEMS * wm_menu_item_h + 4;
        int mx = wm_menu_x, my = wm_menu_y;
        if (mx + mw > gfx_width) mx = gfx_width - mw;
        if (my + mh > gfx_height - 12) my = gfx_height - 12 - mh;
        gfx_fillrect(mx, my, mw, mh, hepu_theme.input_bg);
        gfx_rect(mx, my, mw, mh, hepu_theme.border);
        for (int i = 0; i < WM_MENU_ITEMS; i++) {
            int iy = my + 2 + i * wm_menu_item_h;
            gfx_puts(mx + 6, iy, wm_menu_items[i], hepu_theme.label_text);
            if (i < WM_MENU_ITEMS - 1)
                gfx_hline(mx + 2, iy + wm_menu_item_h - 1, mw - 4, hepu_theme.border);
        }
    }

    /* 任务栏 */
    gfx_fillrect(0, gfx_height - 12, gfx_width, 12, WM_COLOR_TASKBAR);
    gfx_puts(2, 190, "HEPU", 0x0F);   /* 左侧品牌 (4字=32px) */
    extern const char *hepu_kernel_version;

    /* V2.31: 任务栏——打开的窗口按钮(最小化/已开窗口, 点击还原/置顶) */
    int tb_x = 38;
    const int tb_bw = 72;  /* V2.39: 任务栏按钮固定宽度, 可容纳更多窗口 */
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (windows[i].used && &windows[i] != desktop) {
            wm_window_t *w = &windows[i];
            if (tb_x + tb_bw > 238) break;   /* 预留时钟区 (244+) */
            /* V2.39: 最前可见窗口高亮(凹陷态), 最小化窗口深灰 */
            int is_front = 0;
            if (w->visible) {
                is_front = 1;
                for (int k = 0; k < WM_MAX_WINDOWS; k++) {
                    if (windows[k].used && windows[k].visible &&
                        &windows[k] != desktop && windows[k].z_order > w->z_order) {
                        is_front = 0; break;
                    }
                }
            }
            uint8_t bg = is_front ? 0x09 : (w->visible ? 0x01 : 0x08);
            gfx_fillrect(tb_x, 189, tb_bw, 10, bg);
            gfx_rect(tb_x, 189, tb_bw, 10, is_front ? 0x0F : 0x07);
            /* 标题截断到 8 字符 */
            char tb_title[9];
            int ti = 0;
            while (ti < 8 && w->title[ti]) { tb_title[ti] = w->title[ti]; ti++; }
            tb_title[ti] = 0;
            gfx_puts(tb_x + 3, 190, tb_title, 0x0F);
            tb_x += tb_bw + 2;
        }
    }

    /* V2.31: 任务栏时钟 (从 PIT ticks 换算 HH:MM:SS) */
    {
        extern uint32_t pit_get_seconds(void);
        uint32_t sec = pit_get_seconds();
        int hh = (int)(sec / 3600) % 24;
        int mm = (int)((sec % 3600) / 60);
        int ss = (int)(sec % 60);
        char tbuf[12];
        tbuf[0] = '0' + hh / 10; tbuf[1] = '0' + hh % 10;
        tbuf[2] = ':';
        tbuf[3] = '0' + mm / 10; tbuf[4] = '0' + mm % 10;
        tbuf[5] = ':';
        tbuf[6] = '0' + ss / 10; tbuf[7] = '0' + ss % 10;
        tbuf[8] = 0;
        gfx_puts(244, 190, tbuf, 0x07);
        (void)hepu_kernel_version;
    }
}

