/* ============================================================================
 * 赫普OS - 固定大小内存池 头文件
 * ========================================================================== */
#ifndef __MPOOL_H
#define __MPOOL_H

#include <stdint.h>

void mempool_init(void);
void *mempool_alloc(uint32_t size);
void mempool_free(void *ptr);
void mempool_stats(void);

#endif
