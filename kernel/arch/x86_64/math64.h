/* ============================================================================
 * 赫普OS - 64位内核定点数学库
 * 提供无浮点依赖的三角函数、开方、向量与插值运算
 * 定点格式：16.16（int32），值域约 [-32768, 32767]
 * V0.13.1: 新增（矢量图形基础）
 * ========================================================================== */

#ifndef MATH64_H
#define MATH64_H

#include <stdint.h>

/* ---------- 16.16 定点数 ---------- */
typedef int32_t fixed_t;

#define FIX_SHIFT       16
#define FIX_ONE         ((fixed_t)1 << FIX_SHIFT)
#define FIX_HALF        (FIX_ONE >> 1)
#define INT_TO_FIX(i)   ((fixed_t)(i) << FIX_SHIFT)
#define FIX_TO_INT(f)   ((f) >> FIX_SHIFT)
#define FIX_MUL(a, b)   ((fixed_t)(((int64_t)(a) * (b)) >> FIX_SHIFT))
#define FIX_DIV(a, b)   ((fixed_t)(((int64_t)(a) << FIX_SHIFT) / (b)))
#define FIX_ABS(a)      ((a) < 0 ? -(a) : (a))
#define FIX_MIN(a, b)   ((a) < (b) ? (a) : (b))
#define FIX_MAX(a, b)   ((a) > (b) ? (a) : (b))
#define FIX_CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

/* ---------- 通用数学 ---------- */
int math64_min(int a, int b);
int math64_max(int a, int b);
int math64_clamp(int v, int lo, int hi);
int math64_abs(int v);
int math64_lerp(int a, int b, int t, int t_max);   /* a + (b-a)*t/t_max */

/* ---------- 三角函数（角度为整数度 0-360） ---------- */
/* 返回 16.16 定点值，如 sin(90°) = 65536 */
fixed_t math64_sin_deg(int deg);
fixed_t math64_cos_deg(int deg);
fixed_t math64_tan_deg(int deg);

/* 弧度版（16.16 定点弧度，2π = 65536） */
fixed_t math64_sin_rad(fixed_t rad);
fixed_t math64_cos_rad(fixed_t rad);

/* ---------- 开方 ---------- */
uint32_t math64_isqrt(uint32_t n);            /* 整数平方根 */
fixed_t math64_sqrt(fixed_t v);               /* 16.16 定点平方根 */

/* ---------- 2D 向量 ---------- */
typedef struct {
    fixed_t x;
    fixed_t y;
} math64_vec2_t;

math64_vec2_t math64_vec2_add(math64_vec2_t a, math64_vec2_t b);
math64_vec2_t math64_vec2_sub(math64_vec2_t a, math64_vec2_t b);
math64_vec2_t math64_vec2_scale(math64_vec2_t a, fixed_t s);
fixed_t math64_vec2_dot(math64_vec2_t a, math64_vec2_t b);
fixed_t math64_vec2_len(math64_vec2_t a);
math64_vec2_t math64_vec2_normalize(math64_vec2_t a);
math64_vec2_t math64_vec2_polar(fixed_t radius, int deg);  /* 极坐标 -> 直角坐标 */
int math64_atan2_deg(int dy, int dx);

/* ---------- 幂/对数（16.16 定点, V0.13.4 P1 数学库） ---------- */
fixed_t math64_pow2(fixed_t x);          /* 2^x */
fixed_t math64_log2(fixed_t x);          /* log2(x), x>0 */
fixed_t math64_exp(fixed_t x);           /* e^x */
fixed_t math64_pow(fixed_t a, fixed_t b);
fixed_t math64_ln(fixed_t x);
fixed_t math64_atan2_rad(fixed_t dy, fixed_t dx);
fixed_t math64_hypot(fixed_t dx, fixed_t dy);/* a^b, a>0 */

/* ---------- 2×3 变换矩阵（V0.13.4 P1） ---------- */
/* 行主序 [a b e; c d f]: x' = a*x + b*y + e;  y' = c*x + d*y + f */
typedef struct {
    fixed_t a, b, e;
    fixed_t c, d, f;
} math64_mat2x3_t;

math64_mat2x3_t math64_mat_identity(void);
math64_mat2x3_t math64_mat_translate(fixed_t tx, fixed_t ty);
math64_mat2x3_t math64_mat_rotate_deg(int deg);
math64_mat2x3_t math64_mat_scale(fixed_t sx, fixed_t sy);
math64_mat2x3_t math64_mat_mul(math64_mat2x3_t a, math64_mat2x3_t b);
void math64_mat_apply(math64_mat2x3_t m, int x, int y, int *ox, int *oy);

/* ---------- 向量扩展（V0.13.4 P1） ---------- */
math64_vec2_t math64_vec2_rotate(math64_vec2_t v, int deg);
math64_vec2_t math64_vec2_perp(math64_vec2_t v);   /* 逆时针垂直 (-y, x) */
fixed_t math64_vec2_dist(math64_vec2_t a, math64_vec2_t b);

/* ---------- Easing 插值（V0.13.4 P1, 动画用） ---------- */
/* 输入 t∈[0,t_max], 返回 16.16 定点进度 [0,FIX_ONE] */
fixed_t math64_ease_linear(int t, int t_max);
fixed_t math64_ease_in(int t, int t_max);          /* 缓入 (t²) */
fixed_t math64_ease_out(int t, int t_max);         /* 缓出 (1-(1-t)²) */
fixed_t math64_ease_inout(int t, int t_max);       /* smoothstep 3t²-2t³ */



/* ---------- 底层扩展（V0.13.17）---------- */
void math64_srand(uint32_t seed);
uint32_t math64_rand_u32(void);
int math64_rand(int lo, int hi);          /* [lo,hi] 闭区间 */
fixed_t math64_rand_fix(void);            /* [0,1) 16.16 */
int math64_floor(fixed_t v);
int math64_ceil(fixed_t v);
int math64_round(fixed_t v);
fixed_t math64_fract(fixed_t v);
fixed_t math64_fmod(fixed_t a, fixed_t b);
int math64_div_round(int a, int b);
int math64_gcd(int a, int b);
int math64_lcm(int a, int b);
uint32_t math64_pow_int(uint32_t base, uint32_t exp);

#endif /* MATH64_H */
