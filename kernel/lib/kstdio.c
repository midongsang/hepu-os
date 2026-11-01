/* ============================================================================
 * 赫普OS - 内核格式化输出 (V1.01新增)
 * 实现：vsnprintf/snprintf，替代不存在的幻觉API
 * 支持：%d %u %x %X %c %s %p %%
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "kstdio.h"
#include "string.h"

/* 无符号转十进制 */
static char *utoa_unsigned(uint32_t value, char *buf, int base, int uppercase)
{
    char *p = buf;
    char *first_digit = buf;

    if (value == 0) {
        *p++ = '0';
        *p = '\0';
        return buf;
    }

    while (value > 0) {
        uint32_t digit = value % base;
        if (digit < 10) {
            *p++ = '0' + digit;
        } else {
            *p++ = (uppercase ? 'A' : 'a') + (digit - 10);
        }
        value /= base;
    }
    *p = '\0';

    /* 反转 */
    p--;
    while (first_digit < p) {
        char tmp = *first_digit;
        *first_digit++ = *p;
        *p-- = tmp;
    }
    return buf;
}

/* 核心格式化 */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
    if (!buf || size == 0) return 0;

    size_t pos = 0;
    const char *p = fmt;

    while (*p && pos + 1 < size) {
        if (*p != '%') {
            buf[pos++] = *p++;
            continue;
        }
        p++;  /* 跳过% */

        /* 标志：左对齐/零填充 */
        int left_align = 0;
        int zero_pad = 0;
        int width = 0;

        if (*p == '-') { left_align = 1; p++; }
        if (*p == '0') { zero_pad = 1; p++; }
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        int long_flag = 0;
        if (*p == 'l') { long_flag = 1; p++; }

        char tmp[32];
        const char *str = NULL;
        char ch = 0;
        int int_val = 0;
        uint32_t uint_val = 0;
        int len = 0;

        switch (*p) {
            case 'd':
            case 'i':
                int_val = va_arg(args, int);
                if (long_flag) int_val = (int)va_arg(args, long);
                if (int_val < 0) {
                    buf[pos++] = '-';
                    uint_val = (uint32_t)(-int_val);
                } else {
                    uint_val = (uint32_t)int_val;
                }
                utoa_unsigned(uint_val, tmp, 10, 0);
                str = tmp;
                break;
            case 'u':
                uint_val = va_arg(args, unsigned int);
                if (long_flag) uint_val = (uint32_t)va_arg(args, unsigned long);
                utoa_unsigned(uint_val, tmp, 10, 0);
                str = tmp;
                break;
            case 'x':
            case 'X':
                uint_val = va_arg(args, unsigned int);
                if (long_flag) uint_val = (uint32_t)va_arg(args, unsigned long);
                utoa_unsigned(uint_val, tmp, 16, (*p == 'X'));
                str = tmp;
                break;
            case 'p':
                uint_val = va_arg(args, uint32_t);
                /* V3.18修复: 原连续写 '0'+'x' 无边界检查，pos 接近 size 时
                 * 可能写越界 1 字节；补 pos+2<size 保护 */
                if (pos + 2 < size) {
                    buf[pos++] = '0';
                    buf[pos++] = 'x';
                } else if (pos + 1 < size) {
                    buf[pos++] = '0';
                }
                utoa_unsigned(uint_val, tmp, 16, 0);
                str = tmp;
                break;
            case 'c':
                ch = (char)va_arg(args, int);
                buf[pos++] = ch;
                continue;
            case 's':
                str = va_arg(args, const char *);
                if (!str) str = "(null)";
                break;
            case '%':
                buf[pos++] = '%';
                continue;
            default:
                buf[pos++] = '%';
                if (*p) buf[pos++] = *p;
                p++;
                continue;
        }

        len = (int)strlen(str);
        /* 宽度填充（仅右对齐时） */
        if (!left_align && width > len) {
            while (pos + 1 < size && width > len) {
                buf[pos++] = zero_pad ? '0' : ' ';
                width--;
            }
        }
        /* 复制字符串 */
        while (*str && pos + 1 < size) {
            buf[pos++] = *str++;
            len--;
        }
        /* 左对齐补空格 */
        if (left_align) {
            while (pos + 1 < size && len > 0) {
                buf[pos++] = ' ';
                len--;
            }
        }
        p++;
    }

    /* V3.18修复: pos 可能因 %p 双写达到 size，原 buf[pos] 越界 1 字节 */
    if (pos < size) buf[pos] = '\0';
    else buf[size - 1] = '\0';
    return (int)pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return ret;
}
