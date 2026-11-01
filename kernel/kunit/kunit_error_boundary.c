/* ============================================================================
 * 赫普OS - 错误处理与边界单元测试
 * 覆盖：空指针/整数溢出/缓冲区溢出/除零/范围检查/错误码
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === 空指针检查测试 === */
static int safe_strlen(const char *s)
{
    if (!s) return -1;
    int len = 0;
    while (s[len]) len++;
    return len;
}

static kunit_result_t test_null_pointer_strlen(void)
{
    KUNIT_ASSERT_EQ(safe_strlen(0), -1, " empty ptr 返回 -1");
    KUNIT_ASSERT_EQ(safe_strlen(""), 0, " empty 字符串返回 0");
    KUNIT_ASSERT_EQ(safe_strlen("HEPU"), 4, " 正常字符串返回 4");
    return KUNIT_PASS;
}

static int safe_memcpy(void *dst, const void *src, u32 n)
{
    if (!dst || !src) return -1;
    if (n == 0) return 0;
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    for (u32 i = 0; i < n; i++) d[i] = s[i];
    return (int)n;
}

static kunit_result_t test_null_pointer_memcpy(void)
{
    u8 buf[16] = {0};
    KUNIT_ASSERT_EQ(safe_memcpy(0, buf, 16), -1, " empty targets 返回 -1");
    KUNIT_ASSERT_EQ(safe_memcpy(buf, 0, 16), -1, " empty 源返回 -1");
    KUNIT_ASSERT_EQ(safe_memcpy(buf, "test", 0), 0, " 零 len 返回 0");
    return KUNIT_PASS;
}

/* === 整数溢出测试 === */
static int safe_add_u32(u32 a, u32 b, u32 *result)
{
    if (!result) return -1;
    if (a > 0xFFFFFFFF - b) return -2;  /* 溢出 */
    *result = a + b;
    return 0;
}

static kunit_result_t test_u32_add_overflow(void)
{
    u32 result;
    KUNIT_ASSERT_EQ(safe_add_u32(100, 200, &result), 0, " 正常加法 ");
    KUNIT_ASSERT_EQ(result, 300, " result =300");
    KUNIT_ASSERT_EQ(safe_add_u32(0xFFFFFFFF, 1, &result), -2, " 溢出 detect ");
    KUNIT_ASSERT_EQ(safe_add_u32(0x80000000, 0x80000000, &result), -2, " boundary 溢出 ");
    KUNIT_ASSERT_EQ(safe_add_u32(0xFFFFFFFE, 1, &result), 0, " boundary not 溢出 ");
    KUNIT_ASSERT_EQ(result, 0xFFFFFFFF, " result = max value ");
    return KUNIT_PASS;
}

static int safe_mul_u32(u32 a, u32 b, u32 *result)
{
    if (!result) return -1;
    if (a != 0 && b > 0xFFFFFFFF / a) return -2;
    *result = a * b;
    return 0;
}

static kunit_result_t test_u32_mul_overflow(void)
{
    u32 result;
    KUNIT_ASSERT_EQ(safe_mul_u32(100, 200, &result), 0, " 正常乘法 ");
    KUNIT_ASSERT_EQ(result, 20000, " result =20000");
    KUNIT_ASSERT_EQ(safe_mul_u32(0x10000, 0x10000, &result), -2, " 乘法溢出 ");
    KUNIT_ASSERT_EQ(safe_mul_u32(0, 0xFFFFFFFF, &result), 0, " 乘零 not 溢出 ");
    return KUNIT_PASS;
}

/* === 缓冲区边界测试 === */
#define BOUND_BUF_SIZE 32

static int safe_copy(char *dst, u32 dst_size, const char *src)
{
    if (!dst || !src || dst_size == 0) return -1;
    u32 i = 0;
    while (i < dst_size - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return (int)i;
}

static kunit_result_t test_buffer_truncation(void)
{
    char buf[8];
    int ret = safe_copy(buf, 8, "Hello World!");
    KUNIT_ASSERT_EQ(ret, 7, " 截断 to 7 字符 ");
    KUNIT_ASSERT_EQ(strcmp(buf, "Hello W"), 0, " 截断内容正确 ");
    KUNIT_ASSERT_EQ(buf[7], '\0', " 末尾 empty 终止 ");
    return KUNIT_PASS;
}

static kunit_result_t test_buffer_exact_fit(void)
{
    char buf[5];
    int ret = safe_copy(buf, 5, "HEPU");
    KUNIT_ASSERT_EQ(ret, 4, " 正好 4 字符 ");
    KUNIT_ASSERT_EQ(strcmp(buf, "HEPU"), 0, " 内容正确 ");
    return KUNIT_PASS;
}

static kunit_result_t test_buffer_zero_size(void)
{
    char buf[1];
    KUNIT_ASSERT_EQ(safe_copy(buf, 0, "test"), -1, " 零 size 返回 -1");
    return KUNIT_PASS;
}

/* === 除零保护测试 === */
static int safe_divide(s32 a, s32 b, s32 *result)
{
    if (!result) return -1;
    if (b == 0) return -2;
    *result = a / b;
    return 0;
}

static kunit_result_t test_divide_by_zero(void)
{
    s32 result;
    KUNIT_ASSERT_EQ(safe_divide(10, 2, &result), 0, " 正常除法 ");
    KUNIT_ASSERT_EQ(result, 5, " result =5");
    KUNIT_ASSERT_EQ(safe_divide(10, 0, &result), -2, " 除零 detect ");
    KUNIT_ASSERT_EQ(safe_divide(0, 5, &result), 0, " 零除以正 count ");
    KUNIT_ASSERT_EQ(result, 0, " result =0");
    return KUNIT_PASS;
}

/* === 范围检查测试 === */
static int in_range(s32 val, s32 min, s32 max)
{
    return (val >= min && val <= max) ? 0 : -1;
}

static kunit_result_t test_range_check(void)
{
    KUNIT_ASSERT_EQ(in_range(50, 0, 100), 0, " 范围内 ");
    KUNIT_ASSERT_EQ(in_range(0, 0, 100), 0, " 下 boundary ");
    KUNIT_ASSERT_EQ(in_range(100, 0, 100), 0, " boundary ");
    KUNIT_ASSERT_EQ(in_range(-1, 0, 100), -1, " low 于下 boundary ");
    KUNIT_ASSERT_EQ(in_range(101, 0, 100), -1, " high 于 boundary ");
    return KUNIT_PASS;
}

static int clamp(s32 val, s32 min, s32 max)
{
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

static kunit_result_t test_clamp(void)
{
    KUNIT_ASSERT_EQ(clamp(50, 0, 100), 50, " 范围内 not 变 ");
    KUNIT_ASSERT_EQ(clamp(-10, 0, 100), 0, " low 于下限钳 bit ");
    KUNIT_ASSERT_EQ(clamp(150, 0, 100), 100, " high 于 限钳 bit ");
    KUNIT_ASSERT_EQ(clamp(0, 0, 100), 0, " 下 boundary not 变 ");
    KUNIT_ASSERT_EQ(clamp(100, 0, 100), 100, " boundary not 变 ");
    return KUNIT_PASS;
}

/* === 错误码测试 === */
#define HEPU_OK 0
#define HEPU_ERR_GENERIC -1
#define HEPU_ERR_INVALID_PARAM -2
#define HEPU_ERR_OUT_OF_MEMORY -3
#define HEPU_ERR_NOT_FOUND -4
#define HEPU_ERR_PERMISSION -5
#define HEPU_ERR_BUSY -6
#define HEPU_ERR_TIMEOUT -7
#define HEPU_ERR_NOT_SUPPORTED -8

static const char *error_string(int err)
{
    switch (err) {
        case HEPU_OK: return "OK";
        case HEPU_ERR_GENERIC: return "Generic error";
        case HEPU_ERR_INVALID_PARAM: return "Invalid parameter";
        case HEPU_ERR_OUT_OF_MEMORY: return "Out of memory";
        case HEPU_ERR_NOT_FOUND: return "Not found";
        case HEPU_ERR_PERMISSION: return "Permission denied";
        case HEPU_ERR_BUSY: return "Resource busy";
        case HEPU_ERR_TIMEOUT: return "Timeout";
        case HEPU_ERR_NOT_SUPPORTED: return "Not supported";
        default: return "Unknown error";
    }
}

static kunit_result_t test_error_codes(void)
{
    KUNIT_ASSERT_EQ(HEPU_OK, 0, "OK=0");
    KUNIT_ASSERT_EQ(HEPU_ERR_INVALID_PARAM, -2, "INVALID_PARAM=-2");
    KUNIT_ASSERT_EQ(HEPU_ERR_OUT_OF_MEMORY, -3, "OUT_OF_MEMORY=-3");
    KUNIT_ASSERT_EQ(HEPU_ERR_NOT_FOUND, -4, "NOT_FOUND=-4");
    KUNIT_ASSERT_EQ(HEPU_ERR_PERMISSION, -5, "PERMISSION=-5");
    return KUNIT_PASS;
}

static kunit_result_t test_error_strings(void)
{
    KUNIT_ASSERT_EQ(strcmp(error_string(HEPU_OK), "OK"), 0, "OK 字符串 ");
    KUNIT_ASSERT_EQ(strcmp(error_string(HEPU_ERR_TIMEOUT), "Timeout"), 0, "Timeout 字符串 ");
    KUNIT_ASSERT_EQ(strcmp(error_string(999), "Unknown error"), 0, " unknown error ");
    return KUNIT_PASS;
}

/* === 数组边界测试 === */
static int array_get(const int *arr, u32 size, u32 index, int *out)
{
    if (!arr || !out) return -1;
    if (index >= size) return -2;
    *out = arr[index];
    return 0;
}

static kunit_result_t test_array_bounds(void)
{
    int arr[] = {10, 20, 30, 40, 50};
    int val;

    KUNIT_ASSERT_EQ(array_get(arr, 5, 0, &val), 0, " 索引 0");
    KUNIT_ASSERT_EQ(val, 10, " value =10");
    KUNIT_ASSERT_EQ(array_get(arr, 5, 4, &val), 0, " 索引 4");
    KUNIT_ASSERT_EQ(val, 50, " value =50");
    KUNIT_ASSERT_EQ(array_get(arr, 5, 5, &val), -2, " 越界索引 5");
    KUNIT_ASSERT_EQ(array_get(arr, 5, 100, &val), -2, " 越界索引 100");
    KUNIT_ASSERT_EQ(array_get(0, 5, 0, &val), -1, " empty count 组 ");
    return KUNIT_PASS;
}

/* === 字符串长度边界测试 === */
static kunit_result_t test_string_boundaries(void)
{
    KUNIT_ASSERT_EQ(strlen(""), 0, " empty 字符串 len =0");
    KUNIT_ASSERT_EQ(strlen("A"), 1, " 单字符 len =1");
    KUNIT_ASSERT_EQ(strcmp("", ""), 0, " empty 字符串相 etc ");
    KUNIT_ASSERT(strcmp("a", "b") < 0, "a < b");
    KUNIT_ASSERT(strcmp("abc", "abd") < 0, "abc < abd");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t error_boundary_tests[] = {
    {" empty ptr strlen", test_null_pointer_strlen, "error"},
    {" empty ptr memcpy", test_null_pointer_memcpy, "error"},
    {"u32 加法溢出 ", test_u32_add_overflow, "error"},
    {"u32 乘法溢出 ", test_u32_mul_overflow, "error"},
    {" buffer region 截断 ", test_buffer_truncation, "error"},
    {" buffer region 正好 ", test_buffer_exact_fit, "error"},
    {" buffer region 零 size ", test_buffer_zero_size, "error"},
    {" 除零 protected ", test_divide_by_zero, "error"},
    {" 范围 check ", test_range_check, "error"},
    {" 钳 bit funcs ", test_clamp, "error"},
    {" error 码 ", test_error_codes, "error"},
    {" error 字符串 ", test_error_strings, "error"},
    {" count 组 boundary ", test_array_bounds, "error"},
    {" 字符串 boundary ", test_string_boundaries, "error"},
};
static const kunit_suite_t kunit_error_boundary_suite = {
    " error handling & boundary ", error_boundary_tests, 14
};

void kunit_error_boundary_register(void)
{
    kunit_register_suite(&kunit_error_boundary_suite);
    vga_puts("[KUNIT] error handling & boundary test suite registered(14 cases)\n");
}
