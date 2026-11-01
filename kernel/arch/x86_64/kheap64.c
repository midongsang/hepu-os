/* ============================================================================
 * 赫普OS 64位 - 内核堆分配器（从32位kheap.c移植）
 * 设计：链表式 first-fit 分配器，支持任意大小分配与对齐
 * 功能：Magic验证、分裂/合并、性能统计、泄漏追踪、对齐分配
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);

#define PAGE_SIZE 4096
#define HEADER_MAGIC 0x484550553634ULL  /* "HEPU64" */
#define ALIGN(x, a) (((x) + (a) - 1) & ~((uint64_t)(a) - 1))

/* 堆块头部 */
typedef struct block_header {
    uint64_t magic;
    uint64_t size;            /* 数据区大小（不含头部） */
    uint8_t  used;            /* 1=已分配, 0=空闲 */
    struct block_header *next;
    struct block_header *prev;
} __attribute__((packed)) block_header_t;

#define HEADER_SIZE sizeof(block_header_t)

/* 内核堆状态 */
static block_header_t *heap_start = NULL;
static uint64_t heap_end = 0;
static uint64_t heap_total = 0;
static uint64_t heap_used = 0;

/* 性能统计 */
static uint64_t kmalloc_calls = 0;
static uint64_t kfree_calls = 0;
static uint64_t kmalloc_total_bytes = 0;
static uint64_t kheap_peak = 0;
static uint64_t kmalloc_fail = 0;

/* 内存泄漏追踪（256条） */
#define KMEM_TRACE_SLOTS 256
typedef struct {
    void *ptr;
    uint64_t size;
} kmem_trace_entry_t;

static kmem_trace_entry_t kmem_trace[KMEM_TRACE_SLOTS];
static uint64_t kmem_trace_cnt = 0;
static uint64_t kmem_trace_overflow = 0;

/* 内存复制 */
static void kheap_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

/* 内存设置 */
static void kheap_memset(void *dst, uint8_t val, size_t n)
{
    uint8_t *d = (uint8_t*)dst;
    for (size_t i = 0; i < n; i++) d[i] = val;
}

/* 泄漏追踪：添加分配记录 */
static void trace_add(void *ptr, uint64_t size)
{
    if (kmem_trace_cnt >= KMEM_TRACE_SLOTS) {
        kmem_trace_overflow++;
        return;
    }
    for (int i = 0; i < KMEM_TRACE_SLOTS; i++) {
        if (!kmem_trace[i].ptr) {
            kmem_trace[i].ptr = ptr;
            kmem_trace[i].size = size;
            kmem_trace_cnt++;
            return;
        }
    }
    kmem_trace_overflow++;
}

/* 泄漏追踪：删除释放记录 */
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

/* 泄漏检测：打印活跃分配 */
void kmem_trace_dump(void)
{
    vga_puts("[LEAK] live allocs=");
    char buf[16];
    uint64_t n = kmem_trace_cnt;
    int i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
    vga_puts(buf);
    vga_puts(" overflow=");
    n = kmem_trace_overflow;
    i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
    vga_puts(buf);
    vga_puts("\n");
}

/* 内核堆静态数组（256KB，在内核BSS段，已映射） */
#define KHEAP_SIZE (256 * 1024)
static uint8_t kheap_memory[KHEAP_SIZE] __attribute__((aligned(4096)));

/* 初始化内核堆 */
void kheap64_init(void *start_addr, uint64_t size)
{
    /* 忽略传入参数，使用静态数组 */
    heap_start = (block_header_t *)kheap_memory;
    heap_end = (uint64_t)kheap_memory + KHEAP_SIZE;
    heap_total = KHEAP_SIZE;
    heap_used = 0;

    /* 初始化为一个大空闲块（必须用KHEAP_SIZE，传入的size=4MB与实际256KB不符会导致越界） */
    heap_start->magic = HEADER_MAGIC;
    heap_start->size = KHEAP_SIZE - HEADER_SIZE;
    heap_start->used = 0;
    heap_start->next = NULL;
    heap_start->prev = NULL;

    kmalloc_calls = 0;
    kfree_calls = 0;
    kmalloc_total_bytes = 0;
    kheap_peak = 0;
    kmalloc_fail = 0;
    kmem_trace_cnt = 0;
    kmem_trace_overflow = 0;
    for (int i = 0; i < KMEM_TRACE_SLOTS; i++) {
        kmem_trace[i].ptr = NULL;
        kmem_trace[i].size = 0;
    }

    vga_puts("[KHEAP] kheap init: 256KB (from 32-bit design)\n");
}

/* 分裂一个空闲块 */
static void split_block(block_header_t *block, uint64_t size)
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

/* 分配内存（first-fit） */
void* kmalloc(size_t size)
{
    if (size == 0) return NULL;
    size = ALIGN(size, 8);

    block_header_t *cur = heap_start;
    while (cur) {
        if (cur->magic != HEADER_MAGIC) {
            vga_puts("[KHEAP] ERROR: corrupted block header\n");
            return NULL;
        }
        if (!cur->used && cur->size >= size) {
            split_block(cur, size);
            cur->used = 1;
            heap_used += cur->size + HEADER_SIZE;
            kmalloc_calls++;
            kmalloc_total_bytes += size;
            if (heap_used > kheap_peak) kheap_peak = heap_used;
            void *ptr = (void *)((uint8_t *)cur + HEADER_SIZE);
            trace_add(ptr, size);
            return ptr;
        }
        cur = cur->next;
    }

    kmalloc_fail++;
    vga_puts("[KHEAP] OOM: req=");
    char buf[16];
    uint64_t n = size;
    int i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
    vga_puts(buf);
    vga_puts(" used=");
    n = heap_used / 1024;
    i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
    vga_puts(buf);
    vga_puts("KB\n");
    return NULL;
}

/* 对齐分配 */
void* kmalloc_aligned(size_t size, size_t alignment)
{
    if (alignment == 0) alignment = 8;
    if (alignment < 8) alignment = 8;

    /* 多分配 alignment + HEADER_SIZE 字节，用于对齐 */
    size_t total = size + alignment + HEADER_SIZE;
    void *raw = kmalloc(total);
    if (!raw) return NULL;

    /* 计算对齐后的地址 */
    uint64_t addr = (uint64_t)raw + alignment;
    addr &= ~(alignment - 1);

    /* 存储原始指针在对齐地址之前 */
    ((void **)addr)[-1] = raw;

    return (void *)addr;
}

/* 释放内存 */
void kfree(void *ptr)
{
    if (!ptr) return;

    /* 检查是否是对齐分配的指针 */
    void *raw = ((void **)ptr)[-1];
    /* 简单检查：如果raw在堆范围内，使用raw */
    if ((uint64_t)raw >= (uint64_t)heap_start &&
        (uint64_t)raw < heap_end) {
        ptr = raw;
    }

    block_header_t *block = (block_header_t *)((uint8_t *)ptr - HEADER_SIZE);

    if (block->magic != HEADER_MAGIC) {
        vga_puts("[KHEAP] ERROR: invalid magic in kfree\n");
        return;
    }

    if (block->used == 0) {
        vga_puts("[KHEAP] WARNING: double free detected\n");
        return;
    }

    block->used = 0;
    heap_used -= block->size + HEADER_SIZE;
    kfree_calls++;
    trace_del(ptr);
    merge_blocks(block);
}

/* 重新分配内存 */
void* krealloc(void *ptr, size_t new_size)
{
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }

    block_header_t *block = (block_header_t *)((uint8_t *)ptr - HEADER_SIZE);
    if (block->magic != HEADER_MAGIC) return NULL;

    if (block->size >= new_size) {
        return ptr;  /* 空间足够 */
    }

    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) return NULL;

    kheap_memcpy(new_ptr, ptr, block->size);
    kfree(ptr);

    return new_ptr;
}

/* 分配并清零内存 */
void* kcalloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *ptr = kmalloc(total);
    if (ptr) {
        kheap_memset(ptr, 0, total);
    }
    return ptr;
}

/* 获取堆统计信息 */
void kheap64_get_stats(uint64_t *total, uint64_t *used,
                       uint64_t *free, uint64_t *alloc_count,
                       uint64_t *free_count)
{
    if (total) *total = heap_total;
    if (used) *used = heap_used;
    if (free) *free = heap_total - heap_used;
    if (alloc_count) *alloc_count = kmalloc_calls;
    if (free_count) *free_count = kfree_calls;
}

/* 获取堆使用百分比 */
int kheap64_get_usage_percent(void)
{
    if (heap_total == 0) return 0;
    return (int)(heap_used * 100 / heap_total);
}

/* 获取堆峰值使用 */
uint64_t kheap64_get_peak(void)
{
    return kheap_peak;
}

/* 获取分配失败次数 */
uint64_t kheap64_get_fail_count(void)
{
    return kmalloc_fail;
}
