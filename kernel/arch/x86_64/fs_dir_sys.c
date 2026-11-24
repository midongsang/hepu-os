/* ============================================================================
 * HepuOS 64 位 - 目录类 syscall 业务实现
 *
 * 从 syscall64_full.c 切出的第一批（KERNEL_SCOPE §5-5 第 1 步）。
 * 选这一簇的理由：依赖面最小 —— 只需 copy_user_path + ext2_64 的四个入口，
 * 不碰 fd 表、不碰锁、不碰进程上下文，切出去之后行为不可能变。
 *
 * 行为一字未改，只是换了住址。
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "fs_sys.h"
#include "vfs64.h"
#include "../../shared/fs/fs_errno.h"
#include "../../shared/fs/fs_path.h"

extern void *kmalloc(uint32_t size);
extern void  kfree(void *ptr);

#define FS_GETDENTS_MAX_ENTRIES 64
#define FS_GETDENTS_MAX_BYTES   8192

/* 把 fd 对应目录的条目打成 Linux dirent64 字节流，写入内核缓冲。
 * 返回写入字节数；<0 为 POSIX 错误码。 */
int fs_do_getdents_kernel(int fd, uint8_t *kbuf, int count)
{
    char path[256];
    struct vfs_node node;
    struct vfs_dirent *de;
    int n, total = 0;

    if (!kbuf || count < 19) return 0;
    if (fs_fd_get_path(fd, path, sizeof(path)) != 0) return FS_EBADF;

    /* 先确认是目录：对普通文件 getdents 必须返回 ENOTDIR，而不是空结果。 */
    if (vfs64_lookup(path, &node) != 0) return FS_ENOENT;
    if (node.type != VFS_DIR) return FS_ENOTDIR;

    de = (struct vfs_dirent *)kmalloc(
        FS_GETDENTS_MAX_ENTRIES * sizeof(struct vfs_dirent));
    if (!de) return FS_ENOMEM;

    n = vfs64_iterate(path, de, FS_GETDENTS_MAX_ENTRIES);
    if (n < 0) {
        kfree(de);
        return FS_EIO;
    }

    for (int i = 0; i < n; i++) {
        int name_len = 0;
        int reclen;
        uint64_t d_ino;
        int64_t d_off;
        uint16_t d_reclen;

        while (name_len < 255 && de[i].d_name[name_len]) name_len++;
        reclen = fs_dirent64_reclen(name_len);
        if (total + reclen > count) break;

        memset(kbuf + total, 0, (uint32_t)reclen);
        d_ino = de[i].d_ino;
        d_off = (int64_t)(de[i].d_off + de[i].d_reclen);
        d_reclen = (uint16_t)reclen;
        memcpy(kbuf + total + 0, &d_ino, 8);
        memcpy(kbuf + total + 8, &d_off, 8);
        memcpy(kbuf + total + 16, &d_reclen, 2);
        kbuf[total + 18] = de[i].d_type;
        memcpy(kbuf + total + 19, de[i].d_name, (uint32_t)name_len);
        total += reclen;
    }

    kfree(de);
    return total;
}

int64_t fs_do_getdents(int fd, uint64_t dirp, int count)
{
    int cap;
    int64_t total;
    uint8_t *kbuf;

    if (count < 19) return 0;
    cap = count > FS_GETDENTS_MAX_BYTES ? FS_GETDENTS_MAX_BYTES : count;
    kbuf = (uint8_t *)kmalloc((uint32_t)cap);
    if (!kbuf) return FS_ENOMEM;

    total = fs_do_getdents_kernel(fd, kbuf, cap);
    if (total >= 0 && total > 0 && copy_to_user((void *)dirp, kbuf,
                                                (size_t)total) != 0) {
        total = FS_EIO;
    }
    kfree(kbuf);
    return total;
}

int64_t sys_unlink(const char *path)
{
    if (!path) return -1;
    char kpath[256];
    if (copy_user_path(kpath, sizeof(kpath), path) < 0) return -1;

    /* S6: 先走 VFS 后端 unlink；未挂载/后端不支持回退 ext2 直调。
     * 返回码口径不变：0 成功，-1 未命中/失败。 */
    int rc = vfs64_unlink(kpath);
    if (rc >= 0) return rc;
    extern int ext2_64_delete_file(const char *path);
    return ext2_64_delete_file(kpath);
}

int64_t sys_mkdir(const char *path, int mode)
{
    if (!path) return -1;
    char kpath[256];
    if (copy_user_path(kpath, sizeof(kpath), path) < 0) return -1;

    /* S6: 先走 VFS 后端 mkdir；mode 与旧口径一致，v1 忽略。回退 ext2 直调。 */
    int rc = vfs64_mkdir(kpath, mode);
    if (rc >= 0) return rc;
    extern int ext2_64_create_dir(const char *path);
    return ext2_64_create_dir(kpath);
}

int64_t sys_rmdir(const char *path)
{
    if (!path) return -1;
    char kpath[256];
    if (copy_user_path(kpath, sizeof(kpath), path) < 0) return -1;

    /* S6: 先走 VFS 后端 rmdir；回退 ext2 直调。非空目录仍返回 -2（透传 ext2 口径）。 */
    int rc = vfs64_rmdir(kpath);
    if (rc >= 0) return rc;
    extern int ext2_64_delete_dir(const char *path);
    return ext2_64_delete_dir(kpath);
}
