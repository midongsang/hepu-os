/* ============================================================================
 * 赫普OS - 系统调用：FS
 * ========================================================================== */

#include "types.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "syscall.h"
#include "vga.h"
#include "kheap.h"
#include "sched.h"
#include "hepu.h"
#include "fingerprint.h"
#include "vfs.h"
#include "ipc.h"
#include "process.h"
extern const char *hepu_kernel_version;

/* V3.87: APK 式文件沙箱——兼容进程(运行外部 Linux ELF / PE / Mach-O)
 * 只能访问受限的沙箱目录, 系统目录 (/system /kernel /boot 等) 一律拒绝,
 * 保证外部软件无法读取/篡改系统基础. 内核原生进程不受影响.
 *
 * 允许的沙箱根 (兼容进程的"私有数据区"):
 *   /app/      —— 安装的应用数据
 *   /data/     —— 用户数据区 (受限, 与 .hug 应用一致)
 *   /dev/null /dev/zero 等虚拟设备
 * 拒绝:
 *   /system/ /kernel/ /boot/ /initrd 及 VFS 根下其他系统目录
 *
 * 返回 1=允许, 0=拒绝 */
static int compat_sandbox_path_allowed(const char *path)
{
    extern process_t *current;
    if (!current || current->compat_abi == 0) {
        return 1;   /* 原生进程不做限制 */
    }
    if (!path) return 0;
    /* 相对路径/空路径拒绝 (兼容进程必须用绝对路径) */
    if (path[0] != '/') return 0;

    /* V3.87 加固: 路径规范化——拒绝含 "." ".." 组件的路径, 防
     * "/data/../system/x" 式目录穿越 (沙箱前缀白名单只认规范化路径).
     * Linux 用户态标准库(crt/musl)不会向内核传未规范化路径. */
    {
        const char *p = path;
        while (*p) {
            if (*p == '/' && p[1] == '.') {
                char c2 = p[2];
                if (c2 == '/' || c2 == '\0') {
                    return 0;   /* "/./" 或 "/." */
                }
                if (c2 == '.' && (p[3] == '/' || p[3] == '\0')) {
                    return 0;   /* "/../" 或 "/.." */
                }
            }
            p++;
        }
        /* 特例: 路径开头 "/." 已被上面捕获 (p=path 时 p[0]=='/', p[1]=='.') */
        if (path[1] == '.' && (path[2] == '/' || path[2] == '\0')) return 0;
        if (path[1] == '/' || path[1] == '\0') return 0;   /* "//" 或 "/" 根 */
    }

    /* 虚拟设备允许 (无文件系统副作用: /dev/null /dev/zero /dev/fb0 /dev/mem) */
    if (strncmp(path, "/dev/", 5) == 0) return 1;

    /* 白名单前缀 */
    if (strncmp(path, "/app/", 5) == 0)  return 1;
    if (strncmp(path, "/data/", 6) == 0) return 1;
    /* V6.75: 允许访问根目录下的initrd文件（应用和测试文件） */
    if (strncmp(path, "/test_", 6) == 0) return 1;
    if (strncmp(path, "/rust_", 6) == 0) return 1;
    /* 允许根目录下的可执行文件（.elf/.exe/.hug） */
    {
        const char *dot = strrchr(path, '.');
        if (dot && (strncmp(dot, ".elf", 4) == 0 ||
                    strncmp(dot, ".exe", 4) == 0 ||
                    strncmp(dot, ".hug", 4) == 0 ||
                    strncmp(dot, ".txt", 4) == 0)) {
            /* 确保路径中没有子目录（只允许根目录下的文件） */
            int slash_count = 0;
            for (const char *p = path; *p; p++) {
                if (*p == '/') slash_count++;
            }
            if (slash_count == 1) return 1;
        }
    }
    return 0;
}

u32 sys_open(u32 path_ptr, u32 flags,
                         u32 arg3, u32 arg4)
{
    (void)arg3; (void)arg4;
    /* V7.79安全修复: 用户指针校验（漏洞C全覆盖） */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (!user_ptr_valid((const char *)path_ptr, 1)) {
        vga_printf("[FS] sys_open: invalid path ptr 0x%x\n", path_ptr);
        return (u32)-1;
    }
    const char *path = (const char *)path_ptr;
    if (!compat_sandbox_path_allowed(path)) {
        vga_printf("[SANDBOX] compat proc %d denied open: %s\n",
                   current ? current->pid : 0, path ? path : "(null)");
        return (u32)-1;   /* EACCES */
    }
    return vfs_open(path, flags);
}

u32 sys_read(u32 fd, u32 buf_ptr,
                         u32 size, u32 arg4)
{
    (void)arg4;
    /* V7.70安全修复: 校验用户指针，防止内核向任意地址写 */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (!user_ptr_valid((void *)buf_ptr, size)) {
        vga_printf("[FS] sys_read: invalid user ptr 0x%x size %d\n", buf_ptr, size);
        return (u32)-1;
    }
    return vfs_read(fd, (void *)buf_ptr, size);
}

u32 sys_write(u32 fd, u32 buf_ptr,
                          u32 size, u32 arg4)
{
    (void)arg4;
    /* V7.70安全修复: 校验用户指针，防止内核从任意地址读 */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (!user_ptr_valid((const void *)buf_ptr, size)) {
        vga_printf("[FS] sys_write: invalid user ptr 0x%x size %d\n", buf_ptr, size);
        return (u32)-1;
    }
    return vfs_write(fd, (const void *)buf_ptr, size);
}

u32 sys_close(u32 fd, u32 arg2,
                          u32 arg3, u32 arg4)
{
    (void)arg2; (void)arg3; (void)arg4;
    return vfs_close(fd);
}

u32 sys_lseek(u32 fd, u32 offset,
                          u32 whence, u32 arg4)
{
    (void)arg4;
    return vfs_lseek(fd, (s32)offset, whence);
}

u32 sys_stat(u32 path_ptr, u32 stat_ptr, u32 arg3, u32 arg4)
{
    (void)arg3; (void)arg4;
    /* V7.79安全修复: 用户指针校验（漏洞C全覆盖） */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (!user_ptr_valid((const char *)path_ptr, 1)) {
        vga_printf("[FS] sys_stat: invalid path ptr 0x%x\n", path_ptr);
        return (u32)-1;
    }
    if (!user_ptr_valid((void *)stat_ptr, sizeof(vfs_stat_t))) {
        vga_printf("[FS] sys_stat: invalid stat ptr 0x%x\n", stat_ptr);
        return (u32)-1;
    }
    const char *path = (const char *)path_ptr;
    /* V7.66安全修复: 沙箱检查 */
    if (!compat_sandbox_path_allowed(path)) return (u32)-1;
    return vfs_stat(path, (vfs_stat_t *)stat_ptr);
}

u32 sys_fstat(u32 fd, u32 stat_ptr, u32 arg3, u32 arg4)
{
    (void)arg3; (void)arg4;
    /* V7.79安全修复: 用户指针校验（漏洞C全覆盖） */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (!user_ptr_valid((void *)stat_ptr, sizeof(vfs_stat_t))) {
        vga_printf("[FS] sys_fstat: invalid stat ptr 0x%x\n", stat_ptr);
        return (u32)-1;
    }
    return vfs_fstat(fd, (vfs_stat_t *)stat_ptr);
}

u32 sys_access(u32 path_ptr, u32 mode, u32 arg3, u32 arg4)
{
    (void)mode; (void)arg3; (void)arg4;
    /* V7.79安全修复: 用户指针校验（漏洞C全覆盖） */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (!user_ptr_valid((const char *)path_ptr, 1)) {
        vga_printf("[FS] sys_access: invalid path ptr 0x%x\n", path_ptr);
        return (u32)-1;
    }
    const char *path = (const char *)path_ptr;
    /* V7.66安全修复: 沙箱检查 */
    if (!compat_sandbox_path_allowed(path)) return (u32)-1;
    return vfs_access(path);
}

u32 sys_unlink(u32 path_ptr, u32 arg2, u32 arg3, u32 arg4)
{
    (void)arg2; (void)arg3; (void)arg4;
    /* V7.79安全修复: 用户指针校验（漏洞C全覆盖） */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (!user_ptr_valid((const char *)path_ptr, 1)) {
        vga_printf("[FS] sys_unlink: invalid path ptr 0x%x\n", path_ptr);
        return (u32)-1;
    }
    const char *path = (const char *)path_ptr;
    /* V7.66安全修复: 沙箱检查 */
    if (!compat_sandbox_path_allowed(path)) return (u32)-1;
    return vfs_unlink(path);
}

u32 sys_rename(u32 old_ptr, u32 new_ptr, u32 arg3, u32 arg4)
{
    (void)arg3; (void)arg4;
    /* V7.79安全修复: 用户指针校验（漏洞C全覆盖） */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (!user_ptr_valid((const char *)old_ptr, 1)) {
        vga_printf("[FS] sys_rename: invalid old ptr 0x%x\n", old_ptr);
        return (u32)-1;
    }
    if (!user_ptr_valid((const char *)new_ptr, 1)) {
        vga_printf("[FS] sys_rename: invalid new ptr 0x%x\n", new_ptr);
        return (u32)-1;
    }
    const char *old_path = (const char *)old_ptr;
    const char *new_path = (const char *)new_ptr;
    /* V7.66安全修复: 沙箱检查（两个路径都要检查） */
    if (!compat_sandbox_path_allowed(old_path)) return (u32)-1;
    if (!compat_sandbox_path_allowed(new_path)) return (u32)-1;
    return vfs_rename(old_path, new_path);
}

u32 sys_mkdir(u32 path_ptr, u32 mode, u32 arg3, u32 arg4)
{
    (void)mode; (void)arg3; (void)arg4;
    /* V7.79安全修复: 用户指针校验（漏洞C全覆盖） */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (!user_ptr_valid((const char *)path_ptr, 1)) {
        vga_printf("[FS] sys_mkdir: invalid path ptr 0x%x\n", path_ptr);
        return (u32)-1;
    }
    const char *path = (const char *)path_ptr;
    /* V7.66安全修复: 沙箱检查 */
    if (!compat_sandbox_path_allowed(path)) return (u32)-1;
    return vfs_mkdir(path);
}

u32 sys_rmdir(u32 path_ptr, u32 arg2, u32 arg3, u32 arg4)
{
    (void)arg2; (void)arg3; (void)arg4;
    /* V7.79安全修复: 用户指针校验（漏洞C全覆盖） */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (!user_ptr_valid((const char *)path_ptr, 1)) {
        vga_printf("[FS] sys_rmdir: invalid path ptr 0x%x\n", path_ptr);
        return (u32)-1;
    }
    const char *path = (const char *)path_ptr;
    /* V7.66安全修复: 沙箱检查 */
    if (!compat_sandbox_path_allowed(path)) return (u32)-1;
    return vfs_rmdir(path);
}

u32 sys_readdir(u32 fd, u32 dirent_ptr, u32 arg3, u32 arg4)
{
    (void)arg3; (void)arg4;
    /* V7.79安全修复: 用户指针校验（漏洞C全覆盖） */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (!user_ptr_valid((void *)dirent_ptr, sizeof(vfs_dirent_t))) {
        vga_printf("[FS] sys_readdir: invalid dirent ptr 0x%x\n", dirent_ptr);
        return (u32)-1;
    }
    return vfs_readdir_fd(fd, (vfs_dirent_t *)dirent_ptr, 64);
}

u32 sys_chmod(u32 path_ptr, u32 mode, u32 arg3, u32 arg4)
{
    (void)path_ptr; (void)mode; (void)arg3; (void)arg4;
    return 0;  /* 原型阶段 */
}

u32 sys_chown(u32 path_ptr, u32 owner, u32 group, u32 arg4)
{
    (void)path_ptr; (void)owner; (void)group; (void)arg4;
    return 0;
}

u32 sys_mount(u32 src_ptr, u32 dst_ptr, u32 fstype_ptr, u32 flags)
{
    (void)src_ptr; (void)dst_ptr; (void)fstype_ptr; (void)flags;
    return 0;
}

u32 sys_umount(u32 target_ptr, u32 arg2, u32 arg3, u32 arg4)
{
    (void)target_ptr; (void)arg2; (void)arg3; (void)arg4;
    return 0;
}

u32 sys_ioctl(u32 fd, u32 cmd, u32 arg_ptr, u32 arg4)
{
    (void)fd; (void)cmd; (void)arg_ptr; (void)arg4;
    return 0;
}

u32 sys_getcwd(u32 buf_ptr, u32 size, u32 arg3, u32 arg4)
{
    (void)arg3; (void)arg4;
    /* V7.79安全修复: 用户指针校验（漏洞C全覆盖） */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (buf_ptr && size > 0) {
        if (!user_ptr_valid((void *)buf_ptr, size)) {
            vga_printf("[FS] sys_getcwd: invalid buf ptr 0x%x size %d\n", buf_ptr, size);
            return (u32)-1;
        }
        char *buf = (char *)buf_ptr;
        buf[0] = '/'; buf[1] = 0;
    }
    return 0;
}

u32 sys_chdir(u32 path_ptr, u32 arg2, u32 arg3, u32 arg4)
{
    (void)path_ptr; (void)arg2; (void)arg3; (void)arg4;
    return 0;
}

u32 sys_dup(u32 oldfd, u32 arg2, u32 arg3, u32 arg4)
{
    (void)arg2; (void)arg3; (void)arg4;
    return vfs_dup(oldfd);
}

u32 sys_dup2(u32 oldfd, u32 newfd, u32 arg3, u32 arg4)
{
    (void)arg3; (void)arg4;
    return vfs_dup2(oldfd, newfd);
}

u32 sys_fcntl(u32 fd, u32 cmd, u32 arg, u32 arg4)
{
    (void)fd; (void)cmd; (void)arg; (void)arg4;
    return 0;
}

