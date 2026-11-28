/* ============================================================================
 * 赫普OS 桌面 UTF-8 文本工具 (M0)
 *
 * 终端/编辑器此前都按单字节处理键盘：中文 UTF-8 一旦插入，Backspace 会删掉
 * 一个字节，留下半个汉字；光标列宽也会按 3 列算。这里只做最小契约：
 * - 前进/后退一个完整码点
 * - 显示列宽（ASCII=1, CJK=2, 组合字符=0）
 * - 有界追加/退格
 * ========================================================================== */
#include <stdint.h>
#include "hs_text_utf8.h"

static int hs_utf8_cont(unsigned char c)
{
    return (c & 0xC0) == 0x80;
}

/* 从 s 起最多 remain 字节，返回一个合法码点的长度；非法序列按 1 字节处理。 */
int hs_utf8_len_at(const char *s, int remain)
{
    if (!s || remain <= 0) return 0;
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0 && remain >= 2 && hs_utf8_cont((unsigned char)s[1]))
        return 2;
    if ((c & 0xF0) == 0xE0 && remain >= 3 &&
        hs_utf8_cont((unsigned char)s[1]) && hs_utf8_cont((unsigned char)s[2]))
        return 3;
    if ((c & 0xF8) == 0xF0 && remain >= 4 &&
        hs_utf8_cont((unsigned char)s[1]) && hs_utf8_cont((unsigned char)s[2]) &&
        hs_utf8_cont((unsigned char)s[3]))
        return 4;
    return 1;
}

/* 返回 len 字节前缀里最后一个完整码点的字节长度。
 * 用于 Backspace：必须删整个码点，不能只删一个 continuation。 */
int hs_utf8_prev_len(const char *s, int len)
{
    if (!s || len <= 0) return 0;
    int start = len - 1;
    int back = 0;
    while (start > 0 && hs_utf8_cont((unsigned char)s[start]) && back < 3) {
        start--;
        back++;
    }
    int n = hs_utf8_len_at(s + start, len - start);
    if (start + n != len) return 1;  /* 不完整序列：只退一个字节，避免死循环 */
    return n;
}

int hs_utf8_next_pos(const char *s, int len, int pos)
{
    if (pos < 0) pos = 0;
    if (pos >= len) return len;
    int n = hs_utf8_len_at(s + pos, len - pos);
    return pos + (n > 0 ? n : 1);
}

int hs_utf8_is_wide(uint32_t cp)
{
    if (cp >= 0x1100 && cp <= 0x115F) return 1;
    if (cp >= 0x2E80 && cp <= 0xA4CF) return 1;
    if (cp >= 0xAC00 && cp <= 0xD7A3) return 1;
    if (cp >= 0xF900 && cp <= 0xFAFF) return 1;
    if (cp >= 0xFE10 && cp <= 0xFE19) return 1;
    if (cp >= 0xFE30 && cp <= 0xFE6F) return 1;
    if (cp >= 0xFF00 && cp <= 0xFF60) return 1;
    if (cp >= 0xFFE0 && cp <= 0xFFE6) return 1;
    if (cp >= 0x1F300 && cp <= 0x1FAFF) return 1;
    if (cp >= 0x20000 && cp <= 0x3FFFD) return 1;
    return 0;
}

static uint32_t hs_utf8_decode(const char *s, int n)
{
    unsigned char c = (unsigned char)s[0];
    if (n == 1) return c;
    if (n == 2)
        return ((uint32_t)(c & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F);
    if (n == 3)
        return ((uint32_t)(c & 0x0F) << 12) |
               ((uint32_t)((unsigned char)s[1] & 0x3F) << 6) |
               ((unsigned char)s[2] & 0x3F);
    return ((uint32_t)(c & 0x07) << 18) |
           ((uint32_t)((unsigned char)s[1] & 0x3F) << 12) |
           ((uint32_t)((unsigned char)s[2] & 0x3F) << 6) |
           ((unsigned char)s[3] & 0x3F);
}

int hs_utf8_cols(const char *s, int len)
{
    int cols = 0;
    int pos = 0;
    while (s && pos < len) {
        int n = hs_utf8_len_at(s + pos, len - pos);
        if (n <= 0) break;
        uint32_t cp = hs_utf8_decode(s + pos, n);
        if (cp >= 0x0300 && cp <= 0x036F) cols += 0;      /* 组合附加符号 */
        else if (cp < 0x20 || cp == 0x7F) cols += 0;       /* 控制字符 */
        else if (hs_utf8_is_wide(cp)) cols += 2;
        else cols += 1;
        pos += n;
    }
    return cols;
}

int hs_utf8_append(char *buf, int cap, int len, const char *s)
{
    if (!buf || cap <= 1 || !s) return len;
    if (len < 0) len = 0;
    int pos = 0;
    while (s[pos]) {
        int n = hs_utf8_len_at(s + pos, 4);
        if (n <= 0) break;
        if (len + n >= cap) break;
        for (int i = 0; i < n; i++) buf[len + i] = s[pos + i];
        len += n;
        pos += n;
    }
    buf[len] = 0;
    return len;
}

int hs_utf8_backspace(char *buf, int len)
{
    if (!buf || len <= 0) return 0;
    int n = hs_utf8_prev_len(buf, len);
    if (n <= 0) n = 1;
    len -= n;
    buf[len] = 0;
    return len;
}
