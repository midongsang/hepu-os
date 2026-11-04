/* ============================================================================
 * 赫普OS - 内存管理共享（架构无关）
 * 32位和64位共用的内存分配算法、数据结构和统计接口
 * V0.10.87: P2-4内存管理共享，统一源码树
 * ========================================================================== */

#ifndef SHARED_MM_H
#define SHARED_MM_H

#include <stdint.h>
#include <stddef.h>

/* 页大小 */
#define MM_PAGE_SIZE        4096
#define MM_PAGE_SHIFT       12
#define MM_PAGE_MASK        (~(MM_PAGE_SIZE - 1))

/* 页对齐 */
#define MM_PAGE_ALIGN(addr) (((addr) + MM_PAGE_SIZE - 1) & MM_PAGE_MASK)
#define MM_PAGE_ALIGN_DOWN(addr) ((addr) & MM_PAGE_MASK)

/* 内存区域类型 */
typedef enum {
    MM_REGION_FREE = 0,     /* 空闲 */
    MM_REGION_USED,         /* 已使用 */
    MM_REGION_RESERVED,     /* 保留（硬件/BIOS） */
    MM_REGION_KERNEL,       /* 内核代码/数据 */
    MM_REGION_MMIO,         /* 内存映射IO */
    MM_REGION_TYPE_MAX
} mm_region_type_t;

/* 内存区域 */
typedef struct mm_region {
    uint64_t         start;      /* 起始地址 */
    uint64_t         end;        /* 结束地址 */
    uint64_t         size;       /* 大小 */
    mm_region_type_t type;       /* 类型 */
    struct mm_region *next;      /* 下一个区域 */
} mm_region_t;

/* 物理内存统计 */
typedef struct {
    uint64_t total_pages;    /* 总页数 */
    uint64_t used_pages;     /* 已使用页数 */
    uint64_t free_pages;     /* 空闲页数 */
    uint64_t reserved_pages; /* 保留页数 */
    uint64_t total_bytes;    /* 总字节数 */
    uint64_t used_bytes;     /* 已使用字节数 */
    uint64_t free_bytes;     /* 空闲字节数 */
} mm_phys_stats_t;

/* 虚拟内存统计 */
typedef struct {
    uint64_t total_pages;
    uint64_t mapped_pages;
    uint64_t unmapped_pages;
    uint64_t kernel_pages;
    uint64_t user_pages;
} mm_virt_stats_t;

/* 内核堆统计 */
typedef struct {
    uint64_t total_size;     /* 堆总大小 */
    uint64_t used_size;      /* 已使用大小 */
    uint64_t free_size;      /* 空闲大小 */
    uint32_t alloc_count;    /* 分配次数 */
    uint32_t free_count;     /* 释放次数 */
    uint32_t block_count;    /* 内存块数量 */
    uint32_t leak_count;     /* 疑似泄漏数量 */
} mm_heap_stats_t;

/* 内存分配器类型 */
typedef enum {
    MM_ALLOC_FIRST_FIT = 0,  /* 首次适应 */
    MM_ALLOC_BEST_FIT,       /* 最佳适应 */
    MM_ALLOC_WORST_FIT,      /* 最差适应 */
    MM_ALLOC_BUDDY,          /* 伙伴系统 */
    MM_ALLOC_SLAB,           /* Slab分配器 */
    MM_ALLOC_TYPE_MAX
} mm_alloc_type_t;

/* 内存块（first-fit链表） */
typedef struct mm_block {
    uint32_t         magic;      /* 魔数（用于检测越界） */
    uint32_t         size;       /* 块大小（不含头部） */
    uint8_t          used;       /* 是否使用 */
    uint8_t          padding[3]; /* 对齐填充 */
    struct mm_block *next;       /* 下一个块 */
    struct mm_block *prev;       /* 上一个块 */
} mm_block_t;

/* 内存块魔数 */
#define MM_BLOCK_MAGIC 0x4D4D424C  /* "MMBL" */

/* 伙伴系统阶数 */
#define MM_BUDDY_MAX_ORDER 10  /* 最大阶数（2^10 * 4KB = 4MB） */

/* 伙伴系统块 */
typedef struct mm_buddy_block {
    struct mm_buddy_block *next;
    int order;
} mm_buddy_block_t;

/* Slab缓存 */
typedef struct mm_slab_cache {
    const char *name;
    uint32_t    obj_size;       /* 对象大小 */
    uint32_t    objs_per_slab;  /* 每个slab的对象数 */
    uint32_t    active_count;   /* 活跃对象数 */
    void       *full_slabs;     /* 满slab链表 */
    void       *partial_slabs;  /* 部分slab链表 */
    void       *free_slabs;     /* 空闲slab链表 */
} mm_slab_cache_t;

/* 内存池 */
typedef struct mm_pool {
    const char *name;
    uint8_t    *start;          /* 池起始地址 */
    uint8_t    *end;            /* 池结束地址 */
    uint32_t    block_size;     /* 块大小 */
    uint32_t    total_blocks;   /* 总块数 */
    uint32_t    used_blocks;    /* 已使用块数 */
    uint8_t    *bitmap;         /* 分配位图 */
} mm_pool_t;

/* 内存保护标志 */
#define MM_PROT_NONE    0x00
#define MM_PROT_READ    0x01
#define MM_PROT_WRITE   0x02
#define MM_PROT_EXEC    0x04
#define MM_PROT_KERNEL  0x08
#define MM_PROT_USER    0x10

/* 内存映射标志 */
#define MM_MAP_PRIVATE  0x01
#define MM_MAP_SHARED   0x02
#define MM_MAP_ANONYMOUS 0x04
#define MM_MAP_FIXED    0x08

/* 基础工具：向上取整到2的幂 */
static inline uint32_t mm_next_power_of_2(uint32_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

/* 基础工具：判断是否2的幂 */
static inline int mm_is_power_of_2(uint32_t v) {
    return v && !(v & (v - 1));
}

/* 基础工具：计算log2 */
static inline int mm_log2(uint32_t v) {
    int r = 0;
    while (v > 1) { v >>= 1; r++; }
    return r;
}

/* 基础工具：对齐到指定边界 */
static inline uint64_t mm_align(uint64_t addr, uint32_t alignment) {
    return (addr + alignment - 1) & ~((uint64_t)alignment - 1);
}

/* 基础工具：向下对齐 */
static inline uint64_t mm_align_down(uint64_t addr, uint32_t alignment) {
    return addr & ~((uint64_t)alignment - 1);
}

#endif /* SHARED_MM_H */
