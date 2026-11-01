#ifndef DMA_H
#define DMA_H

#include <stdint.h>

/* DMA通道状态 */
typedef enum {
    DMA_FREE = 0,
    DMA_ALLOCATED,
    DMA_TRANSFERRING,
    DMA_COMPLETE,
    DMA_ERROR
} dma_state_t;

/* 初始化 */
void dma_init(void);

/* 通道管理 */
int  dma_alloc_channel(uint8_t width, uint8_t direction);
int  dma_free_channel(int channel);
int  dma_set_transfer(int channel, uint32_t phys_addr, uint32_t length);
int  dma_start(int channel);
int  dma_stop(int channel);
dma_state_t dma_get_state(int channel);

/* 一致性缓冲区 */
void    *dma_alloc_buffer(uint32_t size, uint32_t align);
int      dma_free_buffer(void *addr);
uint32_t dma_get_phys(void *addr);

/* 统计 */
void dma_stats(uint32_t *channels_used, uint32_t *buffers_used,
               uint32_t *total_transfers);

/* V6.50: 自测 */
void dma_selftest(void);

#endif
