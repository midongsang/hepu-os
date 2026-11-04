/* ============================================================================
 * 赫普OS 64位 - hepu_fs 薄实现
 * V0.54.6: 把 ext2_64_* + devfs64_* 包装成 hepu_fs_* 统一接口
 *
 * 注意：ext2_64_* 没有 fd 语义（只有直接路径读写），故 hepu_fs 内部
 * 维护 fd→(path, offset, flags, 大小) 映射表，hepu_fs_read 每次从 offset
 * 位置读取、ext2_64_read_file 后截取。这不是最优实现但跑通了统一接口。
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "fs/hepu_fs.h"
#include "devfs64.h"

extern void devfs64_init(void);

/* ext2 外部符号（ext2_64.c 实现） */
extern int      ext2_64_is_mounted(void);
extern int      ext2_64_read_file(const char *path, uint8_t *buf, int max_size);
extern int      ext2_64_write_file(const char *path, const uint8_t *data, int size);
extern int      ext2_64_create_file(const char *path);
extern uint32_t ext2_64_lookup(const char *path);
extern int      ext2_read_inode(uint32_t ino, void *inode);
extern int      ext2_write_inode(uint32_t ino, const void *inode);

/* fd 表（64 位进程 fd 空间简化为全局共享，将来按进程隔离） */
#define FS64_MAX_FDS 64

typedef struct {
    int      used;
    int      flags;
    int64_t  offset;
    int64_t  file_size;
    char     path[256];
} fs64_fd_slot_t;

static fs64_fd_slot_t g_fs64_fds[FS64_MAX_FDS];

static inline void hepu_fs_init(void) {
    devfs64_init();
    memset(g_fs64_fds, 0, sizeof(g_fs64_fds));
}

static inline int hepu_fs_open(const char *path, int flags) {
    if (!path || !path[0]) return -1;

    /* 找空槽 */
    int fd = -1;
    for (int i = 0; i < FS64_MAX_FDS; i++) {
        if (!g_fs64_fds[i].used) { fd = i; break; }
    }
    if (fd < 0) return -1;

    fs64_fd_slot_t *s = &g_fs64_fds[fd];
    memset(s, 0, sizeof(*s));
    s->used = 1;
    s->flags = flags;
    s->offset = 0;
    strncpy(s->path, path, sizeof(s->path) - 1);

    /* O_CREAT 时创建文件 */
    if (flags & HEPU_FS_O_CREAT) {
        if (ext2_64_is_mounted())
            ext2_64_create_file(path);
    }

    /* 获取初始文件大小（读 0 字节试探 ext2 返回值） */
    if (ext2_64_is_mounted()) {
        uint8_t probe[1];
        s->file_size = ext2_64_read_file(path, probe, 0);
        if (s->file_size < 0) s->file_size = 0;
    }

    return fd;
}

static inline int hepu_fs_close(int fd) {
    if (fd < 0 || fd >= FS64_MAX_FDS) return -1;
    if (!g_fs64_fds[fd].used) return -1;
    g_fs64_fds[fd].used = 0;
    return 0;
}

static inline int hepu_fs_read(int fd, void *buf, uint32_t size) {
    if (fd < 0 || fd >= FS64_MAX_FDS || !g_fs64_fds[fd].used) return -1;
    if (!ext2_64_is_mounted()) return -1;
    if (!buf) return -1;

    fs64_fd_slot_t *s = &g_fs64_fds[fd];

    /* 读完整文件到临时缓冲（ext2_64_read_file 是全文件读） */
    static uint8_t tmp_buf[8192];  /* 8KB 限制，够读启动脚本/配置 */
    int total = ext2_64_read_file(s->path, tmp_buf, sizeof(tmp_buf));
    if (total < 0) return -1;

    /* offset 越界返回 0（EOF） */
    if (s->offset >= total) return 0;

    /* 从 offset 开始拷贝最多 size 字节 */
    uint32_t avail = (uint32_t)(total - s->offset);
    uint32_t to_copy = avail < size ? avail : size;
    memcpy(buf, tmp_buf + s->offset, to_copy);
    s->offset += to_copy;
    return (int)to_copy;
}

static inline int hepu_fs_write(int fd, const void *buf, uint32_t size) {
    if (fd < 0 || fd >= FS64_MAX_FDS || !g_fs64_fds[fd].used) return -1;
    if (!ext2_64_is_mounted()) return -1;
    if (!buf) return -1;

    fs64_fd_slot_t *s = &g_fs64_fds[fd];
    int n = ext2_64_write_file(s->path, (const uint8_t *)buf, (int)size);
    if (n > 0) {
        s->offset += n;
        /* 更新文件大小（下次读时用） */
        s->file_size = n;
    }
    return n;
}

static inline int64_t hepu_fs_lseek(int fd, int64_t offset, int whence) {
    if (fd < 0 || fd >= FS64_MAX_FDS || !g_fs64_fds[fd].used) return -1;
    fs64_fd_slot_t *s = &g_fs64_fds[fd];

    switch (whence) {
    case HEPU_FS_SEEK_SET: s->offset = offset; break;
    case HEPU_FS_SEEK_CUR: s->offset += offset; break;
    case HEPU_FS_SEEK_END: s->offset = s->file_size + offset; break;
    default: return -1;
    }
    if (s->offset < 0) s->offset = 0;
    return s->offset;
}

static inline int64_t hepu_fs_fsize(int fd) {
    if (fd < 0 || fd >= FS64_MAX_FDS || !g_fs64_fds[fd].used) return -1;
    return g_fs64_fds[fd].file_size;
}

static inline int hepu_fs_stat(const char *path, hepu_fs_stat_t *buf) {
    if (!buf || !path) return -1;
    memset(buf, 0, sizeof(*buf));
    if (!ext2_64_is_mounted()) return -1;

    uint32_t ino = ext2_64_lookup(path);
    if (ino == 0) return -1;

    buf->inode = ino;
    buf->type = HEPU_FS_REGULAR;
    /* 用 ext2_read_inode 取 size/权限 */
    extern int ext2_read_inode(uint32_t ino, void *inode);
    /* inode 结构体里有 i_size 和 i_mode，但 64 位这里简化：
       读一次全文件取 size（hepu_fs_read 里已经用过这个 trick） */
    static uint8_t probe[1];
    int sz = ext2_64_read_file(path, probe, 0);
    buf->size = sz > 0 ? (uint64_t)sz : 0;
    return 0;
}

static inline int hepu_fs_fstat(int fd, hepu_fs_stat_t *buf) {
    if (!buf) return -1;
    if (fd < 0 || fd >= FS64_MAX_FDS || !g_fs64_fds[fd].used) return -1;
    return hepu_fs_stat(g_fs64_fds[fd].path, buf);
}

static inline int hepu_fs_access(const char *path) {
    if (!ext2_64_is_mounted()) return 0;
    return ext2_64_lookup(path) != 0 ? 1 : 0;
}

static inline int hepu_fs_mkdir(const char *path) {
    /* 64 位 ext2_64 无 mkdir stub */
    (void)path;
    return -1;
}
static inline int hepu_fs_rmdir(const char *path) { (void)path; return -1; }
static inline int hepu_fs_unlink(const char *path) { (void)path; return -1; }
static inline int hepu_fs_rename(const char *o, const char *n) { (void)o; (void)n; return -1; }
static inline int hepu_fs_readdir(const char *p, hepu_fs_dirent_t *buf, int max) {
    (void)p; (void)buf; (void)max;
    /* 64 位 ext2_64 无 readdir stub */
    return 0;
}

/* ext2 辅助 */
static inline uint32_t hepu_fs_path_to_inode(const char *path) {
    if (!ext2_64_is_mounted() || !path) return 0;
    return ext2_64_lookup(path);
}

static inline int hepu_fs_read_inode(uint32_t ino, void *inode, uint32_t sz) {
    (void)sz;
    return ext2_read_inode(ino, inode);
}

static inline int hepu_fs_write_inode(uint32_t ino, const void *inode, uint32_t sz) {
    (void)sz;
    return ext2_write_inode(ino, inode);
}
