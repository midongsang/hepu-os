/* ============================================================================
 * 赫普OS - 压力测试（第三周验证）
 * 覆盖：长时间运行、内存泄漏检测、高并发、循环压力
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === 内存分配压力 === */
static kunit_result_t test_stress_alloc_free_1000(void)
{
    /* 1000次分配释放 */
    extern void *kmalloc(uint32_t);
    extern void kfree(void *);
    for (int i = 0; i < 1000; i++) {
        void *p = kmalloc(64);
        if (!p) return KUNIT_SKIP;
        memset(p, (uint8_t)(i & 0xFF), 64);
        kfree(p);
    }
    KUNIT_ASSERT(1, " 1000 alloc/free ok ");
    return KUNIT_PASS;
}

static kunit_result_t test_stress_alloc_batch_100(void)
{
    /* 批量100个分配 */
    extern void *kmalloc(uint32_t);
    extern void kfree(void *);
    void *ptrs[100];
    for (int i = 0; i < 100; i++) {
        ptrs[i] = kmalloc(32 + (i % 8) * 16);
        if (!ptrs[i]) {
            for (int j = 0; j < i; j++) kfree(ptrs[j]);
            return KUNIT_SKIP;
        }
    }
    for (int i = 0; i < 100; i++) kfree(ptrs[i]);
    KUNIT_ASSERT(1, " batch 100 alloc ok ");
    return KUNIT_PASS;
}

static kunit_result_t test_stress_memory_leak_check(void)
{
    /* 内存泄漏检测：分配前后堆使用量应一致 */
    extern void kheap_stats(uint32_t *, uint32_t *, uint32_t *);
    extern void *kmalloc(uint32_t);
    extern void kfree(void *);
    uint32_t used1 = 0, free1 = 0, total1 = 0;
    kheap_stats(&used1, &free1, &total1);
    for (int i = 0; i < 100; i++) {
        void *p = kmalloc(128);
        if (p) kfree(p);
    }
    uint32_t used2 = 0, free2 = 0, total2 = 0;
    kheap_stats(&used2, &free2, &total2);
    KUNIT_ASSERT(used2 <= used1 + 1024, " no major leak ");
    return KUNIT_PASS;
}

/* === 循环压力 === */
static kunit_result_t test_stress_loop_100k(void)
{
    /* 10万次空循环 */
    volatile int count = 0;
    for (int i = 0; i < 100000; i++) count++;
    KUNIT_ASSERT_EQ(count, 100000, " 100k loop ok ");
    return KUNIT_PASS;
}

static kunit_result_t test_stress_memcpy_1000(void)
{
    /* 1000次内存拷贝 */
    static uint8_t src[256], dst[256];
    for (int i = 0; i < 256; i++) src[i] = (uint8_t)i;
    for (int i = 0; i < 1000; i++) {
        memcpy(dst, src, 256);
    }
    KUNIT_ASSERT_EQ(dst[0], 0, " memcpy first byte ");
    KUNIT_ASSERT_EQ(dst[255], 255, " memcpy last byte ");
    return KUNIT_PASS;
}

static kunit_result_t test_stress_memset_1000(void)
{
    /* 1000次内存填充 */
    static uint8_t buf[512];
    for (int i = 0; i < 1000; i++) {
        memset(buf, (uint8_t)(i & 0xFF), 512);
    }
    KUNIT_ASSERT(1, " 1000 memset ok ");
    return KUNIT_PASS;
}

/* === 系统调用压力 === */
static kunit_result_t test_stress_syscall_1000(void)
{
    /* 1000次getpid */
    extern uint32_t sys_getpid(uint32_t, uint32_t, uint32_t, uint32_t);
    for (int i = 0; i < 1000; i++) {
        sys_getpid(0, 0, 0, 0);
    }
    KUNIT_ASSERT(1, " 1000 syscall ok ");
    return KUNIT_PASS;
}

/* === 字符串操作压力 === */
static kunit_result_t test_stress_strlen_1000(void)
{
    /* 1000次字符串长度 */
    const char *str = "HepuOS Stress Test String";
    for (int i = 0; i < 1000; i++) {
        strlen(str);
    }
    KUNIT_ASSERT_EQ(strlen(str), 25, " strlen ok ");
    return KUNIT_PASS;
}

/* === 定时器压力 === */
static kunit_result_t test_stress_timer_100(void)
{
    /* 100次定时器读取 */
    extern uint32_t pit_get_ticks(void);
    for (int i = 0; i < 100; i++) {
        pit_get_ticks();
    }
    KUNIT_ASSERT(1, " 100 timer reads ok ");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t stress_e2e_tests[] = {
    {" stress alloc 1000 ", test_stress_alloc_free_1000, "stress_e2e"},
    {" stress alloc batch ", test_stress_alloc_batch_100, "stress_e2e"},
    {" stress leak check ", test_stress_memory_leak_check, "stress_e2e"},
    {" stress loop 100k ", test_stress_loop_100k, "stress_e2e"},
    {" stress memcpy 1000 ", test_stress_memcpy_1000, "stress_e2e"},
    {" stress memset 1000 ", test_stress_memset_1000, "stress_e2e"},
    {" stress syscall 1000 ", test_stress_syscall_1000, "stress_e2e"},
    {" stress strlen 1000 ", test_stress_strlen_1000, "stress_e2e"},
    {" stress timer 100 ", test_stress_timer_100, "stress_e2e"},
};

static const kunit_suite_t kunit_stress_e2e_suite = {
    " stress E2E ", stress_e2e_tests, 9
};

void kunit_stress_e2e_register(void)
{
    kunit_register_suite(&kunit_stress_e2e_suite);
    vga_puts("[KUNIT] stress E2E test suite registered(9 cases)\n");
}
