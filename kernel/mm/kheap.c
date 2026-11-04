/* ============================================================================
 * 赫普OS - 内核堆分配器
 * 实现：链表式 first-fit 分配器，支持任意大小分配与对齐
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "kheap.h"
#include "string.h"
#include "smp.h"
#include "vga.h"

/* 堆块头部 (V3.88-E16: 增加 magic 字段——防对齐识别误判/垃圾块头) */
#define HEADER_MAGIC 0x48455055u  /* 'HEPU' */
typedef struct block_header {
    uint32_t magic;           /* HEADER_MAGIC 防误判 */
    uint32_t size;            /* 数据区大小（不含头部） */
    uint8_t  used;            /* 1=已分配, 0=空闲 */
    struct block_header *next;
    struct block_header *prev;
} block_header_t;

#define HEADER_SIZE sizeof(block_header_t)
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))

static block_header_t *heap_start = NULL;
static uint32_t heap_end = 0;
static uint32_t heap_total = 0;
static uint32_t heap_used = 0;

/* V3.87-E3: 全局堆锁——kmalloc/kfree 临界区中断安全(防中断重入破坏堆结构) */
static spinlock_t kheap_lock = SPINLOCK_INIT;

/* V3.87-E1: 堆性能指标(供 perf/泄漏追踪) */
static uint32_t kmalloc_calls = 0;      /* kmalloc 成功次数 */
static uint32_t kfree_calls = 0;        /* kfree 次数 */
static uint32_t kmalloc_total_bytes = 0; /* 累计分配字节 */
static uint32_t kheap_peak = 0;          /* 已用峰值 */
static uint32_t kmalloc_fail = 0;        /* 分配失败次数 */

/* V3.88-E16: 内存泄漏调用点追踪——
 * 记录每个未释放分配的调用者返回地址; kfree 按指针反查删除。
 * 容量 512 条, 满则停止记录并置 overflow(泄漏检测只保证最近 512 次活分配可见)。 */
#define KMEM_TRACE_SLOTS 512
typedef struct {
    void *ptr;         /* 分配返回指针(原始, 非对齐) */
    uint32_t caller;   /* __builtin_return_address(0) */
    uint32_t size;     /* 请求大小 */
} kmem_trace_entry_t;

static kmem_trace_entry_t kmem_trace[KMEM_TRACE_SLOTS];
static uint32_t kmem_trace_cnt = 0;
static uint32_t kmem_trace_overflow = 0;

static void trace_add(void *ptr, uint32_t caller, uint32_t size)
{
    if (kmem_trace_cnt >= KMEM_TRACE_SLOTS) { kmem_trace_overflow++; return; }
    for (int i = 0; i < KMEM_TRACE_SLOTS; i++) {
        if (!kmem_trace[i].ptr) {
            kmem_trace[i].ptr = ptr;
            kmem_trace[i].caller = caller;
            kmem_trace[i].size = size;
            kmem_trace_cnt++;
            return;
        }
    }
    kmem_trace_overflow++;
}

static void trace_del(void *ptr)
{
    if (!ptr) return;
    for (int i = 0; i < KMEM_TRACE_SLOTS; i++) {
        if (kmem_trace[i].ptr == ptr) {
            kmem_trace[i].ptr = NULL;
            if (kmem_trace_cnt > 0) kmem_trace_cnt--;
            return;
        }
    }
}

/* 泄漏检测: 聚合活跃分配按调用点统计 */
void kmem_trace_dump(void)
{
    vga_printf("[LEAK] live allocs=%d overflow=%d\n",
               kmem_trace_cnt, kmem_trace_overflow);
    /* 按 caller 聚合(简单选择排序, 只打印前 12 个热点) */
    uint32_t callers[24];
    uint32_t counts[24];
    uint32_t bytes[24];
    int nc = 0;
    for (int i = 0; i < KMEM_TRACE_SLOTS; i++) {
        if (!kmem_trace[i].ptr) continue;
        uint32_t c = kmem_trace[i].caller;
        int found = -1;
        for (int j = 0; j < nc; j++)
            if (callers[j] == c) { found = j; break; }
        if (found >= 0) {
            counts[found]++;
            bytes[found] += kmem_trace[i].size;
        } else if (nc < 24) {
            callers[nc] = c;
            counts[nc] = 1;
            bytes[nc] = kmem_trace[i].size;
            nc++;
        }
    }
    /* 冒泡: 按计数降序 */
    for (int i = 0; i < nc - 1; i++)
        for (int j = 0; j < nc - 1 - i; j++)
            if (counts[j] < counts[j + 1]) {
                uint32_t t;
                t = counts[j]; counts[j] = counts[j + 1]; counts[j + 1] = t;
                t = callers[j]; callers[j] = callers[j + 1]; callers[j + 1] = t;
                t = bytes[j]; bytes[j] = bytes[j + 1]; bytes[j + 1] = t;
            }
    int show = nc < 12 ? nc : 12;
    for (int i = 0; i < show; i++) {
        vga_printf("  caller=0x%x cnt=%d bytes=%d\n",
                   callers[i], counts[i], bytes[i]);
    }
    /* 诊断: 打印最多 8 条残留条目的原始指针(区分 slab 区/堆区) */
    vga_puts("  -- raw ptrs (first 8) --\n");
    int shown = 0;
    for (int i = 0; i < KMEM_TRACE_SLOTS && shown < 8; i++) {
        if (kmem_trace[i].ptr)
            vga_printf("    ptr=0x%x caller=0x%x size=%d\n",
                       (uint32_t)kmem_trace[i].ptr,
                       kmem_trace[i].caller, kmem_trace[i].size), shown++;
    }
    vga_puts("  (symbol: objdump -t build_v250/kernel.elf | grep <caller>)\n");
}

void kheap_perf(uint32_t *calls, uint32_t *frees, uint32_t *bytes,
                uint32_t *peak, uint32_t *fails)
{
    if (calls) *calls = kmalloc_calls;
    if (frees) *frees = kfree_calls;
    if (bytes) *bytes = kmalloc_total_bytes;
    if (peak)  *peak = kheap_peak;
    if (fails) *fails = kmalloc_fail;
}
/* V3.83 诊断: 记录最后一次大分配(>=64KB)的调用者, OOM 时输出 */
static uint32_t last_big_caller = 0;
static uint32_t last_big_size = 0;

void kheap_init(uint32_t start_addr, uint32_t size)
{
    /* V3.94-E40: 注册 kheap_lock 到死锁监控 */
    extern int lock_monitor_register(spinlock_t *, const char *);
    lock_monitor_register(&kheap_lock, "kheap_lock");

    heap_start = (block_header_t *)start_addr;
    heap_end = start_addr + size;
    heap_total = size;
    heap_used = 0;

    /* 初始化为一个大空闲块 */
    heap_start->magic = HEADER_MAGIC;
    heap_start->size = size - HEADER_SIZE;
    heap_start->used = 0;
    heap_start->next = NULL;
    heap_start->prev = NULL;

    vga_printf("[KHEAP] kheap init: start =0x%x size =%d KB\n",
               start_addr, size / 1024);
}

/* 分裂一个空闲块 */
static void split_block(block_header_t *block, uint32_t size)
{
    if (block->size <= size + HEADER_SIZE + 16) {
        return; /* 剩余空间太小，不分裂 */
    }
    block_header_t *new_block = (block_header_t *)(
        (uint8_t *)block + HEADER_SIZE + size);
    new_block->magic = HEADER_MAGIC;
    new_block->size = block->size - size - HEADER_SIZE;
    new_block->used = 0;
    new_block->next = block->next;
    new_block->prev = block;
    if (block->next) block->next->prev = new_block;
    block->next = new_block;
    block->size = size;
}

/* 合并相邻空闲块 */
static void merge_blocks(block_header_t *block)
{
    /* 合并下一个 */
    if (block->next && !block->next->used) {
        block->size += HEADER_SIZE + block->next->size;
        block_header_t *next = block->next;
        block->next = next->next;
        if (next->next) next->next->prev = block;
    }
    /* 合并前一个 */
    if (block->prev && !block->prev->used) {
        block->prev->size += HEADER_SIZE + block->size;
        block->prev->next = block->next;
        if (block->next) block->next->prev = block->prev;
    }
}

void *kmalloc(size_t size)
{
    void *ret = NULL;
    if (size == 0) return NULL;
    size = ALIGN(size, 4);

    /* V3.87-E3: 中断安全全局堆锁——保护 slab 缓存与堆链表 */
    uint32_t flags;
    spin_lock_irqsave(&kheap_lock, &flags);

    /* V3.87-E2: 小分配(<=1024B)走 slab 固定池——O(1) 分配 + 消碎片
     * (slab 页由 mm_alloc_page 提供, 不占 heap; 失败回退链表堆) */
    if (size <= 1024) {
        extern void *slab_alloc(uint32_t);
        void *p = slab_alloc(size);
        if (p) {
            kmalloc_calls++;
            kmalloc_total_bytes += size;
            trace_add(p, (uint32_t)__builtin_return_address(0), size);
            ret = p;
            goto out;
        }
    }

    /* V3.83 诊断: 大块分配(>=64KB)记录调用者, 用于定位越界写 */
    if (size >= 64 * 1024) {
        last_big_caller = (uint32_t)__builtin_return_address(0);
        last_big_size = size;
        /* 走串口防滚动: 每个大分配立即记录 */
        extern void serial_puts(uint16_t base, const char *s);
        extern void serial_puthex(uint16_t base, uint32_t val);
        serial_puts(0x3F8, "[KHEAP-BIG] size=");
        serial_puthex(0x3F8, size);
        serial_puts(0x3F8, " caller=");
        serial_puthex(0x3F8, last_big_caller);
        serial_puts(0x3F8, "\n");
    }

    block_header_t *cur = heap_start;
    while (cur) {
        if (!cur->used && cur->size >= size) {
            split_block(cur, size);
            cur->used = 1;
            heap_used += cur->size + HEADER_SIZE;
            kmalloc_calls++;                    /* V3.87-E1 */
            kmalloc_total_bytes += size;
            if (heap_used > kheap_peak) kheap_peak = heap_used;
            ret = (void *)((uint8_t *)cur + HEADER_SIZE);
            trace_add(ret, (uint32_t)__builtin_return_address(0), size);
            goto out;
        }
        cur = cur->next;
    }
    kmalloc_fail++;  /* V3.87-E1 */
    /* V6.85: OOM时先释放锁，避免vga_printf/klog内部调用kmalloc导致递归死锁 */
    spin_unlock_irqrestore(&kheap_lock, flags);
    extern void klog_printf_lvl(int, const char *, ...);
    klog_printf_lvl(0, "[KHEAP] OOM: req=%d used=%d total=%d free=%d\n",
                    size, heap_used, heap_total, heap_total - heap_used);
    vga_printf("[KHEAP] warn: kheap OOM ! req=%d used=%d total=%d free=%d\n",
               size, heap_used, heap_total, heap_total - heap_used);
    /* 重新获取锁以保持out处unlock平衡 */
    spin_lock_irqsave(&kheap_lock, &flags);
out:
    spin_unlock_irqrestore(&kheap_lock, flags);  /* V3.87-E3 */
    return ret;
}

void *kmalloc_aligned(size_t size, size_t alignment)
{
    /* V3.21 重写: 在返回地址前 4 字节保存 raw 指针，kfree 可还原块头，
     * 消除 V3.15 标注的"kfree(对齐地址) 破坏堆"缺陷。
     * 分配 raw+alignment+8，确保 aligned-4 >= raw 且 aligned+size 不越界。 */
    if (alignment <= 1) return kmalloc(size);
    if (alignment > 4096) alignment = 4096;  /* 合理上限 */

    size_t total = size + alignment + 8;
    void *raw = kmalloc(total);
    if (!raw) return NULL;

    uint32_t aligned = ALIGN((uint32_t)raw + 4, alignment);
    ((uint32_t *)(aligned - 4))[0] = (uint32_t)raw;
    return (void *)aligned;
}

void kfree(void *ptr)
{
    if (!ptr) return;

    /* V3.87-E3: 全局堆锁(与 kmalloc 同一把锁) */
    uint32_t flags;
    spin_lock_irqsave(&kheap_lock, &flags);

    /* V3.87-E2: slab 归属判定——slab_free 内部做 magic+对象区间双重校验,
     * 非 slab 对象(堆块)返回 -1 走原释放路径 */
    extern int slab_free(void *);
    if (slab_free(ptr) == 0) {
        kfree_calls++;
        trace_del(ptr);
        spin_unlock_irqrestore(&kheap_lock, flags);
        return;
    }

    /* V4.47-E92: 对齐分配释放——kmalloc_aligned 在 aligned-4 存 raw 指针。
     * raw 若是 slab 对象: slab_free(aligned) 因对象不对齐判定失败, 若继续走
     * 堆块路径会误报 "bad block magic" 且 slab 对象泄漏。此处用 raw 释放。 */
    if ((uint32_t)ptr > 0x1000) {
        uint32_t candidate = *(uint32_t *)((uint8_t *)ptr - 4);
        if (candidate >= 0x1000) {
            /* slab 结构位于对象所在页首(4K), 首字段 magic == 0x534C4241 ("SLBA") */
            volatile uint32_t *pgm = (volatile uint32_t *)((uint32_t)candidate & ~0xFFFu);
            if (*pgm == 0x534C4241) {
                if (slab_free((void *)candidate) == 0) {
                    kfree_calls++;
                    trace_del((void *)candidate);
                    spin_unlock_irqrestore(&kheap_lock, flags);
                    return;
                }
                /* raw 是 slab 页但释放失败(重复释放等): 泄漏容忍, 不再误报堆块 */
                spin_unlock_irqrestore(&kheap_lock, flags);
                return;
            }
        }
    }

    block_header_t *block;

    /* V3.21: 对齐分配识别——ptr-4 可能存有 raw 指针（仅当验证全过才信任） */
    uint32_t candidate = *(uint32_t *)((uint8_t *)ptr - 4);
    block_header_t *cb = (block_header_t *)((uint8_t *)candidate - HEADER_SIZE);
    if ((uint32_t)candidate >= (uint32_t)heap_start &&
        (uint32_t)candidate < heap_end &&
        (uint8_t *)cb >= (uint8_t *)heap_start &&
        cb->magic == HEADER_MAGIC &&       /* V3.88-E16: magic 校验防误判 */
        cb->used == 1 &&
        (uint8_t *)candidate == (uint8_t *)cb + HEADER_SIZE &&
        (uint32_t)ptr >= candidate &&
        (uint32_t)ptr - candidate < cb->size) {
        block = cb;  /* 对齐指针：用原始块头释放 */
    } else {
        block = (block_header_t *)((uint8_t *)ptr - HEADER_SIZE);
    }

    if (block->magic != HEADER_MAGIC) {
        vga_printf("[KHEAP] error: bad block magic at 0x%x (slab/misalign?) caller=0x%x\n",
                   (uint32_t)ptr, (uint32_t)__builtin_return_address(0));
        spin_unlock_irqrestore(&kheap_lock, flags);
        return;
    }

    if (!block->used) {
        vga_puts("[KHEAP] error: double free !\n");
        spin_unlock_irqrestore(&kheap_lock, flags);
        return;
    }

    block->used = 0;
    heap_used -= block->size + HEADER_SIZE;
    kfree_calls++;  /* V3.87-E1 */
    /* E16: 对齐分配释放时用 raw 指针(candidate)反查追踪 */
    trace_del(block == cb ? (void *)candidate : ptr);
    merge_blocks(block);
    spin_unlock_irqrestore(&kheap_lock, flags);  /* V3.87-E3 */
}

void kheap_stats(uint32_t *used, uint32_t *free, uint32_t *total)
{
    if (used)  *used = heap_used;
    if (free)  *free = heap_total - heap_used;
    if (total) *total = heap_total;
}

/* V3.95-E41: 堆碎片统计——空闲块数/最大连续块/碎片率(1-最大块/总空闲, %).
 * 碎片率=0 表示空闲区连续(无碎片); 越高说明空闲区越破碎. */
void kheap_frag_stats(uint32_t *free_blocks, uint32_t *largest_free,
                      uint32_t *frag_rate_pct)
{
    block_header_t *cur = heap_start;
    uint32_t nfree = 0, largest = 0;
    while (cur) {
        if (!cur->used) {
            nfree++;
            if (cur->size > largest) largest = cur->size;
        }
        cur = cur->next;
    }
    uint32_t total_free = heap_total - heap_used;
    if (free_blocks)  *free_blocks  = nfree;
    if (largest_free) *largest_free = largest;
    if (frag_rate_pct) {
        if (nfree <= 1 || total_free == 0) *frag_rate_pct = 0;
        else *frag_rate_pct = 100u - (largest * 100u / total_free);
    }
}

/* V3.83 诊断: 返回已分配块 ptr 的下一块头地址(块头未损坏时的理论值) */
void *kheap_peek_next(void *ptr)
{
    if (!ptr) return NULL;
    block_header_t *b = (block_header_t *)((uint8_t *)ptr - HEADER_SIZE);
    return (void *)((uint8_t *)b + HEADER_SIZE + b->size);
}
