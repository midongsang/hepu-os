/* ============================================================================
 * 赫普OS - 算法正确性单元测试
 * 覆盖：排序/搜索/字符串/数学/CRC/校验和
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === 冒泡排序测试 === */
static void bubble_sort(int *arr, int n)
{
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - i - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                int tmp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = tmp;
            }
        }
    }
}

static kunit_result_t test_bubble_sort(void)
{
    int arr[] = {64, 34, 25, 12, 22, 11, 90};
    int expected[] = {11, 12, 22, 25, 34, 64, 90};
    bubble_sort(arr, 7);
    KUNIT_ASSERT_EQ(memcmp(arr, expected, sizeof(arr)), 0, " 冒泡排序正确 ");
    return KUNIT_PASS;
}

static kunit_result_t test_bubble_sort_empty(void)
{
    int arr[1] = {0};
    bubble_sort(arr, 0);
    KUNIT_ASSERT(1, " empty count 组 not 崩溃 ");
    return KUNIT_PASS;
}

static kunit_result_t test_bubble_sort_single(void)
{
    int arr[] = {42};
    bubble_sort(arr, 1);
    KUNIT_ASSERT_EQ(arr[0], 42, " unit 素 not 变 ");
    return KUNIT_PASS;
}

/* === 快速排序测试 === */
static void quick_sort(int *arr, int low, int high)
{
    if (low >= high) return;
    int pivot = arr[high];
    int i = low - 1;
    for (int j = low; j < high; j++) {
        if (arr[j] <= pivot) {
            i++;
            int tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
        }
    }
    int tmp = arr[i + 1]; arr[i + 1] = arr[high]; arr[high] = tmp;
    int pi = i + 1;
    quick_sort(arr, low, pi - 1);
    quick_sort(arr, pi + 1, high);
}

static kunit_result_t test_quick_sort(void)
{
    int arr[] = {64, 34, 25, 12, 22, 11, 90, 5, 77, 33};
    int expected[] = {5, 11, 12, 22, 25, 33, 34, 64, 77, 90};
    quick_sort(arr, 0, 9);
    KUNIT_ASSERT_EQ(memcmp(arr, expected, sizeof(arr)), 0, " 快速排序正确 ");
    return KUNIT_PASS;
}

static kunit_result_t test_quick_sort_reverse(void)
{
    int arr[] = {9, 8, 7, 6, 5, 4, 3, 2, 1};
    int expected[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    quick_sort(arr, 0, 8);
    KUNIT_ASSERT_EQ(memcmp(arr, expected, sizeof(arr)), 0, " 逆序排序正确 ");
    return KUNIT_PASS;
}

/* === 二分搜索测试 === */
static int binary_search(int *arr, int n, int target)
{
    int low = 0, high = n - 1;
    while (low <= high) {
        int mid = low + (high - low) / 2;
        if (arr[mid] == target) return mid;
        if (arr[mid] < target) low = mid + 1;
        else high = mid - 1;
    }
    return -1;
}

static kunit_result_t test_binary_search_found(void)
{
    int arr[] = {1, 3, 5, 7, 9, 11, 13, 15, 17, 19};
    KUNIT_ASSERT_EQ(binary_search(arr, 10, 7), 3, " found 7 at 索引 3");
    KUNIT_ASSERT_EQ(binary_search(arr, 10, 1), 0, " found 1 at 索引 0");
    KUNIT_ASSERT_EQ(binary_search(arr, 10, 19), 9, " found 19 at 索引 9");
    return KUNIT_PASS;
}

static kunit_result_t test_binary_search_miss(void)
{
    int arr[] = {1, 3, 5, 7, 9};
    KUNIT_ASSERT_EQ(binary_search(arr, 5, 4), -1, " not found 4");
    KUNIT_ASSERT_EQ(binary_search(arr, 5, 0), -1, " not found 0");
    KUNIT_ASSERT_EQ(binary_search(arr, 5, 100), -1, " not found 100");
    return KUNIT_PASS;
}

/* === 字符串算法测试 === */
static kunit_result_t test_str_reverse(void)
{
    char str[] = "HEPU";
    int len = 4;
    for (int i = 0; i < len / 2; i++) {
        char tmp = str[i];
        str[i] = str[len - 1 - i];
        str[len - 1 - i] = tmp;
    }
    KUNIT_ASSERT_EQ(strcmp(str, "UPEH"), 0, " 字符串反转 ");
    return KUNIT_PASS;
}

static kunit_result_t test_str_to_upper(void)
{
    char str[] = "hepu os";
    for (int i = 0; str[i]; i++) {
        if (str[i] >= 'a' && str[i] <= 'z') str[i] -= 32;
    }
    KUNIT_ASSERT_EQ(strcmp(str, "HEPU OS"), 0, " 转大写 ");
    return KUNIT_PASS;
}

static kunit_result_t test_palindrome(void)
{
    const char *s1 = "racecar";
    const char *s2 = "hello";
    int len1 = 7, len2 = 5;

    int is_pal1 = 1;
    for (int i = 0; i < len1 / 2; i++) {
        if (s1[i] != s1[len1 - 1 - i]) { is_pal1 = 0; break; }
    }
    KUNIT_ASSERT(is_pal1, "racecar is 回文 ");

    int is_pal2 = 1;
    for (int i = 0; i < len2 / 2; i++) {
        if (s2[i] != s2[len2 - 1 - i]) { is_pal2 = 0; break; }
    }
    KUNIT_ASSERT(!is_pal2, "hello not is 回文 ");
    return KUNIT_PASS;
}

/* === 数学算法测试 === */
static u32 gcd(u32 a, u32 b)
{
    while (b) { u32 t = b; b = a % b; a = t; }
    return a;
}

static kunit_result_t test_gcd(void)
{
    KUNIT_ASSERT_EQ(gcd(48, 18), 6, "gcd(48,18)=6");
    KUNIT_ASSERT_EQ(gcd(100, 75), 25, "gcd(100,75)=25");
    KUNIT_ASSERT_EQ(gcd(7, 13), 1, "gcd(7,13)=1");
    KUNIT_ASSERT_EQ(gcd(0, 5), 5, "gcd(0,5)=5");
    return KUNIT_PASS;
}

static u32 factorial(u32 n)
{
    u32 result = 1;
    for (u32 i = 2; i <= n; i++) result *= i;
    return result;
}

static kunit_result_t test_factorial(void)
{
    KUNIT_ASSERT_EQ(factorial(0), 1, "0!=1");
    KUNIT_ASSERT_EQ(factorial(1), 1, "1!=1");
    KUNIT_ASSERT_EQ(factorial(5), 120, "5!=120");
    KUNIT_ASSERT_EQ(factorial(10), 3628800, "10!=3628800");
    return KUNIT_PASS;
}

static int is_prime(u32 n)
{
    if (n < 2) return 0;
    if (n == 2) return 1;
    if (n % 2 == 0) return 0;
    for (u32 i = 3; i * i <= n; i += 2) {
        if (n % i == 0) return 0;
    }
    return 1;
}

static kunit_result_t test_prime(void)
{
    KUNIT_ASSERT(is_prime(2), "2 is 素 count ");
    KUNIT_ASSERT(is_prime(17), "17 is 素 count ");
    KUNIT_ASSERT(is_prime(97), "97 is 素 count ");
    KUNIT_ASSERT(!is_prime(1), "1 not is 素 count ");
    KUNIT_ASSERT(!is_prime(4), "4 not is 素 count ");
    KUNIT_ASSERT(!is_prime(100), "100 not is 素 count ");
    return KUNIT_PASS;
}

/* === CRC32测试 === */
static u32 crc32(const u8 *data, u32 len)
{
    u32 crc = 0xFFFFFFFF;
    for (u32 i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (u32)(-(int)(crc & 1)));
        }
    }
    return ~crc;
}

static kunit_result_t test_crc32(void)
{
    /* CRC32("123456789") = 0xCBF43926 */
    u8 data[] = "123456789";
    u32 result = crc32(data, 9);
    KUNIT_ASSERT_EQ(result, 0xCBF43926, "CRC32 standard 向量 ");
    return KUNIT_PASS;
}

static kunit_result_t test_crc32_empty(void)
{
    u32 result = crc32(0, 0);
    KUNIT_ASSERT_EQ(result, 0, "CRC32 empty data =0");
    return KUNIT_PASS;
}

/* === 校验和测试 === */
static u16 internet_checksum(const u16 *data, u32 len)
{
    u32 sum = 0;
    for (u32 i = 0; i < len / 2; i++) {
        sum += data[i];
        if (sum & 0x10000) sum = (sum & 0xFFFF) + 1;
    }
    return (u16)(~sum & 0xFFFF);
}

static kunit_result_t test_checksum(void)
{
    u16 data[] = {0x1234, 0x5678, 0x9ABC, 0xDEF0};
    u16 cksum = internet_checksum(data, 8);
    /* 验证校验和性质: 数据+校验和的校验和应为0 */
    u16 data2[] = {0x1234, 0x5678, 0x9ABC, 0xDEF0, cksum};
    u16 verify = internet_checksum(data2, 10);
    KUNIT_ASSERT_EQ(verify, 0, " verify & verify 0");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t algo_tests[] = {
    {" 冒泡排序 ", test_bubble_sort, "algo"},
    {" 冒泡 empty count 组 ", test_bubble_sort_empty, "algo"},
    {" 冒泡 unit 素 ", test_bubble_sort_single, "algo"},
    {" 快速排序 ", test_quick_sort, "algo"},
    {" 快速逆序 ", test_quick_sort_reverse, "algo"},
    {" 二分查找 hits ", test_binary_search_found, "algo"},
    {" 二分查找 not hits ", test_binary_search_miss, "algo"},
    {" 字符串反转 ", test_str_reverse, "algo"},
    {" 字符串大写 ", test_str_to_upper, "algo"},
    {" 回文判断 ", test_palindrome, "algo"},
    {" max 公约 count ", test_gcd, "algo"},
    {" orders 乘 ", test_factorial, "algo"},
    {" 素 count 判断 ", test_prime, "algo"},
    {"CRC32", test_crc32, "algo"},
    {"CRC32 empty ", test_crc32_empty, "algo"},
    {" verify & ", test_checksum, "algo"},
};
static const kunit_suite_t kunit_algo_suite = {
    " algo correctness ", algo_tests, 16
};

void kunit_algo_register(void)
{
    kunit_register_suite(&kunit_algo_suite);
    vga_puts("[KUNIT] algo correctness test suite registered(16 cases)\n");
}
