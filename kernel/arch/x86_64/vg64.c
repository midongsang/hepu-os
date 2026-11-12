/* vg64.c — HepuOS 统一矢量渲染引擎 (P19.1)
 * 路径(16.16定点) → 贝塞尔细分 → 扫描线填充 → 覆盖率抗锯齿
 * 设计: 参考主流软渲染器 (Skia/Agg 简化版): 
 *   - 路径命令表 (move/line/quad/cubic/close)
 *   - de Casteljau 递归细分至 <1px
 *   - 每扫描线: 全边求交 → 奇偶配对 → 跨度填充
 *   - 边界像素: 亚像素覆盖率 → alpha 混合 (平滑边缘)
 * 全部定点运算, 无浮点依赖, 适配内核 640K 体积约束
 */

#include "vg64.h"

extern void gfx64_put_pixel(int x, int y, uint32_t color);
extern void gfx64_blend_pixel(int x, int y, uint32_t fg, int alpha);

/* ---- 固定容量 (图形路径规模受限于内核静态内存) ---- */
#define VG_MAX_CMD  512
#define VG_MAX_SEG  2048
#define VG_MAX_EDGE 2048

static uint8_t   g_cmd[VG_MAX_CMD];
static int       g_ncmd;
static fixed_t   g_pts[VG_MAX_CMD * 3 * 2];   /* 每命令最多3点 */
static int       g_npts;
static fixed_t   g_cur_x, g_cur_y;            /* 当前笔位 */
static fixed_t   g_start_x, g_start_y;        /* 子路径起点 */

static fixed_t   g_sx0[VG_MAX_SEG], g_sy0[VG_MAX_SEG];
static fixed_t   g_sx1[VG_MAX_SEG], g_sy1[VG_MAX_SEG];
static int       g_nseg;

static fixed_t   g_ey0[VG_MAX_EDGE], g_ey1[VG_MAX_EDGE];
static fixed_t   g_ex[VG_MAX_EDGE],  g_edx[VG_MAX_EDGE];
static int       g_nedge;

static fixed_t   g_ix[VG_MAX_EDGE];           /* 扫描线交点缓存 */

/* 16.16 定点工具 */
#define FIX1  (1 << 16)
static inline fixed_t f2fx(float v)   { return (fixed_t)(v * 65536.0f); }
static inline int fx_int(fixed_t v)   { return (int)(v >> 16); }
static inline int fx_floor(fixed_t v) { return (int)(v >> 16); }
static inline fixed_t fix_mul(fixed_t a, fixed_t b) { return (fixed_t)(((int64_t)a * b) >> 16); }
static inline fixed_t fix_div(fixed_t a, fixed_t b) { if (!b) return 0; return (fixed_t)(((int64_t)a << 16) / b); }
static inline fixed_t fix_avg(fixed_t a, fixed_t b) { return (fixed_t)(((int64_t)a + b) >> 1); }

/* ---- 路径构建 ---- */
void vg_begin(void) { g_ncmd = 0; g_npts = 0; g_cur_x = g_cur_y = g_start_x = g_start_y = 0; }

void vg_move_to(fixed_t x, fixed_t y)
{
    if (g_ncmd >= VG_MAX_CMD) return;
    g_cmd[g_ncmd++] = VG_MOVE;
    g_pts[g_npts++] = x; g_pts[g_npts++] = y;
    g_cur_x = g_start_x = x; g_cur_y = g_start_y = y;
}

void vg_line_to(fixed_t x, fixed_t y)
{
    if (g_ncmd >= VG_MAX_CMD || g_npts + 2 > VG_MAX_CMD * 6) return;
    g_cmd[g_ncmd++] = VG_LINE;
    g_pts[g_npts++] = x; g_pts[g_npts++] = y;
    g_cur_x = x; g_cur_y = y;
}

void vg_quad_to(fixed_t cx, fixed_t cy, fixed_t x, fixed_t y)
{
    if (g_ncmd >= VG_MAX_CMD || g_npts + 4 > VG_MAX_CMD * 6) return;
    g_cmd[g_ncmd++] = VG_QUAD;
    g_pts[g_npts++] = cx; g_pts[g_npts++] = cy;
    g_pts[g_npts++] = x;  g_pts[g_npts++] = y;
    g_cur_x = x; g_cur_y = y;
}

void vg_cubic_to(fixed_t c1x, fixed_t c1y, fixed_t c2x, fixed_t c2y,
                 fixed_t x, fixed_t y)
{
    if (g_ncmd >= VG_MAX_CMD || g_npts + 6 > VG_MAX_CMD * 6) return;
    g_cmd[g_ncmd++] = VG_CUBIC;
    g_pts[g_npts++] = c1x; g_pts[g_npts++] = c1y;
    g_pts[g_npts++] = c2x; g_pts[g_npts++] = c2y;
    g_pts[g_npts++] = x;  g_pts[g_npts++] = y;
    g_cur_x = x; g_cur_y = y;
}

void vg_close(void)
{
    if (g_ncmd >= VG_MAX_CMD) return;
    g_cmd[g_ncmd++] = VG_CLOSE;
    g_cur_x = g_start_x; g_cur_y = g_start_y;
}

/* ---- 线段表 (细分产物) ---- */
static void vg_add_seg(fixed_t x0, fixed_t y0, fixed_t x1, fixed_t y1)
{
    if (g_nseg >= VG_MAX_SEG) return;
    g_sx0[g_nseg] = x0; g_sy0[g_nseg] = y0;
    g_sx1[g_nseg] = x1; g_sy1[g_nseg] = y1;
    g_nseg++;
}

/* 二次贝塞尔细分: 控制点到弦距离 > 1/4px 则对半递归 */
static void vg_flat_quad(fixed_t ax, fixed_t ay, fixed_t cx, fixed_t cy,
                         fixed_t bx, fixed_t by, int depth)
{
    if (depth <= 0) { vg_add_seg(ax, ay, bx, by); return; }
    /* 控制点到弦的偏离判断 */
    fixed_t dx = bx - ax, dy = by - ay;
    /* 点到线距离² ≈ (叉积)²/(弦长²); 用阈值 1/4 px² */
    int64_t cross = (int64_t)(cx - ax) * dy - (int64_t)(cy - ay) * dx;
    int64_t cross2 = cross * cross;
    int64_t len2 = (int64_t)dx * dx + (int64_t)dy * dy;
    /* 若曲线平直 (cross2 <= len2/16, 即距离<1/4px) 或递归过深 */
    if (cross2 * 16 <= len2 + FIX1) {
        vg_add_seg(ax, ay, bx, by);
        return;
    }
    /* de Casteljau: A-C 中点, C-B 中点, 两者中点 */
    fixed_t acx = fix_avg(ax, cx), acy = fix_avg(ay, cy);
    fixed_t cbx = fix_avg(cx, bx), cby = fix_avg(cy, by);
    fixed_t m2x = fix_avg(acx, cbx), m2y = fix_avg(acy, cby);
    vg_flat_quad(ax, ay, acx, acy, m2x, m2y, depth - 1);
    vg_flat_quad(m2x, m2y, cbx, cby, bx, by, depth - 1);
}

/* 三次贝塞尔细分 */
static void vg_flat_cubic(fixed_t ax, fixed_t ay, fixed_t c1x, fixed_t c1y,
                          fixed_t c2x, fixed_t c2y, fixed_t bx, fixed_t by,
                          int depth)
{
    if (depth <= 0) { vg_add_seg(ax, ay, bx, by); return; }
    /* 控制点包络到弦的偏离: 用中点近似 */
    fixed_t dx = bx - ax, dy = by - ay;
    int64_t c1c = (int64_t)(c1x - ax) * dy - (int64_t)(c1y - ay) * dx;
    int64_t c2c = (int64_t)(c2x - ax) * dy - (int64_t)(c2y - ay) * dx;
    int64_t len2 = (int64_t)dx * dx + (int64_t)dy * dy;
    if ((c1c * c1c + c2c * c2c) * 64 <= len2 + FIX1) {
        vg_add_seg(ax, ay, bx, by);
        return;
    }
    /* de Casteljau 中点 */
    fixed_t a1x = fix_avg(ax, c1x),  a1y = fix_avg(ay, c1y);
    fixed_t b1x = fix_avg(c1x, c2x), b1y = fix_avg(c1y, c2y);
    fixed_t c1x2 = fix_avg(c2x, bx),  c1y2 = fix_avg(c2y, by);
    fixed_t a2x = fix_avg(a1x, b1x), a2y = fix_avg(a1y, b1y);
    fixed_t b2x = fix_avg(b1x, c1x2), b2y = fix_avg(b1y, c1y2);
    fixed_t mx = fix_avg(a2x, b2x),  my = fix_avg(a2y, b2y);
    vg_flat_cubic(ax, ay, a1x, a1y, a2x, a2y, mx, my, depth - 1);
    vg_flat_cubic(mx, my, b2x, b2y, c1x2, c1y2, bx, by, depth - 1);
}

/* 细分当前路径 → 线段表 */
static void vg_tessellate(void)
{
    g_nseg = 0;
    fixed_t pen_x = 0, pen_y = 0;
    fixed_t start_x = 0, start_y = 0;
    int have_start = 0;
    int pi = 0;
    for (int i = 0; i < g_ncmd; i++) {
        switch (g_cmd[i]) {
        case VG_MOVE:
            pen_x = g_pts[pi]; pen_y = g_pts[pi + 1]; pi += 2;
            if (!have_start) { start_x = pen_x; start_y = pen_y; have_start = 1; }
            break;
        case VG_LINE:
            vg_add_seg(pen_x, pen_y, g_pts[pi], g_pts[pi + 1]);
            pen_x = g_pts[pi]; pen_y = g_pts[pi + 1]; pi += 2;
            break;
        case VG_QUAD:
            /* 深度 12 时若"平直"判据因 cross² 溢出而恒假, 每段会细分到 4096,
             * 4 条边共 16k 段远超 VG_MAX_SEG(2048) 而被截断 -> 圆弧只剩一截、
             * 形状不闭合 -> 整块不落像素(tab 选中态即此症状)。压到 6 层后
             * 满细分也只有 64 段/曲线, 在 2048 内, 视觉上对 <=40px 圆角足够。 */
            vg_flat_quad(pen_x, pen_y, g_pts[pi], g_pts[pi + 1],
                         g_pts[pi + 2], g_pts[pi + 3], 6);
            pen_x = g_pts[pi + 2]; pen_y = g_pts[pi + 3]; pi += 4;
            break;
        case VG_CUBIC:
            vg_flat_cubic(pen_x, pen_y, g_pts[pi], g_pts[pi + 1],
                          g_pts[pi + 2], g_pts[pi + 3],
                          g_pts[pi + 4], g_pts[pi + 5], 6);
            pen_x = g_pts[pi + 4]; pen_y = g_pts[pi + 5]; pi += 6;
            break;
        case VG_CLOSE:
            /* 闭合: 补上终点回到起点的最后一边 */
            if (have_start)
                vg_add_seg(pen_x, pen_y, start_x, start_y);
            break;
        }
    }
}

/* ---- 边表 (非水平边) ---- */
static void vg_build_edges(void)
{
    g_nedge = 0;
    for (int i = 0; i < g_nseg; i++) {
        fixed_t y0 = g_sy0[i], y1 = g_sy1[i];
        fixed_t x0 = g_sx0[i], x1 = g_sx1[i];
        if (y0 == y1) continue;                /* 水平边忽略 */
        if (y0 > y1) { fixed_t t; t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
        if (g_nedge >= VG_MAX_EDGE) return;
        g_ey0[g_nedge] = y0; g_ey1[g_nedge] = y1;
        g_ex[g_nedge]  = x0;
        g_edx[g_nedge] = fix_div(x1 - x0, y1 - y0);
        g_nedge++;
    }
}

/* ---- 覆盖率混合 (亚像素边缘) ---- */
static void vg_span_fill(int y, fixed_t xa, fixed_t xb, uint32_t color)
{
    /* 端点像素在屏外则整段写 */
    int xa_i = fx_floor(xa);
    int xb_i = fx_floor(xb);
    /* 单像素跨度 */
    if (xb_i <= xa_i) {
        fixed_t cov = xb - xa;
        if (cov <= 0) return;
        int a = (int)((cov >> 8) & 0xFF); if (a > 255) a = 255;
        if (a > 0) gfx64_blend_pixel(xa_i, y, color, a);
        return;
    }
    fixed_t fa = xa & 0xFFFF;
    fixed_t fb = xb & 0xFFFF;
    if (fa) gfx64_blend_pixel(xa_i, y, color, (int)(((FIX1 - fa) * 255) >> 16));
    for (int x = xa_i + 1; x < xb_i; x++)
        gfx64_put_pixel(x, y, color);
    if (fb) gfx64_blend_pixel(xb_i, y, color, (int)((fb * 255) >> 16));
}

/* ---- 扫描线填充 (奇偶规则 + 覆盖率AA) ---- */
void vg_fill(uint32_t color)
{
    vg_tessellate();
    vg_build_edges();
    if (g_nedge == 0) return;
    /* y 范围 */
    fixed_t ymin = g_ey0[0], ymax = g_ey0[0];
    for (int i = 0; i < g_nedge; i++) {
        if (g_ey0[i] < ymin) ymin = g_ey0[i];
        if (g_ey1[i] > ymax) ymax = g_ey1[i];
    }
    int y0i = fx_floor(ymin), y1i = fx_floor(ymax);
    for (int y = y0i; y <= y1i; y++) {
        fixed_t fy = (fixed_t)y << 16;
        /* 收集与扫描线相交的边交点
         * 半开区间采样 [fy, fy+1)：每条边只在其跨过 fy 的扫描线上计一次。
         * 旧的 else-if 分支把 fy1 处的交点也塞进同一行，会让交点数为奇数、
         * 奇偶配对错位，出现整行中间未被填充的横向条纹（圆角矩形/胶囊尤其明显）。 */
        int n = 0;
        for (int i = 0; i < g_nedge; i++) {
            if (g_ey0[i] <= fy && g_ey1[i] > fy) {
                if (n < VG_MAX_EDGE) {
                    g_ix[n++] = g_ex[i] + fix_mul(g_edx[i], fy - g_ey0[i]);
                }
            }
        }
        if (n < 2) continue;
        /* 冒泡排序交点 */
        for (int i = 0; i < n - 1; i++)
            for (int j = 0; j < n - 1 - i; j++)
                if (g_ix[j] > g_ix[j + 1]) {
                    fixed_t t = g_ix[j]; g_ix[j] = g_ix[j + 1]; g_ix[j + 1] = t;
                }
        /* 奇偶配对填充 */
        for (int i = 0; i + 1 < n; i += 2)
            vg_span_fill(y, g_ix[i], g_ix[i + 1], color);
    }
}

#define VG_STROKE_MAX 256
static fixed_t g_ssx0[VG_STROKE_MAX], g_ssy0[VG_STROKE_MAX];
static fixed_t g_ssx1[VG_STROKE_MAX], g_ssy1[VG_STROKE_MAX];

/* ---- 描边: 线段膨胀 (方帽) ---- */
void vg_stroke(uint32_t color, int width)
{
    if (width <= 0) return;
    fixed_t hw = (fixed_t)(width / 2) << 16;
    if (width % 2) hw += 0x8000;                 /* 奇数宽偏移半像素 */
    vg_tessellate();
    /* vg_fill/vg_fill_circle 内部会重建段数组与段计数(污染段数据),
     * 所以先把全部段端点拷到静态副本, 后续循环只用副本 */
    int nseg = g_nseg;
    if (nseg > VG_STROKE_MAX) nseg = VG_STROKE_MAX;
    for (int i = 0; i < nseg; i++) {
        g_ssx0[i] = g_sx0[i]; g_ssy0[i] = g_sy0[i];
        g_ssx1[i] = g_sx1[i]; g_ssy1[i] = g_sy1[i];
    }
    for (int i = 0; i < nseg; i++) {
        fixed_t sx0 = g_ssx0[i], sy0 = g_ssy0[i], sx1 = g_ssx1[i], sy1 = g_ssy1[i];
        /* 端点圆帽 (先画, 在四边形下方, 重叠部分一致) */
        vg_fill_circle(fx_int(sx0), fx_int(sy0), width / 2 + 1, color);
        vg_fill_circle(fx_int(sx1), fx_int(sy1), width / 2 + 1, color);
        /* 线段膨胀四边形 */
        fixed_t dx = sx1 - sx0, dy = sy1 - sy0;
        fixed_t len = math64_sqrt(fix_mul(dx, dx) + fix_mul(dy, dy));
        if (len == 0) continue;
        fixed_t nx = -fix_div(dy, len), ny = fix_div(dx, len);
        fixed_t ox = fix_mul(nx, hw), oy = fix_mul(ny, hw);
        fixed_t x0 = sx0 + ox, y0 = sy0 + oy;
        fixed_t x1 = sx0 - ox, y1 = sy0 - oy;
        fixed_t x2 = sx1 - ox, y2 = sy1 - oy;
        fixed_t x3 = sx1 + ox, y3 = sy1 + oy;
        vg_begin();
        vg_move_to(x0, y0); vg_line_to(x1, y1);
        vg_line_to(x2, y2); vg_line_to(x3, y3); vg_close();
        vg_fill(color);
        vg_begin();
    }
}

/* ---- 快捷图元 ---- */

void vg_fill_round_rect(int x, int y, int w, int h, int r, uint32_t color)
{
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    fixed_t fx0 = (fixed_t)x << 16, fy0 = (fixed_t)y << 16;
    fixed_t fx1 = (fixed_t)(x + w) << 16, fy1 = (fixed_t)(y + h) << 16;
    fixed_t fr = (fixed_t)r << 16;
    /* 圆角: 每角一段二次贝塞尔 (magic ≈ 0.5523 * r) */
    fixed_t k = fix_mul(fr, f2fx(0.5523f));
    vg_begin();
    vg_move_to(fx0 + fr, fy0);
    vg_line_to(fx1 - fr, fy0);
    vg_quad_to(fx1 - fr + k, fy0, fx1, fy0 + fr);
    vg_line_to(fx1, fy1 - fr);
    vg_quad_to(fx1, fy1 - fr + k, fx1 - fr, fy1);
    vg_line_to(fx0 + fr, fy1);
    vg_quad_to(fx0 + fr - k, fy1, fx0, fy1 - fr);
    vg_line_to(fx0, fy0 + fr);
    vg_quad_to(fx0, fy0 + fr - k, fx0 + fr, fy0);
    vg_close();
    vg_fill(color);
}

void vg_stroke_round_rect(int x, int y, int w, int h, int r, int width, uint32_t color)
{
    /* 外轮廓 - 内轮廓: 用外描边 (线段膨胀) 简化实现 */
    vg_begin();
    vg_move_to((fixed_t)(x + r) << 16, (fixed_t)y << 16);
    vg_line_to((fixed_t)(x + w - r) << 16, (fixed_t)y << 16);
    vg_line_to((fixed_t)(x + w) << 16, (fixed_t)(y + r) << 16);
    vg_line_to((fixed_t)(x + w) << 16, (fixed_t)(y + h - r) << 16);
    vg_line_to((fixed_t)(x + w - r) << 16, (fixed_t)(y + h) << 16);
    vg_line_to((fixed_t)(x + r) << 16, (fixed_t)(y + h) << 16);
    vg_line_to((fixed_t)x << 16, (fixed_t)(y + h - r) << 16);
    vg_line_to((fixed_t)x << 16, (fixed_t)(y + r) << 16);
    vg_close();
    vg_stroke(color, width);
}

void vg_fill_circle(int cx, int cy, int r, uint32_t color)
{
    if (r <= 0) return;
    /* 角度步进细分 (每 0.15 rad) */
    fixed_t fc = (fixed_t)cx << 16, fy = (fixed_t)cy << 16;
    fixed_t fr = (fixed_t)r << 16;
    vg_begin();
    vg_move_to(fc + fr, fy);
    for (int a = 1; a < 42; a++) {                 /* 42 段 ≈ 每 0.15 rad */
        fixed_t ca = math64_cos_rad(fix_mul(f2fx(0.15f), a));
        fixed_t sa = math64_sin_rad(fix_mul(f2fx(0.15f), a));
        vg_line_to(fc + fix_mul(fr, ca), fy + fix_mul(fr, sa));
    }
    vg_close();
    vg_fill(color);
}

void vg_stroke_circle(int cx, int cy, int r, int width, uint32_t color)
{
    if (r <= 0) return;
    fixed_t fc = (fixed_t)cx << 16, fy = (fixed_t)cy << 16;
    fixed_t fr = (fixed_t)r << 16;
    vg_begin();
    vg_move_to(fc + fr, fy);
    for (int a = 1; a < 42; a++) {
        fixed_t ca = math64_cos_rad(fix_mul(f2fx(0.15f), a));
        fixed_t sa = math64_sin_rad(fix_mul(f2fx(0.15f), a));
        vg_line_to(fc + fix_mul(fr, ca), fy + fix_mul(fr, sa));
    }
    vg_close();
    vg_stroke(color, width);
}

void vg_fill_ellipse(int cx, int cy, int rx, int ry, uint32_t color)
{
    if (rx <= 0 || ry <= 0) return;
    fixed_t fc = (fixed_t)cx << 16, fy = (fixed_t)cy << 16;
    fixed_t frx = (fixed_t)rx << 16, fry = (fixed_t)ry << 16;
    vg_begin();

    vg_move_to(fc + frx, fy);
    for (int a = 1; a < 42; a++) {
        fixed_t ca = math64_cos_rad(fix_mul(f2fx(0.15f), a));
        fixed_t sa = math64_sin_rad(fix_mul(f2fx(0.15f), a));
        vg_line_to(fc + fix_mul(frx, ca), fy + fix_mul(fry, sa));
    }
    vg_close();
    vg_fill(color);
}

/* P19.5: 椭圆描边 (离散 56 段 + 圆帽) */
void vg_stroke_ellipse(int cx, int cy, int rx, int ry, int width, uint32_t color)
{
    if (rx <= 0 || ry <= 0 || width <= 0) return;
    fixed_t fc = (fixed_t)cx << 16, fy = (fixed_t)cy << 16;
    fixed_t frx = (fixed_t)rx << 16, fry = (fixed_t)ry << 16;
    vg_begin();
    for (int i = 0; i <= 56; i++) {
        int deg = (360 * i) / 56;
        fixed_t px = fc + fix_mul(frx, math64_cos_deg(deg));
        fixed_t py = fy + fix_mul(fry, math64_sin_deg(deg));
        if (i == 0) vg_move_to(px, py); else vg_line_to(px, py);
    }
    vg_close();
    vg_stroke(color, width);
}

/* P19.5: 圆弧描边 (角度制 a0→a1, 顺时针; 圆帽) */
void vg_stroke_arc(int cx, int cy, int r, int a0, int a1, int width, uint32_t color)
{
    if (r <= 0 || width <= 0) return;
    int span = a1 - a0;
    if (span < 0) span += 360;
    if (span == 0) span = 360;
    int N = 24 + span / 15;
    if (N > 96) N = 96;
    fixed_t fc = (fixed_t)cx << 16, fy = (fixed_t)cy << 16;
    fixed_t fr = (fixed_t)r << 16;
    vg_begin();
    for (int i = 0; i <= N; i++) {
        int deg = a0 + (span * i) / N;
        fixed_t px = fc + fix_mul(fr, math64_cos_deg(deg));
        fixed_t py = fy + fix_mul(fr, math64_sin_deg(deg));
        if (i == 0) vg_move_to(px, py); else vg_line_to(px, py);
    }
    vg_stroke(color, width);
}

/* P19.5: 三角形快捷填充 */
void vg_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color)
{
    const int xs[3] = { x0, x1, x2 }, ys[3] = { y0, y1, y2 };
    vg_fill_polygon(xs, ys, 3, color);
}
void vg_fill_polygon(const int *xs, const int *ys, int n, uint32_t color)
{
    if (n < 3) return;
    vg_begin();
    vg_move_to((fixed_t)xs[0] << 16, (fixed_t)ys[0] << 16);
    for (int i = 1; i < n; i++)
        vg_line_to((fixed_t)xs[i] << 16, (fixed_t)ys[i] << 16);
    vg_close();
    vg_fill(color);
}

void vg_fill_arc(int cx, int cy, int r, int a0, int a1, uint32_t color)
{
    /* 饼形扇区: 中心 → 弧线 → 闭合 */
    if (r <= 0) return;
    int sweep = a1 - a0;
    if (sweep <= 0) sweep += 360;
    fixed_t fc = (fixed_t)cx << 16, fy = (fixed_t)cy << 16;
    fixed_t fr = (fixed_t)r << 16;
    vg_begin();
    vg_move_to(fc, fy);
    int steps = sweep / 6; if (steps < 1) steps = 1;
    for (int i = 0; i <= steps; i++) {
        int deg = a0 + sweep * i / steps;
        fixed_t ca = math64_cos_deg(deg), sa = math64_sin_deg(deg);
        vg_line_to(fc + fix_mul(fr, ca), fy + fix_mul(fr, sa));
    }
    vg_close();
    vg_fill(color);
}
