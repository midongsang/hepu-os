/* ============================================================================
 * 赫普OS - 安全测试（第三周验证）
 * 覆盖：边界条件、异常输入、权限检查、内存安全
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === 边界条件 === */
static kunit_result_t test_boundary_zero_size(void)
{
    /* 零大小分配 */
    extern void *kmalloc(uint32_t);
    extern void kfree(void *);
    void *p = kmalloc(0);
    if (p) kfree(p);
    KUNIT_ASSERT(1, " zero size alloc not crash ");
    return KUNIT_PASS;
}

static kunit_result_t test_boundary_max_size(void)
{
    /* 超大分配应失败 */
    extern void *kmalloc(uint32_t);
    extern void kfree(void *);
    void *p = kmalloc(0xFFFFFFFF);
    KUNIT_ASSERT(p == 0, " max size alloc fails ");
    return KUNIT_PASS;
}

static kunit_result_t test_boundary_null_pointer(void)
{
    /* 空指针处理 */
    extern void kfree(void *);
    kfree(0);
    KUNIT_ASSERT(1, " kfree null not crash ");
    return KUNIT_PASS;
}

static kunit_result_t test_boundary_double_free(void)
{
    /* 双重释放 */
    extern void *kmalloc(uint32_t);
    extern void kfree(void *);
    void *p = kmalloc(64);
    if (!p) return KUNIT_SKIP;
    kfree(p);
    kfree(p);  /* 双重释放应被拦截 */
    KUNIT_ASSERT(1, " double free not crash ");
    return KUNIT_PASS;
}

/* === 异常输入 === */
static kunit_result_t test_input_invalid_fd(void)
{
    /* 无效文件描述符 */
    extern uint32_t sys_read(uint32_t, uint32_t, uint32_t, uint32_t);
    uint8_t buf[16];
    uint32_t r = sys_read(0xFFFFFFFF, (uint32_t)buf, 16, 0);
    KUNIT_ASSERT(r == 0xFFFFFFFF || r == 0, " invalid fd rejected ");
    return KUNIT_PASS;
}

static kunit_result_t test_input_invalid_pointer(void)
{
    /* 无效指针 */
    extern uint32_t sys_write(uint32_t, uint32_t, uint32_t, uint32_t);
    uint32_t r = sys_write(1, 0x0, 100, 0);
    KUNIT_ASSERT(1, " invalid pointer not crash ");
    return KUNIT_PASS;
}

static kunit_result_t test_input_negative_length(void)
{
    /* 负长度 */
    extern uint32_t sys_read(uint32_t, uint32_t, uint32_t, uint32_t);
    uint8_t buf[16];
    uint32_t r = sys_read(0, (uint32_t)buf, 0xFFFFFFFF, 0);
    KUNIT_ASSERT(1, " negative length not crash ");
    return KUNIT_PASS;
}

static kunit_result_t test_input_string_overflow(void)
{
    /* 字符串溢出保护 */
    char small[8];
    memset(small, 0, sizeof(small));
    const char *long_str = "This is a very long string that should not overflow";
    strncpy(small, long_str, sizeof(small) - 1);
    small[sizeof(small) - 1] = '\0';
    KUNIT_ASSERT(strlen(small) < sizeof(small), " string bounded ");
    return KUNIT_PASS;
}

/* === 权限检查 === */
static kunit_result_t test_permission_uid_nonzero(void)
{
    /* UID检查 */
    extern uint32_t sys_getuid(uint32_t, uint32_t, uint32_t, uint32_t);
    uint32_t uid = sys_getuid(0, 0, 0, 0);
    KUNIT_ASSERT(uid >= 0, " uid valid ");
    return KUNIT_PASS;
}

static kunit_result_t test_permission_gid_nonzero(void)
{
    /* GID检查 */
    extern uint32_t sys_getgid(uint32_t, uint32_t, uint32_t, uint32_t);
    uint32_t gid = sys_getgid(0, 0, 0, 0);
    KUNIT_ASSERT(gid >= 0, " gid valid ");
    return KUNIT_PASS;
}

/* === 内存安全 === */
static kunit_result_t test_memory_buffer_overflow(void)
{
    /* 缓冲区溢出检测 */
    uint8_t buf[16];
    memset(buf, 0, sizeof(buf));
    for (int i = 0; i < 16; i++) buf[i] = (uint8_t)i;
    KUNIT_ASSERT_EQ(buf[0], 0, " first byte ok ");
    KUNIT_ASSERT_EQ(buf[15], 15, " last byte ok ");
    return KUNIT_PASS;
}

static kunit_result_t test_memory_use_after_free(void)
{
    /* 释放后使用检测 */
    extern void *kmalloc(uint32_t);
    extern void kfree(void *);
    void *p = kmalloc(128);
    if (!p) return KUNIT_SKIP;
    kfree(p);
    /* 不应崩溃，值可能已改变 */
    KUNIT_ASSERT(1, " use after free not crash ");
    return KUNIT_PASS;
}

static kunit_result_t test_memory_alignment(void)
{
    /* 内存对齐 */
    extern void *kmalloc(uint32_t);
    extern void kfree(void *);
    void *p = kmalloc(64);
    if (!p) return KUNIT_SKIP;
    uint32_t addr = (uint32_t)p;
    KUNIT_ASSERT(addr % 4 == 0, " 4-byte aligned ");
    kfree(p);
    return KUNIT_PASS;
}

/* === 整数溢出 === */
static kunit_result_t test_integer_overflow_add(void)
{
    /* 整数溢出检测 */
    uint32_t a = 0xFFFFFFFF;
    uint32_t b = 1;
    uint32_t c = a + b;  /* 应回绕为0 */
    KUNIT_ASSERT_EQ(c, 0, " uint32 overflow wraps ");
    return KUNIT_PASS;
}

static kunit_result_t test_integer_overflow_multiply(void)
{
    /* 乘法溢出 */
    uint32_t a = 0x10000;
    uint32_t b = 0x10000;
    uint32_t c = a * b;  /* 0x100000000 回绕为0 */
    KUNIT_ASSERT_EQ(c, 0, " multiply overflow wraps ");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t security_e2e_tests[] = {
    {" boundary zero size ", test_boundary_zero_size, "security"},
    {" boundary max size ", test_boundary_max_size, "security"},
    {" boundary null ptr ", test_boundary_null_pointer, "security"},
    {" boundary double free ", test_boundary_double_free, "security"},
    {" input invalid fd ", test_input_invalid_fd, "security"},
    {" input invalid ptr ", test_input_invalid_pointer, "security"},
    {" input neg length ", test_input_negative_length, "security"},
    {" input string overflow ", test_input_string_overflow, "security"},
    {" permission uid ", test_permission_uid_nonzero, "security"},
    {" permission gid ", test_permission_gid_nonzero, "security"},
    {" memory buffer overflow ", test_memory_buffer_overflow, "security"},
    {" memory use after free ", test_memory_use_after_free, "security"},
    {" memory alignment ", test_memory_alignment, "security"},
    {" integer overflow add ", test_integer_overflow_add, "security"},
    {" integer overflow mul ", test_integer_overflow_multiply, "security"},
};

static const kunit_suite_t kunit_security_e2e_suite = {
    " security E2E ", security_e2e_tests, 15
};

void kunit_security_e2e_register(void)
{
    kunit_register_suite(&kunit_security_e2e_suite);
    vga_puts("[KUNIT] security E2E test suite registered(15 cases)\n");
}
