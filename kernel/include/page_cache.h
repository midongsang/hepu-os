/*
 * 赫普OS 页缓存（Page Cache）
 * 基于《赫普OS内存、显存与存储资源管理详解》3.4节实现
 *
 * 页缓存是内存与存储之间的桥梁：
 * - 读文件：先查缓存，命中则直接从内存拷贝（微秒级），未命中则从磁盘读取到缓存（毫秒级）
 * - 写文件：数据写入页缓存立即返回，脏页由flusher线程定期写回磁盘
 * - 内存回收：内存不足时按LRU回收缓存页
 *
 * 设计说明：
 * - 当前系统无标准struct page，使用page_cache_entry_t封装物理页地址
 * - 使用哈希表（链式）实现快速查找，后续可优化为基数树（radix tree）
 * - LRU链表用于回收，最近使用的页在链表头部
 */

#ifndef _PAGE_CACHE_H
#define _PAGE_CACHE_H

#include <stdint.h>

#define PAGE_CACHE_HASH_BITS  10   /* 哈希表大小：2^10 = 1024桶 */
#define PAGE_CACHE_HASH_SIZE  (1 << PAGE_CACHE_HASH_BITS)
#define PAGE_CACHE_MAX_PAGES  4096 /* 最大缓存页数（4096 * 4KB = 16MB） */

/* 页缓存标志 */
#define PC_FLAG_DIRTY    (1 << 0)  /* 脏页：已修改，需要写回磁盘 */
#define PC_FLAG_UPTODATE (1 << 1)  /* 最新：数据与磁盘一致 */
#define PC_FLAG_LOCKED   (1 << 2)  /* 锁定：正在IO操作 */

/* 页缓存条目 */
typedef struct page_cache_entry {
    uint32_t index;              /* 页索引（文件内的页号 / 磁盘块号） */
    uint32_t phys_addr;          /* 物理页地址（4KB对齐） */
    uint32_t flags;              /* 标志位 */
    uint32_t refcount;           /* 引用计数 */
    /* LRU链表 */
    struct page_cache_entry *lru_prev;
    struct page_cache_entry *lru_next;
    /* 哈希桶链表 */
    struct page_cache_entry *hash_next;
} page_cache_entry_t;

/* 页缓存集合（每个文件系统/块设备一个） */
typedef struct page_cache {
    const char *name;            /* 缓存名称（用于调试） */
    page_cache_entry_t *hash_table[PAGE_CACHE_HASH_SIZE];  /* 哈希表 */
    page_cache_entry_t *lru_head;  /* LRU链表头（最近使用） */
    page_cache_entry_t *lru_tail;  /* LRU链表尾（最久未使用） */
    uint32_t total_pages;        /* 总缓存页数 */
    uint32_t dirty_pages;        /* 脏页数 */
    uint64_t hits;               /* 缓存命中次数 */
    uint64_t misses;             /* 缓存未命中次数 */
    /* 写回函数指针：脏页写回磁盘 */
    int (*writeback)(struct page_cache *cache, page_cache_entry_t *entry);
} page_cache_t;

/* 初始化页缓存 */
page_cache_t *page_cache_create(const char *name,
                                 int (*writeback)(page_cache_t *, page_cache_entry_t *));

/* 销毁页缓存（写回所有脏页后释放） */
void page_cache_destroy(page_cache_t *cache);

/* 查找缓存页：命中返回entry并增加引用计数，未命中返回NULL */
page_cache_entry_t *page_cache_lookup(page_cache_t *cache, uint32_t index);

/* 添加/更新缓存页：如果已存在则更新物理地址，否则新建条目 */
page_cache_entry_t *page_cache_add(page_cache_t *cache, uint32_t index,
                                    uint32_t phys_addr, uint32_t flags);

/* 释放缓存页引用（减少引用计数） */
void page_cache_release(page_cache_t *cache, page_cache_entry_t *entry);

/* 标记脏页 */
void page_cache_mark_dirty(page_cache_t *cache, page_cache_entry_t *entry);

/* 使指定缓存页失效（写回后从缓存移除） */
int page_cache_invalidate(page_cache_t *cache, uint32_t index);

/* 写回所有脏页 */
int page_cache_flush(page_cache_t *cache);

/* 回收指定数量的缓存页（LRU，跳过脏页和引用页） */
uint32_t page_cache_reclaim(page_cache_t *cache, uint32_t count);

/* 获取缓存统计信息 */
void page_cache_stats(page_cache_t *cache, uint32_t *total, uint32_t *dirty,
                      uint64_t *hits, uint64_t *misses);

#endif /* _PAGE_CACHE_H */
