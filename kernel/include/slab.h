#ifndef SLAB_H
#define SLAB_H

#include <stdint.h>

/* 初始化slab分配器 */
void slab_init(void);

/* 从slab缓存分配对象（32-1024字节） */
void *slab_alloc(uint32_t size);

/* 释放slab对象 */
int slab_free(void *ptr);

/* Slab统计 */
void slab_stats(uint32_t *total_slabs, uint32_t *total_alloc, uint32_t *total_mem);

#endif
