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
#define ALIGNED_TAG  0x4845505541474E44ULL /* "HEPUAGND": 对齐分配标记 */
#define BLOCK_ALIGN  16
#define ALIGN(x, a) (((x) + (a) - 1) & ~((uint64_t)(a) - 1))

/* 堆块头部（48字节，16字节对齐，去掉 packed）
 * 安全修复：
 *  - magic 与块地址绑定（HEADER_MAGIC ^ 块地址），伪造头无法直接复制常量
 *  - used 放在头部倒数第二个8字节：kfree 通过 ptr-16 是否等于 ALIGNED_TAG
 *    区分对齐分配，普通块 ptr-16 处恒为 used(0/1)，不会误判
 *    （旧实现读 ptr[-1]，恰好是 prev 字段，导致非首块 kfree 全部失败）
 */
typedef struct block_header {
    uint64_t magic;
    uint64_t size;            /* 数据区大小（不含头部），恒为16的倍数 */
    struct block_header *next;
    struct block_header *prev;
    uint64_t used;            /* 1=已分配, 0=空闲（必须在 ptr-16） */
    uint64_t reserved;        /* 保留（ptr-8） */
} block_header_t;

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

/* 堆锁：关中断 + 自旋锁（多核/中断上下文安全）
 * 安全修复：旧实现完全无锁，AP 核或中断处理中分配会破坏空闲链表 */
static volatile int heap_lock_word = 0;

#ifdef KHEAP_HOST_TEST
static inline uint64_t heap_lock_acquire(void) { return 0; }
static inline void heap_lock_release(uint64_t f) { (void)f; }
#else
static inline uint64_t heap_lock_acquire(void)
{
    /* 用编译器内建读 RFLAGS，避免内联汇编里 push/pop 踩到红区（未确认全工程 -mno-red-zone） */
    uint64_t flags = __builtin_ia32_readeflags_u64();
    __asm__ volatile("cli" : : : "memory");
    while (__atomic_exchange_n(&heap_lock_word, 1, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("pause" : : : "memory");
    }
    return flags;
}

static inline void heap_lock_release(uint64_t flags)
{
    __atomic_store_n(&heap_lock_word, 0, __ATOMIC_RELEASE);
    if (flags & (1ULL << 9)) {
        __asm__ volatile("sti" : : : "memory");
    }
}
#endif

/* 块头校验：范围、对齐、地址绑定 magic、size 不越界 */
static int hdr_valid(const block_header_t *h)
{
    uint64_t a = (uint64_t)h;
    if (!heap_start) return 0;
    if (a < (uint64_t)heap_start || a + HEADER_SIZE > heap_end) return 0;
    if (a & (BLOCK_ALIGN - 1)) return 0;
    if (h->magic != (HEADER_MAGIC ^ a)) return 0;
    if (h->size > heap_end - (a + HEADER_SIZE)) return 0;
    return 1;
}

/* 链表邻居校验（safe-unlink）：邻居存在时必须有效且回指自己 */
static int next_link_ok(const block_header_t *b)
{
    const block_header_t *n = b->next;
    if (!n) return 1;
    return hdr_valid(n) && n->prev == b;
}

static int prev_link_ok(const block_header_t *b)
{
    const block_header_t *p = b->prev;
    if (!p) return 1;
    return hdr_valid(p) && p->next == b;
}

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

/* V0.54.6: kheap 从 8MB 扩到 32MB
 * 前置: Stage2 页表已扩到 24×2MB (48MB) + 基址移到 0x2E00000 (不被 BSS 覆盖) */
#define KHEAP_SIZE (32 * 1024 * 1024)
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
    heap_start->magic = HEADER_MAGIC ^ (uint64_t)heap_start;
    heap_start->size = KHEAP_SIZE - HEADER_SIZE;
    heap_start->used = 0;
    heap_start->reserved = 0;
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

    /* V0.54.6: 动态打印 kheap 大小 */
    vga_puts("[KHEAP] kheap init: ");
    vga_put_hex((uint64_t)(heap_total >> 20));  /* MB */
    vga_puts("MB (0x");
    vga_put_hex(heap_total);
    vga_puts(" bytes)\n");
}

/* 分裂一个空闲块 */
static void split_block(block_header_t *block, uint64_t size)
{
    if (block->size < size + HEADER_SIZE + BLOCK_ALIGN) {
        return; /* 剩余空间太小，不分裂 */
    }
    block_header_t *new_block = (block_header_t *)(
        (uint8_t *)block + HEADER_SIZE + size);
    new_block->magic = HEADER_MAGIC ^ (uint64_t)new_block;
    new_block->size = block->size - size - HEADER_SIZE;
    new_block->used = 0;
    new_block->reserved = 0;
    new_block->next = block->next;
    new_block->prev = block;
    if (block->next) block->next->prev = new_block;
    block->next = new_block;
    block->size = size;
}

/* 合并相邻空闲块（带 safe-unlink 校验；被吞并的块头 magic 清零，
 * 之后对其再次 kfree 会被识别为无效而不是误判） */
static void merge_blocks(block_header_t *block)
{
    block_header_t *n = block->next;
    if (n && !n->used && hdr_valid(n) && n->prev == block && next_link_ok(n)) {
        block->size += HEADER_SIZE + n->size;
        block->next = n->next;
        if (n->next) n->next->prev = block;
        n->magic = 0;
    }
    block_header_t *p = block->prev;
    if (p && !p->used && hdr_valid(p) && p->next == block && next_link_ok(block)) {
        p->size += HEADER_SIZE + block->size;
        p->next = block->next;
        if (block->next) block->next->prev = p;
        block->magic = 0;
    }
}

/* 十进制输出辅助 */
static void put_dec(uint64_t n)
{
    char buf[24];
    int i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
    buf[i] = 0;
    vga_puts(buf);
}

/* 内部分配（调用方持锁） */
static void *alloc_locked(size_t size)
{
    if (size == 0) return NULL;
    /* 安全修复：先限制上界再对齐，避免 ALIGN 回绕成 0/小值 */
    if (size > heap_total) {
        kmalloc_fail++;
        return NULL;
    }
    size = ALIGN(size, BLOCK_ALIGN);

    block_header_t *cur = heap_start;
    while (cur) {
        if (!hdr_valid(cur)) {
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
    put_dec(size);
    vga_puts(" used=");
    put_dec(heap_used / 1024);
    vga_puts("KB\n");
    return NULL;
}

/* 内部：由用户指针找到所属块头；校验失败返回 NULL（调用方持锁）
 * 对齐分配通过 ptr-16 处的 ALIGNED_TAG 找到原始块 */
static block_header_t *find_block(void *ptr, int *was_aligned)
{
    uint64_t p = (uint64_t)ptr;
    if (was_aligned) *was_aligned = 0;
    if (!heap_start) return NULL;
    if (p < (uint64_t)heap_start + HEADER_SIZE + 0 || p >= heap_end) return NULL;
    if (p & (BLOCK_ALIGN - 1)) return NULL;

    uint64_t *tagp = (uint64_t *)(p - 16);
    if (tagp[0] == ALIGNED_TAG) {
        uint64_t raw = tagp[1];
        if (raw < (uint64_t)heap_start + HEADER_SIZE || raw >= p) return NULL;
        if (raw & (BLOCK_ALIGN - 1)) return NULL;
        p = raw;
        if (was_aligned) *was_aligned = 1;
    }
    block_header_t *blk = (block_header_t *)(p - HEADER_SIZE);
    if (!hdr_valid(blk)) return NULL;
    return blk;
}

/* 内部释放（调用方持锁） */
static void free_locked(void *ptr)
{
    if (!ptr) return;

    int aligned = 0;
    block_header_t *block = find_block(ptr, &aligned);
    if (!block) {
        vga_puts("[KHEAP] ERROR: invalid pointer/magic in kfree\n");
        return;
    }
    if (block->used == 0) {
        vga_puts("[KHEAP] WARNING: double free detected\n");
        return;
    }
    if (!prev_link_ok(block) || !next_link_ok(block)) {
        vga_puts("[KHEAP] ERROR: heap list corrupted in kfree\n");
        return;
    }
    if (aligned) {
        ((uint64_t *)((uint64_t)ptr - 16))[0] = 0;  /* 清除标记，防止对齐指针重复释放 */
    }

    block->used = 0;
    heap_used -= block->size + HEADER_SIZE;
    kfree_calls++;
    trace_del((void *)((uint8_t *)block + HEADER_SIZE));
    merge_blocks(block);
}

/* 分配内存（first-fit） */
void* kmalloc(size_t size)
{
    uint64_t f = heap_lock_acquire();
    void *p = alloc_locked(size);
    heap_lock_release(f);
    return p;
}

/* 对齐分配：alignment 必须是2的幂（最小16） */
void* kmalloc_aligned(size_t size, size_t alignment)
{
    if (alignment < BLOCK_ALIGN) alignment = BLOCK_ALIGN;
    if (alignment & (alignment - 1)) return NULL;   /* 非2的幂 */
    if (size == 0 || alignment > heap_total) return NULL;

    /* 安全修复：溢出检查（旧实现 size + alignment + HEADER_SIZE 可回绕） */
    size_t total;
    if (__builtin_add_overflow(size, alignment, &total)) return NULL;
    if (__builtin_add_overflow(total, (size_t)BLOCK_ALIGN, &total)) return NULL;

    uint64_t f = heap_lock_acquire();
    void *raw = alloc_locked(total);
    if (!raw) {
        heap_lock_release(f);
        return NULL;
    }

    /* raw 为16字节对齐，addr >= raw+16，保证 addr-16 仍在本块数据区内 */
    uint64_t addr = ((uint64_t)raw + alignment) & ~((uint64_t)alignment - 1);
    if (addr < (uint64_t)raw + 16) addr += alignment;

    uint64_t *tagp = (uint64_t *)(addr - 16);
    tagp[0] = ALIGNED_TAG;
    tagp[1] = (uint64_t)raw;
    heap_lock_release(f);

    return (void *)addr;
}

/* 释放内存 */
void kfree(void *ptr)
{
    uint64_t f = heap_lock_acquire();
    free_locked(ptr);
    heap_lock_release(f);
}

/* 重新分配内存（支持对齐分配的指针） */
void* krealloc(void *ptr, size_t new_size)
{
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }

    uint64_t f = heap_lock_acquire();
    int aligned = 0;
    block_header_t *block = find_block(ptr, &aligned);
    if (!block || !block->used) {
        heap_lock_release(f);
        return NULL;
    }

    uint64_t data_start = (uint64_t)block + HEADER_SIZE;
    uint64_t usable = block->size - ((uint64_t)ptr - data_start);

    if (usable >= new_size) {
        heap_lock_release(f);
        return ptr;  /* 空间足够 */
    }

    void *new_ptr = alloc_locked(new_size);
    if (!new_ptr) {
        heap_lock_release(f);
        return NULL;
    }
    kheap_memcpy(new_ptr, ptr, usable);
    free_locked(ptr);
    heap_lock_release(f);
    return new_ptr;
}

/* 分配并清零内存 */
void* kcalloc(size_t nmemb, size_t size)
{
    size_t total;
    /* 安全修复：乘法溢出检查（旧实现 nmemb*size 可回绕成小值导致堆溢出） */
    if (__builtin_mul_overflow(nmemb, size, &total)) return NULL;
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
    /* SB-06: 统计字段本来就在 alloc_locked/kfree_locked 的临界区里更新,
     * 这里读取也必须持同一把锁 —— 否则跨核读到 heap_used 已加、free 还没减
     * 的中间态, 会看到 used+free != total。 */
    uint64_t f = heap_lock_acquire();
    uint64_t u = heap_used;
    uint64_t t = heap_total;
    uint64_t ac = kmalloc_calls, fc = kfree_calls;
    heap_lock_release(f);

    if (total) *total = t;
    if (used) *used = u;
    if (free) *free = t - u;
    if (alloc_count) *alloc_count = ac;
    if (free_count) *free_count = fc;
}

/* 获取堆使用百分比 */
int kheap64_get_usage_percent(void)
{
    uint64_t f = heap_lock_acquire();
    uint64_t u = heap_used, t = heap_total;
    heap_lock_release(f);
    if (t == 0) return 0;
    return (int)(u * 100 / t);
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
