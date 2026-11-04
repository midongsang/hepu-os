#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>
#include <stddef.h>

/* 内核堆初始化 */
void kheap_init(uint32_t start_addr, uint32_t size);

/* 内核内存分配 */
void *kmalloc(size_t size);
void *kmalloc_aligned(size_t size, size_t alignment);
void  kfree(void *ptr);

/* 堆统计 */
void kheap_stats(uint32_t *used, uint32_t *free, uint32_t *total);
void kheap_frag_stats(uint32_t *free_blocks, uint32_t *largest_free,
                      uint32_t *frag_rate_pct);

#endif /* KHEAP_H */
void kmem_trace_dump(void);
