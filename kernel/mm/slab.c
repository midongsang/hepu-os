/* ============================================================================
 * 赫普OS - Slab分配器
 * 实现：固定大小对象的高效缓存分配（类似Linux SLAB）
 * 支持：32/64/128/256/512/1024字节对象缓存
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "slab.h"
#include "kheap.h"
#include "mm.h"
#include "string.h"
#include "vga.h"

#define SLAB_MAGIC 0x534C4241  /* "SLBA" */
#define SLAB_CACHE_COUNT 6

/* Slab头 */
typedef struct slab {
    uint32_t       magic;
    struct slab   *next;
    struct slab   *prev;
    uint32_t       free_count;
    uint32_t       *free_map;     /* 空闲位图 */
    uint8_t       *objects;      /* 对象数组 */
} slab_t;

/* Slab缓存 */
typedef struct {
    uint32_t  obj_size;          /* 对象大小 */
    uint32_t  objs_per_slab;     /* 每个slab的对象数 */
    slab_t   *full;              /* 满slab链表 */
    slab_t   *partial;           /* 部分slab链表 */
    slab_t   *empty;             /* 空slab链表 */
    uint32_t  total_slabs;
    uint32_t  total_allocations;
} slab_cache_t;

/* 全局slab缓存 */
static slab_cache_t slab_caches[SLAB_CACHE_COUNT];
static uint32_t slab_sizes[SLAB_CACHE_COUNT] = {32, 64, 128, 256, 512, 1024};
static int slab_initialized = 0;

/* 计算每个slab的对象数 */
static uint32_t calc_objs_per_slab(uint32_t obj_size)
{
    /* 一个slab占一页(4KB)，减去slab头和位图 */
    uint32_t overhead = sizeof(slab_t) + 32;  /* 32字节位图足够 */
    uint32_t available = PAGE_SIZE - overhead;
    return available / (obj_size + 1);  /* +1 for bitmap bit */
}

/* 创建新slab */
static slab_t *slab_create(slab_cache_t *cache)
{
    /* V6.85: 用mm_alloc_page分配slab页。
     * 注意：mm_alloc_page必须跳过页表页（pmm.c中is_page_table_page检查），
     * 否则slab元数据会覆盖页表项，导致系统挂起（BUG-30）。 */
    uint32_t phys = mm_alloc_page();
    if (!phys) return NULL;

    slab_t *slab = (slab_t *)phys;
    slab->magic = SLAB_MAGIC;
    slab->next = NULL;
    slab->prev = NULL;
    slab->free_count = cache->objs_per_slab;
    slab->free_map = (uint32_t *)((uint8_t *)slab + sizeof(slab_t));
    slab->objects = (uint8_t *)slab + sizeof(slab_t) + 32;

    /* 初始化位图：全部空闲(0) */
    memset(slab->free_map, 0, 32);

    cache->total_slabs++;
    return slab;
}

/* 从slab分配对象 */
static void *slab_alloc_obj(slab_cache_t *cache, slab_t *slab)
{
    if (slab->free_count == 0) return NULL;

    /* 找第一个空闲位 */
    for (uint32_t i = 0; i < cache->objs_per_slab; i++) {
        uint32_t idx = i / 32;
        uint32_t bit = i % 32;
        if (!(slab->free_map[idx] & (1 << bit))) {
            slab->free_map[idx] |= (1 << bit);
            slab->free_count--;
            cache->total_allocations++;
            return slab->objects + i * cache->obj_size;
        }
    }
    return NULL;
}

/* 释放对象到slab */
static int slab_free_obj(slab_cache_t *cache, slab_t *slab, void *obj)
{
    uint32_t offset = (uint8_t *)obj - slab->objects;
    if (offset % cache->obj_size != 0) return -1;
    uint32_t index = offset / cache->obj_size;
    if (index >= cache->objs_per_slab) return -1;

    uint32_t idx = index / 32;
    uint32_t bit = index % 32;
    if (!(slab->free_map[idx] & (1 << bit))) return -1;  /* 重复释放 */

    slab->free_map[idx] &= ~(1 << bit);
    slab->free_count++;
    return 0;
}

/* 从链表移除slab */
static void slab_remove(slab_t **list, slab_t *slab)
{
    if (slab->prev) slab->prev->next = slab->next;
    else *list = slab->next;
    if (slab->next) slab->next->prev = slab->prev;
    slab->next = slab->prev = NULL;
}

/* 添加到链表头部 */
static void slab_add(slab_t **list, slab_t *slab)
{
    slab->next = *list;
    slab->prev = NULL;
    if (*list) (*list)->prev = slab;
    *list = slab;
}

/* 初始化slab分配器 */
void slab_init(void)
{
    for (int i = 0; i < SLAB_CACHE_COUNT; i++) {
        slab_caches[i].obj_size = slab_sizes[i];
        slab_caches[i].objs_per_slab = calc_objs_per_slab(slab_sizes[i]);
        slab_caches[i].full = NULL;
        slab_caches[i].partial = NULL;
        slab_caches[i].empty = NULL;
        slab_caches[i].total_slabs = 0;
        slab_caches[i].total_allocations = 0;
    }
    slab_initialized = 1;
    vga_printf("[SLAB] init done: %d cache(32-1024 bytes)\n", SLAB_CACHE_COUNT);
}

/* 从slab缓存分配 */
void *slab_alloc(uint32_t size)
{
    if (!slab_initialized || size == 0) return NULL;

    /* 找合适的缓存 */
    int cache_idx = -1;
    for (int i = 0; i < SLAB_CACHE_COUNT; i++) {
        if (size <= slab_sizes[i]) {
            cache_idx = i;
            break;
        }
    }
    if (cache_idx < 0) return NULL;  /* 超过最大slab大小 */

    slab_cache_t *cache = &slab_caches[cache_idx];

    /* 先从partial链表分配 */
    if (cache->partial) {
        void *obj = slab_alloc_obj(cache, cache->partial);
        if (obj) {
            if (cache->partial->free_count == 0) {
                /* 移到full链表 */
                slab_t *s = cache->partial;
                slab_remove(&cache->partial, s);
                slab_add(&cache->full, s);
            }
            return obj;
        }
    }

    /* 再从empty链表 */
    if (cache->empty) {
        slab_t *s = cache->empty;
        slab_remove(&cache->empty, s);
        void *obj = slab_alloc_obj(cache, s);
        slab_add(&cache->partial, s);
        return obj;
    }

    /* 创建新slab */
    slab_t *new_slab = slab_create(cache);
    if (!new_slab) return NULL;
    slab_add(&cache->partial, new_slab);
    return slab_alloc_obj(cache, new_slab);
}

/* 释放slab对象 */
int slab_free(void *ptr)
{
    if (!slab_initialized || !ptr) return -1;

    /* 找到对象所属的slab（页对齐） */
    uint32_t page = (uint32_t)ptr & ~(PAGE_SIZE - 1);
    slab_t *slab = (slab_t *)page;
    if (slab->magic != SLAB_MAGIC) return -1;

    /* 找到对应的缓存 */
    slab_cache_t *cache = NULL;
    for (int i = 0; i < SLAB_CACHE_COUNT; i++) {
        if (slab_caches[i].obj_size >= (uint32_t)((uint8_t *)ptr - slab->objects) &&
            (uint32_t)((uint8_t *)ptr - slab->objects) % slab_caches[i].obj_size == 0) {
            /* 验证对象在范围内 */
            uint32_t offset = (uint8_t *)ptr - slab->objects;
            if (offset < slab_caches[i].objs_per_slab * slab_caches[i].obj_size) {
                cache = &slab_caches[i];
                break;
            }
        }
    }
    if (!cache) return -1;

    /* 确定slab在哪个链表 */
    slab_t **list = NULL;
    if (slab->free_count == 0) list = &cache->full;
    else if (slab->free_count == cache->objs_per_slab) list = &cache->empty;
    else list = &cache->partial;

    int ret = slab_free_obj(cache, slab, ptr);
    if (ret < 0) return ret;

    /* 移动到合适的链表 */
    slab_remove(list, slab);
    if (slab->free_count == cache->objs_per_slab) {
        slab_add(&cache->empty, slab);
    } else {
        slab_add(&cache->partial, slab);
    }
    return 0;
}

/* Slab统计 */
void slab_stats(uint32_t *total_slabs, uint32_t *total_alloc, uint32_t *total_mem)
{
    uint32_t slabs = 0, alloc = 0, mem = 0;
    for (int i = 0; i < SLAB_CACHE_COUNT; i++) {
        slabs += slab_caches[i].total_slabs;
        alloc += slab_caches[i].total_allocations;
        mem += slab_caches[i].total_slabs * PAGE_SIZE;
    }
    if (total_slabs) *total_slabs = slabs;
    if (total_alloc) *total_alloc = alloc;
    if (total_mem) *total_mem = mem;
}
