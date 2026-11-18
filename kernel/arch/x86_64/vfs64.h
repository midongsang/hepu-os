/* ============================================================================
 * HepuOS 64 位 - VFS 抽象层（P3-6）
 *
 * 对外契约冻结来源：docs/design/UNIX_LESSONS.md 第一节「VFS ops 表冻结为
 * 对外契约」。本文件的数据结构与 ops 签名与该节逐字对齐，不得另立一套：
 *
 *   struct vfs_file_ops { open, close, read, write, ioctl, stat }
 *
 * 四条铁律（与 lessons 第一节一致）：
 *   1. ubuf 永远是用户态指针，内核负责校验与拷贝；ops 实现内禁止直接解引用
 *      （不得出现 *(uint32_t*)ubuf 这类写法）。
 *   2. 短读写必须走 *done 回报；返回值只表示成功/失败。
 *   3. 字段只增不改：v1 之后新增能力走 ioctl 或在结构体尾部追加函数指针。
 *   4. ops 表编译期常量（const struct vfs_file_ops），不允许运行时替换。
 *
 * v1 拷贝边界说明：vfs64_* kernel-side 入口先把用户缓冲拷到内核缓冲，再调
 * ops；ops 收到的指针是内核缓冲。冻结签名里的形参名 ubuf 保留，以对齐契约
 * 文本。静态自检（grep 直接解引用 ubuf）见 docs/design/VFS64.md。
 * ========================================================================== */
#ifndef HEPU_VFS64_H
#define HEPU_VFS64_H

#include <stdint.h>
#include <stddef.h>

/* 节点类型：对应 Linux S_IFDIR / S_IFREG / S_IFCHR / S_IFBLK 的 v1 子集。 */
enum vfs_type {
    VFS_DIR  = 1,
    VFS_FILE = 2,
    VFS_CHR  = 3,
    VFS_BLK  = 4,
};

/* 后端无关的属性快照。ext2 拿不到的字段（如 mtime）置 0，如实标注，
 * 不伪造 epoch 时间戳。 */
struct vfs_stat {
    uint64_t dev;
    uint64_t ino;
    uint64_t nlink;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    uint64_t blksize;
    uint64_t blocks;
    int64_t  mtime;      /* 拿不到置 0 */
};

struct vfs_node;
struct vfs_file;
struct vfs_backend;

/* S5：跨后端统一的目录项布局（内核 staging，不直接写给用户态）。
 * 字节布局对齐现有 64 位 getdents64 打包：
 *   d_ino@0(8) d_off@8(8) d_reclen@16(2) d_type@18(1) d_name@19[]
 * 后端 iterate 只填这个内核数组；用户态打包（readdir 的 name\0type\0 或
 * getdents 的 dirent64 字节流）由 syscall 入口层负责。 */
#define VFS_DT_REG  8   /* Linux DT_REG  */
#define VFS_DT_DIR  4   /* Linux DT_DIR  */

struct vfs_dirent {
    uint64_t d_ino;
    uint64_t d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[256];
};

/* 冻结契约 ops 表。函数指针顺序与 lessons 第一节一致；
 * read/write 带 uint64_t *done（短读写回报）。 */
struct vfs_file_ops {
    int  (*open)(struct vfs_node *v, struct vfs_file *f, uint32_t flags);
    int  (*close)(struct vfs_file *f);
    long (*read)(struct vfs_file *f, void *ubuf, uint64_t len, uint64_t *done);
    long (*write)(struct vfs_file *f, const void *ubuf, uint64_t len, uint64_t *done);
    long (*ioctl)(struct vfs_file *f, uint64_t cmd, void *uarg);
    int  (*stat)(struct vfs_node *v, struct vfs_stat *out);
};

/* VFS 节点：一次 lookup 的产物。priv 是后端私有指针（ext2 v1 不用，
 * 路径存在 node->path；伪设备用 priv 区分节点）。 */
struct vfs_node {
    enum vfs_type              type;
    uint64_t                   size;
    uint64_t                   ino;
    char                       path[256];
    const struct vfs_file_ops *ops;
    struct vfs_backend        *be;
    void                      *priv;
};

/* 打开后的文件句柄。v1 的读位置仍由 syscall 层 fd 表维护（见 fs_sys.h），
 * 这里的 offset 是打开时刻的快照，ops 实现按它定位。 */
struct vfs_file {
    struct vfs_node  node;
    uint64_t         offset;
    uint32_t         flags;
    void            *priv;
};

/* 后端：注册到挂载点表。lookup 按路径解析节点；0 = 命中并填好 out，
 * -1 = 未命中。
 *
 * 路径式变更方法（create/mkdir/rmdir/unlink/iterate）挂在后端而不是冻结的
 * vfs_file_ops 上：它们不依附某个已打开的句柄，是后端对路径的操作。
 * 全部可选（NULL = 后端不支持，syscall 层回退直调）。 */
struct vfs_backend {
    const char *name;
    int (*lookup)(const char *path, struct vfs_node *out);
    /* S4：O_CREAT 新建普通文件。0 成功，-1 失败。 */
    int (*create)(const char *path, uint32_t flags);
    /* S5：枚举目录项，填 out[0..max)，返回条目数；<0 失败。 */
    int (*iterate)(const char *path, struct vfs_dirent *out, int max);
    /* S6：删除普通文件 / 建目录 / 删空目录。返回后端原始错误码（0 成功，
     * <0 失败；非空目录 rmdir 可能返回 -2）。NULL = 后端不支持。 */
    int (*unlink)(const char *path);
    int (*mkdir)(const char *path, int mode);
    int (*rmdir)(const char *path);
};

struct vfs_mount {
    const char         *prefix;
    struct vfs_backend *be;
};

/* ---- kernel-side 入口（用户指针校验与拷贝的落实点） ---- */

/* 注册内置后端（ext2 挂 "/"，伪设备挂 "/dev"）。幂等。 */
int  vfs64_init(void);

/* 显式挂载：prefix 为路径前缀（"/" 或 "/dev"）。返回 0 成功，-1 表满/参数错。 */
int  vfs64_mount(const char *prefix, struct vfs_backend *be);

/* 路径解析：最长前缀匹配后端并调其 lookup。node 由调用方提供。
 * 0 = 命中；-1 = 未挂载或后端未命中。 */
int  vfs64_lookup(const char *kpath, struct vfs_node *node);

/* 打开：lookup + ops->open。f 由调用方提供，内嵌 node。 */
int  vfs64_open(const char *kpath, uint32_t flags, struct vfs_file *f);

/* S4：按路径新建普通文件（O_CREAT 后端入口）。0 成功，-1 未挂载/后端不支持。
 * 调用方仍负责 fd 分配与失败回退；本入口只做后端分派。 */
int  vfs64_create(const char *kpath, uint32_t flags);

/* S5：按路径枚举目录，填内核 staging 数组；返回条目数，-1 未挂载/后端不支持。 */
int  vfs64_iterate(const char *kpath, struct vfs_dirent *out, int max);

/* S6：目录变更三件套分派。返回后端原始返回码；-1 表示未挂载/后端不支持。 */
int  vfs64_unlink(const char *kpath);
int  vfs64_mkdir(const char *kpath, int mode);
int  vfs64_rmdir(const char *kpath);

/* 读：entry 内 copy_to_user 回写；*done 回报实际字节。返回 0 成功，-1 失败。 */
long vfs64_read(struct vfs_file *f, void *ubuf, uint64_t len, uint64_t *done);

/* 写：entry 内 copy_from_user 拷入；*done 回报实际字节。 */
long vfs64_write(struct vfs_file *f, const void *ubuf, uint64_t len, uint64_t *done);

int  vfs64_close(struct vfs_file *f);

/* 按路径取属性；0 成功，-1 未命中/后端无 stat。 */
int  vfs64_stat(const char *kpath, struct vfs_stat *out);

/* 便捷入口：按路径 + 指定偏移读/写（不回写 fd 游标，语义对齐 pread）。
 * 返回实际字节数；-1 表示失败；未命中由调用方回退直调。 */
long vfs64_read_path(const char *kpath, uint64_t offset,
                     void *ubuf, uint64_t len, uint64_t *done);
long vfs64_write_path(const char *kpath, uint64_t offset,
                      const void *ubuf, uint64_t len, uint64_t *done);

#endif /* HEPU_VFS64_H */
