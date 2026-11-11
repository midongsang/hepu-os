/* ============================================================================
 * 赫普OS - PNG 解码器 (V0.54.13 P0)
 *
 * 自包含实现，不依赖 zlib：
 *   - DEFLATE/inflate (stored + fixed huffman + dynamic huffman, RFC 1951)
 *   - PNG 解析 (IHDR/PLTE/tRNS/IDAT/IEND)
 *   - 反滤波 (None/Sub/Up/Average/Paeth, RFC 2083)
 *   - 输出统一为 RGBA8888
 *
 * 支持的颜色类型：0(灰度) / 2(RGB) / 3(调色板) / 4(灰度+alpha) / 6(RGBA)
 * 位深：8 / 16(取高字节) / 1,2,4(灰度与调色板按位展开)
 * 不支持：隔行(Adam7)。隔行图直接返回错误，由调用方回退。
 * ========================================================================== */
#ifndef PNG64_H
#define PNG64_H

#include <stdint.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t *rgba;      /* width*height*4, 调用方负责释放 */
} png64_image_t;

/* 返回值: 0=成功, <0=错误码
 *  -1 参数/长度错误   -2 签名不是 PNG   -3 不支持的格式(隔行/位深)
 *  -4 inflate 失败    -5 内存不足      -6 数据损坏 */
int png64_decode(const uint8_t *data, int len, png64_image_t *out);
void png64_free(png64_image_t *img);

#endif
