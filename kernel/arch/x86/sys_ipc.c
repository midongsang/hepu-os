/* ============================================================================
 * 赫普OS - 系统调用：IPC
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
extern const char *hepu_kernel_version;

u32 sys_ipc_send(u32 target_pid, u32 type,
                             u32 data_ptr, u32 length)
{
    /* V7.77安全修复: 用户指针校验（漏洞C家族） */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (length > 0 && !user_ptr_valid((const void *)data_ptr, length)) {
        vga_printf("[IPC] sys_ipc_send: invalid user ptr 0x%x\n", data_ptr);
        return (u32)-1;
    }
    return ipc_send(target_pid, type, (void *)data_ptr, length);
}

u32 sys_ipc_recv(u32 msg_ptr, u32 arg2,
                             u32 arg3, u32 arg4)
{
    (void)arg2; (void)arg3; (void)arg4;
    /* V7.77安全修复: 用户指针校验（漏洞C家族） */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (!user_ptr_valid((void *)msg_ptr, sizeof(ipc_message_t))) {
        vga_printf("[IPC] sys_ipc_recv: invalid user ptr 0x%x\n", msg_ptr);
        return (u32)-1;
    }
    return ipc_recv(sched_current_pid(), (ipc_message_t *)msg_ptr);
}

u32 sys_pipe(u32 fds_ptr, u32 arg2,
                         u32 arg3, u32 arg4)
{
    (void)arg2; (void)arg3; (void)arg4;
    /* V7.77安全修复: 用户指针校验（漏洞Y）
     * 原代码fds_ptr可指向任意地址，直接写8字节，
     * 配合漏洞A可改写syscall_table/IDT/proc_table。 */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (!user_ptr_valid((void *)fds_ptr, 2 * sizeof(int))) {
        vga_printf("[IPC] sys_pipe: invalid user ptr 0x%x\n", fds_ptr);
        return (u32)-1;
    }
    int fds[2];
    int ret = ipc_pipe(&fds[0], &fds[1]);
    if (ret == 0) {
        int *user_fds = (int *)fds_ptr;
        user_fds[0] = fds[0];
        user_fds[1] = fds[1];
    }
    return ret;
}

u32 sys_poll(u32 fds_ptr, u32 nfds, u32 timeout, u32 arg4)
{
    (void)fds_ptr; (void)nfds; (void)timeout; (void)arg4;
    return 0;
}

u32 sys_select(u32 nfds, u32 readfds, u32 writefds, u32 exceptfds)
{
    (void)nfds; (void)readfds; (void)writefds; (void)exceptfds;
    return 0;
}

