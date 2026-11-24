/* ============================================================================
 * HepuOS 64 位 - S4 文件系统一致性自检
 *
 * 目的：64 位开机时在真实 ext2 盘上跑一组最小文件操作，并把结果打到串口，
 * 让 tools/regress64.sh 能用断言判断，而不是只看“桌面起来了”。
 *
 * 覆盖：
 *   1. 非根目录 mkdir/create
 *   2. 写文件 -> 读回内容
 *   3. fd -> 路径 -> getdents，目录项里必须能看到刚建的文件
 *
 * 注意：这里直接用 fs_fd_alloc 而不是 fs_do_open，因为 fs_do_open 的路径参数
 * 走 copy_user_path（用户地址边界），内核字符串不能当用户指针传。
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "fs_sys.h"
#include "vfs64.h"

extern void vga_puts(const char *s);
extern int ext2_64_write_file_offset(const char *path, const uint8_t *buf,
                                     uint32_t size, uint32_t offset);
extern int ext2_64_read_file_offset(const char *path, uint8_t *buf,
                                    uint32_t size, uint32_t offset);

#define S4_DIR   "/s4dir"
#define S4_FILE  "/s4dir/p4-getdents.txt"

int fs_s4_selftest(void)
{
    struct vfs_node node;
    uint8_t readback[64];
    uint8_t dirbuf[512];
    int fd, n, off, found;
    int wr, rd;

    vga_puts("[S4] fs selftest begin\n");

    /* 1. 非根目录。已存在时 mkdir/create 返回失败是正常的。 */
    if (vfs64_lookup(S4_DIR, &node) != 0) {
        vfs64_mkdir(S4_DIR, 0x41ED);
    }
    if (vfs64_lookup(S4_FILE, &node) != 0) {
        vfs64_create(S4_FILE, 0);
    }
    if (vfs64_lookup(S4_DIR, &node) != 0 || node.type != VFS_DIR) {
        vga_puts("[S4] mkdir FAIL\n");
        return -1;
    }
    vga_puts("[S4] mkdir PASS\n");

    /* 2. 写回读。用 ext2 内核侧偏移接口，避免把内核缓冲当用户指针。 */
    wr = ext2_64_write_file_offset(S4_FILE, (const uint8_t *)"p4-payload", 10, 0);
    if (wr != 10) {
        vga_puts("[S4] write FAIL\n");
        return -1;
    }
    memset(readback, 0, sizeof(readback));
    rd = ext2_64_read_file_offset(S4_FILE, readback, sizeof(readback), 0);
    if (rd < 10 || memcmp(readback, "p4-payload", 10) != 0) {
        vga_puts("[S4] readback FAIL\n");
        return -1;
    }
    vga_puts("[S4] write-read PASS\n");

    /* 3. fd -> 路径 -> getdents。目录 fd 直接用 fd 表分配，路径是内核串。 */
    fd = fs_fd_alloc(S4_DIR, 0);
    if (fd < 0) {
        vga_puts("[S4] getdents-open FAIL\n");
        return -1;
    }
    n = fs_do_getdents_kernel(fd, dirbuf, (int)sizeof(dirbuf));
    fs_fd_release(fd);

    if (n < 19) {
        vga_puts("[S4] getdents FAIL\n");
        return -1;
    }

    found = 0;
    off = 0;
    while (off + 19 <= n) {
        uint16_t reclen;
        char name[256];
        int i;

        memcpy(&reclen, dirbuf + off + 16, 2);
        if (reclen < 19 || off + reclen > n) break;

        /* dirent64 没有单独的 namelen 字段；名字从 +19 到记录尾。 */
        for (i = 0; i < 255 && off + 19 + i < off + reclen; i++) {
            name[i] = (char)dirbuf[off + 19 + i];
            if (!name[i]) break;
        }
        name[i] = 0;
        if (strcmp(name, "p4-getdents.txt") == 0) found = 1;
        off += reclen;
    }

    if (!found) {
        vga_puts("[S4] getdents-nonroot FAIL\n");
        return -1;
    }
    vga_puts("[S4] getdents-nonroot PASS\n");

    vga_puts("[S4] fs selftest PASS\n");
    return 0;
}
