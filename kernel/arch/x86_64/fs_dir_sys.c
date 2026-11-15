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
#include "fs_sys.h"

int64_t sys_unlink(const char *path)
{
    if (!path) return -1;
    char kpath[256];
    if (copy_user_path(kpath, sizeof(kpath), path) < 0) return -1;
    extern int ext2_64_delete_file(const char *path);
    return ext2_64_delete_file(kpath);
}

int64_t sys_mkdir(const char *path, int mode)
{
    if (!path) return -1;
    (void)mode;
    char kpath[256];
    if (copy_user_path(kpath, sizeof(kpath), path) < 0) return -1;
    extern int ext2_64_create_dir(const char *path);
    return ext2_64_create_dir(kpath);
}

int64_t sys_rmdir(const char *path)
{
    if (!path) return -1;
    char kpath[256];
    if (copy_user_path(kpath, sizeof(kpath), path) < 0) return -1;
    extern int ext2_64_delete_dir(const char *path);
    return ext2_64_delete_dir(kpath);
}
