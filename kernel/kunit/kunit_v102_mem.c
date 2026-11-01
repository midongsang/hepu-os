/* ============================================================================
 * 赫普OS - V1.02内存侧新模块测试
 * 覆盖：内存回收(reclaim)、Swap(swap)、大页(hugetlb)
 * 测试侧重结构/逻辑验证，避免触发真实物理内存副作用
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"
#include "reclaim.h"
#include "swap.h"
#include "hugetlb.h"

/* === 内存回收测试 === */

static kunit_result_t test_reclaim_lru_add_remove(void)
{
    /* 登记页到LRU（不触发真实物理页释放） */
    KUNIT_ASSERT_EQ(reclaim_add_page(0x100000), 0, " add pages 0x100000");
    KUNIT_ASSERT_EQ(reclaim_add_page(0x200000), 0, " add pages 0x200000");
    KUNIT_ASSERT_EQ(reclaim_add_page(0x300000), 0, " add pages 0x300000");

    /* 移除中间页 */
    KUNIT_ASSERT_EQ(reclaim_remove_page(0x200000), 0, " remove pages 0x200000");
    /* 重复移除返回-1 */
    KUNIT_ASSERT_EQ(reclaim_remove_page(0x200000), -1, " 重复 remove 返回 -1");

    uint32_t reclaimed, calls, lru_used, pressure;
    reclaim_stats(&reclaimed, &calls, &lru_used, &pressure);
    KUNIT_ASSERT_EQ(lru_used, 2, "LRU left 2 pages ");

    /* 清理 */
    reclaim_remove_page(0x100000);
    reclaim_remove_page(0x300000);
    return KUNIT_PASS;
}

static kunit_result_t test_reclaim_lru_accessed(void)
{
    reclaim_add_page(0x400000);
    reclaim_add_page(0x500000);

    /* 标记访问不会破坏链表 */
    KUNIT_ASSERT_EQ(reclaim_mark_accessed(0x500000), 0, " 标记访问 0x500000");
    KUNIT_ASSERT_EQ(reclaim_mark_accessed(0x999999), -1, " not exists pages 返回 -1");

    reclaim_remove_page(0x400000);
    reclaim_remove_page(0x500000);
    return KUNIT_PASS;
}

static kunit_result_t test_reclaim_until_boundary(void)
{
    /* 无可回收页时reclaim_until应立即返回（不陷入死循环） */
    int reclaimed = reclaim_until(0xFFFFFFF0);
    KUNIT_ASSERT(reclaimed >= 0, "reclaim_until no pages 可 reclaim runtime 正常返回 ");
    return KUNIT_PASS;
}

static kunit_result_t test_reclaim_pressure_check(void)
{
    /* 压力检查必须返回合法等级且不崩溃（含CRITICAL路径非递归） */
    int p = reclaim_check_pressure();
    KUNIT_ASSERT(p >= MEM_OK && p <= MEM_PRESSURE_CRITICAL,
                 " 压力 etc level at 合法范围 ");
    return KUNIT_PASS;
}

/* === Swap测试 === */

static kunit_result_t test_swap_set_enabled(void)
{
    KUNIT_ASSERT(swap_is_enabled() == 0 || swap_is_enabled() == 1,
                 "swap state 合法 ");
    KUNIT_ASSERT_EQ(swap_set_enabled(0), 0, " disabled swap");
    KUNIT_ASSERT_EQ(swap_set_enabled(1), 0, " enabled swap");

    uint32_t total, used, in, out;
    swap_stats(&total, &used, &in, &out);
    KUNIT_ASSERT(total >= 1, "swap 槽 total count >0");
    return KUNIT_PASS;
}

static kunit_result_t test_swap_out_invalid(void)
{
    /* swap未启用时换出失败 */
    swap_set_enabled(0);
    KUNIT_ASSERT(swap_out(0x100000) < 0, "swap disabled runtime 换出 failed ");

    /* 非法槽换入返回0 */
    KUNIT_ASSERT_EQ(swap_in(999), 0, " non 法槽换入返回 0");
    KUNIT_ASSERT_EQ(swap_in(-1), 0, " 负槽换入返回 0");

    swap_set_enabled(1);
    return KUNIT_PASS;
}

/* === 大页测试 === */

static kunit_result_t test_hugetlb_constants(void)
{
    KUNIT_ASSERT_EQ(HUGE_PAGE_2M, 2 * 1024 * 1024, "2M hugepage =2MB");
    KUNIT_ASSERT_EQ(HUGE_PAGE_4M, 4 * 1024 * 1024, "4M hugepage =4MB");
    KUNIT_ASSERT(HUGE_PAGE_2M < HUGE_PAGE_4M, "2M<4M");
    return KUNIT_PASS;
}

static kunit_result_t test_hugetlb_supported(void)
{
    /* 支持标志必须是0或1 */
    int s = hugetlb_supported();
    KUNIT_ASSERT(s == 0 || s == 1, " hugepage supports 标志合法 ");
    return KUNIT_PASS;
}

static kunit_result_t test_hugetlb_stats(void)
{
    uint32_t h2, h4, a2, a4;
    hugetlb_stats(&h2, &h4, &a2, &a4);
    /* 验证统计接口可调用且计数在合理范围 */
    KUNIT_ASSERT(h2 + h4 + a2 + a4 <= 0xFFFFFF, " hugepage stats at 合理范围 ");
    return KUNIT_PASS;
}

/* === 测试套件 === */
static const kunit_test_t v102_mem_tests[] = {
    {"LRU pages 登记 / remove ", test_reclaim_lru_add_remove, "reclaim"},
    {"LRU 访问标记 ", test_reclaim_lru_accessed, "reclaim"},
    {" bulk reclaim boundary ", test_reclaim_until_boundary, "reclaim"},
    {" memory 压力 check ", test_reclaim_pressure_check, "reclaim"},
    {"swap 启停 ", test_swap_set_enabled, "swap"},
    {"swap non 法 ops ", test_swap_out_invalid, "swap"},
    {" hugepage 常量 ", test_hugetlb_constants, "hugetlb"},
    {" hugepage supports detect ", test_hugetlb_supported, "hugetlb"},
    {" hugepage stats ", test_hugetlb_stats, "hugetlb"},
};
static const kunit_suite_t kunit_v102_mem_suite = {
    "V1.02 memory side module ", v102_mem_tests, 9
};

void kunit_v102_mem_register(void)
{
    kunit_register_suite(&kunit_v102_mem_suite);
    vga_puts("[KUNIT] V1.02 memory side module test suite registered(9 cases)\n");
}
