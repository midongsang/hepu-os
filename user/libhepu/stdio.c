/* ============================================================================
 * 赫普OS 用户态C库 - 标准IO
 * ========================================================================== */

#include <hepu/stdio.h>
#include <hepu/syscall.h>
#include <hepu/string.h>

static char print_buf[1024];

int putchar(int c)
{
    char buf[2] = { (char)c, '\0' };
    hepu_print(buf);
    return c;
}

int puts(const char *s)
{
    hepu_print(s);
    hepu_print("\n");
    return strlen(s) + 1;
}

/* 整数转字符串 */
static int itoa(char *buf, uint32_t val, int base, int uppercase)
{
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[32];
    int pos = 0;

    if (val == 0) {
        buf[0] = '0';
        return 1;
    }

    while (val > 0) {
        tmp[pos++] = digits[val % base];
        val /= base;
    }

    for (int i = 0; i < pos; i++) {
        buf[i] = tmp[pos - 1 - i];
    }
    return pos;
}

int vsprintf(char *buf, const char *fmt, va_list ap)
{
    char *out = buf;
    const char *p = fmt;

    while (*p) {
        if (*p != '%') {
            *out++ = *p++;
            continue;
        }
        p++;

        int long_flag = 0;
        int width = 0;
        int left = 0, zero = 0;
        if (*p == '-') { left = 1; p++; }
        if (*p == '0') { zero = 1; p++; }
        while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }
        if (*p == 'l') { long_flag = 1; p++; }

        switch (*p) {
            case 'd': case 'i': {
                char tmp[24];
                int val = va_arg(ap, int);
                int neg = 0;
                int n;
                if (val < 0) { neg = 1; val = -val; }
                n = itoa(tmp, (uint32_t)val, 10, 0);
                int body = n + (neg ? 1 : 0);
                int pad = width - body;
                if (pad < 0) pad = 0;
                if (!left && zero) {
                    if (neg) *out++ = '-';
                    while (pad-- > 0) *out++ = '0';
                    for (int i = 0; i < n; i++) *out++ = tmp[i];
                } else if (!left) {
                    while (pad-- > 0) *out++ = ' ';
                    if (neg) *out++ = '-';
                    for (int i = 0; i < n; i++) *out++ = tmp[i];
                } else {
                    if (neg) *out++ = '-';
                    for (int i = 0; i < n; i++) *out++ = tmp[i];
                    while (pad-- > 0) *out++ = ' ';
                }
                break;
            }
            case 'u': {
                char tmp[24];
                uint32_t val = va_arg(ap, uint32_t);
                int n = itoa(tmp, val, 10, 0);
                int pad = width - n;
                if (pad < 0) pad = 0;
                if (!left && zero) while (pad-- > 0) *out++ = '0';
                else if (!left) while (pad-- > 0) *out++ = ' ';
                for (int i = 0; i < n; i++) *out++ = tmp[i];
                if (left) while (pad-- > 0) *out++ = ' ';
                break;
            }
            case 'x': case 'X': {
                char tmp[24];
                uint32_t val = va_arg(ap, uint32_t);
                int n = itoa(tmp, val, 16, (*p == 'X'));
                int pad = width - n;
                if (pad < 0) pad = 0;
                if (!left && zero) while (pad-- > 0) *out++ = '0';
                else if (!left) while (pad-- > 0) *out++ = ' ';
                for (int i = 0; i < n; i++) *out++ = tmp[i];
                if (left) while (pad-- > 0) *out++ = ' ';
                break;
            }
            case 'c': {
                *out++ = (char)va_arg(ap, int);
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                int n = 0;
                while (s[n]) n++;
                int pad = width - n;
                if (pad < 0) pad = 0;
                if (!left) while (pad-- > 0) *out++ = ' ';
                while (*s) *out++ = *s++;
                if (left) while (pad-- > 0) *out++ = ' ';
                break;
            }
            case 'p': {
                uint32_t val = va_arg(ap, uint32_t);
                *out++ = '0'; *out++ = 'x';
                out += itoa(out, val, 16, 0);
                break;
            }
            case '%': {
                *out++ = '%';
                break;
            }
            default: {
                *out++ = '%';
                *out++ = *p;
                break;
            }
        }
        p++;
    }
    *out = '\0';
    return out - buf;
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vsprintf(buf, fmt, ap);
    va_end(ap);
    return ret;
}

int vprintf(const char *fmt, va_list ap)
{
    int len = vsprintf(print_buf, fmt, ap);
    hepu_print(print_buf);
    return len;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vprintf(fmt, ap);
    va_end(ap);
    return ret;
}

/* === V0.38 新增带长度限制的格式化函数 === */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    if (size == 0) return 0;
    /* 先用大缓冲区格式化，再截断复制 */
    char tmp[1024];
    int len = vsprintf(tmp, fmt, ap);
    if (len >= (int)size) len = (int)size - 1;
    memcpy(buf, tmp, len);
    buf[len] = '\0';
    return len;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

/* 从键盘读取一行（简易实现，通过系统调用） */
int getchar(void)
{
    /* 原型阶段：返回EOF，实际需要键盘驱动支持 */
    return -1;
}

char *fgets(char *s, int size, void *stream)
{
    (void)stream;
    if (size <= 0) return NULL;
    int i = 0;
    while (i < size - 1) {
        int c = getchar();
        if (c == -1) break;
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    s[i] = '\0';
    return i > 0 ? s : NULL;
}
