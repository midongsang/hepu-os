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
extern int      ext2_64_create_file(const char *path);
extern int      ext2_64_stat_ino(const char *path, uint32_t *size,
                                 uint8_t *isdir, uint32_t *ino);
extern int      ext2_64_read_file_offset(const char *path, uint8_t *buf,
                                        uint32_t size, uint32_t offset);
extern int      ext2_64_write_file_offset(const char *path, const uint8_t *buf,
                                           uint32_t size, uint32_t offset);
extern int      ext2_64_list_dir_entries_ex(const char *path, char names[][256],
                                             uint8_t *types, uint32_t *sizes,
                                             int max_entries);
extern int      ext2_64_delete_file(const char *path);
extern int      ext2_64_create_dir(const char *path);
extern int      ext2_64_delete_dir(const char *path);
extern void    *kmalloc(uint32_t size);
extern void     kfree(void *ptr);

#define EXT2_ITER_MAX  64

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

/* S4：O_CREAT 新建普通文件。返回 ext2_64_create_file 的 0/-1。 */
static int ext2_create(const char *path, uint32_t flags)
{
    (void)flags;
    if (!ext2_64_is_mounted()) return -1;
    return ext2_64_create_file(path);
}

/* S5：枚举目录项。names/types 走 kmalloc（64×256=16KB，不能压栈）。
 * d_ino 通过对子路径再 lookup 取得；list_ex 本身不回传 inode。 */
static int ext2_iterate(const char *path, struct vfs_dirent *out, int max)
{
    if (!ext2_64_is_mounted()) return -1;
    if (max > EXT2_ITER_MAX) max = EXT2_ITER_MAX;

    char (*names)[256] = (char (*)[256])kmalloc(EXT2_ITER_MAX * 256);
    uint8_t  *types  = (uint8_t  *)kmalloc(EXT2_ITER_MAX);
    uint32_t *sizes  = (uint32_t *)kmalloc(EXT2_ITER_MAX * 4);
    if (!names || !types || !sizes) { kfree(names); kfree(types); kfree(sizes); return -1; }

    int n = ext2_64_list_dir_entries_ex(path, names, types, sizes, EXT2_ITER_MAX);
    if (n < 0) { kfree(names); kfree(types); kfree(sizes); return -1; }
    if (n > max) n = max;

    uint64_t off = 0;
    for (int i = 0; i < n; i++) {
        int nl = 0; while (names[i][nl] && nl < 255) nl++;

        char child[256];
        size_t pl = strlen(path);
        strncpy(child, path, sizeof(child) - 1);
        child[sizeof(child) - 1] = 0;
        size_t cl = strlen(child);
        if (!(cl == 1 && child[0] == '/') && child[cl - 1] != '/') {
            if (cl < sizeof(child) - 1) { child[cl++] = '/'; child[cl] = 0; }
        }
        strncpy(child + cl, names[i], sizeof(child) - cl - 1);
        child[sizeof(child) - 1] = 0;
        (void)pl;

        uint32_t ino = ext2_64_lookup(child);
        uint16_t reclen = (uint16_t)((19 + nl + 7) & ~7);

        memset(&out[i], 0, sizeof(out[i]));
        out[i].d_ino    = ino;
        out[i].d_off    = off;
        out[i].d_reclen = reclen;
        out[i].d_type   = types[i] ? VFS_DT_DIR : VFS_DT_REG;
        strncpy(out[i].d_name, names[i], sizeof(out[i].d_name) - 1);
        off += reclen;
    }

    kfree(names); kfree(types); kfree(sizes);
    return n;
}

/* S6：目录变更三件套。返回 ext2 原始错误码透传：0 成功；-1 未命中/非目录等；
 * -2 目录非空（rmdir）。mode 与旧 sys_mkdir 口径一致，v1 忽略。 */
static int ext2_unlink(const char *path)
{
    return ext2_64_delete_file(path);
}

static int ext2_mkdir(const char *path, int mode)
{
    (void)mode;
    return ext2_64_create_dir(path);
}

static int ext2_rmdir(const char *path)
{
    return ext2_64_delete_dir(path);
}

struct vfs_backend vfs64_be_ext2 = {
    .name     = "ext2",
    .lookup   = ext2_lookup,
    .create   = ext2_create,
    .iterate  = ext2_iterate,
    .unlink   = ext2_unlink,
    .mkdir    = ext2_mkdir,
    .rmdir    = ext2_rmdir,
};
