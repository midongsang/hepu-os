/* ============================================================================
 * 赫普OS 64位 - 用户态测试程序
 * 测试系统调用：write, getpid, time, brk, exit
 * V0.10.73: 初始版本
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 用户态系统调用封装（在user_lib.c中定义） */
extern int64_t sys_write(int fd, const void *buf, size_t count);
extern int64_t sys_read(int fd, void *buf, size_t count);
extern int64_t sys_open(const char *path, int flags, int mode);
extern int64_t sys_close(int fd);
extern void sys_exit(int exit_code);
extern int64_t sys_getpid(void);
extern int64_t sys_time(void);
extern void *sys_brk(void *addr);
extern void user_puts(const char *s);
extern void user_putchar(char c);
extern void user_putint(int64_t num);

#define STDOUT_FILENO 1

/* 用户态主程序 */
void user_program_main(void)
{
    /* 测试1: write系统调用 - 输出欢迎信息 */
    user_puts("=== HepuOS User Mode Test ===\n");
    user_puts("Hello from user mode!\n\n");

    /* 测试2: getpid系统调用 */
    user_puts("[Test] getpid: ");
    int64_t pid = sys_getpid();
    user_putint(pid);
    user_puts("\n");

    /* 测试3: time系统调用 */
    user_puts("[Test] time (seconds since midnight): ");
    int64_t time = sys_time();
    user_putint(time);
    user_puts("\n");

    /* 测试4: brk系统调用 */
    user_puts("[Test] brk(0): ");
    void *old_brk = sys_brk((void *)0);
    user_putint((int64_t)old_brk);
    user_puts("\n");

    user_puts("[Test] brk(0x600000): ");
    void *new_brk = sys_brk((void *)0x600000);
    user_putint((int64_t)new_brk);
    user_puts("\n");

    /* 测试5: 连续write */
    user_puts("\n[Test] Continuous write test:\n");
    for (int i = 0; i < 5; i++) {
        user_puts("  Line ");
        user_putint(i);
        user_puts("\n");
    }

    /* 测试6: open/close文件 */
    user_puts("\n[Test] open('/test.txt'): ");
    int64_t fd = sys_open("/test.txt", 0, 0);
    user_putint(fd);
    user_puts("\n");

    if (fd >= 0) {
        user_puts("[Test] close(fd): ");
        int64_t ret = sys_close((int)fd);
        user_putint(ret);
        user_puts("\n");
    }

    /* 完成 */
    user_puts("\n=== All tests completed ===\n");
    user_puts("User mode test PASSED!\n");

    /* 退出 */
    sys_exit(0);
}

/* 用户态入口（被内核调用） */
void user_program_syscall_test(void)
{
    user_program_main();
}
