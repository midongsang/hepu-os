/* ============================================================================
 * HepuOS 64 位 - VFS 伪设备后端（P3-6 解耦验收点）
 *
 * 验收标准（UNIX_LESSONS.md 第一节）：新增一个只读设备，只实现 open/read/close
 * 三个函数，不改任何调用方（syscall）代码。本文件就是这个样例：
 *
 *   /dev/nullread  read 恒返回 EOF（*done = 0）
 *   /dev/vfsdemo   read 返回固定内容 "HEPU-VFS-DEMO\n"
 *
 * write / ioctl / stat 置 NULL：v1 不提供，上层走 VFS 空指针保护。
 *
 * 注意：这是 vfs64 自己的伪设备层，与旧 devfs64.c（uint32 offset/void* buf
 * 的旧签名、未接进 open 路径）互不替代；旧 devfs64 的迁移是 deferred 项。
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "vfs64.h"

#define DEV_NODE_NULL  0
#define DEV_NODE_DEMO  1

static const char DEMO_MSG[] = "HEPU-VFS-DEMO\n";

static int dev_open(struct vfs_node *v, struct vfs_file *f, uint32_t flags)
{
    (void)v; (void)f; (void)flags;
    return 0;
}

static int dev_close(struct vfs_file *f)
{
    (void)f;
    return 0;
}

/* ubuf 是 vfs64_read 入口拷好的内核缓冲；逐字节填，不做用户态解引用。 */
static long dev_read(struct vfs_file *f, void *ubuf, uint64_t len, uint64_t *done)
{
    *done = 0;
    int id = (int)(intptr_t)f->node.priv;

    if (id == DEV_NODE_NULL) return 0;   /* EOF：无数据可读 */

    uint64_t mlen = sizeof(DEMO_MSG) - 1;
    uint64_t n = len < mlen ? len : mlen;
    /* ubuf 是 vfs64_read 入口拷好的内核缓冲；经 memcpy 填充，源码内不做直接解引用。 */
    memcpy(ubuf, DEMO_MSG, (size_t)n);
    *done = n;
    return 0;
}

/* 编译期常量 ops 表（铁律 4）：只填 open/read/close，其余留空。 */
static const struct vfs_file_ops vfs64_dev_ops = {
    .open  = dev_open,
    .close = dev_close,
    .read  = dev_read,
    .write = 0,
    .ioctl = 0,
    .stat  = 0,
};

static int dev_lookup(const char *path, struct vfs_node *out)
{
    int id;
    uint64_t size = 0;

    if (strcmp(path, "/dev/nullread") == 0) {
        id = DEV_NODE_NULL;
    } else if (strcmp(path, "/dev/vfsdemo") == 0) {
        id = DEV_NODE_DEMO;
        size = sizeof(DEMO_MSG) - 1;
    } else {
        return -1;
    }

    strncpy(out->path, path, sizeof(out->path) - 1);
    out->path[sizeof(out->path) - 1] = '\0';
    out->type = VFS_CHR;
    out->size = size;
    out->priv = (void *)(intptr_t)id;
    out->ops  = &vfs64_dev_ops;
    return 0;
}

struct vfs_backend vfs64_be_dev = {
    .name   = "dev",
    .lookup = dev_lookup,
};
