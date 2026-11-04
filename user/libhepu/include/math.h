/* ============================================================================
 * 赫普OS - math.h 头文件
 * 数学函数声明和常量定义
 * V8.20: 初始实现
 * ========================================================================== */

#ifndef _MATH_H
#define _MATH_H

/* 数学常量 */
#define M_E          2.7182818284590452354   /* e */
#define M_LOG2E      1.4426950408889634074   /* log2(e) */
#define M_LOG10E     0.43429448190325182765  /* log10(e) */
#define M_LN2        0.69314718055994530942  /* ln(2) */
#define M_LN10       2.30258509299404568402  /* ln(10) */
#define M_PI         3.14159265358979323846  /* pi */
#define M_PI_2       1.57079632679489661923  /* pi/2 */
#define M_PI_4       0.78539816339744830962  /* pi/4 */
#define M_1_PI       0.31830988618379067154  /* 1/pi */
#define M_2_PI       0.63661977236758134308  /* 2/pi */
#define M_2_SQRTPI   1.12837916709551257390  /* 2/sqrt(pi) */
#define M_SQRT2      1.41421356237309504880  /* sqrt(2) */
#define M_SQRT1_2    0.70710678118654752440  /* 1/sqrt(2) */

/* 无穷大 */
#define HUGE_VAL     ((double)(1.0 / 0.0))
#define HUGE_VALF    ((float)(1.0f / 0.0f))
#define HUGE_VALL    ((long double)(1.0L / 0.0L))

/* NaN */
#define NAN          ((float)(0.0f / 0.0f))

/* 浮点分类 */
#define FP_NAN       0  /* 非数字 */
#define FP_INFINITE  1  /* 无穷大 */
#define FP_ZERO      2  /* 零 */
#define FP_SUBNORMAL 3  /* 次正规数 */
#define FP_NORMAL    4  /* 正规数 */

/* 错误值 */
#define DOMAIN       1  /* 定义域错误 */
#define SING         2  /* 奇点错误 */
#define OVERFLOW     3  /* 溢出 */
#define UNDERFLOW    4  /* 下溢 */
#define TLOSS        5  /* 总精度丢失 */
#define PLOSS        6  /* 部分精度丢失 */

/* 函数声明 */

/* 三角函数 */
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);

/* 双曲函数 */
double sinh(double x);
double cosh(double x);
double tanh(double x);
double asinh(double x);
double acosh(double x);
double atanh(double x);

/* 指数和对数函数 */
double exp(double x);
double exp2(double x);
double expm1(double x);
double log(double x);
double log2(double x);
double log10(double x);
double log1p(double x);
double logb(double x);

/* 幂函数 */
double pow(double x, double y);
double sqrt(double x);
double cbrt(double x);
double hypot(double x, double y);

/* 误差函数 */
double erf(double x);
double erfc(double x);

/* Gamma函数 */
double tgamma(double x);
double lgamma(double x);

/* 最近整数函数 */
double ceil(double x);
double floor(double x);
double trunc(double x);
double round(double x);
double nearbyint(double x);
double rint(double x);
long lrint(double x);
long long llrint(double x);
long lround(double x);
long long llround(double x);

/* 浮点操作函数 */
double fmod(double x, double y);
double remainder(double x, double y);
double modf(double x, double *iptr);
double frexp(double x, int *exp);
double ldexp(double x, int exp);
double scalbn(double x, int n);
double scalbln(double x, long n);
double nextafter(double x, double y);
double nexttoward(double x, long double y);

/* 绝对值和最小值/最大值 */
double fabs(double x);
double fmin(double x, double y);
double fmax(double x, double y);
double fdim(double x, double y);

/* 符号函数 */
int signbit(double x);
double copysign(double x, double y);

/* 浮点分类 */
int fpclassify(double x);
int isfinite(double x);
int isinf(double x);
int isnan(double x);
int isnormal(double x);

/* 比较函数 */
int isgreater(double x, double y);
int isgreaterequal(double x, double y);
int isless(double x, double y);
int islessequal(double x, double y);
int islessgreater(double x, double y);
int isunordered(double x, double y);

/* 浮点版本函数 */
float sinf(float x);
float cosf(float x);
float tanf(float x);
float asinf(float x);
float acosf(float x);
float atanf(float x);
float atan2f(float y, float x);
float sinhf(float x);
float coshf(float x);
float tanhf(float x);
float expf(float x);
float exp2f(float x);
float logf(float x);
float log2f(float x);
float log10f(float x);
float powf(float x, float y);
float sqrtf(float x);
float cbrtf(float x);
float hypotf(float x, float y);
float ceilf(float x);
float floorf(float x);
float truncf(float x);
float roundf(float x);
float fmodf(float x, float y);
float fabsf(float x);
float fminf(float x, float y);
float fmaxf(float x, float y);
float copysignf(float x, float y);

/* 长双精度版本函数 */
long double sinl(long double x);
long double cosl(long double x);
long double tanl(long double x);
long double expl(long double x);
long double logl(long double x);
long double powl(long double x, long double y);
long double sqrtl(long double x);
long double fabsl(long double x);

/* 复数支持（前向声明） */
/* #include <complex.h> */

#endif /* _MATH_H */
