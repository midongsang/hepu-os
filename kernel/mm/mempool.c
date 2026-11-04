/* ============================================================================
 * 赫普OS - 固定大小内存池
 * 功能: 32/64/128B 块池, 快速分配/释放, 减少 kheap 碎片
 * 原理: 预分配大块, 切成固定大小块, 空闲链表管理
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "mempool.h"
#include "kheap.h"
#include "vga.h"

/* 内存池块头 (嵌入在空闲块开头) */
typedef struct mp_block {
    struct mp_block *next;
} mp_block_t;

/* 内存池描述符 */
typedef struct {
    uint32_t block_size;      /* 每块大小 */
    mp_block_t *free_list;     /* 空闲链表头 */
    uint32_t total_blocks;     /* 总块数 */
    uint32_t free_blocks;     /* 空闲块数 */
    uint32_t alloc_count;      /* 累计分配次数 */
    uint32_t free_count;       /* 累计释放次数 */
} mp_pool_t;

/* 三个固定大小池 */
#define POOL_32_SIZE    32
#define POOL_64_SIZE    64
#define POOL_128_SIZE   128

#define POOL_32_BLOCKS  256    /* 256*32 = 8KB */
#define POOL_64_BLOCKS  128    /* 128*64 = 8KB */
#define POOL_128_BLOCKS 64     /* 64*128 = 8KB */

static mp_pool_t pool_32;
static mp_pool_t pool_64;
static mp_pool_t pool_128;

static uint8_t pool_32_mem[POOL_32_BLOCKS * POOL_32_SIZE] __attribute__((aligned(16)));
static uint8_t pool_64_mem[POOL_64_BLOCKS * POOL_64_SIZE] __attribute__((aligned(16)));
static uint8_t pool_128_mem[POOL_128_BLOCKS * POOL_128_SIZE] __attribute__((aligned(16)));

/* 初始化一个池 */
static void mp_pool_init(mp_pool_t *pool, void *mem, uint32_t block_size, uint32_t num_blocks)
{
    pool->block_size = block_size;
    pool->total_blocks = num_blocks;
    pool->free_blocks = num_blocks;
    pool->alloc_count = 0;
    pool->free_count = 0;
    pool->free_list = NULL;

    /* 把所有块链入空闲链表 */
    uint8_t *p = (uint8_t *)mem;
    for (uint32_t i = 0; i < num_blocks; i++) {
        mp_block_t *block = (mp_block_t *)p;
        block->next = pool->free_list;
        pool->free_list = block;
        p += block_size;
    }
}

/* 从池分配 */
static void *mp_pool_alloc(mp_pool_t *pool)
{
    if (!pool->free_list) return NULL;
    mp_block_t *block = pool->free_list;
    pool->free_list = block->next;
    pool->free_blocks--;
    pool->alloc_count++;
    return (void *)block;
}

/* 释放回池 */
static void mp_pool_free(mp_pool_t *pool, void *ptr)
{
    if (!ptr) return;
    mp_block_t *block = (mp_block_t *)ptr;
    block->next = pool->free_list;
    pool->free_list = block;
    pool->free_blocks++;
    pool->free_count++;
}

/* 判断指针是否在池内存范围内 */
static int mp_pool_owns(mp_pool_t *pool, void *ptr, void *mem, uint32_t total_size)
{
    uint8_t *p = (uint8_t *)ptr;
    return (p >= (uint8_t *)mem && p < (uint8_t *)mem + total_size);
}

/* 初始化所有池 */
void mempool_init(void)
{
    mp_pool_init(&pool_32, pool_32_mem, POOL_32_SIZE, POOL_32_BLOCKS);
    mp_pool_init(&pool_64, pool_64_mem, POOL_64_SIZE, POOL_64_BLOCKS);
    mp_pool_init(&pool_128, pool_128_mem, POOL_128_SIZE, POOL_128_BLOCKS);
    vga_puts("[MPOOL] 3 pools ready (32B/64B/128B)\n");
}

/* 分配: 小对象走池, 大对象走 kheap */
void *mempool_alloc(uint32_t size)
{
    if (size <= POOL_32_SIZE)
        return mp_pool_alloc(&pool_32);
    if (size <= POOL_64_SIZE)
        return mp_pool_alloc(&pool_64);
    if (size <= POOL_128_SIZE)
        return mp_pool_alloc(&pool_128);
    return kmalloc(size);
}

/* 释放: 判断属于哪个池 */
void mempool_free(void *ptr)
{
    if (!ptr) return;
    if (mp_pool_owns(&pool_32, ptr, pool_32_mem, sizeof(pool_32_mem))) {
        mp_pool_free(&pool_32, ptr);
        return;
    }
    if (mp_pool_owns(&pool_64, ptr, pool_64_mem, sizeof(pool_64_mem))) {
        mp_pool_free(&pool_64, ptr);
        return;
    }
    if (mp_pool_owns(&pool_128, ptr, pool_128_mem, sizeof(pool_128_mem))) {
        mp_pool_free(&pool_128, ptr);
        return;
    }
    kfree(ptr);
}

/* 打印池统计 (供 shell 命令) */
void mempool_stats(void)
{
    vga_printf("[MPOOL] 32B: %d/%d free (%d alloc, %d free)\n",
               pool_32.free_blocks, pool_32.total_blocks,
               pool_32.alloc_count, pool_32.free_count);
    vga_printf("[MPOOL] 64B: %d/%d free (%d alloc, %d free)\n",
               pool_64.free_blocks, pool_64.total_blocks,
               pool_64.alloc_count, pool_64.free_count);
    vga_printf("[MPOOL] 128B: %d/%d free (%d alloc, %d free)\n",
               pool_128.free_blocks, pool_128.total_blocks,
               pool_128.alloc_count, pool_128.free_count);
}
