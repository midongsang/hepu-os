/* ============================================================================
 * HepuOS 64 位 - 文件读写语义（从 syscall64_full.c 切出，§5-5 第 5 批）
 *
 * 边界判断（和前四批不一样，值得写下来）：
 *
 * 前三批切的是"分发"——拿 fd、查表、选错误码、调 ext2。这批的 read/write
 * 里混着三样东西，只有第 (3) 样属于文件系统：
 *
 *   (1) fd 0/1/2 的 stdio 特判 —— fd 0 从键盘读（阻塞 hlt 等中断），
 *       fd 1/2 逐字节镜像到串口。这是**终端**，不是文件。
 *   (2) 管道 —— is_pipe 分支里是 PIPE_BUF_SIZE 环形缓冲 + g_pipe_lock。
 *       这是 **IPC**，不是文件分发。
 *   (3) 真文件的读与写 + 读位置(offset)的维护 —— 这个才归 fs。
 *
 * 所以 (1)(2) 留在 syscall 层做分流，(3) 搬到这里。分流依据是调用方取好的
 * fs_fd_info_t：由 syscall 层取，是因为"是不是管道"这个判断和随后的语义调用
 * 必须用**同一份**快照，各取一次就会拼出缝合视图。
 *
 * offset 为什么不在锁里做完整流程:
 *   ext2 的 IO 和 copy_to_user 都不能在 g_fd_lock 内做（持锁做 IO 会把别的核的
 *   open/close 一起堵住，copy_to_user 缺页会把锁带进异常路径）。所以流程是
 *   "锁外读数据 -> 锁内把 offset 推到新值"。这里有个明确的取舍：两个并发 read
 *   同一个 fd 会各自从同一 offset 读到相同内容（与 Linux 语义一致，本就不保证
 *   并发 read 的原子性），但**不会**丢字节或互相覆盖 offset。
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "fs_sys.h"

/* ---- 内核侧依赖（与 syscall64_full.c 现有实现同一份，不另起一套） ---- */
extern int  copy_to_user(void *user_dst, const void *kernel_src, size_t size);
extern int  copy_from_user(void *kernel_dst, const void *user_src, size_t size);
extern void *kmalloc(uint32_t size);
extern void kfree(void *ptr);

extern int  ext2_64_read_file_offset(const char *path, uint8_t *buf,
                                     uint32_t size, uint32_t offset);
extern int  ext2_64_write_file(const char *path, const uint8_t *buf, uint32_t size);

/* 单次调用最多搬这么多: 与 sys_read/sys_write 原有的 4096 上限一致。
 * 放大它要同时确认 ext2 侧的块缓冲够不够，不是纯数字调整。 */
#define FS_RW_MAX_XFER   4096

int64_t fs_do_write(int fd, const fs_fd_info_t *fi, const char *ubuf, int count)
{
    if (!fi || count <= 0) return 0;

    /* 只读打开的文件不能写。这条原来在 sys_write 里, 现在跟着语义一起搬。 */
    if (fi->mode == 0) return -1;

    int n = count;
    if (n > FS_RW_MAX_XFER) n = FS_RW_MAX_XFER;

    uint8_t *kbuf = (uint8_t *)kmalloc((uint32_t)n);
    if (!kbuf) return -1;
    if (copy_from_user(kbuf, ubuf, (size_t)n) != 0) {
        kfree(kbuf);
        return -1;
    }

    /* 现有语义: ext2_64_write_file 是整文件写入(不按 offset 定位)。
     * 这里照原样保留 —— 改写入语义不是这批的事, 是另一条待定的技术债。 */
    int wr = ext2_64_write_file(fi->path, kbuf, (uint32_t)n);
    kfree(kbuf);

    if (wr < 0) return -1;

    /* 读位置回写。写完之后 offset 前进 n, 与旧实现一致。
     * 失败(比如并发 close 掉了这个 fd)不算错误 —— 数据已经写进文件了,
     * 返回 -1 会让调用方以为没写成功而重试, 反而写出两份。 */
    (void)fs_fd_set_offset(fd, fi->offset + (int64_t)n);

    return wr;
}

int64_t fs_do_pread(int fd, const fs_fd_info_t *fi, char *ubuf, int count,
                    int64_t offset)
{
    (void)fd;   /* pread 不回写 offset, 用不到 fd */
    if (!fi || count <= 0) return 0;
    if (fi->mode == 1) return -1;        /* 只写打开的文件不能读 */
    if (offset < 0) return -1;
    if (offset > 0x7FFFFFFF) return -1;

    int n = count;
    if (n > FS_RW_MAX_XFER) n = FS_RW_MAX_XFER;

    uint8_t *kbuf = (uint8_t *)kmalloc((uint32_t)n);
    if (!kbuf) return -1;

    int len = ext2_64_read_file_offset(fi->path, kbuf, (uint32_t)n,
                                       (uint32_t)offset);
    if (len <= 0) {
        kfree(kbuf);
        return 0;      /* EOF / 读失败都是 0 字节, 与旧实现一致 */
    }

    if (copy_to_user(ubuf, kbuf, (size_t)len) != 0) {
        kfree(kbuf);
        return -1;
    }
    kfree(kbuf);

    return len;   /* 关键差别: 这里**没有** fs_fd_set_offset */
}

int64_t fs_do_read(int fd, const fs_fd_info_t *fi, char *ubuf, int count)
{
    if (!fi || count <= 0) return 0;

    /* 只写打开的文件不能读。 */
    if (fi->mode == 1) return -1;

    /* 负 offset 会让 ext2 侧算出越界块号。防御性检查: 这个值按理进不来
     * (fs_fd_set_offset 和 sys_lseek 都拦了), 但 fd 表是共享状态, 别的路径
     * 写坏它时不该由这里来崩。 */
    if (fi->offset < 0) return -1;

    int n = count;
    if (n > FS_RW_MAX_XFER) n = FS_RW_MAX_XFER;

    uint8_t *kbuf = (uint8_t *)kmalloc((uint32_t)n);
    if (!kbuf) return -1;

    /* 按文件内偏移读 —— 旧实现只读前 4KB, 导致大文件的后半段永远读不到。 */
    int len = ext2_64_read_file_offset(fi->path, kbuf, (uint32_t)n,
                                       (uint32_t)fi->offset);
    if (len <= 0) {
        kfree(kbuf);
        return 0;      /* EOF 或读失败都是 0 字节: 与旧实现一致 */
    }

    if (copy_to_user(ubuf, kbuf, (size_t)len) != 0) {
        kfree(kbuf);
        return -1;
    }
    kfree(kbuf);

    /* 读位置前进。失败同上不计为错误。 */
    (void)fs_fd_set_offset(fd, fi->offset + (int64_t)len);

    return len;
}
