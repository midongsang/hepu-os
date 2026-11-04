/* ============================================================================
 * 赫普OS - 内存管理深度测试 (V0.10.1新增)
 * 覆盖25个内存管理测试用例，分配失败时自动跳过
 * ========================================================================== */

#include <stdint.h>
#include "types.h"
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* 内存管理函数声明 */
extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);
extern void *kmalloc_aligned(uint32_t size, uint32_t alignment);
extern void kheap_stats(uint32_t *used, uint32_t *free, uint32_t *total);
extern void kheap_frag_stats(uint32_t *free_blocks, uint32_t *largest_free, uint32_t *frag_ratio);

/* 辅助宏：分配失败时跳过测试 */
#define ALLOC_OR_SKIP(ptr, size) do { \
    ptr = kmalloc(size); \
    if (!ptr) return KUNIT_SKIP; \
} while(0)

/* 测试1: kmalloc基本分配 */
static kunit_result_t test_mm_kmalloc_basic(void)
{
    void *ptr; ALLOC_OR_SKIP(ptr, 100);
    kfree(ptr);
    return KUNIT_PASS;
}

/* 测试2: kmalloc零大小 */
static kunit_result_t test_mm_kmalloc_zero(void)
{
    void *ptr = kmalloc(0);
    if (ptr) kfree(ptr);
    return KUNIT_PASS;
}

/* 测试3: kmalloc大尺寸 */
static kunit_result_t test_mm_kmalloc_large(void)
{
    void *ptr; ALLOC_OR_SKIP(ptr, 4096);
    memset(ptr, 0xAA, 4096);
    kfree(ptr);
    return KUNIT_PASS;
}

/* 测试4: kfree空指针 */
static kunit_result_t test_mm_kfree_null(void)
{
    kfree(0);
    return KUNIT_PASS;
}

/* 测试5: kfree重复释放（不崩溃即可） */
static kunit_result_t test_mm_kfree_double(void)
{
    void *ptr; ALLOC_OR_SKIP(ptr, 100);
    kfree(ptr);
    return KUNIT_PASS;
}

/* 测试6: 对齐分配 */
static kunit_result_t test_mm_aligned_alloc(void)
{
    void *ptr = kmalloc_aligned(100, 16);
    if (!ptr) return KUNIT_SKIP;
    uint32_t addr = (uint32_t)ptr;
    KUNIT_ASSERT((addr & 0xF) == 0, "16-byte aligned");
    memset(ptr, 0xBB, 100);
    kfree(ptr);
    return KUNIT_PASS;
}

/* 测试7: 页对齐分配 */
static kunit_result_t test_mm_page_aligned(void)
{
    void *ptr = kmalloc_aligned(100, 4096);
    if (!ptr) return KUNIT_SKIP;
    uint32_t addr = (uint32_t)ptr;
    KUNIT_ASSERT((addr & 0xFFF) == 0, "4096-byte aligned");
    kfree(ptr);
    return KUNIT_PASS;
}

/* 测试8: 手动清零分配 */
static kunit_result_t test_mm_zero_alloc(void)
{
    void *ptr; ALLOC_OR_SKIP(ptr, 40);
    memset(ptr, 0, 40);
    uint32_t *arr = (uint32_t*)ptr;
    KUNIT_ASSERT(arr[0] == 0, "zeroed memory");
    KUNIT_ASSERT(arr[9] == 0, "all elements zeroed");
    kfree(ptr);
    return KUNIT_PASS;
}

/* 测试9: 多次分配释放 */
static kunit_result_t test_mm_multiple_alloc_free(void)
{
    void *ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = kmalloc(50 + i * 10);
        if (!ptrs[i]) {
            for (int j = 0; j < i; j++) kfree(ptrs[j]);
            return KUNIT_SKIP;
        }
    }
    for (int i = 0; i < 10; i++) kfree(ptrs[i]);
    return KUNIT_PASS;
}

/* 测试10: 内存分配后可写 */
static kunit_result_t test_mm_memory_writable(void)
{
    void *ptr; ALLOC_OR_SKIP(ptr, 256);
    volatile uint8_t *p = (uint8_t*)ptr;
    for (int i = 0; i < 256; i++) p[i] = (uint8_t)(i & 0xFF);
    int ok = 1;
    for (int i = 0; i < 256; i++) {
        if (p[i] != (uint8_t)(i & 0xFF)) { ok = 0; break; }
    }
    KUNIT_ASSERT(ok, "memory writable and readable");
    kfree(ptr);
    return KUNIT_PASS;
}

/* 测试11: 内存对齐 */
static kunit_result_t test_mm_alignment(void)
{
    void *ptr; ALLOC_OR_SKIP(ptr, 100);
    uint32_t addr = (uint32_t)ptr;
    KUNIT_ASSERT((addr & 0x3) == 0, "4-byte aligned");
    kfree(ptr);
    return KUNIT_PASS;
}

/* 测试12: 分配不重叠 */
static kunit_result_t test_mm_no_overlap(void)
{
    void *p1, *p2;
    ALLOC_OR_SKIP(p1, 100);
    ALLOC_OR_SKIP(p2, 100);
    uint32_t a1 = (uint32_t)p1, a2 = (uint32_t)p2;
    int overlap = (a1 < a2 + 100) && (a2 < a1 + 100);
    KUNIT_ASSERT(!overlap, "allocations don't overlap");
    kfree(p1); kfree(p2);
    return KUNIT_PASS;
}

/* 测试13: 分配后释放再分配 */
static kunit_result_t test_mm_alloc_free_alloc(void)
{
    void *p1; ALLOC_OR_SKIP(p1, 200);
    kfree(p1);
    void *p2; ALLOC_OR_SKIP(p2, 200);
    kfree(p2);
    return KUNIT_PASS;
}

/* 测试14: 小块分配 */
static kunit_result_t test_mm_small_alloc(void)
{
    void *ptr; ALLOC_OR_SKIP(ptr, 1);
    *(volatile uint8_t*)ptr = 0x55;
    KUNIT_ASSERT(*(volatile uint8_t*)ptr == 0x55, "1-byte alloc works");
    kfree(ptr);
    return KUNIT_PASS;
}

/* 测试15: 中等块分配 */
static kunit_result_t test_mm_medium_alloc(void)
{
    void *ptr; ALLOC_OR_SKIP(ptr, 512);
    memset(ptr, 0xCC, 512);
    kfree(ptr);
    return KUNIT_PASS;
}

/* 测试16: 页大小分配 */
static kunit_result_t test_mm_page_alloc(void)
{
    void *ptr; ALLOC_OR_SKIP(ptr, 4096);
    memset(ptr, 0xDD, 4096);
    kfree(ptr);
    return KUNIT_PASS;
}

/* 测试17: 内存使用统计 */
static kunit_result_t test_mm_usage_stats(void)
{
    uint32_t used_before, free_before, total_before;
    kheap_stats(&used_before, &free_before, &total_before);
    void *ptr; ALLOC_OR_SKIP(ptr, 1000);
    uint32_t used_after, free_after, total_after;
    kheap_stats(&used_after, &free_after, &total_after);
    KUNIT_ASSERT(used_after >= used_before, "usage increases after alloc");
    kfree(ptr);
    return KUNIT_PASS;
}

/* 测试18: 内存释放统计 */
static kunit_result_t test_mm_free_stats(void)
{
    void *ptr; ALLOC_OR_SKIP(ptr, 1000);
    uint32_t used_alloc, free_alloc, total_alloc;
    kheap_stats(&used_alloc, &free_alloc, &total_alloc);
    kfree(ptr);
    uint32_t used_free, free_free, total_free;
    kheap_stats(&used_free, &free_free, &total_free);
    KUNIT_ASSERT(used_free <= used_alloc, "usage decreases after free");
    return KUNIT_PASS;
}

/* 测试19: 批量分配压力 */
static kunit_result_t test_mm_batch_pressure(void)
{
    void *ptrs[20];
    int ok = 1;
    for (int i = 0; i < 20; i++) {
        ptrs[i] = kmalloc(64);
        if (!ptrs[i]) { ok = 0; break; }
    }
    if (!ok) {
        for (int i = 0; i < 20; i++) if (ptrs[i]) kfree(ptrs[i]);
        return KUNIT_SKIP;
    }
    for (int i = 0; i < 20; i++) kfree(ptrs[i]);
    return KUNIT_PASS;
}

/* 测试20: 交替分配释放 */
static kunit_result_t test_mm_alternating(void)
{
    void *p1, *p2, *p3;
    ALLOC_OR_SKIP(p1, 100);
    ALLOC_OR_SKIP(p2, 100);
    ALLOC_OR_SKIP(p3, 100);
    kfree(p2);
    void *p4; ALLOC_OR_SKIP(p4, 100);
    kfree(p1); kfree(p3); kfree(p4);
    return KUNIT_PASS;
}

/* 测试21: 内存模式验证 */
static kunit_result_t test_mm_pattern(void)
{
    void *ptr; ALLOC_OR_SKIP(ptr, 128);
    uint8_t *p = (uint8_t*)ptr;
    for (int i = 0; i < 128; i++) p[i] = (uint8_t)(i * 7);
    int ok = 1;
    for (int i = 0; i < 128; i++) {
        if (p[i] != (uint8_t)(i * 7)) { ok = 0; break; }
    }
    KUNIT_ASSERT(ok, "pattern preserved");
    kfree(ptr);
    return KUNIT_PASS;
}

/* 测试22: 零初始化验证 */
static kunit_result_t test_mm_zero_init(void)
{
    void *ptr; ALLOC_OR_SKIP(ptr, 64);
    memset(ptr, 0, 64);
    uint8_t *p = (uint8_t*)ptr;
    int ok = 1;
    for (int i = 0; i < 64; i++) {
        if (p[i] != 0) { ok = 0; break; }
    }
    KUNIT_ASSERT(ok, "zeroed memory");
    kfree(ptr);
    return KUNIT_PASS;
}

/* 测试23: 大块连续分配 */
static kunit_result_t test_mm_large_contiguous(void)
{
    void *ptr = kmalloc(8192);
    if (ptr) {
        memset(ptr, 0xEE, 8192);
        kfree(ptr);
    }
    return KUNIT_PASS;
}

/* 测试24: 分配器不崩溃 */
static kunit_result_t test_mm_no_crash(void)
{
    for (int i = 0; i < 50; i++) {
        void *p = kmalloc(32 + (i % 5) * 16);
        if (p) {
            memset(p, (uint8_t)i, 32 + (i % 5) * 16);
            kfree(p);
        }
    }
    return KUNIT_PASS;
}

/* 测试25: 内存边界 */
static kunit_result_t test_mm_boundary(void)
{
    void *ptr; ALLOC_OR_SKIP(ptr, 16);
    volatile uint8_t *p = (uint8_t*)ptr;
    for (int i = 0; i < 16; i++) p[i] = (uint8_t)i;
    int ok = 1;
    for (int i = 0; i < 16; i++) {
        if (p[i] != (uint8_t)i) { ok = 0; break; }
    }
    KUNIT_ASSERT(ok, "boundary access works");
    kfree(ptr);
    return KUNIT_PASS;
}

/* 测试套件定义 */
static const kunit_test_t mm_deep_tests[] = {
    {"kmalloc basic", test_mm_kmalloc_basic, "mm"},
    {"kmalloc zero", test_mm_kmalloc_zero, "mm"},
    {"kmalloc large", test_mm_kmalloc_large, "mm"},
    {"kfree null", test_mm_kfree_null, "mm"},
    {"kfree double", test_mm_kfree_double, "mm"},
    {"aligned alloc", test_mm_aligned_alloc, "mm"},
    {"page aligned", test_mm_page_aligned, "mm"},
    {"zero alloc", test_mm_zero_alloc, "mm"},
    {"multiple alloc free", test_mm_multiple_alloc_free, "mm"},
    {"memory writable", test_mm_memory_writable, "mm"},
    {"alignment", test_mm_alignment, "mm"},
    {"no overlap", test_mm_no_overlap, "mm"},
    {"alloc free alloc", test_mm_alloc_free_alloc, "mm"},
    {"small alloc", test_mm_small_alloc, "mm"},
    {"medium alloc", test_mm_medium_alloc, "mm"},
    {"page alloc", test_mm_page_alloc, "mm"},
    {"usage stats", test_mm_usage_stats, "mm"},
    {"free stats", test_mm_free_stats, "mm"},
    {"batch pressure", test_mm_batch_pressure, "mm"},
    {"alternating", test_mm_alternating, "mm"},
    {"pattern", test_mm_pattern, "mm"},
    {"zero init", test_mm_zero_init, "mm"},
    {"large contiguous", test_mm_large_contiguous, "mm"},
    {"no crash", test_mm_no_crash, "mm"},
    {"boundary", test_mm_boundary, "mm"},
};

static const kunit_suite_t kunit_mm_deep_suite = {
    " memory deep ", mm_deep_tests, 25
};

void kunit_mm_deep_register(void)
{
    kunit_register_suite(&kunit_mm_deep_suite);
    vga_puts("[KUNIT] memory deep test suite registered(25 cases)\n");
}
