/* ============================================================================
 * 赫普OS - 内存管理单元测试
 * 覆盖：页表/物理内存/堆分配/内存对齐
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === 页大小测试 === */
#define PAGE_SIZE_4K   4096
#define PAGE_SIZE_2M   (2 * 1024 * 1024)
#define PAGE_SIZE_4M   (4 * 1024 * 1024)

static kunit_result_t test_page_sizes(void)
{
    KUNIT_ASSERT_EQ(PAGE_SIZE_4K, 4096, "4K page size ");
    KUNIT_ASSERT_EQ(PAGE_SIZE_2M, 2097152, "2M page size ");
    KUNIT_ASSERT_EQ(PAGE_SIZE_4M, 4194304, "4M page size ");
    KUNIT_ASSERT(PAGE_SIZE_4K < PAGE_SIZE_2M, "4K<2M");
    KUNIT_ASSERT(PAGE_SIZE_2M < PAGE_SIZE_4M, "2M<4M");
    return KUNIT_PASS;
}

static kunit_result_t test_page_alignment(void)
{
    /* 页对齐宏（#undef避免与types.h中的全局定义冲突） */
    #undef PAGE_ALIGN
    #undef PAGE_ALIGN_DOWN
    #define PAGE_ALIGN(addr) (((addr) + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1))
    #define PAGE_ALIGN_DOWN(addr) ((addr) & ~(PAGE_SIZE_4K - 1))

    KUNIT_ASSERT_EQ(PAGE_ALIGN(0), 0, "0 pair 齐 0");
    KUNIT_ASSERT_EQ(PAGE_ALIGN(1), 4096, "1 pair 齐 4096");
    KUNIT_ASSERT_EQ(PAGE_ALIGN(4095), 4096, "4095 pair 齐 4096");
    KUNIT_ASSERT_EQ(PAGE_ALIGN(4096), 4096, "4096 pair 齐 4096");
    KUNIT_ASSERT_EQ(PAGE_ALIGN(4097), 8192, "4097 pair 齐 8192");

    KUNIT_ASSERT_EQ(PAGE_ALIGN_DOWN(4095), 0, "4095 向下 pair 齐 0");
    KUNIT_ASSERT_EQ(PAGE_ALIGN_DOWN(4096), 4096, "4096 向下 pair 齐 4096");
    KUNIT_ASSERT_EQ(PAGE_ALIGN_DOWN(8191), 4096, "8191 向下 pair 齐 4096");
    return KUNIT_PASS;
}

/* === 页表项测试 === */
#define PTE_PRESENT  0x001
#define PTE_WRITABLE 0x002
#define PTE_USER     0x004
#define PTE_PWT      0x008
#define PTE_PCD      0x010
#define PTE_ACCESSED 0x020
#define PTE_DIRTY    0x040
#define PTE_PS       0x080
#define PTE_GLOBAL   0x100

static kunit_result_t test_pte_flags(void)
{
    /* 构造一个页表项 */
    u32 pte = PTE_PRESENT | PTE_WRITABLE | PTE_USER;
    pte |= (0x1000 & 0xFFFFF000);  /* 物理地址0x1000 */

    KUNIT_ASSERT(pte & PTE_PRESENT, " exists bit ");
    KUNIT_ASSERT(pte & PTE_WRITABLE, " 可写 bit ");
    KUNIT_ASSERT(pte & PTE_USER, " user bit ");
    KUNIT_ASSERT(!(pte & PTE_PWT), " no PWT bit ");
    KUNIT_ASSERT(!(pte & PTE_DIRTY), " no 脏 bit ");

    /* 提取物理地址 */
    u32 phys_addr = pte & 0xFFFFF000;
    KUNIT_ASSERT_EQ(phys_addr, 0x1000, " physical addr 提取 ");
    return KUNIT_PASS;
}

static kunit_result_t test_pde_flags(void)
{
    /* 页目录项 */
    u32 pde = PTE_PRESENT | PTE_WRITABLE;
    pde |= (0x20000000 & 0xFFC00000);  /* 页表物理地址(高位) */

    KUNIT_ASSERT(pde & PTE_PRESENT, "PDE exists bit ");
    KUNIT_ASSERT(pde & PTE_WRITABLE, "PDE 可写 bit ");

    u32 pt_addr = pde & 0xFFFFF000;
    KUNIT_ASSERT_EQ(pt_addr, 0x20000000, " pages table addr 提取 ");
    return KUNIT_PASS;
}

/* === 物理内存位图测试 === */
#define PHYS_MEM_BASE 0x100000  /* 1MB */
#define PHYS_MEM_SIZE (16 * 1024 * 1024)  /* 16MB */
#define PHYS_PAGE_COUNT (PHYS_MEM_SIZE / PAGE_SIZE_4K)

static u8 phys_bitmap[PHYS_PAGE_COUNT / 8];

static void phys_bitmap_init(void)
{
    memset(phys_bitmap, 0, sizeof(phys_bitmap));
    /* 标记前1MB为已用 */
    u32 used_pages = PHYS_MEM_BASE / PAGE_SIZE_4K;
    for (u32 i = 0; i < used_pages; i++) {
        phys_bitmap[i / 8] |= (1 << (i % 8));
    }
}

static int phys_alloc_page(void)
{
    for (u32 i = 0; i < PHYS_PAGE_COUNT; i++) {
        if (!(phys_bitmap[i / 8] & (1 << (i % 8)))) {
            phys_bitmap[i / 8] |= (1 << (i % 8));
            return (int)i;
        }
    }
    return -1;
}

static int phys_free_page(u32 page_idx)
{
    if (page_idx >= PHYS_PAGE_COUNT) return -1;
    phys_bitmap[page_idx / 8] &= ~(1 << (page_idx % 8));
    return 0;
}

static kunit_result_t test_phys_bitmap_alloc(void)
{
    phys_bitmap_init();

    int page1 = phys_alloc_page();
    int page2 = phys_alloc_page();
    int page3 = phys_alloc_page();

    KUNIT_ASSERT(page1 >= 0, " alloc pages 1 ok ");
    KUNIT_ASSERT(page2 >= 0, " alloc pages 2 ok ");
    KUNIT_ASSERT(page3 >= 0, " alloc pages 3 ok ");
    KUNIT_ASSERT(page1 != page2, " pages 1!= pages 2");
    KUNIT_ASSERT(page2 != page3, " pages 2!= pages 3");

    /* 释放页2 */
    phys_free_page((u32)page2);

    /* 再次分配应该得到页2 */
    int page4 = phys_alloc_page();
    KUNIT_ASSERT_EQ(page4, page2, " 重用 release pages ");
    return KUNIT_PASS;
}

static kunit_result_t test_phys_bitmap_exhaust(void)
{
    phys_bitmap_init();

    /* 分配所有可用页 */
    int count = 0;
    while (phys_alloc_page() >= 0) count++;

    KUNIT_ASSERT(count > 0, " alloc 了一些 pages ");

    /* 再次分配应该失败 */
    int exhausted = phys_alloc_page();
    KUNIT_ASSERT_EQ(exhausted, -1, " memory 耗尽返回 -1");
    return KUNIT_PASS;
}

/* === 堆分配测试 === */
#define HEAP_SIZE (1024 * 1024)  /* 1MB */
static u8 heap_memory[HEAP_SIZE];
static u32 heap_offset = 0;

static void *test_malloc(u32 size)
{
    /* 简单的 bump allocator */
    if (heap_offset + size > HEAP_SIZE) return NULL;
    void *ptr = heap_memory + heap_offset;
    heap_offset += size;
    return ptr;
}

static kunit_result_t test_heap_alloc(void)
{
    heap_offset = 0;

    void *p1 = test_malloc(64);
    void *p2 = test_malloc(128);
    void *p3 = test_malloc(256);

    KUNIT_ASSERT_NOT_NULL(p1, " alloc 64 bytes ");
    KUNIT_ASSERT_NOT_NULL(p2, " alloc 128 bytes ");
    KUNIT_ASSERT_NOT_NULL(p3, " alloc 256 bytes ");

    /* 验证地址连续性 */
    KUNIT_ASSERT_EQ((u32)p2 - (u32)p1, 64, "p2-p1=64");
    KUNIT_ASSERT_EQ((u32)p3 - (u32)p2, 128, "p3-p2=128");
    return KUNIT_PASS;
}

static kunit_result_t test_heap_overflow(void)
{
    heap_offset = 0;

    /* 分配接近堆大小 */
    void *p1 = test_malloc(HEAP_SIZE - 100);
    KUNIT_ASSERT_NOT_NULL(p1, " alloc 大部分 heap ");

    /* 再分配应该失败 */
    void *p2 = test_malloc(200);
    KUNIT_ASSERT_NULL(p2, " heap 溢出返回 NULL");
    return KUNIT_PASS;
}

/* === 内存对齐分配测试 === */
static kunit_result_t test_aligned_alloc(void)
{
    /* 模拟对齐分配 */
    heap_offset = 0;

    /* 分配8字节对齐 */
    void *p = test_malloc(100);
    u32 addr = (u32)p;
    KUNIT_ASSERT_EQ(addr % 8, 0, "8 bytes pair 齐(假设 start pair 齐)");

    /* 分配后检查偏移 */
    void *p2 = test_malloc(16);
    KUNIT_ASSERT_EQ((u32)p2 - (u32)p, 100, " 连续 alloc 偏移 ");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t mm_tests[] = {
    {" page size ", test_page_sizes, "mm"},
    {" pages pair 齐 ", test_page_alignment, "mm"},
    {"PTE 标志 ", test_pte_flags, "mm"},
    {"PDE 标志 ", test_pde_flags, "mm"},
    {" physical pages alloc ", test_phys_bitmap_alloc, "mm"},
    {" physical pages 耗尽 ", test_phys_bitmap_exhaust, "mm"},
    {" heap alloc ", test_heap_alloc, "mm"},
    {" heap 溢出 ", test_heap_overflow, "mm"},
    {" pair 齐 alloc ", test_aligned_alloc, "mm"},
};
static const kunit_suite_t kunit_mm_suite = {
    " memory mgr ", mm_tests, 9
};

void kunit_mm_register(void)
{
    kunit_register_suite(&kunit_mm_suite);
    vga_puts("[KUNIT] memory mgr test suite registered(9 cases)\n");
}
