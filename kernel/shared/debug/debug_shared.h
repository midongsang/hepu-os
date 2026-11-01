/* ============================================================================
 * 赫普OS - 共享错误处理与调试工具
 * 统一源码树：32位和64位共用
 * V0.10.152: 初始版本
 * ========================================================================== */

#ifndef SHARED_DEBUG_H
#define SHARED_DEBUG_H

#include <stdint.h>
#include <stddef.h>

/* 错误码定义 */
#define HEPU_OK          0
#define HEPU_ERR        -1
#define HEPU_ERR_NOMEM  -2
#define HEPU_ERR_INVAL  -3
#define HEPU_ERR_NOENT  -4
#define HEPU_ERR_EXIST  -5
#define HEPU_ERR_BUSY   -6
#define HEPU_ERR_PERM   -7
#define HEPU_ERR_IO     -8
#define HEPU_ERR_NOSPC  -9
#define HEPU_ERR_RANGE  -10

/* 调试级别 */
#define DEBUG_LEVEL_NONE  0
#define DEBUG_LEVEL_ERROR 1
#define DEBUG_LEVEL_WARN  2
#define DEBUG_LEVEL_INFO  3
#define DEBUG_LEVEL_DEBUG 4

/* 断言宏 */
#define HEPU_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            hepu_debug_assert_fail(#cond, __FILE__, __LINE__); \
        } \
    } while (0)

/* 静态断言 */
#define HEPU_STATIC_ASSERT(cond, msg) \
    typedef char hepu_static_assert_##__LINE__[(cond) ? 1 : -1]

/* 调试输出函数（由架构相关代码实现） */
extern void hepu_debug_puts(const char *str);
extern void hepu_debug_put_hex(uint64_t val);
extern void hepu_debug_put_dec(uint64_t val);

/* 断言失败处理 */
extern void hepu_debug_assert_fail(const char *expr, const char *file, int line);

/* 错误码转字符串 */
static inline const char *hepu_strerror(int err)
{
    switch (err) {
        case HEPU_OK:         return "OK";
        case HEPU_ERR:        return "General error";
        case HEPU_ERR_NOMEM:  return "Out of memory";
        case HEPU_ERR_INVAL:  return "Invalid argument";
        case HEPU_ERR_NOENT:  return "No such file or directory";
        case HEPU_ERR_EXIST:  return "File exists";
        case HEPU_ERR_BUSY:   return "Device or resource busy";
        case HEPU_ERR_PERM:   return "Permission denied";
        case HEPU_ERR_IO:     return "I/O error";
        case HEPU_ERR_NOSPC:  return "No space left on device";
        case HEPU_ERR_RANGE:  return "Value out of range";
        default:              return "Unknown error";
    }
}

/* 调试宏 */
#define HEPU_DEBUG_INFO(fmt, ...) \
    do { \
        hepu_debug_puts("[INFO] "); \
        hepu_debug_puts(__FILE__); \
        hepu_debug_puts(":"); \
        hepu_debug_put_dec(__LINE__); \
        hepu_debug_puts(" "); \
    } while (0)

#define HEPU_DEBUG_ERROR(msg) \
    do { \
        hepu_debug_puts("[ERROR] "); \
        hepu_debug_puts(msg); \
        hepu_debug_puts("\n"); \
    } while (0)

#define HEPU_DEBUG_WARN(msg) \
    do { \
        hepu_debug_puts("[WARN] "); \
        hepu_debug_puts(msg); \
        hepu_debug_puts("\n"); \
    } while (0)

/* 函数追踪 */
#define HEPU_FUNC_ENTER() \
    do { \
        hepu_debug_puts("[ENTER] "); \
        hepu_debug_puts(__func__); \
        hepu_debug_puts("\n"); \
    } while (0)

#define HEPU_FUNC_EXIT() \
    do { \
        hepu_debug_puts("[EXIT] "); \
        hepu_debug_puts(__func__); \
        hepu_debug_puts("\n"); \
    } while (0)

/* 编译时检查 */
#define HEPU_CHECK_SIZE(type, size) \
    HEPU_STATIC_ASSERT(sizeof(type) == size, "Size mismatch for " #type)

#define HEPU_CHECK_OFFSET(type, field, offset) \
    HEPU_STATIC_ASSERT(offsetof(type, field) == offset, "Offset mismatch for " #type "." #field)

#endif /* SHARED_DEBUG_H */
