/*
 * 赫普OS 页缓存（Page Cache）实现
 * 基于《赫普OS内存、显存与存储资源管理详解》3.4节实现
 *
 * 核心机制：
 * - 哈希表：O(1)查找缓存页
 * - LRU链表：内存不足时回收最久未使用的页
 * - 脏页标记：写操作标记脏页，由flusher定期写回磁盘
 * - 引用计数：防止正在使用的页被回收
 */

#include "page_cache.h"
#include <stddef.h>

/* 简单哈希函数（索引对哈希表大小取模） */
static inline uint32_t page_cache_hash(uint32_t index)
{
    /* 乘法哈希，减少冲突 */
    uint32_t h = index * 2654435761u;  /* Knuth乘法哈希常数 */
    return (h >> (32 - PAGE_CACHE_HASH_BITS)) & (PAGE_CACHE_HASH_SIZE - 1);
}

/* 从LRU链表移除条目 */
static void lru_remove(page_cache_t *cache, page_cache_entry_t *entry)
{
    if (entry->lru_prev)
        entry->lru_prev->lru_next = entry->lru_next;
    else
        cache->lru_head = entry->lru_next;  /* 移除头节点 */

    if (entry->lru_next)
        entry->lru_next->lru_prev = entry->lru_prev;
    else
        cache->lru_tail = entry->lru_prev;  /* 移除尾节点 */

    entry->lru_prev = NULL;
    entry->lru_next = NULL;
}

/* 添加到LRU链表头部（最近使用） */
static void lru_add_head(page_cache_t *cache, page_cache_entry_t *entry)
{
    entry->lru_prev = NULL;
    entry->lru_next = cache->lru_head;
    if (cache->lru_head)
        cache->lru_head->lru_prev = entry;
    cache->lru_head = entry;
    if (!cache->lru_tail)
        cache->lru_tail = entry;  /* 空链表时尾节点也是头节点 */
}

/* 移动到LRU链表头部（访问后调用，表示最近使用） */
static void lru_touch(page_cache_t *cache, page_cache_entry_t *entry)
{
    if (entry == cache->lru_head)
        return;  /* 已经在头部 */
    lru_remove(cache, entry);
    lru_add_head(cache, entry);
}

/* 创建页缓存 */
page_cache_t *page_cache_create(const char *name,
                                 int (*writeback)(page_cache_t *, page_cache_entry_t *))
{
    /* 从内核堆分配缓存结构（使用kmalloc） */
    extern void *kmalloc(uint32_t size);
    page_cache_t *cache = (page_cache_t *)kmalloc(sizeof(page_cache_t));
    if (!cache)
        return NULL;

    /* 初始化 */
    cache->name = name;
    cache->lru_head = NULL;
    cache->lru_tail = NULL;
    cache->total_pages = 0;
    cache->dirty_pages = 0;
    cache->hits = 0;
    cache->misses = 0;
    cache->writeback = writeback;

    /* 清空哈希表 */
    for (uint32_t i = 0; i < PAGE_CACHE_HASH_SIZE; i++)
        cache->hash_table[i] = NULL;

    return cache;
}

/* 查找缓存页 */
page_cache_entry_t *page_cache_lookup(page_cache_t *cache, uint32_t index)
{
    if (!cache)
        return NULL;

    uint32_t bucket = page_cache_hash(index);
    page_cache_entry_t *entry = cache->hash_table[bucket];

    while (entry) {
        if (entry->index == index) {
            /* 命中：增加引用计数，移动到LRU头部 */
            entry->refcount++;
            lru_touch(cache, entry);
            cache->hits++;
            return entry;
        }
        entry = entry->hash_next;
    }

    /* 未命中 */
    cache->misses++;
    return NULL;
}

/* 添加/更新缓存页 */
page_cache_entry_t *page_cache_add(page_cache_t *cache, uint32_t index,
                                    uint32_t phys_addr, uint32_t flags)
{
    if (!cache)
        return NULL;

    /* 先查找是否已存在 */
    uint32_t bucket = page_cache_hash(index);
    page_cache_entry_t *entry = cache->hash_table[bucket];

    while (entry) {
        if (entry->index == index) {
            /* 已存在：更新物理地址和标志 */
            entry->phys_addr = phys_addr;
            entry->flags = flags;
            entry->refcount++;
            lru_touch(cache, entry);
            return entry;
        }
        entry = entry->hash_next;
    }

    /* 检查是否超过最大缓存页数，超过则先回收 */
    if (cache->total_pages >= PAGE_CACHE_MAX_PAGES) {
        page_cache_reclaim(cache, PAGE_CACHE_MAX_PAGES / 4);  /* 回收1/4 */
    }

    /* 新建条目 */
    extern void *kmalloc(uint32_t size);
    entry = (page_cache_entry_t *)kmalloc(sizeof(page_cache_entry_t));
    if (!entry)
        return NULL;

    entry->index = index;
    entry->phys_addr = phys_addr;
    entry->flags = flags;
    entry->refcount = 1;
    entry->lru_prev = NULL;
    entry->lru_next = NULL;

    /* 添加到哈希桶头部 */
    entry->hash_next = cache->hash_table[bucket];
    cache->hash_table[bucket] = entry;

    /* 添加到LRU头部 */
    lru_add_head(cache, entry);

    cache->total_pages++;
    if (flags & PC_FLAG_DIRTY)
        cache->dirty_pages++;

    return entry;
}

/* 释放缓存页引用 */
void page_cache_release(page_cache_t *cache, page_cache_entry_t *entry)
{
    if (!cache || !entry)
        return;
    if (entry->refcount > 0)
        entry->refcount--;
}

/* 标记脏页 */
void page_cache_mark_dirty(page_cache_t *cache, page_cache_entry_t *entry)
{
    if (!cache || !entry)
        return;
    if (!(entry->flags & PC_FLAG_DIRTY)) {
        entry->flags |= PC_FLAG_DIRTY;
        cache->dirty_pages++;
    }
}

/* 使指定缓存页失效 */
int page_cache_invalidate(page_cache_t *cache, uint32_t index)
{
    if (!cache)
        return -1;

    uint32_t bucket = page_cache_hash(index);
    page_cache_entry_t *entry = cache->hash_table[bucket];
    page_cache_entry_t *prev = NULL;

    while (entry) {
        if (entry->index == index) {
            /* 正在使用的页不能失效 */
            if (entry->refcount > 0)
                return -1;

            /* 脏页先写回 */
            if ((entry->flags & PC_FLAG_DIRTY) && cache->writeback)
                cache->writeback(cache, entry);

            /* 从哈希表移除 */
            if (prev)
                prev->hash_next = entry->hash_next;
            else
                cache->hash_table[bucket] = entry->hash_next;

            /* 从LRU移除 */
            lru_remove(cache, entry);

            /* 更新统计 */
            cache->total_pages--;
            if (entry->flags & PC_FLAG_DIRTY)
                cache->dirty_pages--;

            /* 释放物理页和条目结构 */
            extern void kfree(void *ptr);
            extern void mm_free_page(uint32_t phys);
            if (entry->phys_addr)
                mm_free_page(entry->phys_addr);
            kfree(entry);

            return 0;
        }
        prev = entry;
        entry = entry->hash_next;
    }

    return -1;  /* 未找到 */
}

/* 写回所有脏页 */
int page_cache_flush(page_cache_t *cache)
{
    if (!cache || !cache->writeback)
        return -1;

    uint32_t flushed = 0;
    page_cache_entry_t *entry = cache->lru_head;

    while (entry) {
        page_cache_entry_t *next = entry->lru_next;  /* 保存下一个，防止写回时修改链表 */
        if (entry->flags & PC_FLAG_DIRTY) {
            if (cache->writeback(cache, entry) == 0) {
                entry->flags &= ~PC_FLAG_DIRTY;
                cache->dirty_pages--;
                flushed++;
            }
        }
        entry = next;
    }

    return flushed;
}

/* 回收缓存页（LRU，跳过脏页和引用页） */
uint32_t page_cache_reclaim(page_cache_t *cache, uint32_t count)
{
    if (!cache || count == 0)
        return 0;

    uint32_t reclaimed = 0;
    page_cache_entry_t *entry = cache->lru_tail;  /* 从尾部（最久未使用）开始 */

    while (entry && reclaimed < count) {
        page_cache_entry_t *prev = entry->lru_prev;  /* 保存前一个 */

        /* 跳过脏页（先写回才能回收）和引用页 */
        if (!(entry->flags & PC_FLAG_DIRTY) && entry->refcount == 0) {
            uint32_t bucket = page_cache_hash(entry->index);
            page_cache_entry_t *e = cache->hash_table[bucket];
            page_cache_entry_t *p = NULL;

            while (e) {
                if (e == entry) {
                    /* 从哈希表移除 */
                    if (p)
                        p->hash_next = e->hash_next;
                    else
                        cache->hash_table[bucket] = e->hash_next;
                    break;
                }
                p = e;
                e = e->hash_next;
            }

            /* 从LRU移除 */
            lru_remove(cache, entry);

            /* 释放物理页和条目结构 */
            extern void kfree(void *ptr);
            extern void mm_free_page(uint32_t phys);
            if (entry->phys_addr)
                mm_free_page(entry->phys_addr);
            kfree(entry);

            cache->total_pages--;
            reclaimed++;
        }

        entry = prev;
    }

    return reclaimed;
}

/* 销毁页缓存 */
void page_cache_destroy(page_cache_t *cache)
{
    if (!cache)
        return;

    /* 先写回所有脏页 */
    page_cache_flush(cache);

    /* 释放所有条目 */
    page_cache_entry_t *entry = cache->lru_head;
    while (entry) {
        page_cache_entry_t *next = entry->lru_next;
        extern void kfree(void *ptr);
        extern void mm_free_page(uint32_t phys);
        if (entry->phys_addr)
            mm_free_page(entry->phys_addr);
        kfree(entry);
        entry = next;
    }

    /* 释放缓存结构 */
    extern void kfree(void *ptr);
    kfree(cache);
}

/* 获取缓存统计信息 */
void page_cache_stats(page_cache_t *cache, uint32_t *total, uint32_t *dirty,
                      uint64_t *hits, uint64_t *misses)
{
    if (!cache)
        return;
    if (total)  *total  = cache->total_pages;
    if (dirty)  *dirty  = cache->dirty_pages;
    if (hits)   *hits   = cache->hits;
    if (misses) *misses = cache->misses;
}
