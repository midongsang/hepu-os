/* ============================================================================
 * 赫普OS - 数学函数实现
 * V8.20: 初始实现（基础数学函数）
 * ========================================================================== */

#include <math.h>

/* 绝对值 */
double fabs(double x)
{
    return x < 0 ? -x : x;
}

float fabsf(float x)
{
    return x < 0 ? -x : x;
}

long double fabsl(long double x)
{
    return x < 0 ? -x : x;
}

/* 向上取整 */
double ceil(double x)
{
    double int_part = (double)(long long)x;
    if (x > int_part) return int_part + 1.0;
    return int_part;
}

float ceilf(float x)
{
    float int_part = (float)(long long)x;
    if (x > int_part) return int_part + 1.0f;
    return int_part;
}

/* 向下取整 */
double floor(double x)
{
    double int_part = (double)(long long)x;
    if (x < int_part) return int_part - 1.0;
    return int_part;
}

float floorf(float x)
{
    float int_part = (float)(long long)x;
    if (x < int_part) return int_part - 1.0f;
    return int_part;
}

/* 截断（向零取整） */
double trunc(double x)
{
    return (double)(long long)x;
}

float truncf(float x)
{
    return (float)(long long)x;
}

/* 四舍五入 */
double round(double x)
{
    if (x >= 0.0) return floor(x + 0.5);
    return ceil(x - 0.5);
}

float roundf(float x)
{
    if (x >= 0.0f) return floorf(x + 0.5f);
    return ceilf(x - 0.5f);
}

/* 取模 */
double fmod(double x, double y)
{
    if (y == 0.0) return 0.0;
    double quotient = x / y;
    long long int_q = (long long)quotient;
    return x - (double)int_q * y;
}

float fmodf(float x, float y)
{
    if (y == 0.0f) return 0.0f;
    float quotient = x / y;
    long long int_q = (long long)quotient;
    return x - (float)int_q * y;
}

/* 最小值 */
double fmin(double x, double y)
{
    return x < y ? x : y;
}

float fminf(float x, float y)
{
    return x < y ? x : y;
}

/* 最大值 */
double fmax(double x, double y)
{
    return x > y ? x : y;
}

float fmaxf(float x, float y)
{
    return x > y ? x : y;
}

/* 平方根（牛顿迭代法） */
double sqrt(double x)
{
    if (x < 0.0) return 0.0;
    if (x == 0.0) return 0.0;

    double guess = x / 2.0;
    for (int i = 0; i < 50; i++) {
        double new_guess = (guess + x / guess) / 2.0;
        if (fabs(new_guess - guess) < 1e-15) break;
        guess = new_guess;
    }
    return guess;
}

float sqrtf(float x)
{
    if (x < 0.0f) return 0.0f;
    if (x == 0.0f) return 0.0f;

    float guess = x / 2.0f;
    for (int i = 0; i < 30; i++) {
        float new_guess = (guess + x / guess) / 2.0f;
        if (fabsf(new_guess - guess) < 1e-7f) break;
        guess = new_guess;
    }
    return guess;
}

/* 指数函数（e^x，泰勒级数） */
double exp(double x)
{
    if (x > 709.0) return HUGE_VAL;
    if (x < -709.0) return 0.0;

    /* 使用 e^x = 2^(x/ln2) 分解 */
    double ln2 = 0.6931471805599453;
    double x_over_ln2 = x / ln2;
    long long k = (long long)floor(x_over_ln2 + 0.5);
    double r = x - k * ln2;

    /* e^r 泰勒级数 */
    double result = 1.0;
    double term = 1.0;
    for (int i = 1; i < 100; i++) {
        term *= r / i;
        result += term;
        if (fabs(term) < 1e-15) break;
    }

    /* 乘以 2^k */
    if (k >= 0) {
        for (long long i = 0; i < k; i++) result *= 2.0;
    } else {
        for (long long i = 0; i < -k; i++) result /= 2.0;
    }

    return result;
}

float expf(float x)
{
    return (float)exp((double)x);
}

/* 自然对数（ln x，牛顿迭代法） */
double log(double x)
{
    if (x <= 0.0) return -HUGE_VAL;
    if (x == 1.0) return 0.0;

    /* 使用 ln(x) = ln(2^k * m) = k*ln2 + ln(m)，其中 1 <= m < 2 */
    double ln2 = 0.6931471805599453;
    long long k = 0;
    double m = x;

    while (m >= 2.0) { m /= 2.0; k++; }
    while (m < 1.0) { m *= 2.0; k--; }

    /* ln(m) 使用 ln((1+y)/(1-y)) = 2*(y + y^3/3 + y^5/5 + ...) */
    double y = (m - 1.0) / (m + 1.0);
    double y2 = y * y;
    double result = y;
    double term = y;
    for (int i = 1; i < 100; i++) {
        term *= y2;
        double add = term / (2 * i + 1);
        result += add;
        if (fabs(add) < 1e-15) break;
    }
    result *= 2.0;

    return result + k * ln2;
}

float logf(float x)
{
    return (float)log((double)x);
}

/* 以10为底的对数 */
double log10(double x)
{
    return log(x) / 2.302585092994046;
}

float log10f(float x)
{
    return (float)log10((double)x);
}

/* 以2为底的对数 */
double log2(double x)
{
    return log(x) / 0.6931471805599453;
}

float log2f(float x)
{
    return (float)log2((double)x);
}

/* 幂函数（x^y） */
double pow(double x, double y)
{
    if (x == 0.0) {
        if (y > 0.0) return 0.0;
        if (y == 0.0) return 1.0;
        return HUGE_VAL;
    }
    if (y == 0.0) return 1.0;
    if (y == 1.0) return x;
    if (x < 0.0) {
        /* 负数的非整数次幂是复数，简化处理 */
        long long iy = (long long)y;
        if ((double)iy == y) {
            double result = 1.0;
            for (long long i = 0; i < (iy < 0 ? -iy : iy); i++) {
                result *= x;
            }
            return iy < 0 ? 1.0 / result : result;
        }
        return 0.0;  /* 简化：返回0 */
    }
    return exp(y * log(x));
}

float powf(float x, float y)
{
    return (float)pow((double)x, (double)y);
}

/* 正弦函数（泰勒级数） */
double sin(double x)
{
    /* 归一化到 [-pi, pi] */
    double pi = 3.14159265358979323846;
    x = fmod(x, 2.0 * pi);
    if (x > pi) x -= 2.0 * pi;
    if (x < -pi) x += 2.0 * pi;

    /* 泰勒级数：sin(x) = x - x^3/3! + x^5/5! - ... */
    double result = x;
    double term = x;
    double x2 = x * x;
    for (int i = 1; i < 50; i++) {
        term *= -x2 / ((2 * i) * (2 * i + 1));
        result += term;
        if (fabs(term) < 1e-15) break;
    }
    return result;
}

float sinf(float x)
{
    return (float)sin((double)x);
}

/* 余弦函数（泰勒级数） */
double cos(double x)
{
    /* cos(x) = sin(x + pi/2) */
    double pi_2 = 1.57079632679489661923;
    return sin(x + pi_2);
}

float cosf(float x)
{
    return (float)cos((double)x);
}

/* 正切函数 */
double tan(double x)
{
    double c = cos(x);
    if (c == 0.0) return HUGE_VAL;
    return sin(x) / c;
}

float tanf(float x)
{
    return (float)tan((double)x);
}

/* 双曲正弦 */
double sinh(double x)
{
    return (exp(x) - exp(-x)) / 2.0;
}

float sinhf(float x)
{
    return (float)sinh((double)x);
}

/* 双曲余弦 */
double cosh(double x)
{
    return (exp(x) + exp(-x)) / 2.0;
}

float coshf(float x)
{
    return (float)cosh((double)x);
}

/* 双曲正切 */
double tanh(double x)
{
    double e2x = exp(2.0 * x);
    return (e2x - 1.0) / (e2x + 1.0);
}

float tanhf(float x)
{
    return (float)tanh((double)x);
}

/* 立方根 */
double cbrt(double x)
{
    if (x == 0.0) return 0.0;
    int sign = (x < 0) ? -1 : 1;
    x = fabs(x);

    /* 牛顿迭代法 */
    double guess = x / 3.0;
    for (int i = 0; i < 50; i++) {
        double new_guess = (2.0 * guess + x / (guess * guess)) / 3.0;
        if (fabs(new_guess - guess) < 1e-15) break;
        guess = new_guess;
    }
    return sign * guess;
}

float cbrtf(float x)
{
    return (float)cbrt((double)x);
}

/* 直角三角形斜边 */
double hypot(double x, double y)
{
    return sqrt(x * x + y * y);
}

float hypotf(float x, float y)
{
    return (float)hypot((double)x, (double)y);
}

/* 复制符号 */
double copysign(double x, double y)
{
    if ((x < 0 && y > 0) || (x > 0 && y < 0)) return -x;
    return x;
}

float copysignf(float x, float y)
{
    if ((x < 0 && y > 0) || (x > 0 && y < 0)) return -x;
    return x;
}

/* 符号位检测 */
int signbit(double x)
{
    return x < 0.0;
}

/* 浮点分类 */
int fpclassify(double x)
{
    if (x != x) return FP_NAN;  /* NaN */
    if (x == HUGE_VAL || x == -HUGE_VAL) return FP_INFINITE;
    if (x == 0.0) return FP_ZERO;
    return FP_NORMAL;
}

int isfinite(double x)
{
    return fpclassify(x) == FP_NORMAL || fpclassify(x) == FP_ZERO;
}

int isinf(double x)
{
    return fpclassify(x) == FP_INFINITE;
}

int isnan(double x)
{
    return fpclassify(x) == FP_NAN;
}

int isnormal(double x)
{
    return fpclassify(x) == FP_NORMAL;
}

/* 误差函数（简化实现） */
double erf(double x)
{
    /* 使用近似公式 */
    double t = 1.0 / (1.0 + 0.3275911 * fabs(x));
    double y = 1.0 - (((((1.061405429 * t - 1.453152027) * t) +
                        1.421413741) * t - 0.284496736) * t +
                       0.254829592) * t * exp(-x * x);
    return x < 0 ? -y : y;
}

double erfc(double x)
{
    return 1.0 - erf(x);
}

/* 分解浮点数为尾数和指数 */
double frexp(double x, int *exp)
{
    if (!exp) return x;
    if (x == 0.0) {
        *exp = 0;
        return 0.0;
    }

    *exp = 0;
    double abs_x = fabs(x);
    while (abs_x >= 1.0) { abs_x /= 2.0; (*exp)++; }
    while (abs_x < 0.5) { abs_x *= 2.0; (*exp)--; }

    return x < 0 ? -abs_x : abs_x;
}

/* 乘以2的幂 */
double ldexp(double x, int exp)
{
    if (exp >= 0) {
        for (int i = 0; i < exp; i++) x *= 2.0;
    } else {
        for (int i = 0; i < -exp; i++) x /= 2.0;
    }
    return x;
}

/* 分解整数和小数部分 */
double modf(double x, double *iptr)
{
    if (!iptr) return x;
    *iptr = (double)(long long)x;
    return x - *iptr;
}

/* 余数 */
double remainder(double x, double y)
{
    if (y == 0.0) return 0.0;
    double q = round(x / y);
    return x - q * y;
}
