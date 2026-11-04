/* vg64.h — HepuOS 统一矢量渲染引擎 (P19.1)
 * 路径(定点16.16) + 贝塞尔细分 + 扫描线填充 + 覆盖率抗锯齿
 * 所有图形图元统一走此管线: 填充/描边/圆角/圆/椭圆/弧/多边形/任意贝塞尔路径
 */
#ifndef VG64_H
#define VG64_H

#include <stdint.h>
#include "math64.h"

/* ---- 路径段命令 ---- */
#define VG_MOVE  0
#define VG_LINE  1
#define VG_QUAD  2    /* 后随2点: ctrl, end */
#define VG_CUBIC 3    /* 后随3点: c1, c2, end */
#define VG_CLOSE 4

/* ---- 路径构建 ---- */
void vg_begin(void);
void vg_move_to(fixed_t x, fixed_t y);
void vg_line_to(fixed_t x, fixed_t y);
void vg_quad_to(fixed_t cx, fixed_t cy, fixed_t x, fixed_t y);
void vg_cubic_to(fixed_t c1x, fixed_t c1y, fixed_t c2x, fixed_t c2y,
                 fixed_t x, fixed_t y);
void vg_close(void);

/* ---- 渲染 (当前路径) ---- */
void vg_fill(uint32_t color);                 /* even-odd 填充 + 抗锯齿 */
void vg_stroke(uint32_t color, int width);   /* 描边 (线段膨胀, 方帽) */

/* ---- 快捷图元 (整数坐标, 全部抗锯齿) ---- */
void vg_fill_round_rect(int x, int y, int w, int h, int r, uint32_t color);
void vg_stroke_round_rect(int x, int y, int w, int h, int r, int width, uint32_t color);
void vg_fill_circle(int cx, int cy, int r, uint32_t color);
void vg_stroke_circle(int cx, int cy, int r, int width, uint32_t color);
void vg_fill_ellipse(int cx, int cy, int rx, int ry, uint32_t color);
void vg_fill_polygon(const int *xs, const int *ys, int n, uint32_t color);
void vg_fill_arc(int cx, int cy, int r, int a0, int a1, uint32_t color);
void vg_stroke_ellipse(int cx, int cy, int rx, int ry, int width, uint32_t color);
void vg_stroke_arc(int cx, int cy, int r, int a0, int a1, int width, uint32_t color);
void vg_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color);

#endif
