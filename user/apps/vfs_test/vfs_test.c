/* ============================================================================
 * 赫普OS - VFS文件操作测试应用
 * 测试 open/read/close 系统调用
 * ========================================================================== */

#include <hepu/syscall.h>
#include <hepu/stdio.h>
#include <hepu/string.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("=== VFS 文件操作测试 ===\n");

    unsigned char buf[16];

    /* 测试1: 读取 /initrd/hello.hug */
    printf("\n[测试1] 打开 /initrd/hello.hug\n");
    int fd = hepu_open("/initrd/hello.hug", O_RDONLY);
    if (fd < 0) {
        printf("  失败: 无法打开文件\n");
    } else {
        printf("  成功: fd=%d\n", fd);

        /* 读取前16字节 */
        int n = hepu_read(fd, buf, 16);
        printf("  读取 %d 字节: ", n);
        for (int i = 0; i < n && i < 16; i++) {
            printf("%02x ", buf[i]);
        }
        printf("\n");

        /* 测试 lseek */
        int pos = hepu_lseek(fd, 0, SEEK_END);
        printf("  文件大小: %d 字节\n", pos);

        hepu_close(fd);
        printf("  文件已关闭\n");
    }

    /* 测试2: /dev/null */
    printf("\n[测试2] /dev/null\n");
    fd = hepu_open("/dev/null", O_RDWR);
    if (fd >= 0) {
        int w = hepu_write(fd, "test", 4);
        printf("  写入 4 字节 -> 返回 %d (应丢弃)\n", w);
        int r = hepu_read(fd, buf, 16);
        printf("  读取 -> 返回 %d (应返回0=EOF)\n", r);
        hepu_close(fd);
    }

    /* 测试3: /dev/zero */
    printf("\n[测试3] /dev/zero\n");
    fd = hepu_open("/dev/zero", O_RDONLY);
    if (fd >= 0) {
        int r = hepu_read(fd, buf, 8);
        printf("  读取 %d 字节: ", r);
        for (int i = 0; i < r; i++) printf("%02x ", buf[i]);
        printf("\n");
        hepu_close(fd);
    }

    printf("\n=== VFS 测试完成 ===\n");
    return 0;
}
