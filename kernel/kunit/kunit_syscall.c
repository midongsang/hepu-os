/* ============================================================================
 * 赫普OS - 系统调用单元测试
 * 覆盖：系统调用号/参数传递/返回值/错误处理
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"
#include "syscall.h"

/* === 系统调用号测试 === */
static kunit_result_t test_syscall_numbers(void)
{
    KUNIT_ASSERT_EQ(SYS_HEPU_INFO, 0, "HEPU_INFO=0");
    KUNIT_ASSERT_EQ(SYS_HEPU_FP_VERIFY, 1, "HEPU_FP_VERIFY=1");
    KUNIT_ASSERT_EQ(SYS_PRINT, 2, "PRINT=2");
    KUNIT_ASSERT_EQ(SYS_MALLOC, 3, "MALLOC=3");
    KUNIT_ASSERT_EQ(SYS_FREE, 4, "FREE=4");
    KUNIT_ASSERT_EQ(SYS_EXIT, 5, "EXIT=5");
    KUNIT_ASSERT_EQ(SYS_YIELD, 6, "YIELD=6");
    KUNIT_ASSERT_EQ(SYS_GETPID, 7, "GETPID=7");
    KUNIT_ASSERT_EQ(SYS_OPEN, 8, "OPEN=8");
    KUNIT_ASSERT_EQ(SYS_READ, 9, "READ=9");
    KUNIT_ASSERT_EQ(SYS_WRITE, 10, "WRITE=10");
    KUNIT_ASSERT_EQ(SYS_CLOSE, 11, "CLOSE=11");
    KUNIT_ASSERT(SYS_MAX > 50, "SYS_MAX>50");
    return KUNIT_PASS;
}

static kunit_result_t test_syscall_range(void)
{
    /* 系统调用号范围检查 */
    KUNIT_ASSERT(SYS_HEPU_INFO >= 0, " min call nums >=0");
    KUNIT_ASSERT(SYS_MAX <= 256, " max call nums <=256");

    /* 验证系统调用号连续性（关键调用） */
    KUNIT_ASSERT_EQ(SYS_READ, SYS_OPEN + 1, "READ=OPEN+1");
    KUNIT_ASSERT_EQ(SYS_WRITE, SYS_READ + 1, "WRITE=READ+1");
    KUNIT_ASSERT_EQ(SYS_CLOSE, SYS_WRITE + 1, "CLOSE=WRITE+1");
    return KUNIT_PASS;
}

/* === 系统调用参数测试 === */
typedef u32 (*syscall_fn_t)(u32, u32, u32, u32);

static u32 test_syscall_handler(u32 num, u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    (void)arg4;
    (void)arg3;
    /* 模拟系统调用分发 */
    switch (num) {
        case 0: return 0x12345678;  /* 固定返回值 */
        case 1: return arg1 + arg2;  /* 加法 */
        case 2: return arg1 * arg2;  /* 乘法 */
        case 3: return (arg1 > arg2) ? arg1 : arg2;  /* 最大值 */
        default: return (u32)-1;  /* 无效调用 */
    }
}

static kunit_result_t test_syscall_args(void)
{
    /* 测试参数传递 */
    u32 result = test_syscall_handler(1, 10, 20, 0, 0);
    KUNIT_ASSERT_EQ(result, 30, " 加法 param 传递 ");

    result = test_syscall_handler(2, 6, 7, 0, 0);
    KUNIT_ASSERT_EQ(result, 42, " 乘法 param 传递 ");

    result = test_syscall_handler(3, 100, 200, 0, 0);
    KUNIT_ASSERT_EQ(result, 200, " max value param 传递 ");
    return KUNIT_PASS;
}

static kunit_result_t test_syscall_return(void)
{
    /* 测试返回值 */
    u32 result = test_syscall_handler(0, 0, 0, 0, 0);
    KUNIT_ASSERT_EQ(result, 0x12345678, " 固定返回 value ");

    /* 无效调用返回-1 */
    result = test_syscall_handler(999, 0, 0, 0, 0);
    KUNIT_ASSERT_EQ(result, (u32)-1, " invalid call 返回 -1");
    return KUNIT_PASS;
}

/* === 系统调用错误处理测试 === */
static u32 test_syscall_with_error(u32 num, u32 arg1)
{
    /* 模拟带错误检查的系统调用 */
    if (num >= 100) return (u32)-1;  /* ENOSYS */
    if (arg1 == 0) return (u32)-2;   /* EINVAL */
    if (arg1 >= 0xFFFF0000) return (u32)-3;  /* EFAULT */
    return arg1 * 2;
}

static kunit_result_t test_syscall_errors(void)
{
    KUNIT_ASSERT_EQ(test_syscall_with_error(999, 100), (u32)-1, " invalid call nums ");
    KUNIT_ASSERT_EQ(test_syscall_with_error(1, 0), (u32)-2, " invalid param ");
    KUNIT_ASSERT_EQ(test_syscall_with_error(1, 0xFFFF1234), (u32)-3, " bad addr ");
    KUNIT_ASSERT_EQ(test_syscall_with_error(1, 21), 42, " 正常 call ");
    return KUNIT_PASS;
}

/* === 文件描述符测试 === */
#define TEST_FD_MAX 64
#define FD_FREE 0
#define FD_USED 1

static u8 fd_table[TEST_FD_MAX];
static u32 fd_offset_table[TEST_FD_MAX];

static void fd_table_init(void)
{
    memset(fd_table, FD_FREE, sizeof(fd_table));
    memset(fd_offset_table, 0, sizeof(fd_offset_table));
}

static int fd_alloc(void)
{
    for (int i = 0; i < TEST_FD_MAX; i++) {
        if (fd_table[i] == FD_FREE) {
            fd_table[i] = FD_USED;
            fd_offset_table[i] = 0;
            return i;
        }
    }
    return -1;
}

static int fd_free(int fd)
{
    if (fd < 0 || fd >= TEST_FD_MAX) return -1;
    if (fd_table[fd] == FD_FREE) return -1;
    fd_table[fd] = FD_FREE;
    fd_offset_table[fd] = 0;
    return 0;
}

static kunit_result_t test_fd_alloc(void)
{
    fd_table_init();

    int fd1 = fd_alloc();
    int fd2 = fd_alloc();
    int fd3 = fd_alloc();

    KUNIT_ASSERT_EQ(fd1, 0, " 第一 fd=0");
    KUNIT_ASSERT_EQ(fd2, 1, " 第二 fd=1");
    KUNIT_ASSERT_EQ(fd3, 2, " 第三 fd=2");

    fd_free(fd2);
    int fd4 = fd_alloc();
    KUNIT_ASSERT_EQ(fd4, 1, " 重用 release fd=1");
    return KUNIT_PASS;
}

static kunit_result_t test_fd_exhaust(void)
{
    fd_table_init();

    int fds[TEST_FD_MAX];
    for (int i = 0; i < TEST_FD_MAX; i++) {
        fds[i] = fd_alloc();
        KUNIT_ASSERT_EQ(fds[i], i, " alloc all fd");
    }

    int overflow = fd_alloc();
    KUNIT_ASSERT_EQ(overflow, -1, "fd 耗尽返回 -1");

    for (int i = 0; i < TEST_FD_MAX; i++) fd_free(fds[i]);
    return KUNIT_PASS;
}

static kunit_result_t test_fd_invalid(void)
{
    fd_table_init();

    KUNIT_ASSERT_EQ(fd_free(-1), -1, " 负 fd release failed ");
    KUNIT_ASSERT_EQ(fd_free(TEST_FD_MAX), -1, " 超出范围 fd release failed ");
    KUNIT_ASSERT_EQ(fd_free(0), -1, " not alloc fd release failed ");
    return KUNIT_PASS;
}

/* === lseek偏移测试 === */
static kunit_result_t test_fd_lseek(void)
{
    fd_table_init();

    int fd = fd_alloc();
    KUNIT_ASSERT(fd >= 0, " alloc fd");

    /* 模拟lseek */
    fd_offset_table[fd] = 100;
    KUNIT_ASSERT_EQ(fd_offset_table[fd], 100, " 偏移 set 100");

    fd_offset_table[fd] += 50;
    KUNIT_ASSERT_EQ(fd_offset_table[fd], 150, " 偏移增加 to 150");

    fd_free(fd);
    return KUNIT_PASS;
}

/* === 权限检查测试 === */
#define PERM_READ  0x4
#define PERM_WRITE 0x2
#define PERM_EXEC  0x1

static int check_permission(u16 mode, u8 user_type, u8 requested)
{
    u16 perm;
    switch (user_type) {
        case 0: perm = (mode >> 6) & 0x7; break;
        case 1: perm = (mode >> 3) & 0x7; break;
        case 2: perm = mode & 0x7; break;
        default: return -1;
    }
    return (perm & requested) == requested ? 0 : -1;
}

static kunit_result_t test_permission_check(void)
{
    u16 mode = 0755;  /* rwxr-xr-x */

    KUNIT_ASSERT_EQ(check_permission(mode, 0, PERM_READ), 0, "owner 读 ");
    KUNIT_ASSERT_EQ(check_permission(mode, 0, PERM_WRITE), 0, "owner 写 ");
    KUNIT_ASSERT_EQ(check_permission(mode, 0, PERM_EXEC), 0, "owner exec ");
    KUNIT_ASSERT_EQ(check_permission(mode, 1, PERM_WRITE), -1, "group no 写 ");
    KUNIT_ASSERT_EQ(check_permission(mode, 2, PERM_WRITE), -1, "other no 写 ");
    KUNIT_ASSERT_EQ(check_permission(mode, 2, PERM_READ), 0, "other 读 ");
    KUNIT_ASSERT_EQ(check_permission(mode, 2, PERM_EXEC), 0, "other exec ");

    mode = 0644;  /* rw-r--r-- */
    KUNIT_ASSERT_EQ(check_permission(mode, 0, PERM_WRITE), 0, "owner 写 ");
    KUNIT_ASSERT_EQ(check_permission(mode, 0, PERM_EXEC), -1, "owner no exec ");
    KUNIT_ASSERT_EQ(check_permission(mode, 1, PERM_WRITE), -1, "group no 写 ");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t syscall_tests[] = {
    {" syscall 号 ", test_syscall_numbers, "syscall"},
    {" call nums 范围 ", test_syscall_range, "syscall"},
    {" param 传递 ", test_syscall_args, "syscall"},
    {" 返回 value ", test_syscall_return, "syscall"},
    {" error handling ", test_syscall_errors, "syscall"},
    {"fd alloc ", test_fd_alloc, "syscall"},
    {"fd 耗尽 ", test_fd_exhaust, "syscall"},
    {"fd invalid ", test_fd_invalid, "syscall"},
    {"fd lseek", test_fd_lseek, "syscall"},
    {" perm check ", test_permission_check, "syscall"},
};
static const kunit_suite_t kunit_syscall_suite = {
    " syscall ", syscall_tests, 10
};

void kunit_syscall_register(void)
{
    kunit_register_suite(&kunit_syscall_suite);
    vga_puts("[KUNIT] syscall test suite registered(10 cases)\n");
}
