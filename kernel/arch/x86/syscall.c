/* ============================================================================
 * 赫普OS - 系统调用接口 (int 0x80)
 * 用户态通过 eax=调用号, ebx/ecx/edx/esi=参数 触发系统调用
 * ========================================================================== */

#include "types.h"
#include <stdint.h>
#include <stddef.h>
#include "syscall.h"
#include "vga.h"
#include "kheap.h"
#include "sched.h"
#include "hepu.h"
#include "fingerprint.h"
#include "vfs.h"
#include "ipc.h"
#include "process.h"
#include "serial.h"

extern const char *hepu_kernel_version;

/* 系统调用函数指针表 */
typedef uint32_t (*syscall_fn_t)(uint32_t, uint32_t, uint32_t, uint32_t);

/* 网络系统调用（在sys_net.c中） */
extern uint32_t sys_socket(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_bind(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_listen(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_accept(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_connect(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_send(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_recv(uint32_t, uint32_t, uint32_t, uint32_t);

/* 赫普专属系统调用 */
extern uint32_t sys_hepu_info(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_hepu_fp_verify(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_print(uint32_t, uint32_t, uint32_t, uint32_t);

/* 进程管理系统调用 */
extern uint32_t sys_exit(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_yield(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_getpid(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_fork(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_clone(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_waitpid(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_execve(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_kill(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_signal(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_brk(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_gfx_init(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_gfx_text(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_gfx_flip(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_gfx_demo(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_gfx_3d_demo(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_wm_demo(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_game_snake(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_game_tetris(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_game_minesweeper(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_service_register(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_service_lookup(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_socketcall(uint32_t, uint32_t, uint32_t, uint32_t);  /* V6.69 */

/* 文件系统系统调用 */
extern uint32_t sys_open(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_read(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_write(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_close(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_lseek(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_stat(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_fstat(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_access(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_unlink(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_rename(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_mkdir(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_rmdir(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_readdir(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_chmod(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_chown(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_mount(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_umount(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_ioctl(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_getcwd(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_chdir(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_dup(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_dup2(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_fcntl(uint32_t, uint32_t, uint32_t, uint32_t);

/* 内存管理系统调用 */
extern uint32_t sys_malloc(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_free(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_mmap(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_munmap(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_mprotect(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_shmget(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_shmat(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_shmdt(uint32_t, uint32_t, uint32_t, uint32_t);   /* V3.77 */

/* IPC系统调用 */
extern uint32_t sys_ipc_send(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_ipc_recv(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_pipe(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_poll(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_select(uint32_t, uint32_t, uint32_t, uint32_t);

/* 时间系统调用 */
extern uint32_t sys_sleep(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_time(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_gettimeofday(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_settimeofday(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_nanosleep(uint32_t, uint32_t, uint32_t, uint32_t);

/* 用户管理系统调用 */
extern uint32_t sys_getuid(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_geteuid(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_getgid(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_getegid(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_set_password(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_verify_password(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_switch_user(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_create_user(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_remove_user(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_lock_user(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_unlock_user(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_list_users(uint32_t, uint32_t, uint32_t, uint32_t);
extern uint32_t sys_get_current_user(uint32_t, uint32_t, uint32_t, uint32_t);

/* ---- 系统调用分发表 ---- */

/* ---- 文件系统调用 ---- */

/* ---- IPC 系统调用 ---- */

/* === V0.37 新增系统调用实现 === */

/* 时间相关 */
/* 文件状态/操作 */
/* 挂载 */
/* 设备控制 */
/* 内存映射 */
/* 进程管理 */
/* 高精度睡眠 */
/* IO多路复用 */
/* 网络socket */
/* 工作目录 */
/* 文件描述符操作 */
/* 用户/组ID */
/* 系统调用表 */
static syscall_fn_t syscall_table[SYS_MAX] = {
    [SYS_HEPU_INFO]      = sys_hepu_info,
    [SYS_HEPU_FP_VERIFY] = sys_hepu_fp_verify,
    [SYS_PRINT]          = sys_print,
    [SYS_MALLOC]         = sys_malloc,
    [SYS_FREE]           = sys_free,
    [SYS_EXIT]           = sys_exit,
    [SYS_YIELD]          = sys_yield,
    [SYS_GETPID]         = sys_getpid,
    [SYS_OPEN]           = sys_open,
    [SYS_READ]           = sys_read,
    [SYS_WRITE]          = sys_write,
    [SYS_CLOSE]          = sys_close,
    [SYS_IPC_SEND]       = sys_ipc_send,
    [SYS_IPC_RECV]       = sys_ipc_recv,
    [SYS_LSEEK]          = sys_lseek,
    [SYS_SLEEP]          = sys_sleep,
    [SYS_PIPE]           = sys_pipe,
    [SYS_SHMGET]         = sys_shmget,
    [SYS_SHMAT]          = sys_shmat,
    [SYS_SHMDT]          = sys_shmdt,   /* V3.77: 释放共享内存 */
    [SYS_KILL]           = sys_kill,
    [SYS_SIGNAL]         = sys_signal,
    /* V0.37 新增 */
    [SYS_TIME]           = sys_time,
    [SYS_GETTIMEOFDAY]   = sys_gettimeofday,
    [SYS_SETTIMEOFDAY]   = sys_settimeofday,
    [SYS_STAT]           = sys_stat,
    [SYS_FSTAT]          = sys_fstat,
    [SYS_ACCESS]         = sys_access,
    [SYS_UNLINK]         = sys_unlink,
    [SYS_RENAME]         = sys_rename,
    [SYS_MKDIR]          = sys_mkdir,
    [SYS_RMDIR]          = sys_rmdir,
    [SYS_READDIR]        = sys_readdir,
    [SYS_CHMOD]          = sys_chmod,
    [SYS_CHOWN]          = sys_chown,
    [SYS_MOUNT]          = sys_mount,
    [SYS_UMOUNT]         = sys_umount,
    [SYS_IOCTL]          = sys_ioctl,
    [SYS_MMAP]           = sys_mmap,
    [SYS_MUNMAP]         = sys_munmap,
    [SYS_MPROTECT]       = sys_mprotect,
    [SYS_CLONE]          = sys_clone,
    [SYS_WAITPID]        = sys_waitpid,
    [SYS_EXECVE]         = sys_execve,
    [SYS_FORK]           = sys_fork,
    [SYS_NANOSLEEP]      = sys_nanosleep,
    [SYS_POLL]           = sys_poll,
    [SYS_SELECT]         = sys_select,
    [SYS_SOCKET]         = sys_socket,
    [SYS_BIND]           = sys_bind,
    [SYS_LISTEN]         = sys_listen,
    [SYS_ACCEPT]         = sys_accept,
    [SYS_CONNECT]        = sys_connect,
    [SYS_SEND]           = sys_send,
    [SYS_RECV]           = sys_recv,
    [SYS_GETCWD]         = sys_getcwd,
    [SYS_CHDIR]          = sys_chdir,
    [SYS_DUP]            = sys_dup,
    [SYS_DUP2]           = sys_dup2,
    [SYS_FCNTL]          = sys_fcntl,
    [SYS_GETUID]         = sys_getuid,
    [SYS_GETEUID]        = sys_geteuid,
    [SYS_GETGID]         = sys_getgid,
    [SYS_GETEGID]        = sys_getegid,
    [SYS_BRK]            = sys_brk,
    [SYS_GFX_INIT]       = sys_gfx_init,
    [SYS_GFX_TEXT]       = sys_gfx_text,
    [SYS_GFX_FLIP]       = sys_gfx_flip,
    [SYS_GFX_DEMO]       = sys_gfx_demo,
    [SYS_WM_DEMO]        = sys_wm_demo,
    [SYS_GAME_SNAKE]     = sys_game_snake,
    [SYS_GAME_TETRIS]    = sys_game_tetris,
    [SYS_GAME_MINESWEEPER] = sys_game_minesweeper,
    [SYS_SOCKETCALL]     = sys_socketcall,   /* V6.69: Linux socketcall多路复用 */
    [SYS_SERVICE_REGISTER] = sys_service_register,
    [SYS_SERVICE_LOOKUP]   = sys_service_lookup,
    [SYS_SET_PASSWORD]     = sys_set_password,
    [SYS_VERIFY_PASSWORD]  = sys_verify_password,
    [SYS_SWITCH_USER]      = sys_switch_user,
    [SYS_CREATE_USER]      = sys_create_user,
    [SYS_REMOVE_USER]      = sys_remove_user,
    [SYS_LOCK_USER]        = sys_lock_user,
    [SYS_UNLOCK_USER]      = sys_unlock_user,
    [SYS_LIST_USERS]       = sys_list_users,
    [SYS_GET_CURRENT_USER] = sys_get_current_user,
    [SYS_GFX_3D_DEMO]      = sys_gfx_3d_demo,
};

/* 系统调用主处理函数 */
uint32_t syscall_handler(uint32_t num, uint32_t arg1, uint32_t arg2,
                         uint32_t arg3, uint32_t arg4)
{
    /* V3.87: Linux i386 ABI 翻译——兼容进程(运行 Linux ELF)的
     * int 0x80 调用号是 Linux 号(write=4 等), 需翻译为 HEPU 号.
     * 原生 .hug / 内核进程直接用 HEPU 号. */
    extern uint32_t linux_abi_translate(uint32_t);
    if (current && current->compat_abi == 1) {
        uint32_t t = linux_abi_translate(num);
        if (t == 0xFFFFFFFFu) {
            vga_printf("[LINUX-ABI] unsupported syscall %d\n", num);
            return (uint32_t)-1;
        }
        num = t & 0x7FFFFFFFu;
    }

    /* V3.87 阶段C: Darwin (macOS) Mach trap 号翻译——Mach-O 兼容进程
     * (compat_abi=3) 的 int 0x80 调用号是 0x2000000 基址的 Mach trap 号 */
    extern uint32_t darwin_abi_translate(uint32_t);
    if (current && current->compat_abi == 3) {
        uint32_t t = darwin_abi_translate(num);
        if (t == 0xFFFFFFFFu) {
            vga_printf("[DARWIN-ABI] unsupported syscall %d\n", num);
            return (uint32_t)-1;
        }
        num = t & 0x7FFFFFFFu;
    }

    if (num >= SYS_MAX || !syscall_table[num]) {
        vga_printf("[SYSCALL] unknown syscall: %d\n", num);
        return (uint32_t)-1;
    }
    /* V3.81 止血: 表项地址范围校验——内核 .text 在 0x100000-0x1A0000. */
    {
        uint32_t fn = (uint32_t)(uintptr_t)syscall_table[num];
        if (fn < 0x100000u || fn >= 0x1A0000u) {
            vga_printf("[SYSCALL] corrupt table[%d]=0x%x, rejected\n", num, fn);
            return (uint32_t)-1;
        }
    }
    return syscall_table[num](arg1, arg2, arg3, arg4);
}

/* V3.81 诊断: 供启动流程分段检查表完整性, 定位越界写破坏者 */
void syscall_dbg_check(const char *tag)
{
    vga_printf("[SYSCALL] dbg[%s] t0=0x%x t1=0x%x t3=0x%x t4=0x%x\n",
               tag,
               (uint32_t)(uintptr_t)syscall_table[0],
               (uint32_t)(uintptr_t)syscall_table[1],
               (uint32_t)(uintptr_t)syscall_table[3],
               (uint32_t)(uintptr_t)syscall_table[4]);
    {
        extern void serial_puts(uint16_t, const char *);
        extern int serial_printf(const char *, ...);
        /* 串口镜像(简单十六进制输出) */
        extern void serial_puthex(uint16_t, uint32_t);
        serial_puts(0x3F8, "[DBG]");
        serial_puts(0x3F8, tag);
        serial_puts(0x3F8, " t0=");
        serial_puthex(0x3F8, (uint32_t)(uintptr_t)syscall_table[0]);
        serial_puts(0x3F8, " t1=");
        serial_puthex(0x3F8, (uint32_t)(uintptr_t)syscall_table[1]);
        serial_puts(0x3F8, " t3=");
        serial_puthex(0x3F8, (uint32_t)(uintptr_t)syscall_table[3]);
        serial_puts(0x3F8, "\n");
    }
}

void syscall_init(void)
{
    vga_puts("[SYSCALL] syscall iface init(int 0x80)\n");
    vga_printf("[SYSCALL] registered %d syscall \n", SYS_MAX);
    /* V3.81 诊断检查点: 若此处表已坏, 说明破坏发生在 process/ipc/driver 段 */
    vga_printf("[SYSCALL] dbg table[0]=0x%x table[1]=0x%x table[3]=0x%x\n",
               (uint32_t)(uintptr_t)syscall_table[0],
               (uint32_t)(uintptr_t)syscall_table[1],
               (uint32_t)(uintptr_t)syscall_table[3]);
}

