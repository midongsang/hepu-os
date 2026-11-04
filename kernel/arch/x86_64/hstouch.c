/* ============================================================================
 * 赫普OS - 多点触控/手势抽象层实现 (V0.13.6 P3)
 * 纯逻辑状态机: down/move/up → 手势 (tap/drag/long_press/pinch/swipe)
 * ========================================================================== */
#include "hstouch.h"

#define HS_LONG_PRESS_MS 500
#define HS_TAP_MAX_DIST  12
#define HS_PINCH_START_GAP 10  /* 双指最小起始距离 */

static hs_touch_point_t *find_point(hs_touch_state_t *st, int id)
{
    for (int i = 0; i < HS_TOUCH_MAX_POINTS; i++) {
        if (st->pts[i].active && st->pts[i].id == id) return &st->pts[i];
    }
    return 0;
}

static int point_dist(int x1, int y1, int x2, int y2)
{
    int dx = x1 - x2, dy = y1 - y2;
    return dx * dx + dy * dy;  /* 平方距离, 避免开方 */
}

static int active_points(hs_touch_state_t *st)
{
    int n = 0;
    for (int i = 0; i < HS_TOUCH_MAX_POINTS; i++) if (st->pts[i].active) n++;
    return n;
}

void hs_touch_reset(hs_touch_state_t *st)
{
    for (int i = 0; i < HS_TOUCH_MAX_POINTS; i++) {
        st->pts[i].active = 0;
        st->pts[i].id = -1;
    }
    st->active_count = 0;
    st->last_gesture = HS_GESTURE_NONE;
    st->pinch_start_dist = 0;
    st->pinch_last_dist = 0;
    st->moved = 0;
}

/* 单指 → tap/drag/long_press; 双指 → pinch/swipe */
hs_gesture_t hs_touch_process(hs_touch_state_t *st, const hs_touch_event_t *ev)
{
    hs_gesture_t g;
    g.gesture = HS_GESTURE_NONE;
    g.pinch_scale = 256;
    g.scroll_delta = 0;
    g.x = ev->x;
    g.y = ev->y;

    if (!st || !ev) return g;

    /* ---- down: 登记触点 ---- */
    if (ev->type == 0) {
        hs_touch_point_t *p = find_point(st, ev->id);
        if (!p) {
            for (int i = 0; i < HS_TOUCH_MAX_POINTS; i++) {
                if (!st->pts[i].active) {
                    p = &st->pts[i];
                    break;
                }
            }
        }
        if (!p) return g;  /* 触点满 */
        p->active = 1;
        p->id = ev->id;
        p->x = p->px = ev->x;
        p->y = p->py = ev->y;
        p->start_x = ev->x;
        p->start_y = ev->y;
        p->down_ticks = ev->ticks;
        st->active_count = active_points(st);

        if (st->active_count == 1) {
            st->moved = 0;
            st->prev_x = ev->x;
            st->prev_y = ev->y;
        } else if (st->active_count == 2) {
            /* 双指: 记录捏合起始距离 */
            hs_touch_point_t *a = 0, *b = 0;
            for (int i = 0; i < HS_TOUCH_MAX_POINTS; i++) {
                if (!st->pts[i].active) continue;
                if (!a) a = &st->pts[i]; else { b = &st->pts[i]; break; }
            }
            if (a && b) {
                int d = point_dist(a->x, a->y, b->x, b->y);
                if (d >= HS_PINCH_START_GAP * HS_PINCH_START_GAP) {
                    st->pinch_start_dist = d;
                    st->pinch_last_dist = d;
                    st->px_center = (a->x + b->x) / 2;
                    st->py_center = (a->y + b->y) / 2;
                } else {
                    st->pinch_start_dist = 0;
                }
            }
        }
        return g;
    }

    /* ---- up: 释放触点, 结算手势 ---- */
    if (ev->type == 2) {
        hs_touch_point_t *p = find_point(st, ev->id);
        int was_active = st->active_count;
        if (p) {
            p->active = 0;
            p->id = -1;
            st->active_count = active_points(st);
        }
        if (was_active == 1) {
            /* 单指抬起: tap / long_press */
            if (!st->moved && st->last_gesture != HS_GESTURE_LONG_PRESS) {
                g.gesture = HS_GESTURE_TAP;
            }
            st->last_gesture = HS_GESTURE_NONE;
        } else if (was_active == 2) {
            /* 双指释放完: 若未产生手势则无 */
            st->pinch_start_dist = 0;
            st->last_gesture = HS_GESTURE_NONE;
        }
        return g;
    }

    /* ---- move: 更新触点 + 手势判定 ---- */
    hs_touch_point_t *p = find_point(st, ev->id);
    if (!p) return g;
    p->px = p->x;
    p->py = p->y;
    p->x = ev->x;
    p->y = ev->y;
    if (ev->x != p->px || ev->y != p->py) st->moved = 1;

    if (st->active_count == 1) {
        /* 单指: 拖动判定 (位移>阈值) */
        int d = point_dist(ev->x, ev->y, st->prev_x, st->prev_y);
        if (d > HS_TAP_MAX_DIST * HS_TAP_MAX_DIST) {
            g.gesture = HS_GESTURE_DRAG;
            g.x = ev->x;
            g.y = ev->y;
        }
        st->prev_x = ev->x;
        st->prev_y = ev->y;
        /* 长按 (累计位移小于阈值且超时, 允许微小抖动) */
        {
            int tot = point_dist(ev->x, ev->y, p->start_x, p->start_y);
            if (tot < HS_TAP_MAX_DIST * HS_TAP_MAX_DIST &&
                ev->ticks - p->down_ticks > HS_LONG_PRESS_MS) {
                g.gesture = HS_GESTURE_LONG_PRESS;
                st->last_gesture = HS_GESTURE_LONG_PRESS;
            }
        }
        return g;
    }

    if (st->active_count == 2 && st->pinch_start_dist > 0) {
        /* 双指: 捏合缩放 + 双指滑动 */
        hs_touch_point_t *a = 0, *b = 0;
        for (int i = 0; i < HS_TOUCH_MAX_POINTS; i++) {
            if (!st->pts[i].active) continue;
            if (!a) a = &st->pts[i]; else { b = &st->pts[i]; break; }
        }
        if (a && b) {
            int d = point_dist(a->x, a->y, b->x, b->y);
            if (d > 0) {
                g.pinch_scale = (d * 256) / st->pinch_start_dist;
                if (g.pinch_scale < 32) g.pinch_scale = 32;
                if (g.pinch_scale > 2048) g.pinch_scale = 2048;
                g.gesture = HS_GESTURE_PINCH;
                g.x = (a->x + b->x) / 2;
                g.y = (a->y + b->y) / 2;
            }
            /* 双指中心垂直滑动 → scroll */
            int cx = (a->x + b->x) / 2;
            int cy = (a->y + b->y) / 2;
            int dcy = cy - st->py_center;
            if (dcy > 2 || dcy < -2) {
                g.scroll_delta = dcy;
                st->py_center = cy;
                st->px_center = cx;
                g.gesture = HS_GESTURE_SWIPE;
            }
        }
        return g;
    }

    return g;
}
