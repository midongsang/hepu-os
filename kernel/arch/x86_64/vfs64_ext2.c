/* ============================================================================
 * HepuOS 64 位 - VFS ext2 后端（P3-6）
 *
 * 把 ext2_64 的按路径 API 包成 vfs_file_ops。映射关系（与 VFS64.md 对照表
 * 一致）：
 *
 *   VFS ops        | ext2_64 底层函数
 *   ---------------|------------------------------------------
 *   lookup         | ext2_64_is_mounted + ext2_64_lookup +
 *                   ext2_64_stat_ino（取 size/isdir/ino）
 *   open           | 无状态（ext2 API 按路径，无句柄需要持有）
 *   close          | 无状态
 *   read           | ext2_64_read_file_offset
 *   write          | ext2_64_write_file_offset
 *   stat           | ext2_64_stat_ino
 *   ioctl          | v1 未实现，返回 -1
 *
 * 注意：ext2_64_lookup 返回 inode 号（0 = 未命中），不是句柄；本后端把路径
 * 留在 vfs_node.path，后续 read/write 仍按路径 + 偏移调用。
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "vfs64.h"

extern int      ext2_64_is_mounted(void);
extern uint32_t ext2_64_lookup(const char *path);
extern int      ext2_64_stat_ino(const char *path, uint32_t *size,
                                 uint8_t *isdir, uint32_t *ino);
extern int      ext2_64_read_file_offset(const char *path, uint8_t *buf,
                                        uint32_t size, uint32_t offset);
extern int      ext2_64_write_file_offset(const char *path, const uint8_t *buf,
                                           uint32_t size, uint32_t offset);

static int ext2_open(struct vfs_node *v, struct vfs_file *f, uint32_t flags)
{
    (void)v; (void)f; (void)flags;
    return 0;
}

static int ext2_close(struct vfs_file *f)
{
    (void)f;
    return 0;
}

/* ubuf 是 vfs64_read 入口拷好的内核缓冲；这里不做任何用户态解引用。 */
static long ext2_read(struct vfs_file *f, void *ubuf, uint64_t len, uint64_t *done)
{
    struct vfs_node *v = &f->node;
    int r = ext2_64_read_file_offset(v->path, (uint8_t *)ubuf,
                                     (uint32_t)len, (uint32_t)f->offset);
    if (r < 0) { *done = 0; return -1; }
    *done = (uint64_t)r;   /* 短读：实际读到的字节经 done 回报 */
    return 0;
}

static long ext2_write(struct vfs_file *f, const void *ubuf,
                       uint64_t len, uint64_t *done)
{
    struct vfs_node *v = &f->node;
    int r = ext2_64_write_file_offset(v->path, (const uint8_t *)ubuf,
                                      (uint32_t)len, (uint32_t)f->offset);
    if (r < 0) { *done = 0; return -1; }
    *done = (uint64_t)r;
    return 0;
}

static long ext2_ioctl(struct vfs_file *f, uint64_t cmd, void *uarg)
{
    (void)f; (void)cmd; (void)uarg;
    return -1;   /* deferred：v1 未接 ioctl */
}

static int ext2_stat(struct vfs_node *v, struct vfs_stat *out)
{
    uint32_t sz = 0;
    uint8_t  isdir = 0;
    uint32_t ino = 0;
    if (ext2_64_stat_ino(v->path, &sz, &isdir, &ino) != 0) return -1;

    out->dev     = 1;
    out->ino     = ino;
    out->nlink   = 1;
    out->mode    = isdir ? 0x41ED : 0x81A4;   /* 0755 dir / 0644 file */
    out->uid     = 0;
    out->gid     = 0;
    out->size    = sz;
    out->blksize = 4096;
    out->blocks  = (sz + 511) / 512;
    out->mtime   = 0;   /* ext2 inode 时间戳 v1 未取，置 0 如实标注 */
    return 0;
}

/* 编译期常量 ops 表（铁律 4）：不允许运行时替换函数指针。 */
static const struct vfs_file_ops vfs64_ext2_ops = {
    .open  = ext2_open,
    .close = ext2_close,
    .read  = ext2_read,
    .write = ext2_write,
    .ioctl = ext2_ioctl,
    .stat  = ext2_stat,
};

static int ext2_lookup(const char *path, struct vfs_node *out)
{
    if (!ext2_64_is_mounted()) return -1;

    uint32_t ino = ext2_64_lookup(path);
    if (ino == 0) return -1;

    uint32_t sz = 0;
    uint8_t  isdir = 0;
    if (ext2_64_stat_ino(path, &sz, &isdir, &ino) != 0) return -1;

    strncpy(out->path, path, sizeof(out->path) - 1);
    out->path[sizeof(out->path) - 1] = '\0';
    out->type = isdir ? VFS_DIR : VFS_FILE;
    out->size = sz;
    out->ino  = ino;
    out->ops  = &vfs64_ext2_ops;
    return 0;
}

struct vfs_backend vfs64_be_ext2 = {
    .name   = "ext2",
    .lookup = ext2_lookup,
};
