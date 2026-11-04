/* ============================================================================
 * 赫普OS - 网络系统调用（从syscall.c拆分）
 * ========================================================================== */

#include <stdint.h>
#include "types.h"
#include "syscall.h"
#include "socket.h"
#include "vga.h"
#include "string.h"
#include "process.h"

u32 sys_socket(u32 domain, u32 type, u32 protocol, u32 arg4)
{
    (void)domain; (void)type; (void)protocol; (void)arg4;
    extern int sys_socket_impl(int domain, int type, int protocol);
    return (u32)sys_socket_impl(domain, type, protocol);
}

u32 sys_bind(u32 fd, u32 addr_ptr, u32 addrlen, u32 arg4)
{
    (void)fd; (void)addrlen; (void)arg4;
    /* V7.79安全修复: 用户指针校验（漏洞C全覆盖） */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (addr_ptr && !user_ptr_valid((void *)addr_ptr, sizeof(sockaddr_in_t))) {
        vga_printf("[NET] sys_bind: invalid addr ptr 0x%x\n", addr_ptr);
        return (u32)-1;
    }
    extern int sys_bind_impl(int fd, sockaddr_in_t *addr);
    return (u32)sys_bind_impl(fd, (sockaddr_in_t *)addr_ptr);
}

u32 sys_listen(u32 fd, u32 backlog, u32 arg3, u32 arg4)
{
    (void)fd; (void)backlog; (void)arg3; (void)arg4;
    extern int sys_listen_impl(int fd, int backlog);
    return (u32)sys_listen_impl(fd, backlog);
}

u32 sys_accept(u32 fd, u32 addr_ptr, u32 addrlen_ptr, u32 arg4)
{
    (void)fd; (void)addrlen_ptr; (void)arg4;
    /* V7.79安全修复: 用户指针校验（漏洞C全覆盖） */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (addr_ptr && !user_ptr_valid((void *)addr_ptr, sizeof(sockaddr_in_t))) {
        vga_printf("[NET] sys_accept: invalid addr ptr 0x%x\n", addr_ptr);
        return (u32)-1;
    }
    extern int sys_accept_impl(int fd, sockaddr_in_t *addr);
    return (u32)sys_accept_impl(fd, (sockaddr_in_t *)addr_ptr);
}

u32 sys_connect(u32 fd, u32 addr_ptr, u32 addrlen, u32 arg4)
{
    (void)fd; (void)addrlen; (void)arg4;
    /* V7.79安全修复: 用户指针校验（漏洞C全覆盖） */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (addr_ptr && !user_ptr_valid((void *)addr_ptr, sizeof(sockaddr_in_t))) {
        vga_printf("[NET] sys_connect: invalid addr ptr 0x%x\n", addr_ptr);
        return (u32)-1;
    }
    extern int sys_connect_impl(int fd, sockaddr_in_t *addr);
    return (u32)sys_connect_impl(fd, (sockaddr_in_t *)addr_ptr);
}

u32 sys_send(u32 fd, u32 buf_ptr, u32 len, u32 flags)
{
    (void)fd; (void)flags;
    /* V7.78安全修复: 用户指针校验（漏洞C家族） */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (len > 0 && !user_ptr_valid((const void *)buf_ptr, len)) {
        vga_printf("[NET] sys_send: invalid user ptr 0x%x len %d\n", buf_ptr, len);
        return (u32)-1;
    }
    extern int sys_send_impl(int fd, const void *buf, u32 len, int flags);
    return (u32)sys_send_impl(fd, (const void *)buf_ptr, len, flags);
}

u32 sys_recv(u32 fd, u32 buf_ptr, u32 len, u32 flags)
{
    (void)fd; (void)flags;
    /* V7.78安全修复: 用户指针校验（漏洞C家族） */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (len > 0 && !user_ptr_valid((void *)buf_ptr, len)) {
        vga_printf("[NET] sys_recv: invalid user ptr 0x%x len %d\n", buf_ptr, len);
        return (u32)-1;
    }
    extern int sys_recv_impl(int fd, void *buf, u32 len, int flags);
    return (u32)sys_recv_impl(fd, (void *)buf_ptr, len, flags);
}

/* V6.69: Linux socketcall多路复用系统调用
 * int socketcall(int call, unsigned long *args)
 * call: 子调用号 (SYS_SOCKET=1, SYS_BIND=2, SYS_CONNECT=3, SYS_LISTEN=4,
 *               SYS_ACCEPT=5, SYS_SEND=9, SYS_RECV=10)
 * args: 指向参数数组的用户空间指针 */
u32 sys_socketcall(u32 call, u32 args_ptr, u32 arg3, u32 arg4)
{
    (void)arg3; (void)arg4;
    if (!args_ptr) return (u32)-1;

    /* V7.78安全修复: 用户指针校验（漏洞C家族） */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (!user_ptr_valid((const void *)args_ptr, 6 * sizeof(u32))) {
        vga_printf("[NET] sys_socketcall: invalid args ptr 0x%x\n", args_ptr);
        return (u32)-1;
    }

    /* 从用户空间读取参数数组（最多6个参数） */
    u32 args[6];
    for (int i = 0; i < 6; i++) {
        args[i] = ((u32 *)args_ptr)[i];
    }

    extern int sys_socket_impl(int domain, int type, int protocol);
    extern int sys_bind_impl(int fd, sockaddr_in_t *addr);
    extern int sys_listen_impl(int fd, int backlog);
    extern int sys_accept_impl(int fd, sockaddr_in_t *addr);
    extern int sys_connect_impl(int fd, sockaddr_in_t *addr);
    extern int sys_send_impl(int fd, const void *buf, u32 len, int flags);
    extern int sys_recv_impl(int fd, void *buf, u32 len, int flags);

    switch (call) {
    case 1:  /* SYS_SOCKET */
        return (u32)sys_socket_impl((int)args[0], (int)args[1], (int)args[2]);
    case 2:  /* SYS_BIND */
        return (u32)sys_bind_impl((int)args[0], (sockaddr_in_t *)args[1]);
    case 3:  /* SYS_CONNECT */
        return (u32)sys_connect_impl((int)args[0], (sockaddr_in_t *)args[1]);
    case 4:  /* SYS_LISTEN */
        return (u32)sys_listen_impl((int)args[0], (int)args[1]);
    case 5:  /* SYS_ACCEPT */
        return (u32)sys_accept_impl((int)args[0], (sockaddr_in_t *)args[1]);
    case 9:  /* SYS_SEND */
        return (u32)sys_send_impl((int)args[0], (const void *)args[1],
                                  (u32)args[2], (int)args[3]);
    case 10: /* SYS_RECV */
        return (u32)sys_recv_impl((int)args[0], (void *)args[1],
                                  (u32)args[2], (int)args[3]);
    default:
        vga_printf("[SOCKETCALL] unknown call %d\n", call);
        return (u32)-1;
    }
}

