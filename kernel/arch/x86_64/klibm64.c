/* ============================================================================
 * klibm64.c — 内核版 libc/libm 子集(给 Lua / FreeType 这类第三方库用)
 *
 * 内核原本没有: malloc/realloc/free、strtod、memchr/strstr/strspn/strpbrk/
 * memmove/strcoll、以及任何 double 数学函数(math64.c 是定点库)。
 *
 * 注意 ABI: 本文件必须与调用方(e.g. Lua, 用 -msse2 编译)用同样的浮点 ABI,
 * 否则 double 返回值一个走 xmm0、一个走 st(0) 会拿到垃圾。故 Makefile 里
 * 这个文件和 Lua 一起用 -msse2 编, 其余内核代码仍 -mno-sse。
 *
 * 精度: 多项式实现, 目标约 1e-9 相对误差, 够 Lua 的 math 库和文字排版用;
 * 不追求 libm 的 0.5ulp。
 * ========================================================================== */
#include <stddef.h>
#include <stdint.h>

extern void *kmalloc(uint64_t size);
extern void kfree(void *ptr);
extern void *memcpy(void *dst, const void *src, uint64_t n);
extern unsigned long strlen(const char *s);

/* ---------------- 内存 ---------------- */
void *malloc(unsigned long n) { return kmalloc((uint64_t)n); }
void free(void *p) { kfree(p); }

void *calloc(unsigned long n, unsigned long sz)
{
    void *p = malloc(n * sz);
    if (!p) return 0;
    /* 内核 kmalloc 不清零, 这里自己清(只依赖 malloc 的语义) */
    unsigned char *q = (unsigned char *)p;
    for (unsigned long i = 0; i < n * sz; i++) q[i] = 0;
    return p;
}

void *realloc(void *p, unsigned long n)
{
    if (!p) return malloc(n);
    if (!n) { free(p); return 0; }
    /* kmalloc 没有 realloc, 用"新分配+拷贝"降级; 不知道旧块大小,
     * 这里只能按新大小拷贝(调用方都保证新>=旧 或自行处理)。 */
    void *q = malloc(n);
    if (!q) return 0;
    memcpy(q, p, n);
    free(p);
    return q;
}

/* ---------------- 字符串补缺 ---------------- */
void *memmove(void *d, const void *s, unsigned long n)
{
    unsigned char *dd = (unsigned char *)d;
    const unsigned char *ss = (const unsigned char *)s;
    if (dd == ss || n == 0) return d;
    if (dd < ss) { for (unsigned long i = 0; i < n; i++) dd[i] = ss[i]; }
    else { for (unsigned long i = n; i > 0; i--) dd[i-1] = ss[i-1]; }
    return d;
}

void *memchr(const void *s, int c, unsigned long n)
{
    const unsigned char *p = (const unsigned char *)s;
    for (unsigned long i = 0; i < n; i++) if (p[i] == (unsigned char)c) return (void *)(p + i);
    return 0;
}

char *strstr(const char *h, const char *n)
{
    if (!*n) return (char *)h;
    for (; *h; h++) {
        const char *a = h, *b = n;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return (char *)h;
    }
    return 0;
}

unsigned long strspn(const char *s, const char *accept)
{
    unsigned long i = 0;
    for (; s[i]; i++) {
        int hit = 0;
        for (const char *a = accept; *a; a++) if (*a == s[i]) { hit = 1; break; }
        if (!hit) break;
    }
    return i;
}

char *strpbrk(const char *s, const char *accept)
{
    for (; *s; s++)
        for (const char *a = accept; *a; a++)
            if (*a == *s) return (char *)s;
    return 0;
}

int strcoll(const char *a, const char *b)
{
    /* 无 locale 支持, 退化为字典序 */
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* ---------------- 浮点 ---------------- */
double fabs(double x) { return x < 0 ? -x : x; }
double sqrt(double x)
{
    if (x <= 0) return 0;
    double r = x > 1 ? x : 1.0;      /* 牛顿迭代, 初始值取 x 或 1 */
    for (int i = 0; i < 40; i++) r = 0.5 * (r + x / r);
    return r;
}
double floor(double x)
{
    long long i = (long long)x;
    if ((double)i > x) i--;
    return (double)i;
}
double ceil(double x)
{
    long long i = (long long)x;
    if ((double)i < x) i++;
    return (double)i;
}
double fmod(double a, double b)
{
    if (b == 0) return 0;
    double q = a / b;
    long long k = (long long)q;      /* 截断 */
    return a - (double)k * b;
}
double modf(double x, double *ip)
{
    double f = floor(x);
    *ip = f;
    return x - f;
}
double frexp(double x, int *e)
{
    int k = 0;
    if (x == 0) { *e = 0; return 0; }
    double a = fabs(x);
    while (a >= 1.0) { a /= 2.0; k++; }
    while (a < 0.5)  { a *= 2.0; k--; }
    *e = k;
    return x < 0 ? -a : a;
}
double ldexp(double x, int e)
{
    double r = x;
    while (e > 0) { r *= 2.0; e--; }
    while (e < 0) { r /= 2.0; e++; }
    return r;
}

#define LN2 0.69314718055994530942
static double kexp(double x)
{
    if (x > 700) x = 700;
    if (x < -700) x = -700;
    int k = (int)(x / LN2);
    double r = x - (double)k * LN2;
    double t = 1.0, s = 1.0;
    for (int i = 1; i < 18; i++) { t *= r / (double)i; s += t; }
    while (k > 0) { s *= 2.0; k--; }
    while (k < 0) { s /= 2.0; k++; }
    return s;
}
double exp(double x) { return kexp(x); }

static double klog(double x)
{
    if (x <= 0) return -1e308;
    int k = 0;
    while (x >= 2.0) { x /= 2.0; k++; }
    while (x < 1.0)  { x *= 2.0; k--; }
    /* log(m), m in [1,2) —— 用 u=(m-1)/(m+1) 的 atanh 级数 */
    double u = (x - 1.0) / (x + 1.0), u2 = u * u, t = u, s = 0;
    for (int i = 1; i < 40; i += 2) { s += t / (double)i; t *= u2; }
    return 2.0 * s + (double)k * LN2;
}
double log(double x) { return klog(x); }
double log10(double x) { return klog(x) / 2.30258509299404568402; }
double log2(double x) { return klog(x) / LN2; }

double pow(double b, double e)
{
    if (b == 0) return e == 0 ? 1.0 : 0.0;
    if (b < 0) {
        /* 整数指数才能对负数取幂 */
        long long ie = (long long)e;
        if ((double)ie != e) return 0.0;
        double r = kexp(e * klog(-b));
        return (ie & 1) ? -r : r;
    }
    return kexp(e * klog(b));
}

#define PI 3.14159265358979323846
#define TWO_PI 6.28318530717958647692
static double ksin_core(double x)   /* |x| <= pi/2 附近, 泰勒 */
{
    double t = x, s = x, x2 = x * x;
    for (int i = 1; i < 12; i++) { t *= -x2 / (double)((2*i) * (2*i+1)); s += t; }
    return s;
}
static double kcos_core(double x)
{
    double t = 1.0, s = 1.0, x2 = x * x;
    for (int i = 1; i < 12; i++) { t *= -x2 / (double)((2*i-1) * (2*i)); s += t; }
    return s;
}
double sin(double x)
{
    /* 归约到 [-pi, pi], 再利用 sin 的对称性 */
    long long k = (long long)(x / TWO_PI);
    x -= (double)k * TWO_PI;
    if (x > PI) x -= TWO_PI;
    if (x < -PI) x += TWO_PI;
    if (x > PI / 2) x = PI - x;
    if (x < -PI / 2) x = -PI - x;
    return ksin_core(x);
}
double cos(double x) { return sin(x + PI / 2); }
double tan(double x) { double c = cos(x); return c == 0 ? 0 : sin(x) / c; }

double atan(double x)
{
    int neg = x < 0, inv = 0;
    if (neg) x = -x;
    if (x > 1) { x = 1 / x; inv = 1; }
    /* atan 在 [0,1] 的级数收敛慢, 用 x/(1+sqrt(1+x^2)) 形式再展开 */
    double t = x, s = 0;
    for (int i = 0; i < 60; i++) {
        double term = t / (double)(2 * i + 1);
        if (i & 1) s -= term; else s += term;
        t *= x * x;
        if (term < 1e-18 && term > -1e-18) break;
    }
    if (inv) s = PI / 2 - s;
    return neg ? -s : s;
}
double atan2(double y, double x)
{
    if (x > 0) return atan(y / x);
    if (x < 0) return y >= 0 ? atan(y / x) + PI : atan(y / x) - PI;
    return y > 0 ? PI / 2 : (y < 0 ? -PI / 2 : 0);
}
double asin(double x)
{
    if (x >= 1) return PI / 2;
    if (x <= -1) return -PI / 2;
    return atan(x / sqrt(1 - x * x));
}
double acos(double x) { return PI / 2 - asin(x); }

/* ---------------- 数字解析 ---------------- */
double strtod(const char *s, char **end)
{
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    int neg = 0;
    if (*s == '+' || *s == '-') { neg = (*s == '-'); s++; }
    double v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    if (*s == '.') {
        s++;
        double f = 0.1;
        while (*s >= '0' && *s <= '9') { v += (*s - '0') * f; f *= 0.1; s++; }
    }
    if (*s == 'e' || *s == 'E') {
        s++;
        int eneg = 0;
        if (*s == '+' || *s == '-') { eneg = (*s == '-'); s++; }
        int e = 0;
        while (*s >= '0' && *s <= '9') { e = e * 10 + (*s - '0'); s++; }
        v = ldexp(v, eneg ? -e : e);
    }
    if (end) *end = (char *)s;
    return neg ? -v : v;
}

int abs(int x) { return x < 0 ? -x : x; }

/* ------------- 时钟 -------------
 * Lua 的 ltablib 用 clock() 给随机 pivot 播种(防止构造性输入拖慢快排),
 * ldebug 也可能用。内核里用 PIT 的毫秒计数近似。 */
extern unsigned long pit64_get_ticks(void);
long clock(void) { return (long)(pit64_get_ticks() * 1000UL); }

/* ============================================================================
 * libc 桩：Lua 的 lauxlib(loadfile)、ltablib 会引用这些符号，但内核路径
 * 永远走不到（我们不加载文件、不跑 os 库）。给一个安全的空实现让链接过去，
 * 万一真被调用也是"返回失败"而不是乱跳。
 * ========================================================================== */
void *stdin, *stdout, *stderr;

void abort(void) { for (;;) { } }
int *__errno_location(void) { static int e = 0; return &e; }

void *fopen(const char *p, const char *m) { (void)p; (void)m; return 0; }
void *freopen(const char *p, const char *m, void *f) { (void)p; (void)m; (void)f; return 0; }
int   fclose(void *f) { (void)f; return 0; }
int   feof(void *f) { (void)f; return 1; }
int   ferror(void *f) { (void)f; return 0; }
unsigned long fread(void *b, unsigned long sz, unsigned long n, void *f)
{ (void)b; (void)sz; (void)n; (void)f; return 0; }
int   getc(void *f) { (void)f; return -1; }
char *strerror(int e) { (void)e; return (char *)"error"; }

extern unsigned long pit64_get_ticks(void);
long time(long *t) { long v = (long)(pit64_get_ticks() / 100UL); if (t) *t = v; return v; }

struct k_lconv_s { char *decimal_point; };
void *localeconv(void) { static struct k_lconv_s lc = { (char *)"." }; return &lc; }
char *setlocale(int c, const char *s) { (void)c; return (char *)s; }

/* ctype: ASCII 范围判定 */
int isdigit(int c)  { return c >= '0' && c <= '9'; }
int isalpha(int c)  { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isalnum(int c)  { return isalpha(c) || isdigit(c); }
int isspace(int c)  { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
int isupper(int c)  { return c >= 'A' && c <= 'Z'; }
int islower(int c)  { return c >= 'a' && c <= 'z'; }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int iscntrl(int c)  { return (c >= 0 && c < 32) || c == 127; }
int isprint(int c)  { return c >= 32 && c < 127; }
int isgraph(int c)  { return c > 32 && c < 127; }
int ispunct(int c)  { return isgraph(c) && !isalnum(c); }
int tolower(int c)  { return isupper(c) ? c + 32 : c; }
int toupper(int c)  { return islower(c) ? c - 32 : c; }

/* ============================================================================
 * vsnprintf / snprintf —— 为什么不用 kernel/lib/kstdio.c
 *
 * kstdio 的实现只支持 %d/%u/%x/%c/%s/%p, 且它用 -mno-sse 编: SysV 下可变参数的
 * double 走 xmm 寄存器组, 禁 SSE 的代码从整数寄存器组取, 拿到的必然是垃圾。
 * Lua 的 tostring 依赖 "%.14g"(LUAI_NUMFFORMAT), string.format 还要 %f/%e/%x,
 * 所以这里重做一份, 并且本文件整体用 -msse2 编, ABI 才对得上。
 * ========================================================================== */
#include <stdarg.h>

typedef struct {
    char *buf;
    unsigned long cap;
    unsigned long len;
} kout_t;

static void ko_putc(kout_t *o, char c)
{
    if (o->len + 1 < o->cap) o->buf[o->len] = c;
    o->len++;
}
static void ko_pad(kout_t *o, char c, int n) { while (n-- > 0) ko_putc(o, c); }

static long long ko_atoi_fmt(const char **pp)
{
    long long v = 0;
    while (**pp >= '0' && **pp <= '9') { v = v * 10 + (**pp - '0'); (*pp)++; }
    return v;
}

/* 无符号整数转字符串 */
static int ko_utoa(unsigned long long v, char *out, int base, int upper)
{
    char tmp[24];
    int n = 0;
    if (!v) { out[0] = '0'; return 1; }
    while (v) {
        int d = (int)(v % (unsigned long long)base);
        tmp[n++] = (char)(d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10);
        v /= (unsigned long long)base;
    }
    for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    return n;
}

/* 十进制小数: 生成 "整数部分.小数部分", prec 位小数, 尾部 0 可选裁剪 */
static int ko_dtoa_fixed(double d, char *out, int prec, int trim)
{
    int n = 0;
    if (d < 0) { out[n++] = '-'; d = -d; }
    if (d > 1e18) {   /* 大数直接走科学计数法由调用方处理 */
        return -1;
    }
    unsigned long long ip = (unsigned long long)d;
    double frac = d - (double)ip;
    char ibuf[24];
    int ilen = ko_utoa(ip, ibuf, 10, 0);
    for (int i = 0; i < ilen; i++) out[n++] = ibuf[i];
    if (prec > 0) {
        out[n++] = '.';
        for (int i = 0; i < prec; i++) {
            frac *= 10.0;
            int digit = (int)frac;
            if (digit > 9) digit = 9;
            out[n++] = (char)('0' + digit);
            frac -= (double)digit;
        }
        if (trim) {   /* %g: 去掉尾随 0 和孤立的小数点 */
            int k = n;
            while (k > 0 && out[k - 1] == '0') k--;
            if (k > 0 && out[k - 1] == '.') k--;
            n = k;
        }
    }
    out[n] = 0;
    return n;
}

/* %e: d.dddddde±XX */
static int ko_dtoa_sci(double d, char *out, int prec, int upper)
{
    int n = 0;
    int e = 0;
    if (d != 0) {
        while (d >= 10.0) { d /= 10.0; e++; }
        while (d < 1.0)   { d *= 10.0; e--; }
    }
    char tmp[64];
    int t = ko_dtoa_fixed(d, tmp, prec, 0);
    if (t < 0) return -1;
    for (int i = 0; i < t; i++) out[n++] = tmp[i];
    out[n++] = upper ? 'E' : 'e';
    out[n++] = e < 0 ? '-' : '+';
    int ae = e < 0 ? -e : e;
    if (ae < 10) out[n++] = '0';
    char eb[8];
    int el = ko_utoa((unsigned long long)ae, eb, 10, 0);
    for (int i = 0; i < el; i++) out[n++] = eb[i];
    out[n] = 0;
    return n;
}

int ksnprintf(char *buf, unsigned long size, const char *fmt, va_list ap)
{
    kout_t o = { buf, size, 0 };

    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { ko_putc(&o, *p); continue; }

        p++;
        int left = 0, zero = 0;
        if (*p == '-') { left = 1; p++; }
        if (*p == '0') { zero = 1; p++; }
        int width = (int)ko_atoi_fmt(&p);
        int prec = -1;                      /* "%.N": N 是精度, 缺省按格式各自的默认 */
        if (*p == '.') { p++; prec = (int)ko_atoi_fmt(&p); }
        int lcount = 0;
        while (*p == 'l' || *p == 'h') { lcount++; p++; }

        switch (*p) {
        case 'd': case 'i': {
            long long v = (lcount >= 2) ? va_arg(ap, long long) : (long long)va_arg(ap, int);
            char s[32];
            int n = 0;
            if (v < 0) { s[n++] = '-'; v = -v; }
            char ub[24];
            int ul = ko_utoa((unsigned long long)v, ub, 10, 0);
            for (int i = 0; i < ul; i++) s[n++] = ub[i];
            if (lcount >= 1 && lcount < 2) { /* %ld 也按 64 位处理, 同 %lld */ }
            int padn = width - n;
            if (!left) { ko_pad(&o, zero ? '0' : ' ', padn); }
            for (int i = 0; i < n; i++) ko_putc(&o, s[i]);
            if (left) ko_pad(&o, ' ', padn);
            break;
        }
        case 'u': case 'x': case 'X': case 'o': {
            unsigned long long v = (lcount >= 1) ? va_arg(ap, unsigned long long)
                                                 : (unsigned long long)va_arg(ap, unsigned int);
            char ub[24];
            int base = (*p == 'u') ? 10 : (*p == 'o') ? 8 : 16;
            int ul = ko_utoa(v, ub, base, *p == 'X');
            int padn = width - ul;
            if (!left) ko_pad(&o, zero ? '0' : ' ', padn);
            for (int i = 0; i < ul; i++) ko_putc(&o, ub[i]);
            if (left) ko_pad(&o, ' ', padn);
            break;
        }
        case 'c': ko_putc(&o, (char)va_arg(ap, int)); break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int sl = 0;
            while (s[sl]) sl++;
            int padn = width - sl;
            if (!left) ko_pad(&o, ' ', padn);
            for (int i = 0; i < sl; i++) ko_putc(&o, s[i]);
            if (left) ko_pad(&o, ' ', padn);
            break;
        }
        case 'p': {
            unsigned long long v = (unsigned long long)va_arg(ap, void *);
            char ub[24];
            int ul = ko_utoa(v, ub, 16, 0);
            ko_putc(&o, '0'); ko_putc(&o, 'x');
            for (int i = 0; i < ul; i++) ko_putc(&o, ub[i]);
            break;
        }
        case 'f': case 'F': {
            double d = va_arg(ap, double);
            char s[128];
            int n = ko_dtoa_fixed(d, s, prec < 0 ? 6 : prec, 0);
            if (n < 0) { ko_putc(&o, '?'); break; }
            for (int i = 0; i < n; i++) ko_putc(&o, s[i]);
            break;
        }
        case 'e': case 'E': {
            double d = va_arg(ap, double);
            char s[64];
            int n = ko_dtoa_sci(d, s, prec < 0 ? 6 : prec, *p == 'E');
            if (n < 0) { ko_putc(&o, '?'); break; }
            for (int i = 0; i < n; i++) ko_putc(&o, s[i]);
            break;
        }
        case 'g': case 'G': {
            double d = va_arg(ap, double);
            char s[64];
            int n;
            double ad = d < 0 ? -d : d;
            if (ad == 0) { ko_putc(&o, '0'); break; }
            /* %g 的精度是"有效数字", 先定十进制指数再换算成小数位数 */
            int sig = prec < 0 ? 6 : prec;
            int e10 = 0;
            double t = ad;
            while (t >= 10.0) { t /= 10.0; e10++; }
            while (t < 1.0)  { t *= 10.0; e10--; }
            if (e10 < -4 || e10 >= sig)
                n = ko_dtoa_sci(d, s, sig - 1 > 0 ? sig - 1 : 0, *p == 'G');
            else {
                int dprec = sig - 1 - e10;
                if (dprec < 0) dprec = 0;
                n = ko_dtoa_fixed(d, s, dprec, 1);
            }
            if (n < 0) { ko_putc(&o, '?'); break; }
            for (int i = 0; i < n; i++) ko_putc(&o, s[i]);
            break;
        }
        case '%': ko_putc(&o, '%'); break;
        default:  ko_putc(&o, '%'); ko_putc(&o, *p); break;
        }
    }
    if (o.cap) {
        unsigned long idx = o.len < o.cap ? o.len : o.cap - 1;
        o.buf[idx] = 0;
    }
    return (int)o.len;
}

int snprintf(char *buf, unsigned long size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = ksnprintf(buf, size, fmt, ap);
    va_end(ap);
    return r;
}

int vsnprintf(char *buf, unsigned long size, const char *fmt, va_list ap)
{
    return ksnprintf(buf, size, fmt, ap);
}

/* Lua 的 lua_writestringerror 会走 fprintf(stderr,...)。内核没有真实文件,
 * 这里把它落到串口之外的"丢弃"路径: 只格式化不输出, 保证不崩、不误导。 */
int fprintf(void *f, const char *fmt, ...)
{
    (void)f; (void)fmt;
    return 0;
}
int fflush(void *f) { (void)f; return 0; }
