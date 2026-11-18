/* ============================================================================
 * HepuOS 64 位 - VFS 抽象层实现（P3-6）
 *
 * 职责边界：
 *   - 挂载点表 + 最长前缀路径解析，集中在这一层；上层 syscall 不再各自拼路径。
 *   - 用户指针校验与拷贝（copy_from_user / copy_to_user）在 kernel-side 入口
 *     完成；后端 ops 拿到的是内核缓冲（见 vfs64.h 头注）。
 *   - 后端只注册 ops 表与 lookup，不允许自己注册 syscall（lessons 第一节落地
 *     步骤 4）。
 *
 * v1 已知边界（完整清单见 docs/design/VFS64.md）：
 *   - fd 表仍存路径字符串 + 偏移（fs_sys.h），VFS 句柄是每次 open 的临时快照；
 *   - 未挂载/未命中路径由 syscall 层回退 ext2 直调；
 *   - ioctl / mmap / 写回时序 / 并发锁粒度为 deferred 项。
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "vfs64.h"

extern int  copy_from_user(void *kernel_dst, const void *user_src, size_t size);
extern int  copy_to_user(void *user_dst, const void *kernel_src, size_t size);
extern void *kmalloc(uint32_t size);
extern void  kfree(void *ptr);

#define VFS64_MAX_MOUNTS 8

static struct vfs_mount g_mounts[VFS64_MAX_MOUNTS];
static int  g_mount_count = 0;
static int  g_vfs64_ready  = 0;

/* 后端实现分别在 vfs64_ext2.c / vfs64_dev.c。 */
extern struct vfs_backend vfs64_be_ext2;
extern struct vfs_backend vfs64_be_dev;

/* prefix 与 path 匹配：前缀相等，且边界是串尾或 '/'。
 * 例："/dev" 匹配 "/dev" 与 "/dev/nullread"，不匹配 "/devx"。 */
static int prefix_match(const char *prefix, const char *path)
{
    size_t pl = strlen(prefix);
    if (strncmp(prefix, path, pl) != 0) return 0;
    if (path[pl] == '\0') return 1;
    return path[pl] == '/';
}

int vfs64_mount(const char *prefix, struct vfs_backend *be)
{
    if (!prefix || !be || g_mount_count >= VFS64_MAX_MOUNTS) return -1;
    g_mounts[g_mount_count].prefix = prefix;
    g_mounts[g_mount_count].be      = be;
    g_mount_count++;
    return 0;
}

int vfs64_lookup(const char *kpath, struct vfs_node *node)
{
    if (!kpath || !node) return -1;

    const struct vfs_mount *best = 0;
    size_t best_len = 0;
    for (int i = 0; i < g_mount_count; i++) {
        if (prefix_match(g_mounts[i].prefix, kpath)) {
            size_t pl = strlen(g_mounts[i].prefix);
            if (!best || pl > best_len) {
                best     = &g_mounts[i];
                best_len = pl;
            }
        }
    }
    if (!best) return -1;

    memset(node, 0, sizeof(*node));
    return best->be->lookup(kpath, node);
}

int vfs64_open(const char *kpath, uint32_t flags, struct vfs_file *f)
{
    if (!kpath || !f) return -1;
    memset(f, 0, sizeof(*f));

    if (vfs64_lookup(kpath, &f->node) != 0) return -1;
    f->flags = flags;

    if (f->node.ops && f->node.ops->open) {
        if (f->node.ops->open(&f->node, f, flags) != 0) return -1;
    }
    return 0;
}

long vfs64_read(struct vfs_file *f, void *ubuf, uint64_t len, uint64_t *done)
{
    uint64_t d = 0;
    if (done) *done = 0;
    if (!f || !f->node.ops || !f->node.ops->read || len == 0) return -1;

    uint8_t *kbuf = (uint8_t *)kmalloc((uint32_t)len);
    if (!kbuf) return -1;

    /* ops 收到内核缓冲 kbuf；用户态回写在本入口完成（铁律 1/2）。 */
    long r = f->node.ops->read(f, kbuf, len, &d);
    if (r == 0 && d > 0) {
        if (copy_to_user(ubuf, kbuf, (size_t)d) != 0) {
            kfree(kbuf);
            return -1;
        }
    }
    kfree(kbuf);

    if (done) *done = d;
    return r;
}

long vfs64_write(struct vfs_file *f, const void *ubuf, uint64_t len, uint64_t *done)
{
    uint64_t d = 0;
    if (done) *done = 0;
    if (!f || !f->node.ops || !f->node.ops->write || len == 0) return -1;

    uint8_t *kbuf = (uint8_t *)kmalloc((uint32_t)len);
    if (!kbuf) return -1;

    if (copy_from_user(kbuf, ubuf, (size_t)len) != 0) {
        kfree(kbuf);
        return -1;
    }

    long r = f->node.ops->write(f, kbuf, len, &d);
    kfree(kbuf);

    if (done) *done = d;
    return r;
}

int vfs64_close(struct vfs_file *f)
{
    if (!f) return -1;
    if (f->node.ops && f->node.ops->close) f->node.ops->close(f);
    return 0;
}

int vfs64_stat(const char *kpath, struct vfs_stat *out)
{
    if (!kpath || !out) return -1;

    struct vfs_node node;
    if (vfs64_lookup(kpath, &node) != 0) return -1;
    if (!node.ops || !node.ops->stat) return -1;

    memset(out, 0, sizeof(*out));
    return node.ops->stat(&node, out);
}

long vfs64_read_path(const char *kpath, uint64_t offset,
                     void *ubuf, uint64_t len, uint64_t *done)
{
    struct vfs_file f;
    if (vfs64_open(kpath, 0, &f) != 0) return -1;
    f.offset = offset;

    uint64_t d = 0;
    long r = vfs64_read(&f, ubuf, len, &d);
    vfs64_close(&f);

    if (done) *done = d;
    return r == 0 ? (long)d : r;
}

long vfs64_write_path(const char *kpath, uint64_t offset,
                      const void *ubuf, uint64_t len, uint64_t *done)
{
    struct vfs_file f;
    if (vfs64_open(kpath, 0, &f) != 0) return -1;
    f.offset = offset;

    uint64_t d = 0;
    long r = vfs64_write(&f, ubuf, len, &d);
    vfs64_close(&f);

    if (done) *done = d;
    return r == 0 ? (long)d : r;
}

int vfs64_init(void)
{
    if (g_vfs64_ready) return 0;

    /* 根挂载：ext2 是 v1 唯一的块文件系统后端。 */
    vfs64_mount("/", &vfs64_be_ext2);

    /* 伪设备后端：/dev 前缀。只实现 open/read/close，作为抽象层解耦验收点。 */
    vfs64_mount("/dev", &vfs64_be_dev);

    g_vfs64_ready = 1;
    return 0;
}
