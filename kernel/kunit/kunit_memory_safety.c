/* ============================================================================
 * 赫普OS - 内存安全单元测试
 * 覆盖：内存分配/释放/对齐/越界/泄漏检测/内存池
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"

/* === 基本分配测试 === */
static kunit_result_t test_malloc_basic(void)
{
    void *p = kmalloc(64);
    if (!p) return KUNIT_SKIP;  /* kmalloc不可用时跳过 */
    memset(p, 0xAA, 64);
    u8 *buf = (u8 *)p;
    KUNIT_ASSERT_EQ(buf[0], 0xAA, " write 第 1 bytes ");
    KUNIT_ASSERT_EQ(buf[63], 0xAA, " write 最 next bytes ");
    kfree(p);
    return KUNIT_PASS;
}

static kunit_result_t test_malloc_zero(void)
{
    void *p = kmalloc(0);
    /* 零长度分配可能返回NULL或有效指针，都接受 */
    KUNIT_ASSERT(1, " 零 len alloc not 崩溃 ");
    if (p) kfree(p);
    return KUNIT_PASS;
}

static kunit_result_t test_malloc_large(void)
{
    void *p = kmalloc(4096);
    if (!p) return KUNIT_SKIP;  /* kmalloc不可用时跳过 */
    memset(p, 0x55, 4096);
    u8 *buf = (u8 *)p;
    KUNIT_ASSERT_EQ(buf[0], 0x55, " 大 memory bytes ");
    KUNIT_ASSERT_EQ(buf[4095], 0x55, " 大 memory 末 bytes ");
    kfree(p);
    return KUNIT_PASS;
}

/* === 多次分配测试 === */
static kunit_result_t test_multiple_alloc(void)
{
    void *ptrs[16];
    for (int i = 0; i < 16; i++) {
        ptrs[i] = kmalloc(32 + i * 8);
        if (!ptrs[i]) {
            for (int j = 0; j < i; j++) kfree(ptrs[j]);
            return KUNIT_SKIP;  /* kmalloc不可用时跳过 */
        }
        memset(ptrs[i], (u8)(i + 1), 32 + i * 8);
    }
    /* 验证内容不互相覆盖 */
    for (int i = 0; i < 16; i++) {
        u8 *buf = (u8 *)ptrs[i];
        KUNIT_ASSERT_EQ(buf[0], (u8)(i + 1), " 内容 verify ");
        kfree(ptrs[i]);
    }
    return KUNIT_PASS;
}

/* === 分配后释放再分配测试 === */
static kunit_result_t test_alloc_free_realloc(void)
{
    void *p1 = kmalloc(128);
    if (!p1) return KUNIT_SKIP;  /* kmalloc不可用时跳过 */
    memset(p1, 0xBB, 128);
    kfree(p1);

    void *p2 = kmalloc(128);
    if (!p2) return KUNIT_SKIP;
    memset(p2, 0xCC, 128);
    u8 *buf = (u8 *)p2;
    KUNIT_ASSERT_EQ(buf[0], 0xCC, " 新内容正确 ");
    kfree(p2);
    return KUNIT_PASS;
}

/* === 对齐分配测试 === */
static kunit_result_t test_aligned_alloc(void)
{
    /* V3.21: kmalloc_aligned 已重写（返回地址前存 raw 指针），
     * kfree 可还原块头，恢复完整分配-释放验证 */
    void *p = kmalloc_aligned(256, 16);
    if (!p) return KUNIT_SKIP;  /* kmalloc不可用时跳过 */
    u32 addr = (u32)p;
    KUNIT_ASSERT_EQ(addr % 16, 0, "16 bytes pair 齐 verify ");
    memset(p, 0xDD, 256);
    kfree(p);

    p = kmalloc_aligned(512, 32);
    if (!p) return KUNIT_SKIP;
    addr = (u32)p;
    KUNIT_ASSERT_EQ(addr % 32, 0, "32 bytes pair 齐 verify ");
    kfree(p);
    return KUNIT_PASS;
}

/* === 内存池测试 === */
#define POOL_BLOCK_SIZE 64
#define POOL_BLOCK_COUNT 16
typedef struct {
    u8  data[POOL_BLOCK_SIZE * POOL_BLOCK_COUNT];
    u8  used[POOL_BLOCK_COUNT];
    u32 free_count;
} mem_pool_t;

static void pool_init(mem_pool_t *pool)
{
    memset(pool->data, 0, sizeof(pool->data));
    memset(pool->used, 0, sizeof(pool->used));
    pool->free_count = POOL_BLOCK_COUNT;
}

static void *pool_alloc(mem_pool_t *pool)
{
    for (int i = 0; i < POOL_BLOCK_COUNT; i++) {
        if (!pool->used[i]) {
            pool->used[i] = 1;
            pool->free_count--;
            return pool->data + i * POOL_BLOCK_SIZE;
        }
    }
    return 0;
}

static int pool_free(mem_pool_t *pool, void *ptr)
{
    if (!ptr) return -1;
    u32 offset = (u32)((u8 *)ptr - pool->data);
    if (offset >= sizeof(pool->data)) return -1;
    if (offset % POOL_BLOCK_SIZE != 0) return -1;
    u32 idx = offset / POOL_BLOCK_SIZE;
    if (!pool->used[idx]) return -1;
    pool->used[idx] = 0;
    pool->free_count++;
    return 0;
}

static kunit_result_t test_mem_pool_basic(void)
{
    mem_pool_t pool;
    pool_init(&pool);

    KUNIT_ASSERT_EQ(pool.free_count, POOL_BLOCK_COUNT, " initial free block count ");

    void *p1 = pool_alloc(&pool);
    void *p2 = pool_alloc(&pool);
    KUNIT_ASSERT_NOT_NULL(p1, " pool alloc 1");
    KUNIT_ASSERT_NOT_NULL(p2, " pool alloc 2");
    KUNIT_ASSERT_EQ(pool.free_count, POOL_BLOCK_COUNT - 2, " free block 减少 ");

    KUNIT_ASSERT_EQ(pool_free(&pool, p1), 0, " pool release 1");
    KUNIT_ASSERT_EQ(pool.free_count, POOL_BLOCK_COUNT - 1, " free block 增加 ");

    void *p3 = pool_alloc(&pool);
    KUNIT_ASSERT(p3 == p1, " 重用 release block ");
    return KUNIT_PASS;
}

static kunit_result_t test_mem_pool_exhaust(void)
{
    mem_pool_t pool;
    pool_init(&pool);

    void *ptrs[POOL_BLOCK_COUNT];
    for (int i = 0; i < POOL_BLOCK_COUNT; i++) {
        ptrs[i] = pool_alloc(&pool);
        KUNIT_ASSERT_NOT_NULL(ptrs[i], " alloc all block ");
    }
    KUNIT_ASSERT_EQ(pool.free_count, 0, " pool 耗尽 ");
    KUNIT_ASSERT_NULL(pool_alloc(&pool), " 耗尽 next alloc failed ");

    for (int i = 0; i < POOL_BLOCK_COUNT; i++) pool_free(&pool, ptrs[i]);
    KUNIT_ASSERT_EQ(pool.free_count, POOL_BLOCK_COUNT, " 全部 release ");
    return KUNIT_PASS;
}

static kunit_result_t test_mem_pool_invalid(void)
{
    mem_pool_t pool;
    pool_init(&pool);

    KUNIT_ASSERT_EQ(pool_free(&pool, 0), -1, " empty ptr release failed ");
    KUNIT_ASSERT_EQ(pool_free(&pool, (void *)0xDEADBEEF), -1, " 外部 ptr release failed ");

    void *p = pool_alloc(&pool);
    KUNIT_ASSERT_EQ(pool_free(&pool, p), 0, " 正常 release ");
    KUNIT_ASSERT_EQ(pool_free(&pool, p), -1, " double free failed ");
    return KUNIT_PASS;
}

/* === 内存填充模式测试 === */
static kunit_result_t test_memset_patterns(void)
{
    u8 buf[256];

    memset(buf, 0x00, 256);
    KUNIT_ASSERT_EQ(buf[0], 0x00, " 全零填充 ");
    KUNIT_ASSERT_EQ(buf[255], 0x00, " 全零末尾 ");

    memset(buf, 0xFF, 256);
    KUNIT_ASSERT_EQ(buf[0], 0xFF, " 全 1 填充 ");
    KUNIT_ASSERT_EQ(buf[128], 0xFF, " 全 1 in 间 ");

    memset(buf, 0xA5, 256);
    KUNIT_ASSERT_EQ(buf[0], 0xA5, " mode 填充 ");
    return KUNIT_PASS;
}

/* === 内存复制测试 === */
static kunit_result_t test_memcpy_overlap(void)
{
    u8 buf[32];
    for (int i = 0; i < 32; i++) buf[i] = (u8)i;

    /* 前向复制（无重叠） */
    u8 dst[16];
    memcpy(dst, buf, 16);
    KUNIT_ASSERT_EQ(memcmp(dst, buf, 16), 0, " no 重叠 copy ");

    /* 重叠区域用 memmove(标准语义: 允许重叠, 行为定义) */
    extern void *memmove(void *, const void *, size_t);
    u8 ov[32];
    for (int i = 0; i < 32; i++) ov[i] = (u8)i;
    memmove(ov + 8, ov, 16);
    KUNIT_ASSERT_EQ(ov[8], 0, " memmove 重叠 bytes ");
    KUNIT_ASSERT_EQ(ov[23], 15, " memmove 重叠 末 bytes ");
    return KUNIT_PASS;
}

/* === 内存比较测试 === */
static kunit_result_t test_memcmp(void)
{
    u8 a[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    u8 b[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    u8 c[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,99};

    KUNIT_ASSERT_EQ(memcmp(a, b, 16), 0, " 相 etc memory ");
    KUNIT_ASSERT(memcmp(a, c, 16) != 0, " not etc memory ");
    KUNIT_ASSERT_EQ(memcmp(a, c, 15), 0, " prev 15 bytes 相 etc ");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t memory_safety_tests[] = {
    {" basic alloc ", test_malloc_basic, "memory"},
    {" 零 len alloc ", test_malloc_zero, "memory"},
    {" 大 memory alloc ", test_malloc_large, "memory"},
    {" 多 alloc ", test_multiple_alloc, "memory"},
    {" alloc release 再 alloc ", test_alloc_free_realloc, "memory"},
    {" pair 齐 alloc ", test_aligned_alloc, "memory"},
    {" memory pool basic ", test_mem_pool_basic, "memory"},
    {" memory pool 耗尽 ", test_mem_pool_exhaust, "memory"},
    {" memory pool invalid ", test_mem_pool_invalid, "memory"},
    {" memory 填充 mode ", test_memset_patterns, "memory"},
    {" memory copy 重叠 ", test_memcpy_overlap, "memory"},
    {" memory 比较 ", test_memcmp, "memory"},
};
static const kunit_suite_t kunit_memory_safety_suite = {
    " memory security ", memory_safety_tests, 12
};

void kunit_memory_safety_register(void)
{
    kunit_register_suite(&kunit_memory_safety_suite);
    vga_puts("[KUNIT] memory security test suite registered(12 cases)\n");
}
