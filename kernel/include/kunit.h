#ifndef KUNIT_H
#include "types.h"
#define KUNIT_H

#include <stdint.h>

/* 测试断言宏涉及类型混合比较（int vs unsigned等），
 * 已知安全，豁免相关诊断 */
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"

/* 测试结果 */
typedef enum {
    KUNIT_PASS = 0,
    KUNIT_FAIL = 1,
    KUNIT_SKIP = 2
} kunit_result_t;

/* 测试用例 */
typedef struct {
    const char *name;
    kunit_result_t (*func)(void);
    const char *module;
} kunit_test_t;

/* 测试套件 */
typedef struct {
    const char *name;
    const kunit_test_t *tests;
    u32 test_count;
} kunit_suite_t;

/* 测试统计 */
typedef struct {
    u32 total;
    u32 passed;
    u32 failed;
    u32 skipped;
    u64 total_time_us;
} kunit_stats_t;

/* 断言宏 */
#define KUNIT_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        vga_printf("[KUNIT] 断言失败: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        extern void serial_puts(uint16_t, const char *); \
        serial_puts(0x3F8, "[KUNIT-ASSERT] "); \
        serial_puts(0x3F8, msg); \
        serial_puts(0x3F8, "\n"); \
        return KUNIT_FAIL; \
    } \
} while (0)

#define KUNIT_ASSERT_EQ(a, b, msg) do { \
    __auto_type _ka = (a); \
    __auto_type _kb = (b); \
    if (_ka != _kb) { \
        vga_printf("[KUNIT] 断言失败: %s (%d != %d) (%s:%d)\n", \
                   msg, (int)_ka, (int)_kb, __FILE__, __LINE__); \
        extern void serial_puts(uint16_t, const char *); \
        serial_puts(0x3F8, "[KUNIT-ASSERT] "); \
        serial_puts(0x3F8, msg); \
        serial_puts(0x3F8, "\n"); \
        return KUNIT_FAIL; \
    } \
} while (0)

#define KUNIT_ASSERT_NE(a, b, msg) do { \
    if ((a) == (b)) { \
        vga_printf("[KUNIT] 断言失败: %s (%d == %d) (%s:%d)\n", \
                   msg, (int)(a), (int)(b), __FILE__, __LINE__); \
        return KUNIT_FAIL; \
    } \
} while (0)

#define KUNIT_ASSERT_NULL(ptr, msg) KUNIT_ASSERT_EQ((ptr), NULL, msg)
#define KUNIT_ASSERT_NOT_NULL(ptr, msg) KUNIT_ASSERT_NE((ptr), NULL, msg)

/* API */
void kunit_init(void);
int kunit_run_suite(const kunit_suite_t *suite);
int kunit_run_all(void);
void kunit_register_suite(const kunit_suite_t *suite);
void kunit_get_stats(kunit_stats_t *stats);
void kunit_print_report(void);

/* 内置测试套件 */
extern const kunit_suite_t kunit_string_suite;
extern const kunit_suite_t kunit_math_suite;
extern const kunit_suite_t kunit_bitops_suite;
extern const kunit_suite_t kunit_heap_suite;

#endif /* KUNIT_H */
