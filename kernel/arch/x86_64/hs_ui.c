/* hs_ui.c —— 最小布局基类实现。见 hs_ui.h 顶部的设计说明。
 *
 * 布局规则(刻意保持简单):
 *   竖向: 子项宽度 = 内容区宽; 高度取 measure 或 fixed_h; 依次下排, 间距 gap
 *   横向: 有 fixed_w 的用固定宽, flex 的按权重分剩余宽, 其余均分
 * 越界不裁: 桌面各面板都在屏幕内, 裁切属于"渲染原语"那层(Surface), 不在这里做。
 */

#include "hs_ui.h"

void hs_ui_init_box(hs_ui_box_t *box, int dir, int pad, int gap)
{
    if (!box) return;
    box->x = box->y = box->w = box->h = 0;
    box->pad = pad;
    box->gap = gap;
    box->dir = dir;
    box->fixed_h = 0;
    box->fixed_w = 0;
    box->flex = 0;
    box->measure = 0;
    box->paint = 0;
    box->tag = 0;
    box->first = box->last = box->next = 0;
}

void hs_ui_add(hs_ui_box_t *parent, hs_ui_box_t *child)
{
    if (!parent || !child) return;
    child->next = 0;
    if (!parent->first) {
        parent->first = parent->last = child;
    } else {
        parent->last->next = child;
        parent->last = child;
    }
}

/* 子项需要的高度 */
static int hs_ui_child_h(hs_ui_box_t *c, int avail_w)
{
    if (c->measure) {
        int h = c->measure(c, avail_w);
        if (h > 0) return h;
    }
    if (c->fixed_h > 0) return c->fixed_h;
    return c->h;
}

/* 只定位, 不绘制 */
void hs_ui_arrange(hs_ui_box_t *root, int x, int y, int w, int h)
{
    if (!root) return;
    root->x = x; root->y = y; root->w = w; root->h = h;

    int cx = x + root->pad;
    int cy = y + root->pad;
    int cw = w - root->pad * 2;
    int ch = h - root->pad * 2;
    if (cw < 0) cw = 0;
    if (ch < 0) ch = 0;

    if (root->dir == HS_UI_COL) {
        for (hs_ui_box_t *c = root->first; c; c = c->next) {
            int hh = hs_ui_child_h(c, cw);
            hs_ui_arrange(c, cx, cy, cw, hh);
            cy += hh + root->gap;
        }
    } else {
        /* 横向: 先统计固定宽与 flex 权重 */
        int fixed_total = 0, flex_total = 0, n_auto = 0;
        for (hs_ui_box_t *c = root->first; c; c = c->next) {
            if (c->fixed_w > 0) fixed_total += c->fixed_w;
            else if (c->flex > 0) flex_total += c->flex;
            else n_auto++;
        }
        int gaps = 0;
        for (hs_ui_box_t *c = root->first; c && c->next; c = c->next) gaps += root->gap;
        int remain = cw - fixed_total - gaps;
        if (remain < 0) remain = 0;
        int auto_w = n_auto ? remain / (n_auto + (flex_total > 0 ? 0 : 0)) : 0;
        if (n_auto > 0 && flex_total > 0) auto_w = 0;

        for (hs_ui_box_t *c = root->first; c; c = c->next) {
            int bw;
            if (c->fixed_w > 0) bw = c->fixed_w;
            else if (c->flex > 0) bw = flex_total ? remain * c->flex / flex_total : 0;
            else bw = auto_w;
            int bh = hs_ui_child_h(c, bw);
            if (bh > ch) bh = ch;
            hs_ui_arrange(c, cx, cy, bw, bh);
            cx += bw + root->gap;
        }
    }
}

static void hs_ui_paint_rec(hs_ui_box_t *b)
{
    if (!b) return;
    if (b->paint) b->paint(b);
    for (hs_ui_box_t *c = b->first; c; c = c->next) hs_ui_paint_rec(c);
}

void hs_ui_layout(hs_ui_box_t *root, int x, int y, int w, int h)
{
    hs_ui_arrange(root, x, y, w, h);
    hs_ui_paint_rec(root);
}

/* 深度优先找最深的命中项(子项在父项之上) */
hs_ui_box_t *hs_ui_hit(hs_ui_box_t *root, int px, int py)
{
    if (!root) return 0;
    if (px < root->x || px >= root->x + root->w) return 0;
    if (py < root->y || py >= root->y + root->h) return 0;
    /* 逆序(后面的子项画在上层) */
    hs_ui_box_t *stack[64];
    int n = 0;
    for (hs_ui_box_t *c = root->first; c && n < 64; c = c->next) stack[n++] = c;
    for (int i = n - 1; i >= 0; i--) {
        hs_ui_box_t *r = hs_ui_hit(stack[i], px, py);
        if (r) return r;
    }
    return root;
}
