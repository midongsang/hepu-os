#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>

#define VFS_MAX_NAME 64
#define VFS_MAX_PATH 256
#define VFS_MAX_FILES 256   /* V6.77: 从128扩展到256 */
#define VFS_MAX_FDS 256     /* V6.77: 从128扩展到256, 与constants.h MAX_FDS对齐 */

/* 文件类型 */
typedef enum {
    VFS_REGULAR = 0,
    VFS_DIRECTORY,
    VFS_CHAR_DEVICE,
    VFS_BLOCK_DEVICE,
    VFS_FIFO,
    VFS_SYMLINK
} vfs_node_type_t;

/* 打开标志 */
#define VFS_O_RDONLY  0x00
#define VFS_O_WRONLY  0x01
#define VFS_O_RDWR    0x02
#define VFS_O_APPEND  0x08
#define VFS_O_CREAT   0x40
#define VFS_O_TRUNC   0x200

/* seek 起始位置 */
#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2

/* 前向声明 */
struct vfs_node;
typedef struct vfs_node vfs_node_t;

/* 文件操作接口 */
typedef struct {
    int (*open)(vfs_node_t *node, int flags);
    int (*close)(vfs_node_t *node);
    int (*read)(vfs_node_t *node, uint32_t offset, void *buf, uint32_t size);
    int (*write)(vfs_node_t *node, uint32_t offset, const void *buf, uint32_t size);
    int (*ioctl)(vfs_node_t *node, uint32_t cmd, uint32_t arg);
} vfs_file_ops_t;

/* VFS 节点 (inode) */
struct vfs_node {
    char name[VFS_MAX_NAME];
    vfs_node_type_t type;
    uint32_t size;
    uint32_t inode_num;
    uint32_t permissions;
    uint32_t uid;
    uint32_t gid;
    uint32_t links;

    /* 子节点 (目录用) */
    vfs_node_t *children;
    vfs_node_t *next;       /* 同级链表 */
    vfs_node_t *parent;

    /* 文件操作 */
    vfs_file_ops_t *ops;

    /* 私有数据 (文件系统特定) */
    void *private_data;
};

/* 打开的文件描述符 */
typedef struct {
    int used;
    vfs_node_t *node;
    int flags;
    uint32_t position;
} vfs_fd_t;

/* 初始化VFS */
void vfs_init(void);

/* 创建节点 */
vfs_node_t *vfs_create_node(const char *name, vfs_node_type_t type);

/* 添加子节点 */
void vfs_add_child(vfs_node_t *parent, vfs_node_t *child);

/* 查找节点 (按路径) */
vfs_node_t *vfs_find(const char *path);

/* 获取根节点 */
vfs_node_t *vfs_get_root(void);

/* 挂载文件系统到路径 */
int vfs_mount(const char *path, vfs_node_t *root);

/* 文件操作 (通过文件描述符) */
int vfs_bind_stdio(vfs_node_t *node);
int vfs_open(const char *path, int flags);
int vfs_close(int fd);
int vfs_read(int fd, void *buf, uint32_t size);
int vfs_write(int fd, const void *buf, uint32_t size);
int vfs_lseek(int fd, int32_t offset, int whence);
int vfs_fsize(int fd);

/* 目录操作 */
typedef struct {
    char name[VFS_MAX_NAME];
    vfs_node_type_t type;
    uint32_t size;
} vfs_dirent_t;

int vfs_readdir(const char *path, vfs_dirent_t *buf, int max_entries);
int vfs_readdir_fd(int fd, vfs_dirent_t *buf, int max_entries);

/* === V0.40 新增：完整文件操作 === */

/* 文件状态结构 */
typedef struct {
    uint32_t inode;
    uint32_t type;
    uint32_t size;
    uint32_t permissions;
    uint32_t uid;
    uint32_t gid;
    uint32_t links;
} vfs_stat_t;

/* 目录/文件操作 */
int vfs_mkdir(const char *path);
int vfs_rmdir(const char *path);
int vfs_unlink(const char *path);
int vfs_rename(const char *old_path, const char *new_path);
int vfs_stat(const char *path, vfs_stat_t *buf);
int vfs_fstat(int fd, vfs_stat_t *buf);
int vfs_access(const char *path);

/* 软链接操作（V8.21新增） */
int vfs_symlink(const char *target, const char *linkpath);
int vfs_readlink(const char *path, char *buf, uint32_t bufsiz);
int vfs_lstat(const char *path, vfs_stat_t *buf);

/* 硬链接操作（V8.21新增） */
int vfs_link(const char *oldpath, const char *newpath);

/* 文件权限操作（V8.21新增） */
int vfs_chmod(const char *path, uint32_t mode);
int vfs_fchmod(int fd, uint32_t mode);
int vfs_chown(const char *path, uint32_t uid, uint32_t gid);
int vfs_fchown(int fd, uint32_t uid, uint32_t gid);

/* 文件描述符操作 */
int vfs_dup(int oldfd);
int vfs_dup2(int oldfd, int newfd);

/* ============================================================================
 * V0.54.6: hepu_fs_* 统一接口 → vfs_* 私有 API 映射
 * 新代码 include "fs/hepu_fs.h" 后直接用 hepu_fs_* 即可跨架构
 * ========================================================================== */
#include "fs/hepu_fs.h"

static inline void hepu_fs_init(void)                     { vfs_init(); }
static inline int  hepu_fs_open(const char *path, int f)  { return vfs_open(path, f); }
static inline int  hepu_fs_close(int fd)                  { return vfs_close(fd); }
static inline int  hepu_fs_read(int fd, void *b, uint32_t s) { return vfs_read(fd, b, s); }
static inline int  hepu_fs_write(int fd, const void *b, uint32_t s) { return vfs_write(fd, b, s); }
static inline int64_t hepu_fs_lseek(int fd, int64_t o, int w) { return (int64_t)vfs_lseek(fd, (int32_t)o, w); }
static inline int64_t hepu_fs_fsize(int fd)               { return (int64_t)vfs_fsize(fd); }
static inline int  hepu_fs_stat(const char *p, hepu_fs_stat_t *buf) {
    if (!buf) return -1;
    vfs_stat_t vs;
    if (vfs_stat(p, &vs) != 0) return -1;
    buf->inode = vs.inode; buf->type = vs.type; buf->size = vs.size;
    buf->permissions = vs.permissions; buf->uid = vs.uid; buf->gid = vs.gid;
    buf->links = vs.links;
    return 0;
}
static inline int  hepu_fs_fstat(int fd, hepu_fs_stat_t *buf) {
    if (!buf) return -1;
    vfs_stat_t vs;
    if (vfs_fstat(fd, &vs) != 0) return -1;
    buf->inode = vs.inode; buf->type = vs.type; buf->size = vs.size;
    buf->permissions = vs.permissions; buf->uid = vs.uid; buf->gid = vs.gid;
    buf->links = vs.links;
    return 0;
}
static inline int  hepu_fs_access(const char *p)          { return vfs_access(p); }
static inline int  hepu_fs_mkdir(const char *p)           { return vfs_mkdir(p); }
static inline int  hepu_fs_rmdir(const char *p)           { return vfs_rmdir(p); }
static inline int  hepu_fs_unlink(const char *p)          { return vfs_unlink(p); }
static inline int  hepu_fs_rename(const char *o, const char *n) { return vfs_rename(o, n); }
static inline int  hepu_fs_readdir(const char *p, hepu_fs_dirent_t *buf, int max) {
    /* 需要转换 vfs_dirent_t → hepu_fs_dirent_t */
    static vfs_dirent_t tmp[16];
    int n = vfs_readdir(p, tmp, max);
    for (int i = 0; i < n && buf; i++) {
        buf[i].type = tmp[i].type; buf[i].size = tmp[i].size;
        for (int j = 0; j < 64 && tmp[i].name[j]; j++) buf[i].name[j] = tmp[i].name[j];
        buf[i].name[63] = 0;
    }
    return n;
}
/* ext2 辅助：32 位 stub，不走 inode 直接路径解析 */
static inline uint32_t hepu_fs_path_to_inode(const char *p) { return 0; }
static inline int hepu_fs_read_inode(uint32_t i, void *b, uint32_t s) { return -1; }
static inline int hepu_fs_write_inode(uint32_t i, const void *b, uint32_t s) { return -1; }

#endif /* VFS_H */
