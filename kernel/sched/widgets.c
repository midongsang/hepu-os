/* ============================================================================
 * 赫普OS - 控件库 (V2.35)
 * 统一绘制 button/label/input/list/checkbox/progress/scrollbar,
 * 颜色全部来自 hepu_theme, 支持按压/焦点/禁用视觉。
 * ========================================================================== */

#include "widgets.h"
#include "theme.h"
#include "vga_gfx.h"

/* 字符宽度 (8x8 点阵) */
static int w_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

/* 绘制一个按钮 (按压内缩 + 焦点高亮 + 居中文字) */
static void draw_button(wm_window_t *win, wm_widget_t *wgt, int is_focused)
{
    int x = win->x + wgt->x;
    int y = win->y + wgt->y;
    int w = wgt->w, h = wgt->h;

    uint8_t bg = wgt->pressed ? hepu_theme.button_press : hepu_theme.button;
    gfx_fillrect(x, y, w, h, bg);
    gfx_rect(x, y, w, h, hepu_theme.border);
    /* 按压内缩: 下边/右边加暗影, 模拟 3D 凹陷 */
    if (wgt->pressed) {
        gfx_hline(x + 1, y + h - 1, w - 2, hepu_theme.button_press);
        gfx_vline(x + w - 1, y + 1, h - 2, hepu_theme.button_press);
    } else {
        gfx_hline(x + 1, y + h - 1, w - 2, hepu_theme.border);
    }
    /* 焦点: 黄色外框 (V2.29 保留) */
    if (is_focused) {
        gfx_rect(x - 1, y - 1, w + 2, h + 2, hepu_theme.focus);
    }
    int tx = x + (w - w_strlen(wgt->text) * 8) / 2;
    int ty = y + (h - 8) / 2;
    gfx_puts(tx, ty, wgt->text, hepu_theme.button_text);
}

/* 检测字符串是否含 UTF-8 多字节 (中文) */
static int label_has_zh(const char *s)
{
    while (s && *s) {
        if ((unsigned char)*s >= 0x80) return 1;
        s++;
    }
    return 0;
}

/* 标签: 可多行 (\n 换行), V2.36 支持中文 (16x16 自动检测) */
static void draw_label(wm_window_t *win, wm_widget_t *wgt)
{
    int x = win->x + wgt->x;
    int y = win->y + wgt->y;
    int row = 0;
    const char *p = wgt->text;
    int zh = label_has_zh(wgt->text);
    int line_h = zh ? 16 : 8;
    while (*p) {
        const char *nl = p;
        while (*nl && *nl != '\n') nl++;
        int len = (int)(nl - p);
        char tmp[48];
        if (len > 47) len = 47;
        for (int i = 0; i < len; i++) tmp[i] = p[i];
        tmp[len] = 0;
        if (zh) {
            extern int gfx_puts_zh(int, int, const char *, uint8_t);
            gfx_puts_zh(x, y + row * line_h, tmp, hepu_theme.label_text);
        } else {
            gfx_puts(x, y + row * line_h, tmp, hepu_theme.label_text);
        }
        row++;
        if (*nl == '\n') p = nl + 1;
        else break;
        if (row > 30) break;
    }
}

/* 输入框: 白底 + 焦点蓝框 + 闪烁光标 */
static void draw_input(wm_window_t *win, wm_widget_t *wgt, int is_focused)
{
    int x = win->x + wgt->x;
    int y = win->y + wgt->y;
    gfx_fillrect(x, y, wgt->w, wgt->h, hepu_theme.input_bg);
    if (is_focused || wgt->focused)
        gfx_rect(x, y, wgt->w, wgt->h, hepu_theme.focus);
    else
        gfx_rect(x, y, wgt->w, wgt->h, hepu_theme.border);
    /* 截断超长文本 */
    char buf[64];
    int len = w_strlen(wgt->text);
    int maxch = (wgt->w - 4) / 8;
    if (maxch < 1) maxch = 1;
    if (len > maxch) {
        for (int i = 0; i < maxch; i++) buf[i] = wgt->text[i];
        buf[maxch] = 0;
    } else {
        for (int i = 0; i < len; i++) buf[i] = wgt->text[i];
        buf[len] = 0;
    }
    gfx_puts(x + 2, y + 1, buf, hepu_theme.input_text);
    if (wgt->focused) {
        int cx = x + 2 + (len > maxch ? maxch : len) * 8;
        gfx_vline(cx, y + 1, wgt->h - 2, hepu_theme.input_text);
    }
}

/* 列表: 条目 + 选中高亮 + 滚动 */
static void draw_list(wm_window_t *win, wm_widget_t *wgt)
{
    int x = win->x + wgt->x;
    int y = win->y + wgt->y;
    gfx_fillrect(x, y, wgt->w, wgt->h, hepu_theme.input_bg);
    gfx_rect(x, y, wgt->w, wgt->h, hepu_theme.border);

    wm_list_data_t *ld = &wgt->u.list;
    if (ld->item_h <= 0) ld->item_h = 10;
    int vis_rows = wgt->h / ld->item_h;
    if (vis_rows < 1) vis_rows = 1;

    for (int i = 0; i < vis_rows; i++) {
        int idx = ld->scroll + i;
        if (idx >= ld->count) break;
        int ry = y + i * ld->item_h;
        if (idx == ld->selected) {
            gfx_fillrect(x + 1, ry + 1, wgt->w - 2, ld->item_h - 2, hepu_theme.select);
            gfx_puts(x + 2, ry + 1, ld->items[idx], hepu_theme.select_text);
        } else {
            gfx_puts(x + 2, ry + 1, ld->items[idx], hepu_theme.label_text);
        }
    }
}

/* 复选框: 方框 + 勾选 + 文本 */
static void draw_checkbox(wm_window_t *win, wm_widget_t *wgt)
{
    int x = win->x + wgt->x;
    int y = win->y + wgt->y;
    gfx_rect(x, y, 10, 10, hepu_theme.border);
    if (wgt->u.checked) {
        /* 画勾: 三条线段 */
        gfx_hline(x + 1, y + 5, 3, hepu_theme.button_text);
        gfx_hline(x + 4, y + 8, 4, hepu_theme.button_text);
        gfx_vline(x + 7, y + 3, 5, hepu_theme.button_text);
    }
    gfx_puts(x + 14, y, wgt->text, hepu_theme.label_text);
}

/* 进度条: 底色 + 填充 + 百分比文字 */
static void draw_progress(wm_window_t *win, wm_widget_t *wgt)
{
    int x = win->x + wgt->x;
    int y = win->y + wgt->y;
    gfx_fillrect(x, y, wgt->w, wgt->h, hepu_theme.input_bg);
    gfx_rect(x, y, wgt->w, wgt->h, hepu_theme.border);
    int pct = wgt->u.progress;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int fw = (wgt->w - 2) * pct / 100;
    if (fw > 0)
        gfx_fillrect(x + 1, y + 1, fw, wgt->h - 2, hepu_theme.ok);
    /* 居中百分比 */
    char buf[8];
    int n = 0;
    int v = pct;
    do { buf[n++] = '0' + (v % 10); v /= 10; } while (v);
    buf[n++] = '%';
    for (int i = 0; i < n / 2; i++) { char t = buf[i]; buf[i] = buf[n-1-i]; buf[n-1-i] = t; }
    buf[n] = 0;
    int tx = x + (wgt->w - n * 8) / 2;
    int ty = y + (wgt->h - 8) / 2;
    gfx_puts(tx, ty, buf, hepu_theme.input_text);
}

/* 滚动条: 轨道 + 滑块 */
static void draw_scrollbar(wm_window_t *win, wm_widget_t *wgt)
{
    int x = win->x + wgt->x;
    int y = win->y + wgt->y;
    gfx_fillrect(x, y, wgt->w, wgt->h, hepu_theme.taskbar);
    gfx_rect(x, y, wgt->w, wgt->h, hepu_theme.border);
    int max = wgt->u.list.selected;  /* 复用: max 值暂存 selected */
    if (max < 1) max = 1;
    int pos = wgt->u.scroll_pos;
    if (pos < 0) pos = 0;
    if (pos > max) pos = max;
    /* 滑块高 = 轨道 * 30% */
    int sh = wgt->h / 3;
    if (sh < 4) sh = 4;
    int sy = y + (wgt->h - sh) * pos / max;
    gfx_fillrect(x + 1, sy, wgt->w - 2, sh, hepu_theme.button);
}

/* 对外: 绘制一个控件 */
void widget_draw(wm_window_t *win, wm_widget_t *wgt, int is_focused)
{
    if (!win || !wgt) return;
    switch (wgt->type) {
        case WM_WIDGET_BUTTON:    draw_button(win, wgt, is_focused); break;
        case WM_WIDGET_LABEL:     draw_label(win, wgt); break;
        case WM_WIDGET_INPUT:     draw_input(win, wgt, is_focused); break;
        case WM_WIDGET_LIST:      draw_list(win, wgt); break;
        case WM_WIDGET_CHECKBOX:  draw_checkbox(win, wgt); break;
        case WM_WIDGET_PROGRESS:  draw_progress(win, wgt); break;
        case WM_WIDGET_SCROLLBAR: draw_scrollbar(win, wgt); break;
    }
}

/* 命中测试 */
int widget_hit(wm_widget_t *wgt, int wx, int wy, int x, int y)
{
    return (x >= wx + wgt->x && x < wx + wgt->x + wgt->w &&
            y >= wy + wgt->y && y < wy + wgt->y + wgt->h);
}

/* 列表点击: 计算选中的条目 */
int widget_list_click(wm_widget_t *wgt, int wx, int wy, int x, int y)
{
    if (!widget_hit(wgt, wx, wy, x, y)) return -1;
    wm_list_data_t *ld = &wgt->u.list;
    if (ld->item_h <= 0) ld->item_h = 10;
    int row = (y - (wy + wgt->y)) / ld->item_h;
    int idx = ld->scroll + row;
    if (idx >= 0 && idx < ld->count) {
        ld->selected = idx;
        return idx;
    }
    return -1;
}

/* 滚动条点击: 设置滑块位置 */
void widget_scrollbar_click(wm_widget_t *wgt, int wx, int wy, int x, int y)
{
    int max = wgt->u.list.selected;
    if (max < 1) max = 1;
    int rel = y - (wy + wgt->y);
    int track = wgt->h;
    if (track <= 0) track = 1;
    int pos = rel * max / track;
    if (pos < 0) pos = 0;
    if (pos > max) pos = max;
    wgt->u.scroll_pos = pos;
}
