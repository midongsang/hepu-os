/* ============================================================================
 * 赫普OS - 内置单元测试套件
 * ========================================================================== */

#include <stdint.h>
#include "types.h"
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === 字符串测试 === */
static kunit_result_t test_strlen(void)
{
    KUNIT_ASSERT_EQ(strlen("hello"), 5, "strlen basic ");
    KUNIT_ASSERT_EQ(strlen(""), 0, "strlen empty 串 ");
    KUNIT_ASSERT_EQ(strlen("a"), 1, "strlen 单字符 ");
    return KUNIT_PASS;
}

static kunit_result_t test_strcpy(void)
{
    char buf[32];
    strcpy(buf, "test");
    KUNIT_ASSERT_EQ(strcmp(buf, "test"), 0, "strcpy result ");
    return KUNIT_PASS;
}

static kunit_result_t test_strcmp(void)
{
    KUNIT_ASSERT_EQ(strcmp("abc", "abc"), 0, "strcmp 相 etc ");
    KUNIT_ASSERT(strcmp("abc", "abd") < 0, "strcmp 小于 ");
    KUNIT_ASSERT(strcmp("abd", "abc") > 0, "strcmp 大于 ");
    return KUNIT_PASS;
}

static kunit_result_t test_memset(void)
{
    u8 buf[16];
    memset(buf, 0xAA, 16);
    for (int i = 0; i < 16; i++) {
        KUNIT_ASSERT_EQ(buf[i], 0xAA, "memset value ");
    }
    return KUNIT_PASS;
}

static kunit_result_t test_memcpy(void)
{
    u8 src[8] = {1,2,3,4,5,6,7,8};
    u8 dst[8] = {0};
    memcpy(dst, src, 8);
    for (int i = 0; i < 8; i++) {
        KUNIT_ASSERT_EQ(dst[i], src[i], "memcpy value ");
    }
    return KUNIT_PASS;
}

static const kunit_test_t string_tests[] = {
    {"strlen", test_strlen, "string"},
    {"strcpy", test_strcpy, "string"},
    {"strcmp", test_strcmp, "string"},
    {"memset", test_memset, "string"},
    {"memcpy", test_memcpy, "string"},
};
const kunit_suite_t kunit_string_suite = {
    " 字符串 ops ", string_tests, 5
};

/* === 数学测试 === */
static kunit_result_t test_addition(void)
{
    KUNIT_ASSERT_EQ(1 + 1, 2, " basic 加法 ");
    KUNIT_ASSERT_EQ(100 + 200, 300, " 大 count 加法 ");
    return KUNIT_PASS;
}

static kunit_result_t test_subtraction(void)
{
    KUNIT_ASSERT_EQ(10 - 3, 7, " basic 减法 ");
    KUNIT_ASSERT_EQ(0 - 1, (u32)-1, " 负 count 减法 ");
    return KUNIT_PASS;
}

static kunit_result_t test_multiplication(void)
{
    KUNIT_ASSERT_EQ(6 * 7, 42, " basic 乘法 ");
    KUNIT_ASSERT_EQ(0 * 100, 0, " 零乘法 ");
    return KUNIT_PASS;
}

static kunit_result_t test_division(void)
{
    KUNIT_ASSERT_EQ(100 / 4, 25, " basic 除法 ");
    KUNIT_ASSERT_EQ(10 / 3, 3, " 整 count 除法 ");
    return KUNIT_PASS;
}

static kunit_result_t test_modulo(void)
{
    KUNIT_ASSERT_EQ(10 % 3, 1, " basic 取模 ");
    KUNIT_ASSERT_EQ(7 % 7, 0, " 整除取模 ");
    return KUNIT_PASS;
}

static const kunit_test_t math_tests[] = {
    {" 加法 ", test_addition, "math"},
    {" 减法 ", test_subtraction, "math"},
    {" 乘法 ", test_multiplication, "math"},
    {" 除法 ", test_division, "math"},
    {" 取模 ", test_modulo, "math"},
};
const kunit_suite_t kunit_math_suite = {
    " count 学运算 ", math_tests, 5
};

/* === 位操作测试 === */
static kunit_result_t test_bit_and(void)
{
    KUNIT_ASSERT_EQ(0xFF & 0x0F, 0x0F, " 按 bit & ");
    KUNIT_ASSERT_EQ(0b1010 & 0b1100, 0b1000, " 二进制 & ");
    return KUNIT_PASS;
}

static kunit_result_t test_bit_or(void)
{
    KUNIT_ASSERT_EQ(0xF0 | 0x0F, 0xFF, " 按 bit 或 ");
    return KUNIT_PASS;
}

static kunit_result_t test_bit_xor(void)
{
    KUNIT_ASSERT_EQ(0xFF ^ 0x0F, 0xF0, " 按 bit 异或 ");
    KUNIT_ASSERT_EQ(0xAA ^ 0xAA, 0, " 自异或 ");
    return KUNIT_PASS;
}

static kunit_result_t test_bit_shift(void)
{
    KUNIT_ASSERT_EQ(1 << 8, 256, " 左移 ");
    KUNIT_ASSERT_EQ(256 >> 8, 1, " 右移 ");
    return KUNIT_PASS;
}

static kunit_result_t test_bit_not(void)
{
    KUNIT_ASSERT_EQ((u8)(~0x0F), 0xF0, " 按 bit 取反 ");
    return KUNIT_PASS;
}

static const kunit_test_t bitops_tests[] = {
    {" 按 bit & ", test_bit_and, "bitops"},
    {" 按 bit 或 ", test_bit_or, "bitops"},
    {" 按 bit 异或 ", test_bit_xor, "bitops"},
    {" bit 移 ", test_bit_shift, "bitops"},
    {" 按 bit 取反 ", test_bit_not, "bitops"},
};
const kunit_suite_t kunit_bitops_suite = {
    " bit ops ", bitops_tests, 5
};
