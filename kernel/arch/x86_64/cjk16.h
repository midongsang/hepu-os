#ifndef HEPU_CJK16_H
#define HEPU_CJK16_H

#include <stdint.h>

/* 16x16 点阵兜底：供 TTF 子集缺字时绘制 CJK。
 * 数据源为 font_cjk_big.h（3,775 个码点，按码点升序）。 */
int cjk16_has(uint32_t cp);
int cjk16_draw(uint32_t cp, int x, int y, uint32_t color, int size);

#endif
