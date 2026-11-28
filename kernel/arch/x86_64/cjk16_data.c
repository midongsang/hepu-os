/* ============================================================================
 * 赫普OS 64位 - 16x16 CJK 点阵兜底
 *
 * 64 位桌面内置的 NotoSansCJK 子集只有 724 字；输入法词库 1,985 字，
 * TTF 缺字会让候选/正文出现空白。这里复用 32 位已有的 font_cjk_big
 * 3,775 字点阵，在 gfx64 TTF 渲染失败时兜底。
 *
 * 字体数据是纯只读资源，按码点二分查找；绘制为二值点阵，小字号（12~18px）
 * 与现有 UI 字号匹配，大字号会有明显颗粒感，后续再替换为外置轮廓字体。
 * ============================================================================ */
#include <stdint.h>
#include "font_cjk_big.h"
#include "cjk16.h"

extern void gfx64_put_pixel(int x, int y, uint32_t color);

static int cjk16_find(uint32_t cp)
{
    int lo = 0;
    int hi = (int)HEPU_CJK_BIG_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t v = hepu_cjk_big[mid].unicode;
        if (v == cp) return mid;
        if (v < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

int cjk16_has(uint32_t cp)
{
    return cjk16_find(cp) >= 0;
}

int cjk16_draw(uint32_t cp, int x, int y, uint32_t color, int size)
{
    if (size <= 0) return 0;
    int idx = cjk16_find(cp);
    if (idx < 0) return 0;

    const uint8_t *glyph = hepu_cjk_big[idx].glyph;
    for (int ty = 0; ty < size; ty++) {
        int sy = (ty * 16) / size;
        if (sy < 0) sy = 0;
        if (sy > 15) sy = 15;
        const uint8_t *row = glyph + sy * 2;
        for (int tx = 0; tx < size; tx++) {
            int sx = (tx * 16) / size;
            if (sx < 0) sx = 0;
            if (sx > 15) sx = 15;
            uint8_t bits = row[sx >> 3];
            if (bits & (uint8_t)(0x80u >> (sx & 7))) {
                gfx64_put_pixel(x + tx, y + ty, color);
            }
        }
    }
    return size;
}
