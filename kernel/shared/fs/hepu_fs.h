/* ============================================================================
 * 赫普OS - 统一文件系统抽象层（类型/常量层）
 * V0.54.6: 两边共用的文件操作类型/常量/标志定义。
 *          hepu_fs_* 函数签名由 vfs.h（32 位）和 devfs64.h（64 位）末尾的
 *          static inline 包装提供——同一个名字两边都有，调用点零改动。
 * ========================================================================== */

#ifndef HEPU_FS_H
#define HEPU_FS_H

#include <stdint.h>
#include <stddef.h>

/* ---- 打开标志 ---- */
#define HEPU_FS_O_RDONLY   0x00
#define HEPU_FS_O_WRONLY   0x01
#define HEPU_FS_O_RDWR     0x02
#define HEPU_FS_O_APPEND   0x08
#define HEPU_FS_O_CREAT    0x40
#define HEPU_FS_O_TRUNC    0x200

/* ---- seek 起始位置 ---- */
#define HEPU_FS_SEEK_SET   0
#define HEPU_FS_SEEK_CUR   1
#define HEPU_FS_SEEK_END   2

/* ---- 文件类型 ---- */
typedef enum {
    HEPU_FS_REGULAR = 0,
    HEPU_FS_DIRECTORY,
    HEPU_FS_CHAR_DEVICE,
    HEPU_FS_BLOCK_DEVICE,
    HEPU_FS_FIFO,
    HEPU_FS_SYMLINK
} hepu_fs_type_t;

/* ---- 统一 stat 结构（64 位 size） ---- */
typedef struct {
    uint32_t inode;
    uint32_t type;
    uint64_t size;
    uint32_t permissions;
    uint32_t uid;
    uint32_t gid;
    uint32_t links;
} hepu_fs_stat_t;

/* ---- 统一 dirent 结构 ---- */
typedef struct {
    char name[64];
    uint32_t type;
    uint64_t size;
} hepu_fs_dirent_t;

/* 注：hepu_fs_* 函数由架构头文件提供 static inline 实现：
 *   32 位 → kernel/include/vfs.h 末尾（#include "fs/hepu_fs.h" 之后）
 *   64 位 → kernel/arch/x86_64/hepu_fs64.c（编译进内核）
 * 这样避免了 extern 声明和 static inline 在同一 .c 里的冲突。 */

#endif /* HEPU_FS_H */
