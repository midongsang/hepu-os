/* ============================================================================
 * 赫普OS - Darwin (macOS) i386 syscall ABI 翻译层
 * macOS 用户态 int 0x80 调用号 = Mach trap 号 (UNIX 号 + 0x2000000 基址,
 * 来源: XNU 公开 syscalls.master) → HEPU syscall 号 (kernel/include/syscall.h)
 * 阶段 C 配套: Mach-O 兼容进程 (compat_abi=3) 的 syscall 翻译入口.
 * ========================================================================== */

#include "darwin_abi.h"
#include "syscall.h"

/* 一对一的 syscall 号映射表.
 * 结构: { darwin_nr, hepu_nr }
 * 表小, 线性扫描即可 (不依赖排序). */
typedef struct {
    uint32_t darwin_nr;
    uint32_t hepu_nr;
} darwin_abi_map_t;

static const darwin_abi_map_t abi_map[] = {
    { DARWIN_SYS_exit,    SYS_EXIT },
    { DARWIN_SYS_fork,    SYS_FORK },
    { DARWIN_SYS_read,    SYS_READ },
    { DARWIN_SYS_write,   SYS_WRITE },
    { DARWIN_SYS_open,    SYS_OPEN },
    { DARWIN_SYS_close,   SYS_CLOSE },
    { DARWIN_SYS_wait4,   SYS_WAITPID },
    { DARWIN_SYS_unlink,  SYS_UNLINK },
    { DARWIN_SYS_chdir,   SYS_CHDIR },
    { DARWIN_SYS_chmod,   SYS_CHMOD },
    { DARWIN_SYS_getpid,  SYS_GETPID },
    { DARWIN_SYS_getuid,  SYS_GETUID },
    { DARWIN_SYS_access,  SYS_ACCESS },
    { DARWIN_SYS_kill,    SYS_KILL },
    { DARWIN_SYS_getgid,  SYS_GETGID },
    { DARWIN_SYS_munmap,  SYS_MUNMAP },
    { DARWIN_SYS_mmap,    SYS_MMAP },
};

uint32_t darwin_abi_translate(uint32_t darwin_nr)
{
    for (uint32_t i = 0; i < sizeof(abi_map) / sizeof(abi_map[0]); i++) {
        if (abi_map[i].darwin_nr == darwin_nr) {
            return abi_map[i].hepu_nr;
        }
    }
    return DARWIN_ABI_UNSUPPORTED;
}
