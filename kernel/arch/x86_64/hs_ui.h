#ifndef HS_UI_H
#define HS_UI_H

/* ============================================================================
 * hs_ui —— 最小控件/布局基类 (V0.59.6)
 *
 * 背景: 桌面各界面长期手写坐标(cx + i * SETK(32)), 每处自己算间距与内边距,
 * 直接后果是"同一屏里的两块面板边界差几个像素"(顶栏 15 vs 侧栏 18 那类问题),
 * 而且改一个间距要全文件找常数。这里把"位置/间距"从绘制里剥出来:
 *
 *   容器(box) 负责: 自己的矩形 + 内边距 + 子项间距 + 排列方向
 *   子项只负责: 在"已经算好的矩形"里画自己 (paint 回调拿到的 x/y/w/h 就是最终值)
 *
 * 布局是单遍的(measure → 定位 → paint), 不做约束求解, 也不做重排缓存:
 * 桌面每帧本来就重画, 布局开销可忽略, 简单可预测比"聪明"重要。
 *
 * 现状(两步都做了): 布局统一 + hit-test 已接入。
 * 设置窗口左侧导航栏是第一个走控件命中路径的界面 —— 点击由 hs_ui_hit 判定,
 * 命中区与绘制区共用同一棵树(之前是手算下标的第二套算法)。
 * 仍未做的: 通用事件分发(控件自己订阅/回调点击)、脏区上报、控件状态(按下/悬停)。
 * ==========================================================================*/

#define HS_UI_COL 0   /* 子项竖向排列 */
#define HS_UI_ROW 1   /* 子项横向排列 */

typedef struct hs_ui_box {
    /* 布局结果(绝对坐标, 由 hs_ui_layout 写入) */
    int x, y, w, h;

    /* 布局参数 */
    int pad;        /* 内边距: 内容区 = 自身矩形内缩 pad */
    int gap;        /* 与下一个子项之间的间距 */
    int dir;        /* HS_UI_COL / HS_UI_ROW */

    /* 尺寸策略 */
    int fixed_h;    /* >0: 固定高度(竖向排列时用) */
    int fixed_w;    /* >0: 固定宽度(横向排列时用) */
    int flex;       /* >0: 在横向上按权重分剩余宽度 */

    /* 可选: 由内容决定高度(如多行文本)。返回需要的像素高度。 */
    int (*measure)(struct hs_ui_box *self, int avail_w);

    /* 绘制: 只在 self->x/y/w/h 内画自己, 不要再算偏移 */
    void (*paint)(struct hs_ui_box *self);

    /* 调用方自用的标识(页面序号、控件类型等), 基类不解释 */
    int tag;

    struct hs_ui_box *first, *last, *next;
} hs_ui_box_t;

/* 把 child 追加为 parent 的子项(调用方保证生命周期长于 parent) */
void hs_ui_add(hs_ui_box_t *parent, hs_ui_box_t *child);

/* 初始化一个容器 */
void hs_ui_init_box(hs_ui_box_t *box, int dir, int pad, int gap);

/* 布局 + 绘制: 给定根矩形, 递归算子项矩形, 然后自顶向下 paint */
void hs_ui_layout(hs_ui_box_t *root, int x, int y, int w, int h);

/* 只布局不绘制(需要先量后画的场合) */
void hs_ui_arrange(hs_ui_box_t *root, int x, int y, int w, int h);

/* 命中测试: 返回最上层(最深)命中 box; 未命中返回 0 */
hs_ui_box_t *hs_ui_hit(hs_ui_box_t *root, int px, int py);

#endif
