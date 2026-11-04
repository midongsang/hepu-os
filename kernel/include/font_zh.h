#ifndef FONT_ZH_H
#define FONT_ZH_H

#include <stdint.h>

/*
 * 赫普OS 中文渲染 (V2.36)
 * 基于内置 16x16 点阵字库 (kernel/fonts/font_zh.h, 584 常用字)。
 * 支持 UTF-8 中英文混排: 中文 16x16 占 16px, 英文 8x8 占 8px。
 */

/* 查询 UTF-8 三字节编码对应的字模索引, 未找到返回 -1 */
int zh_lookup(const uint8_t *utf8);

/* 渲染单个 UTF-8 字符 (中文/英文), 返回消耗的 UTF-8 字节数 */
int gfx_putchar_zh(int x, int y, const char *utf8, uint8_t color);

/* 渲染 UTF-8 字符串 (中英文混排), 返回字符串像素宽度 */
int gfx_puts_zh(int x, int y, const char *s, uint8_t color);

/* 计算 UTF-8 字符串像素宽度 (不绘制) */
int zh_str_width(const char *s);

#endif /* FONT_ZH_H */
