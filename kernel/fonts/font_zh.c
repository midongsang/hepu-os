/* ============================================================================
 * 赫普OS - 中文渲染 (V2.36, V3.27 分桶优化)
 * 内置 16x16 点阵字库 (2151 常用字), 支持 UTF-8 中英文混排。
 * 中文: 16x16 (32字节/字), 英文: 8x8 (复用 vga_gfx 字体)。
 * ========================================================================== */

#include "font_zh.h"
#include "vga_gfx.h"

#include "font_zh_data.h"

/* V3.27: 按 UTF-8 首字节分桶索引 (0xE3..0xEA 八桶), 避免 2151 字全表线性扫.
 * zh_bucket_start[b] = 首个首字节 >= (0xE3+b) 的字模索引; 桶内范围为
 * [start[b], start[b+1]), 末桶到 ZH_FONT_COUNT. 运行时首次查找前构建一次. */
static int zh_bucket_start[9];
static int zh_bucket_ready = 0;

static void zh_bucket_init(void)
{
    for (int b = 0; b <= 8; b++) {
        uint8_t fb = (uint8_t)(0xE3 + b);
        int i = 0;
        while (i < ZH_FONT_COUNT && zh_utf8[i][0] < fb) i++;
        zh_bucket_start[b] = i;
    }
    zh_bucket_ready = 1;
}

/* 查询 UTF-8 三字节汉字对应字模索引 */
int zh_lookup(const uint8_t *utf8)
{
    if (!utf8) return -1;
    if (!zh_bucket_ready) zh_bucket_init();

    uint8_t fb = utf8[0];
    int lo, hi;
    if (fb >= 0xE3 && fb <= 0xEA) {
        int b = fb - 0xE3;
        lo = zh_bucket_start[b];
        hi = zh_bucket_start[b + 1];
    } else {
        lo = 0;
        hi = ZH_FONT_COUNT;
    }
    for (int i = lo; i < hi; i++) {
        if (zh_utf8[i][0] == fb &&
            zh_utf8[i][1] == utf8[1] &&
            zh_utf8[i][2] == utf8[2]) {
            return i;
        }
    }
    return -1;
}

/* 渲染 16x16 中文字模 */
static void draw_zh_glyph(int x, int y, int idx, uint8_t color)
{
    const uint8_t *g = zh_bmp[idx];
    for (int row = 0; row < 16; row++) {
        uint8_t hi = g[row * 2];
        uint8_t lo = g[row * 2 + 1];
        for (int col = 0; col < 8; col++) {
            if (hi & (0x80 >> col))
                gfx_putpixel(x + col, y + row, color);
            if (lo & (0x80 >> col))
                gfx_putpixel(x + 8 + col, y + row, color);
        }
    }
}

/* 渲染单个 UTF-8 字符 (中文/英文), 返回消耗字节数 */
int gfx_putchar_zh(int x, int y, const char *utf8, uint8_t color)
{
    if (!utf8 || !utf8[0]) return 0;
    const uint8_t *p = (const uint8_t *)utf8;

    if (p[0] < 0x80) {
        /* ASCII: 8x8 */
        gfx_putchar(x, y, (char)p[0], color);
        return 1;
    }
    if (p[0] >= 0xE0 && p[0] <= 0xEF && p[1] && p[2]) {
        /* 3 字节汉字: 16x16 */
        int idx = zh_lookup(p);
        if (idx >= 0) {
            draw_zh_glyph(x, y, idx, color);
        } else {
            /* 未收录字: 画占位框 */
            gfx_rect(x, y, 16, 16, color);
        }
        return 3;
    }
    return 1;
}

/* 渲染 UTF-8 字符串 (中英文混排) */
int gfx_puts_zh(int x, int y, const char *s, uint8_t color)
{
    int cx = x;
    int base_y = y;
    while (s && *s) {
        if (*s == '\n') {
            base_y += 16;
            cx = x;
            s++;
            continue;
        }
        const uint8_t *p = (const uint8_t *)s;
        if (p[0] < 0x80) {
            gfx_putchar(cx, base_y, (char)p[0], color);
            cx += 8;
            s++;
        } else if (p[0] >= 0xE0 && p[0] <= 0xEF && p[1] && p[2]) {
            int n = gfx_putchar_zh(cx, base_y, s, color);
            cx += 16;
            s += n;
        } else {
            s++;
        }
    }
    return cx - x;
}

/* 计算 UTF-8 字符串像素宽度 */
int zh_str_width(const char *s)
{
    int w = 0;
    while (s && *s) {
        const uint8_t *p = (const uint8_t *)s;
        if (p[0] < 0x80) { w += 8; s++; }
        else if (p[0] >= 0xE0 && p[0] <= 0xEF && p[1] && p[2]) { w += 16; s += 3; }
        else s++;
    }
    return w;
}
