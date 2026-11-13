/* cursor_modern.h — 内置第二套光标 (Modern) */
#ifndef CURSOR_MODERN_H
#define CURSOR_MODERN_H
#include <stdint.h>

typedef struct {
    int id;
    int w, h;
    int hx, hy;
    const uint8_t *alpha;
    const uint8_t *bits;    /* 每像素 1bit: 1=填充色, 0=描边色 */
    uint32_t fill;
    uint32_t stroke;
} cur_modern_t;

extern const cur_modern_t g_cur_modern[];
extern const int g_cur_modern_count;

#endif
