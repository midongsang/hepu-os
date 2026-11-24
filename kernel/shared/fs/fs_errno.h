/* ============================================================================
 * 赫普OS - 文件系统共享错误码契约
 *
 * 32/64 位 VFS 对外返回同一组 POSIX 数值，禁止各自再定义一套。
 * 数值与 Linux 一致，负值表示失败。
 * ========================================================================== */
#ifndef HEPU_FS_ERRNO_H
#define HEPU_FS_ERRNO_H

#define FS_EPERM        (-1)
#define FS_ENOENT       (-2)
#define FS_EIO          (-5)
#define FS_EBADF        (-9)
#define FS_ENOMEM       (-12)
#define FS_EACCES       (-13)
#define FS_EBUSY        (-16)
#define FS_EEXIST       (-17)
#define FS_ENOTDIR      (-20)
#define FS_EISDIR       (-21)
#define FS_EINVAL       (-22)
#define FS_EMFILE       (-24)
#define FS_ENOSPC       (-28)
#define FS_ENOTEMPTY    (-39)

#endif /* HEPU_FS_ERRNO_H */
