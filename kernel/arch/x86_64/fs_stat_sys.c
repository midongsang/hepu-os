/* ============================================================================
 * HepuOS 64 位 - stat / fstat 业务实现
 *
 * 从 syscall64_full.c 切出的第二批（KERNEL_SCOPE §5-5 第 2 步）。
 *
 * 为什么这一簇比目录三件套难、但仍然现在切：
 *  - 它有**分支逻辑**（字段填充的偏移、错误码选择），文档明确要求"先补单测
 *    后切"。单测就是 user/apps/statprobe.c：它按字节偏移读原始缓冲，不看
 *    结构体定义 —— 因为结构体定义本身可能就是错的，用错的尺子量错的布。
 *    修布局前它报 4 个 FAIL，修完 9 项全绿，这条链就是这批的验收。
 *  - 它碰 fd 表。这里不直接摸表，只调 fs_fd_get_path（syscall 层实现的
 *    最小查询，锁在内部取）—— fd 表的"用哪张表/加什么锁/锁里怎么记账"
 *    留在 syscall 层，否则切出去的文件还得复制一份同样的认知。
 *
 * 关键约束（KERNEL_SCOPE 原话）：偏移错一位，ld.so 会拿错误的
 * (st_dev, st_ino) 去重、把 libc.so.6 认成主程序，而**标准回归照样全绿**
 * （原生 child 不用 glibc）。所以这批的正确性不能靠回归证明，只能靠探针。
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "fs_sys.h"
#include "vfs64.h"

/* 与 syscall64_full.c 的错误码保持一致（同 #define，不是另一套） */
#define FS_EFAULT   (-14)
#define FS_ENOENT   (-2)

#define STAT_BYTES 144

extern int ext2_64_stat_ino(const char *path, uint32_t *size, uint8_t *isdir,
                            uint32_t *ino);

/* Linux x86_64 struct stat 布局（这份注释是**唯一**权威，stat 与 fstat 共用）:
 *   0 st_dev(8)      | 8 st_ino(8)    | 16 st_nlink(8)  | 24 st_mode(4)
 *   28 st_uid(4)     | 32 st_gid(4)   | 36 __pad0(4)    | 40 st_rdev(8)
 *   48 st_size(8)    | 56 st_blksize(8) | 64 st_blocks(8)
 *   72 st_atim(16)   | 88 st_mtim(16) | 104 st_ctim(16) | 120 __unused(24)
 *
 * 为什么按偏移填而不定义结构体：原来定义过 struct linux_stat，把
 * st_blksize / st_blocks 写成 int32_t，而这两个字段在 x86_64 上是 8 字节，
 * 于是从 offset 56 起后面整体前移 8 字节；sizeof 又只有 128，copy_to_user
 * 少拷 16 字节，用户态读 st_ctim 拿到的是未初始化内存。结构体一旦写错，
 * 编译器不会报错，只会让所有字段安静地错位 —— 按偏移填，偏移是显式的数字，
 * 一眼能核对。
 *
 * st_ino 必须是**真实** inode：ld.so 用 (st_dev, st_ino) 判断"两个路径是不是
 * 同一个已加载对象"。全 0 会让所有文件看起来是同一个。
 */
static void fill_stat(uint8_t *sb, uint32_t sz, uint8_t isdir, uint32_t ino)
{
    for (int i = 0; i < STAT_BYTES; i++) sb[i] = 0;
    *(uint64_t *)(sb + 0)  = 1;                       /* st_dev */
    *(uint64_t *)(sb + 8)  = ino;                     /* st_ino (真实) */
    *(uint64_t *)(sb + 16) = 1;                       /* st_nlink */
    *(uint32_t *)(sb + 24) = isdir ? 0x41ED : 0x81A4;  /* 0755 dir / 0644 file */
    *(uint32_t *)(sb + 28) = 0;                       /* st_uid (系统没有 uid 概念) */
    *(uint32_t *)(sb + 32) = 0;                       /* st_gid */
    *(uint64_t *)(sb + 40) = 0;                       /* st_rdev */
    *(uint64_t *)(sb + 48) = sz;                      /* st_size */
    *(uint64_t *)(sb + 56) = 4096;                    /* st_blksize */
    *(uint64_t *)(sb + 64) = (sz + 511) / 512;        /* st_blocks */
    /* st_atim/st_mtim/st_ctim (72/88/104) 全部给 0：
     * ext2 里读时间戳要额外一趟 inode 解析，当前没有调用方依赖它。
     * 给 0 是"如实：没有这个信息"，不是"假装时间戳是 epoch"——两者在
     * 排序/缓存场景里后果不同，这里选前者。 */
}

/* P3-6: 属性解析先走 VFS（后端 -> vfs_stat）；未命中挂载后端时回退
 * ext2_64_stat_ino 直调。已走/未走清单见 docs/design/VFS64.md。 */
static int resolve_stat(const char *kp, uint32_t *sz, uint8_t *isdir, uint32_t *ino)
{
    struct vfs_stat vs;
    if (vfs64_stat(kp, &vs) == 0) {
        *sz     = (uint32_t)vs.size;
        *isdir  = (vs.mode & 0x4000) ? 1 : 0;
        *ino    = (uint32_t)vs.ino;
        return 0;
    }
    return ext2_64_stat_ino(kp, sz, isdir, ino);
}

int64_t sys_stat(const char *path, void *statbuf)
{
    if (!path) return -1;
    char kp[256];
    if (copy_user_path(kp, sizeof(kp), path) < 0) return -1;

    uint32_t sz = 0; uint8_t id = 0; uint32_t ino = 0;
    if (resolve_stat(kp, &sz, &id, &ino) != 0) return -1;

    uint8_t sb[STAT_BYTES];
    fill_stat(sb, sz, id, ino);
    if (statbuf && copy_to_user(statbuf, sb, STAT_BYTES) != 0) return FS_EFAULT;
    return 0;
}

int64_t sys_fstat(int fd, void *statbuf)
{
    if (!statbuf) return FS_EFAULT;

    char kp[256];
    if (fs_fd_get_path(fd, kp, sizeof(kp)) != 0) return -1;

    uint32_t sz = 0; uint8_t id = 0; uint32_t ino = 0;
    if (resolve_stat(kp, &sz, &id, &ino) != 0) return FS_ENOENT;

    uint8_t sb[STAT_BYTES];
    fill_stat(sb, sz, id, ino);
    if (copy_to_user(statbuf, sb, STAT_BYTES) != 0) return FS_EFAULT;
    return 0;
}
