#ifndef JPEG64_H
#define JPEG64_H
#include <stdint.h>

typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t *rgba;      /* width*height*4, bump 分配, 常驻不回收 */
} jpeg64_image_t;

/* 0=成功, -1 参数错误, -2 解码失败(非 JPEG/截断/超限) */
int jpeg64_decode(const uint8_t *data, int len, jpeg64_image_t *out);
void jpeg64_free(jpeg64_image_t *img);

#endif
