/* ============================================================================
 * 赫普OS - 64位内核定点数学库实现
 * 无浮点依赖：正弦余弦用 0-90° 查表 + 象限展开 + 线性插值
 * V0.13.1: 新增（矢量图形基础）
 * ========================================================================== */

#include "math64.h"

/* ---------- 通用数学 ---------- */
int math64_min(int a, int b) { return a < b ? a : b; }
int math64_max(int a, int b) { return a > b ? a : b; }
int math64_clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
int math64_abs(int v) { return v < 0 ? -v : v; }

int math64_lerp(int a, int b, int t, int t_max)
{
    if (t_max <= 0) return a;
    return a + (int)(((int64_t)(b - a) * t) / t_max);
}

/* ---------- 正弦表：0-90°，每度一个 16.16 定点值（int32，90°=65536） ---------- */
static const int32_t SIN_TABLE[91] = {
    0, 1144, 2287, 3430, 4572, 5712, 6850, 7987, 9121, 10252,
    11380, 12505, 13626, 14742, 15855, 16962, 18064, 19161, 20252, 21336,
    22415, 23486, 24550, 25607, 26656, 27697, 28729, 29753, 30767, 31772,
    32768, 33754, 34729, 35693, 36647, 37590, 38521, 39441, 40348, 41243,
    42126, 42995, 43852, 44695, 45525, 46341, 47143, 47930, 48703, 49461,
    50203, 50931, 51643, 52339, 53020, 53684, 54332, 54963, 55578, 56175,
    56756, 57319, 57865, 58393, 58903, 59396, 59870, 60326, 60764, 61183,
    61584, 61966, 62328, 62672, 62997, 63303, 63589, 63856, 64104, 64332,
    64540, 64729, 64898, 65048, 65177, 65287, 65376, 65446, 65496, 65526,
    65536,
};

/* 查表：0-90 度正弦，16.16 定点 */
static fixed_t sin_table_lookup(int deg)
{
    if (deg < 0) deg = 0;
    if (deg > 90) deg = 90;
    return SIN_TABLE[deg];
}

/* 整数角度 -> 16.16 正弦（任意角度） */
fixed_t math64_sin_deg(int deg)
{
    /* 归一到 0-359 */
    deg = deg % 360;
    if (deg < 0) deg += 360;

    if (deg <= 90)      return sin_table_lookup(deg);
    if (deg <= 180)     return sin_table_lookup(180 - deg);
    if (deg <= 270)     return -sin_table_lookup(deg - 180);
    return -sin_table_lookup(360 - deg);
}

fixed_t math64_cos_deg(int deg)
{
    return math64_sin_deg(deg + 90);
}

fixed_t math64_tan_deg(int deg)
{
    fixed_t s = math64_sin_deg(deg);
    fixed_t c = math64_cos_deg(deg);
    if (c == 0) return FIX_ONE * 1000;  /* 大数近似 */
    return FIX_DIV(s, c);
}

/* 弧度版：2π = 65536（16.16） */
static int rad_to_deg(fixed_t rad)
{
    /* deg = rad * 360 / 65536 */
    return (int)(((int64_t)rad * 360) >> 16);
}

fixed_t math64_sin_rad(fixed_t rad)
{
    return math64_sin_deg(rad_to_deg(rad));
}

fixed_t math64_cos_rad(fixed_t rad)
{
    return math64_cos_deg(rad_to_deg(rad));
}

/* ---------- 开方 ---------- */
/* 整数平方根（牛顿迭代，无浮点） */
uint32_t math64_isqrt(uint32_t n)
{
    if (n == 0) return 0;
    uint32_t x = n;
    uint32_t y = (x + 1) >> 1;
    while (y < x) {
        x = y;
        y = (x + n / x) >> 1;
    }
    return x;
}

/* 16.16 定点平方根：sqrt(a * 65536) = isqrt(a) * 256 */
fixed_t math64_sqrt(fixed_t v)
{
    if (v <= 0) return 0;
    return (fixed_t)(math64_isqrt((uint32_t)v) << 8);
}

/* ---------- 2D 向量 ---------- */
math64_vec2_t math64_vec2_add(math64_vec2_t a, math64_vec2_t b)
{
    math64_vec2_t r = { a.x + b.x, a.y + b.y };
    return r;
}

math64_vec2_t math64_vec2_sub(math64_vec2_t a, math64_vec2_t b)
{
    math64_vec2_t r = { a.x - b.x, a.y - b.y };
    return r;
}

math64_vec2_t math64_vec2_scale(math64_vec2_t a, fixed_t s)
{
    math64_vec2_t r = { FIX_MUL(a.x, s), FIX_MUL(a.y, s) };
    return r;
}

fixed_t math64_vec2_dot(math64_vec2_t a, math64_vec2_t b)
{
    return FIX_MUL(a.x, b.x) + FIX_MUL(a.y, b.y);
}

fixed_t math64_vec2_len(math64_vec2_t a)
{
    int64_t sum = (int64_t)FIX_MUL(a.x, a.x) + (int64_t)FIX_MUL(a.y, a.y);
    return math64_sqrt((fixed_t)sum);
}

math64_vec2_t math64_vec2_normalize(math64_vec2_t a)
{
    fixed_t len = math64_vec2_len(a);
    math64_vec2_t r = { 0, 0 };
    if (len > 0) {
        r.x = FIX_DIV(a.x, len);
        r.y = FIX_DIV(a.y, len);
    }
    return r;
}

/* 极坐标（半径 16.16，角度整数度）-> 直角坐标（16.16） */
math64_vec2_t math64_vec2_polar(fixed_t radius, int deg)
{
    math64_vec2_t r;
    r.x = FIX_MUL(radius, math64_cos_deg(deg));
    r.y = FIX_MUL(radius, math64_sin_deg(deg));
    return r;
}

/* ---------- atan2（返回角度 0-359，无浮点） ----------
 * 方法: 映射到第一象限 t=|min/max| ∈ [0,1]（固定点16.16）,
 * 用泰勒级数 atan(x)=x - x³/3 + x⁵/5 - x⁷/7 求 0-45°, 再按象限/对称合成 */
int math64_atan2_deg(int dy, int dx)
{
    if (dx == 0 && dy == 0) return 0;
    int ax = math64_abs(dx);
    int ay = math64_abs(dy);
    int swap = (ay > ax);          /* 若 |dy|>|dx|, 对调后 t=ax/ay */
    int32_t t;
    if (swap) t = (ax == 0) ? 0 : (int32_t)(((int64_t)ax << 16) / ay);
    else      t = (ay == 0) ? 0 : (int32_t)(((int64_t)ay << 16) / ax);
    if (t > FIX_ONE) t = FIX_ONE;

    /* 泰勒 atan(t) 9 项: t - t³/3 + t⁵/5 - t⁷/7 + t⁹/9 - t¹¹/11 (|t|<=1, 16.16)
     * 45°(t=1) 处 4 项误差约 4°, 9 项降至 <0.05° */
    int32_t t2 = (int32_t)(((int64_t)t * t) >> FIX_SHIFT);
    int32_t t3 = (int32_t)(((int64_t)t2 * t) >> FIX_SHIFT);
    /* atan(t): |t|<=1. 分段保证收敛 (16.16 弧度, 1.0=65536):
     * - |t| <= tan(22.5°)=0.4142: 9 项泰勒 (t¹¹ 项误差 <0.002°)
     * - |t| > 0.4142: atan(t)=π/4 + atan((t-1)/(t+1)), |(t-1)/(t+1)|<=0.4142 */
    int32_t q = 51472;  /* π/4 ≈ 0.785398 × 65536 */
    int32_t u = (t > 27150) ? FIX_DIV(t - FIX_ONE, t + FIX_ONE) : t;  /* 27150≈0.4142×65536 */
    int32_t u2 = (int32_t)(((int64_t)u * u) >> FIX_SHIFT);
    int32_t u3 = (int32_t)(((int64_t)u2 * u) >> FIX_SHIFT);
    int32_t u5 = (int32_t)(((int64_t)u2 * u3) >> FIX_SHIFT);
    int32_t u7 = (int32_t)(((int64_t)u2 * u5) >> FIX_SHIFT);
    int32_t u9 = (int32_t)(((int64_t)u2 * u7) >> FIX_SHIFT);
    int32_t u11 = (int32_t)(((int64_t)u2 * u9) >> FIX_SHIFT);
    int32_t at = u - u3 / 3 + u5 / 5 - u7 / 7 + u9 / 9 - u11 / 11;
    if (t > 27150) at = q + at;

    /* 弧度→度: ×180/π≈57.2958; 用 ×57 + ×19/64 (57.296875, 误差 0.002%), 四舍五入 */
    int deg = (int)(((((int64_t)at * 57) + ((int64_t)at * 19 >> 6)) + FIX_HALF) >> FIX_SHIFT);
    if (deg > 45) deg = 45;

    /* 若交换过, 角度补到 90-deg (即 45-90° 区) */
    if (swap) deg = 90 - deg;

    /* 象限: 第一象限 0-90 基础上按 dx,dy 符号映射 */
    if (dx >= 0 && dy >= 0) return deg;              /* 第一 */
    if (dx < 0  && dy >= 0) return 180 - deg;        /* 第二 */
    if (dx < 0  && dy < 0)  return 180 + deg;        /* 第三 */
    return 360 - deg;                                 /* 第四 */
}

/* ============================================================================
 * V0.13.4 P1: 幂/对数/矩阵/向量扩展/Easing
 * 全部 16.16 定点, 无浮点
 * ========================================================================== */

/* ---------- 2^x（泰勒: 2^f ≈ 1 + f·L + f²L²/2 + f³L³/6, L=ln2=45426(16.16)） ---------- */
fixed_t math64_pow2(fixed_t x)
{
    if (x >= 16 * FIX_ONE) return 0x7FFFFFFF;
    if (x <= -16 * FIX_ONE) return 0;
    int ip = x >> FIX_SHIFT;
    int fr = x - (ip << FIX_SHIFT);     /* 真小数（可负）, 负数也正确 */
    fixed_t L = 45426;                  /* ln2 ≈ 0.693147 × 65536 */
    fixed_t u = FIX_MUL(fr, L);
    fixed_t u2 = FIX_MUL(u, u);
    fixed_t u3 = FIX_MUL(u2, u);
    /* e^u = 1+u+u²/2+u³/6 （|u|<0.693, 4项收敛） */
    fixed_t e = FIX_ONE + u + (u2 >> 1) + (u3 / 6);
    int64_t res = (ip >= 0) ? ((int64_t)e << ip) : ((int64_t)e >> (-ip));
    if (res > 0x7FFFFFFF) res = 0x7FFFFFFF;
    return (fixed_t)res;
}

/* ---------- log2(x)（m∈[1,2), 若 m>√2 折半使 u∈[-0.293,0.414], ln(1+u) 泰勒5项） ---------- */
fixed_t math64_log2(fixed_t x)
{
    if (x <= 0) return -16 * FIX_ONE;
    int k = 0;
    while (x >= 2 * FIX_ONE) { x >>= 1; k++; }
    while (x < FIX_ONE)      { x <<= 1; k--; }
    if (x > 92682) { x >>= 1; k++; }    /* √2≈1.4142 ×65536=92682: 折半保收敛 */
    int u = x - FIX_ONE;                /* u ∈ [-0.293, 0.414) */
    fixed_t u2 = FIX_MUL(u, u);
    fixed_t u3 = FIX_MUL(u2, u);
    fixed_t u4 = FIX_MUL(u2, u2);
    fixed_t u5 = FIX_MUL(u2, u3);
    /* ln(1+u) = u - u²/2 + u³/3 - u⁴/4 + u⁵/5 */
    fixed_t ln = u - (u2 >> 1) + (u3 / 3) - (u4 >> 2) + (u5 / 5);
    fixed_t inv_ln2 = 94548;            /* 1/ln2 ≈ 1.442695 × 65536 */
    return (fixed_t)(k << FIX_SHIFT) + FIX_MUL(ln, inv_ln2);
}

/* ---------- e^x = 2^(x·log2e) ---------- */
fixed_t math64_exp(fixed_t x)
{
    fixed_t log2e = 94548;              /* 1.442695 × 65536 */
    return math64_pow2(FIX_MUL(x, log2e));
}

/* ---------- a^b = 2^(b·log2(a)) ---------- */
fixed_t math64_pow(fixed_t a, fixed_t b)
{
    if (a <= 0) return (b == 0) ? FIX_ONE : 0;
    return math64_pow2(FIX_MUL(b, math64_log2(a)));
}

/* ---------- ln(x) = log2(x) × ln2 ---------- */
fixed_t math64_ln(fixed_t x)
{
    fixed_t ln2 = 45426;  /* 0.693147 × 65536 */
    return FIX_MUL(math64_log2(x), ln2);
}

/* ---------- atan2 弧度版 (16.16 弧度, 2π=65536; 输入 16.16) ---------- */
fixed_t math64_atan2_rad(fixed_t dy, fixed_t dx)
{
    int deg = math64_atan2_deg(FIX_TO_INT(dy), FIX_TO_INT(dx));
    return (fixed_t)(((int64_t)deg * 65536) / 360);
}

/* ---------- hypot: √(dx²+dy²) (16.16) ---------- */
fixed_t math64_hypot(fixed_t dx, fixed_t dy)
{
    return math64_sqrt(FIX_MUL(dx, dx) + FIX_MUL(dy, dy));
}

/* ---------- 2×3 矩阵 ---------- */
math64_mat2x3_t math64_mat_identity(void)
{
    math64_mat2x3_t m = { FIX_ONE, 0, 0, 0, FIX_ONE, 0 };
    return m;
}
math64_mat2x3_t math64_mat_translate(fixed_t tx, fixed_t ty)
{
    math64_mat2x3_t m = { FIX_ONE, 0, tx, 0, FIX_ONE, ty };
    return m;
}
math64_mat2x3_t math64_mat_rotate_deg(int deg)
{
    fixed_t c = math64_cos_deg(deg);
    fixed_t s = math64_sin_deg(deg);
    math64_mat2x3_t m = { c, -s, 0, s, c, 0 };
    return m;
}
math64_mat2x3_t math64_mat_scale(fixed_t sx, fixed_t sy)
{
    math64_mat2x3_t m = { sx, 0, 0, 0, sy, 0 };
    return m;
}
math64_mat2x3_t math64_mat_mul(math64_mat2x3_t a, math64_mat2x3_t b)
{
    /* 先 b 后 a: p = a·b */
    math64_mat2x3_t r;
    r.a = FIX_MUL(a.a, b.a) + FIX_MUL(a.b, b.c);
    r.b = FIX_MUL(a.a, b.b) + FIX_MUL(a.b, b.d);
    r.e = FIX_MUL(a.a, b.e) + FIX_MUL(a.b, b.f) + a.e;
    r.c = FIX_MUL(a.c, b.a) + FIX_MUL(a.d, b.c);
    r.d = FIX_MUL(a.c, b.b) + FIX_MUL(a.d, b.d);
    r.f = FIX_MUL(a.c, b.e) + FIX_MUL(a.d, b.f) + a.f;
    return r;
}
void math64_mat_apply(math64_mat2x3_t m, int x, int y, int *ox, int *oy)
{
    *ox = FIX_TO_INT(FIX_MUL(m.a, INT_TO_FIX(x)) + FIX_MUL(m.b, INT_TO_FIX(y)) + m.e);
    *oy = FIX_TO_INT(FIX_MUL(m.c, INT_TO_FIX(x)) + FIX_MUL(m.d, INT_TO_FIX(y)) + m.f);
}

/* ---------- 向量扩展 ---------- */
math64_vec2_t math64_vec2_rotate(math64_vec2_t v, int deg)
{
    fixed_t c = math64_cos_deg(deg);
    fixed_t s = math64_sin_deg(deg);
    math64_vec2_t r;
    r.x = FIX_MUL(v.x, c) - FIX_MUL(v.y, s);
    r.y = FIX_MUL(v.x, s) + FIX_MUL(v.y, c);
    return r;
}
math64_vec2_t math64_vec2_perp(math64_vec2_t v)
{
    math64_vec2_t r = { -v.y, v.x };
    return r;
}
fixed_t math64_vec2_dist(math64_vec2_t a, math64_vec2_t b)
{
    int64_t dx = (int64_t)a.x - b.x;
    int64_t dy = (int64_t)a.y - b.y;
    return math64_sqrt((fixed_t)((dx * dx + dy * dy) >> FIX_SHIFT));
}

/* ---------- Easing ---------- */
fixed_t math64_ease_linear(int t, int t_max)
{
    if (t_max <= 0) return FIX_ONE;
    if (t <= 0) return 0;
    if (t >= t_max) return FIX_ONE;
    return (fixed_t)(((int64_t)t << FIX_SHIFT) / t_max);
}
fixed_t math64_ease_in(int t, int t_max)
{
    fixed_t p = math64_ease_linear(t, t_max);
    return FIX_MUL(p, p);
}
fixed_t math64_ease_out(int t, int t_max)
{
    fixed_t p = math64_ease_linear(t, t_max);
    fixed_t q = FIX_ONE - p;
    return FIX_ONE - FIX_MUL(q, q);
}
fixed_t math64_ease_inout(int t, int t_max)
{
    fixed_t p = math64_ease_linear(t, t_max);
    /* smoothstep: 3p² - 2p³ */
    fixed_t p2 = FIX_MUL(p, p);
    return FIX_MUL(INT_TO_FIX(3), p2) - FIX_MUL(INT_TO_FIX(2), FIX_MUL(p2, p));
}

/* ============================================================================
 * V0.13.17: 底层数学扩展 — PRNG / 定点舍入 / 定点取模 / 整数运算
 * ========================================================================== */

/* ---------- 伪随机数 (xorshift32, 无浮点, 内核可种子) ---------- */
static uint32_t math64_rng_state = 0x9E3779B9u;  /* 默认种子 */

void math64_srand(uint32_t seed)
{
    if (seed == 0) seed = 0x9E3779B9u;
    math64_rng_state = seed;
}

uint32_t math64_rand_u32(void)
{
    uint32_t x = math64_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    math64_rng_state = x;
    return x;
}

int math64_rand(int lo, int hi)   /* [lo, hi] 闭区间 */
{
    if (hi <= lo) return lo;
    uint32_t range = (uint32_t)(hi - lo) + 1;
    return lo + (int)(math64_rand_u32() % range);
}

fixed_t math64_rand_fix(void)      /* [0, 1) 16.16 定点 */
{
    return (fixed_t)((math64_rand_u32() >> 16) & 0xFFFF);
}

/* ---------- 定点舍入 ---------- */
int math64_floor(fixed_t v)
{
    return (int)(v >> FIX_SHIFT);
}

int math64_ceil(fixed_t v)
{
    if (v <= 0) return (int)(v >> FIX_SHIFT);
    return (int)((v + FIX_ONE - 1) >> FIX_SHIFT);
}

int math64_round(fixed_t v)
{
    /* half away from zero: 正数 +0.5, 负数 -0.5 后算术右移 */
    if (v < 0) return (int)((v - FIX_HALF) >> FIX_SHIFT);
    return (int)((v + FIX_HALF) >> FIX_SHIFT);
}

fixed_t math64_fract(fixed_t v)    /* 小数部分, 与v同号 [0,1) */
{
    fixed_t i = (v >> FIX_SHIFT) << FIX_SHIFT;
    return v - i;
}

/* ---------- 定点取模 (余数与 b 同号) ---------- */
fixed_t math64_fmod(fixed_t a, fixed_t b)
{
    if (b == 0) return 0;
    int64_t q = (int64_t)a / (int64_t)b;        /* 截断商 */
    int64_t r = (int64_t)a - q * (int64_t)b;
    if (r != 0 && ((r < 0) != (b < 0))) r += b; /* 修正为与b同号 */
    return (fixed_t)r;
}

/* ---------- 整数运算 ---------- */
int math64_div_round(int a, int b)   /* 四舍五入除法 (half away from zero) */
{
    if (b == 0) return 0;
    int64_t q2 = (int64_t)a * 2 / (int64_t)b;
    int r = (int)((q2 + (q2 >= 0 ? 1 : -1)) / 2);
    return r;
}

int math64_gcd(int a, int b)
{
    a = math64_abs(a);
    b = math64_abs(b);
    while (b) {
        int t = a % b;
        a = b;
        b = t;
    }
    return a;
}

int math64_lcm(int a, int b)
{
    if (a == 0 || b == 0) return 0;
    return (a / math64_gcd(a, b)) * b;
}

uint32_t math64_pow_int(uint32_t base, uint32_t exp)  /* 快速幂, 溢出返回0 */
{
    uint64_t r = 1, b = base, e = exp;
    while (e) {
        if (e & 1) r *= b;
        b *= b;
        e >>= 1;
        if (r > 0xFFFFFFFFu) return 0;
    }
    return (uint32_t)r;
}
