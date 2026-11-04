/* ============================================================================
 * 赫普OS - 拼音输入法引擎 (V4.25-E70)
 *
 * 演示级 IME: 拼音 → 汉字候选(字库内) → 选字 → 输入缓冲区.
 * 字典与 CJK 字库(font_cjk_big.h)对齐——候选字均可在字库渲染.
 * ========================================================================== */

#include <stdint.h>
#include "vga.h"
#include "string.h"

typedef struct {
    const char *py;      /* 拼音(无音调) */
    const char *han;     /* 候选汉字串(每字 UTF-8) */
} ime_entry_t;

/* 字典: 覆盖字库常用字 (拼音 20 组) */
static const ime_entry_t ime_dict[] = {
    { "zhong", "\xe4\xb8\xad" },                          /* 中 */
    { "wen",   "\xe6\x96\x87" },                          /* 文 */
    { "yu",    "\xe8\xaf\xad" },                          /* 语 */
    { "yan",   "\xe8\xa8\x80" },                          /* 言 */
    { "yi",    "\xe5\xb7\xb2" },                          /* 已 */
    { "yong",  "\xe7\x94\xa8" },                          /* 用 */
    { "huan",  "\xe6\xac\xa2" },                          /* 欢 */
    { "ying",  "\xe8\xbf\x8e" },                          /* 迎 */
    { "shi",   "\xe4\xbd\xbf" },                          /* 使 */
    { "he",    "\xe8\xb5\xab" },                          /* 赫 */
    { "pu",    "\xe6\x99\xae" },                          /* 普 */
    { "xi",    "\xe7\xb3\xbb" },                          /* 系 */
    { "tong",  "\xe7\xbb\x9f" },                          /* 统 */
    { "dang",  "\xe5\xbd\x93" },                          /* 当 */
    { "qian",  "\xe5\x89\x8d" },                          /* 前 */
    { "qie",   "\xe5\x88\x87" },                          /* 切 */
    { "huan2", "\xe6\x8d\xa2" },                          /* 换 */
    { "kai",   "\xe5\xbc\x80" },                          /* 开 */
    { "guan",  "\xe5\x85\xb3" },                          /* 关 */
    { "ri",    "\xe6\x97\xa5" },                          /* 日 */
    { "yue",   "\xe6\x9c\x88" },                          /* 月 */
    { "nian",  "\xe5\xb9\xb4" },                          /* 年 */
    { "dian",  "\xe7\x94\xb5" },                          /* 电 */
    { "hua",   "\xe8\xaf\x9d" },                          /* 话 */
};

#define IME_DICT_COUNT (sizeof(ime_dict) / sizeof(ime_entry_t))
#define IME_BUF_MAX 32

static uint32_t ime_buf[IME_BUF_MAX];   /* 输入缓冲区(unicode) */
static int ime_buf_len = 0;
static int ime_last_py = -1;            /* 上次查询拼音索引 */

/* 查拼音 → 返回字典索引或 -1 */
static int hepu_ime_lookup(const char *py)
{
    for (int i = 0; i < (int)IME_DICT_COUNT; i++) {
        if (strcmp(ime_dict[i].py, py) == 0) return i;
    }
    return -1;
}

/* UTF-8 首字符 → unicode */
static uint32_t utf8_first(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    if (p[0] < 0x80) return p[0];
    if ((p[0] & 0xE0) == 0xC0) return ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    if ((p[0] & 0xF0) == 0xE0)
        return ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    return p[0];
}

/* V4.25-E70: ime <pinyin> —— 显示候选 */
void hepu_ime_query(const char *py)
{
    int idx = hepu_ime_lookup(py);
    if (idx < 0) {
        vga_puts("IME: no candidates for \"");
        vga_puts(py);
        vga_puts("\"\n");
        ime_last_py = -1;
        return;
    }
    ime_last_py = idx;
    vga_puts("IME candidates for \"");
    vga_puts(py);
    vga_puts("\":\n");
    /* 逐个候选字编号 (V4.25-E70: 经 CJK 管线渲染, 防逐字节乱码) */
    extern int cjk_render_unicode(uint32_t);
    const char *p = ime_dict[idx].han;
    int n = 0;
    while (*p) {
        uint32_t u = utf8_first(p);
        int len = (u < 0x80) ? 1 : (u < 0x800 ? 2 : 3);
        vga_printf("  [%d] ", n + 1);
        if (cjk_render_unicode(u) != 0) vga_putc('?');
        p += len;
        vga_putc('\n');
        n++;
        if (n >= 9) break;
    }
    vga_printf("IME: %d candidate(s), pick 1-%d (ime pick <n>)\n", n, n);
}

/* ime pick <n> —— 选字入缓冲区 */
void hepu_ime_pick(int n)
{
    if (ime_last_py < 0) { vga_puts("IME: no active query\n"); return; }
    const char *p = ime_dict[ime_last_py].han;
    int cur = 1;
    while (*p) {
        uint32_t u = utf8_first(p);
        int len = (u < 0x80) ? 1 : (u < 0x800 ? 2 : 3);
        if (cur == n) {
            if (ime_buf_len >= IME_BUF_MAX) {
                vga_puts("IME: buffer full\n");
                return;
            }
            ime_buf[ime_buf_len++] = u;
            vga_puts("IME: committed ");
            if (cjk_render_unicode(u) != 0) vga_putc('?');
            vga_puts(" (buf=");
            vga_printf("%d", ime_buf_len);
            vga_puts(")\n");
            /* V4.26-E71: 注入键盘缓冲——选字直接上屏到 shell 命令行 */
            extern void keyboard_inject(const char *);
            char u8[4];
            if (u < 0x80) { u8[0] = (char)u; u8[1] = 0; }
            else if (u < 0x800) {
                u8[0] = (char)(0xC0 | (u >> 6));
                u8[1] = (char)(0x80 | (u & 0x3F));
                u8[2] = 0;
            } else {
                u8[0] = (char)(0xE0 | (u >> 12));
                u8[1] = (char)(0x80 | ((u >> 6) & 0x3F));
                u8[2] = (char)(0x80 | (u & 0x3F));
                u8[3] = 0;
            }
            keyboard_inject(u8);
            return;
        }
        p += len;
        cur++;
    }
    vga_puts("IME: bad pick index\n");
}

/* ime buf —— 显示缓冲区 */
void hepu_ime_show_buf(void)
{
    vga_printf("IME buffer (%d): ", ime_buf_len);
    for (int i = 0; i < ime_buf_len; i++) {
        /* 从字库找 unicode → 输出槽码 (CJK 管线无 public 接口, 用 vga_puts 逐字转) */
        extern int cjk_render_unicode(uint32_t u);
        if (cjk_render_unicode(ime_buf[i]) != 0) vga_putc('?');  /* 仅渲染失败才补 '?' */
    }
    vga_putc('\n');
}

/* ime clear */
void hepu_ime_clear(void)
{
    ime_buf_len = 0;
    ime_last_py = -1;
    vga_puts("IME buffer cleared\n");
}
/* ============================================================================
 * V4.27-E72: 内嵌式 IME 接口 —— readline 内拼音候选
 * ========================================================================== */

/* 候选数 */
int hepu_ime_candidate_count(const char *py)
{
    int idx = hepu_ime_lookup(py);
    if (idx < 0) return 0;
    /* 数候选字 */
    const char *p = ime_dict[idx].han;
    int n = 0;
    while (*p) { n++; p += (utf8_first(p) < 0x80) ? 1 : (utf8_first(p) < 0x800 ? 2 : 3); }
    return n;
}

/* 第 n 个候选字 UTF-8 写入 out; 返回字长或 0 */
int hepu_ime_candidate_utf8(const char *py, int n, char *out)
{
    int idx = hepu_ime_lookup(py);
    if (idx < 0) return 0;
    const char *p = ime_dict[idx].han;
    int cur = 0;
    while (*p) {
        uint32_t u = utf8_first(p);
        int len = (u < 0x80) ? 1 : (u < 0x800 ? 2 : 3);
        if (cur == n) {
            for (int i = 0; i < len; i++) out[i] = *p++;
            out[len] = '\0';
            return len;
        }
        p += len;
        cur++;
    }
    return 0;
}
