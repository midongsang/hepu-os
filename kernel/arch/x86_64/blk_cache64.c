/* ============================================================================
 * 赫普OS 64位 - 统一块缓存层 (Block Cache)
 * V0.13.58: 从ext2专用缓存抽取为存储层通用缓存
 * - 扇区粒度(512B)缓存槽, 写穿(write-through), LRU淘汰
 * - 挂在 storage_hal 层, 所有存储驱动(IDE/USB/AHCI/NVMe)共享
 * - 缓存槽数据由 kmalloc 动态分配(防BSS越界), 懒初始化
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "blk_cache64.h"

#define BLK_CACHE_SLOTS  256              /* 256 * 512B = 128KB 缓存 */
#define BLK_CACHE_SECTOR 512
#define BLK_CACHE_INVALID 0xFFFFFFFF      /* 槽空闲标记 */

typedef struct {
    uint32_t lba;                         /* 起始LBA, 0xFFFFFFFF=空闲 */
    uint32_t lru;                         /* 最近使用计数(越大越新) */
    uint8_t  *data;                       /* 512B 扇区数据 */
} blk_cache_slot_t;

static blk_cache_slot_t *g_slots = 0;     /* kmalloc 动态分配 */
static int g_slots_alloc = 0;             /* 是否已分配 */
static uint32_t g_lru_counter = 0;
static uint32_t g_hits = 0;
static uint32_t g_misses = 0;

extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);
extern void *memset(void *dst, int val, unsigned long n);
extern void *memcpy(void *dst, const void *src, unsigned long n);

/* 分配全部缓存槽 */
static int blk_cache_alloc_slots(void)
{
    if (g_slots_alloc) return 0;

    g_slots = (blk_cache_slot_t *)kmalloc(sizeof(blk_cache_slot_t) * BLK_CACHE_SLOTS);
    if (!g_slots) return -1;

    for (int i = 0; i < BLK_CACHE_SLOTS; i++) {
        g_slots[i].lba = BLK_CACHE_INVALID;
        g_slots[i].lru = 0;
        g_slots[i].data = (uint8_t *)kmalloc(BLK_CACHE_SECTOR);
        if (!g_slots[i].data) {
            /* 释放已分配的槽 */
            for (int j = 0; j < i; j++) kfree(g_slots[j].data);
            kfree(g_slots);
            g_slots = 0;
            return -1;
        }
    }
    g_slots_alloc = 1;
    return 0;
}

/* 初始化(幂等) */
void blk_cache_init(void)
{
    if (!g_slots_alloc) blk_cache_alloc_slots();
}

/* 按LBA查找槽, 命中则更新LRU */
static blk_cache_slot_t *blk_cache_find(uint32_t lba)
{
    for (int i = 0; i < BLK_CACHE_SLOTS; i++) {
        if (g_slots[i].lba == lba) {
            g_slots[i].lru = ++g_lru_counter;
            return &g_slots[i];
        }
    }
    return 0;
}

/* LRU淘汰: 找lru最小的槽 */
static blk_cache_slot_t *blk_cache_victim(void)
{
    int best = 0;
    for (int i = 1; i < BLK_CACHE_SLOTS; i++) {
        if (g_slots[i].lru < g_slots[best].lru) best = i;
    }
    return &g_slots[best];
}

/* 带缓存读: 逐扇区命中/填充 */
int blk_cache_read(uint32_t lba, uint8_t count, uint8_t *buffer)
{
    if (!buffer || count == 0) return -1;
    blk_cache_init();
    if (!g_slots_alloc) return -1;

    for (uint32_t s = 0; s < count; s++) {
        uint32_t cur = lba + s;
        blk_cache_slot_t *slot = blk_cache_find(cur);
        if (slot) {
            memcpy(buffer + s * BLK_CACHE_SECTOR, slot->data, BLK_CACHE_SECTOR);
            g_hits++;
        } else {
            /* 未命中: 底层原始读单扇区 */
            if (storage_hal_raw_read(cur, 1, buffer + s * BLK_CACHE_SECTOR) != 0) {
                return -1;
            }
            slot = blk_cache_victim();
            slot->lba = cur;
            slot->lru = ++g_lru_counter;
            memcpy(slot->data, buffer + s * BLK_CACHE_SECTOR, BLK_CACHE_SECTOR);
            g_misses++;
        }
    }
    return 0;
}

/* 前向声明：写失败时需失效缓存（定义在本文件后部） */
void blk_cache_invalidate(uint32_t lba, uint8_t count);

/* 带缓存写: 写穿直落盘 + 更新缓存 */
int blk_cache_write(uint32_t lba, uint8_t count, const uint8_t *buffer)
{
    if (!buffer || count == 0) return -1;
    blk_cache_init();
    if (!g_slots_alloc) return -1;

    /* 先直写盘(写穿)。V0.54.12(R4 FS-03): 写失败（含多扇区部分落盘）时
       失效对应缓存槽，否则缓存可能保留旧数据，后续读得到脏内容。 */
    if (storage_hal_raw_write(lba, count, buffer) != 0) {
        blk_cache_invalidate(lba, count);
        return -1;
    }

    /* 更新缓存: 命中槽更新, 未命中填充 */
    for (uint32_t s = 0; s < count; s++) {
        uint32_t cur = lba + s;
        blk_cache_slot_t *slot = blk_cache_find(cur);
        if (slot) {
            memcpy(slot->data, buffer + s * BLK_CACHE_SECTOR, BLK_CACHE_SECTOR);
        } else {
            slot = blk_cache_victim();
            slot->lba = cur;
            slot->lru = ++g_lru_counter;
            memcpy(slot->data, buffer + s * BLK_CACHE_SECTOR, BLK_CACHE_SECTOR);
        }
    }
    return 0;
}

/* 缓存失效: 直写路径绕过缓存后调用, 避免旧数据 */
void blk_cache_invalidate(uint32_t lba, uint8_t count)
{
    if (!g_slots_alloc) return;
    for (uint32_t s = 0; s < count; s++) {
        blk_cache_slot_t *slot = blk_cache_find(lba + s);
        if (slot) slot->lba = BLK_CACHE_INVALID;
    }
}

/* 清空全部缓存槽 */
void blk_cache_flush(void)
{
    if (!g_slots_alloc) return;
    for (int i = 0; i < BLK_CACHE_SLOTS; i++) {
        g_slots[i].lba = BLK_CACHE_INVALID;
        g_slots[i].lru = 0;
    }
}

/* 统计 */
int blk_cache_get_stats(uint32_t *hits, uint32_t *misses,
                        uint32_t *slots_used, uint32_t *slots_total)
{
    blk_cache_init();
    if (!g_slots_alloc) return -1;
    if (hits) *hits = g_hits;
    if (misses) *misses = g_misses;
    if (slots_total) *slots_total = BLK_CACHE_SLOTS;
    if (slots_used) {
        uint32_t used = 0;
        for (int i = 0; i < BLK_CACHE_SLOTS; i++) {
            if (g_slots[i].lba != BLK_CACHE_INVALID) used++;
        }
        *slots_used = used;
    }
    return 0;
}
