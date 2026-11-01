/* ============================================================================
 * 赫普OS - 网络套接字子系统
 * 基于loopback的TCP/UDP原型实现
 * ========================================================================== */

#include <stdint.h>
#include "socket.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"

static socket_t sockets[MAX_SOCKETS];
static int socket_initialized = 0;
static uint16_t next_port = 1024;

void socket_init(void)
{
    memset(sockets, 0, sizeof(sockets));
    socket_initialized = 1;
    vga_puts("[SOCKET] net socket subsystem init(loopback TCP/UDP)\n");
}

static int find_free_socket(void)
{
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!sockets[i].used) return i;
    }
    return -1;
}

int sys_socket_impl(int domain, int type, int protocol)
{
    if (!socket_initialized) return -1;
    if (domain != AF_INET && domain != AF_UNIX) return -1;
    if (type != SOCK_STREAM && type != SOCK_DGRAM) return -1;

    int fd = find_free_socket();
    if (fd < 0) return -1;

    memset(&sockets[fd], 0, sizeof(socket_t));
    sockets[fd].used = 1;
    sockets[fd].domain = domain;
    sockets[fd].type = type;
    sockets[fd].protocol = protocol;
    sockets[fd].state = SS_CREATED;

    vga_printf("[SOCKET] create: fd=%d type=%s\n", fd,
               type == SOCK_STREAM ? "TCP" : "UDP");
    return fd;
}

int sys_bind_impl(int fd, sockaddr_in_t *addr)
{
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].used) return -1;
    if (!addr) return -1;

    /* V7.88安全修复: 用户指针校验（漏洞C家族）
     * sys_socketcall直接调用_impl函数，绕过了sys_bind包装函数的校验 */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (!user_ptr_valid((const void *)addr, sizeof(sockaddr_in_t))) {
        vga_printf("[SOCKET] bind: invalid addr ptr 0x%x\n", (uint32_t)addr);
        return -1;
    }

    sockets[fd].local_addr = *addr;
    sockets[fd].state = SS_BOUND;

    vga_printf("[SOCKET] bind: fd=%d port=%d\n", fd, addr->port);
    return 0;
}

int sys_listen_impl(int fd, int backlog)
{
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].used) return -1;
    if (sockets[fd].state != SS_BOUND) return -1;

    sockets[fd].state = SS_LISTENING;
    sockets[fd].backlog = backlog;

    vga_printf("[SOCKET] listen: fd=%d backlog=%d\n", fd, backlog);
    return 0;
}

int sys_accept_impl(int fd, sockaddr_in_t *addr)
{
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].used) return -1;
    if (sockets[fd].state != SS_LISTENING) return -1;

    /* V7.88安全修复: 用户指针校验（漏洞C家族）
     * 如果addr非NULL，必须校验用户指针有效性 */
    if (addr) {
        extern int user_ptr_valid(const void *ptr, uint32_t size);
        if (!user_ptr_valid((void *)addr, sizeof(sockaddr_in_t))) {
            vga_printf("[SOCKET] accept: invalid addr ptr 0x%x\n", (uint32_t)addr);
            return -1;
        }
    }

    /* V6.69: 查找connect时为监听socket创建的服务器端连接socket */
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (sockets[i].used &&
            sockets[i].state == SS_CONNECTED &&
            sockets[i].is_server_side &&
            sockets[i].listen_fd == fd) {
            /* 找到服务器端连接socket */
            sockets[i].state = SS_ACCEPTED;
            if (addr) {
                *addr = sockets[i].remote_addr;
            }
            vga_printf("[SOCKET] accept: fd=%d -> connfd=%d (server-side, peer=%d)\n",
                       fd, i, sockets[i].peer_fd);
            return i;
        }
    }

    /* 没有已连接的对端，创建新socket（兼容旧行为） */
    int new_fd = find_free_socket();
    if (new_fd < 0) return -1;

    memset(&sockets[new_fd], 0, sizeof(socket_t));
    sockets[new_fd].used = 1;
    sockets[new_fd].domain = sockets[fd].domain;
    sockets[new_fd].type = sockets[fd].type;
    sockets[new_fd].protocol = sockets[fd].protocol;
    sockets[new_fd].state = SS_ACCEPTED;
    sockets[new_fd].local_addr = sockets[fd].local_addr;
    sockets[new_fd].peer_fd = -1;
    sockets[new_fd].is_server_side = 1;
    sockets[new_fd].listen_fd = fd;

    if (addr) {
        addr->family = AF_INET;
        addr->port = next_port++;
        addr->addr = 0x0100007F;  /* 127.0.0.1 */
        sockets[new_fd].remote_addr = *addr;
    }

    vga_printf("[SOCKET] accept: fd=%d -> newfd=%d\n", fd, new_fd);
    return new_fd;
}

int sys_connect_impl(int fd, sockaddr_in_t *addr)
{
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].used) return -1;
    if (!addr) return -1;

    /* V7.88安全修复: 用户指针校验（漏洞C家族）
     * sys_socketcall直接调用_impl函数，绕过了sys_connect包装函数的校验 */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (!user_ptr_valid((const void *)addr, sizeof(sockaddr_in_t))) {
        vga_printf("[SOCKET] connect: invalid addr ptr 0x%x\n", (uint32_t)addr);
        return -1;
    }

    /* V6.69: loopback - 查找监听相同端口的socket */
    int listen_fd = -1;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (sockets[i].used &&
            sockets[i].state == SS_LISTENING &&
            sockets[i].local_addr.port == addr->port) {
            listen_fd = i;
            break;
        }
    }

    sockets[fd].remote_addr = *addr;
    sockets[fd].state = SS_CONNECTED;
    sockets[fd].is_server_side = 0;  /* 客户端 */
    sockets[fd].listen_fd = -1;

    if (listen_fd >= 0) {
        /* 创建服务器端连接socket */
        int server_fd = find_free_socket();
        if (server_fd >= 0) {
            memset(&sockets[server_fd], 0, sizeof(socket_t));
            sockets[server_fd].used = 1;
            sockets[server_fd].domain = sockets[fd].domain;
            sockets[server_fd].type = sockets[fd].type;
            sockets[server_fd].protocol = sockets[fd].protocol;
            sockets[server_fd].state = SS_CONNECTED;
            sockets[server_fd].local_addr = sockets[listen_fd].local_addr;
            sockets[server_fd].remote_addr = sockets[fd].local_addr;
            sockets[server_fd].peer_fd = fd;  /* 对端是客户端 */
            sockets[server_fd].is_server_side = 1;
            sockets[server_fd].listen_fd = listen_fd;  /* 属于监听socket */
            /* 客户端的对端是服务器端连接socket */
            sockets[fd].peer_fd = server_fd;
            vga_printf("[SOCKET] connect: client_fd=%d -> server_fd=%d (listen_fd=%d)\n",
                       fd, server_fd, listen_fd);
        } else {
            vga_printf("[SOCKET] connect: fd=%d -> no free server socket\n", fd);
        }
    } else {
        vga_printf("[SOCKET] connect: fd=%d -> %d.%d.%d.%d:%d (no listener)\n", fd,
                   (addr->addr >> 0) & 0xFF, (addr->addr >> 8) & 0xFF,
                   (addr->addr >> 16) & 0xFF, (addr->addr >> 24) & 0xFF,
                   addr->port);
    }
    return 0;
}

int sys_send_impl(int fd, const void *buf, uint32_t len, int flags)
{
    (void)flags;
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].used) return -1;
    if (sockets[fd].state != SS_CONNECTED &&
        sockets[fd].state != SS_ACCEPTED) return -1;
    if (!buf || len == 0) return 0;

    /* V7.88安全修复: 用户指针校验（漏洞C家族）
     * sys_socketcall直接调用_impl函数，绕过了sys_send包装函数的校验 */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (!user_ptr_valid(buf, len)) {
        vga_printf("[SOCKET] send: invalid buf ptr 0x%x len %d\n", (uint32_t)buf, len);
        return -1;
    }

    /* loopback: 如果有对端，写入对端接收缓冲区 */
    socket_t *s = &sockets[fd];
    if (s->peer_fd >= 0 && s->peer_fd < MAX_SOCKETS && sockets[s->peer_fd].used) {
        socket_t *peer = &sockets[s->peer_fd];
        uint32_t space = SOCKET_BUF_SIZE - peer->recv_len;
        uint32_t copy = len < space ? len : space;
        memcpy(peer->recv_buf + peer->recv_len, buf, copy);
        peer->recv_len += copy;
        return copy;
    }

    /* 无对端：丢弃数据（模拟发送成功） */
    return len;
}

int sys_recv_impl(int fd, void *buf, uint32_t len, int flags)
{
    (void)flags;
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].used) return -1;
    if (!buf || len == 0) return 0;

    /* V7.88安全修复: 用户指针校验（漏洞C家族）
     * sys_socketcall直接调用_impl函数，绕过了sys_recv包装函数的校验 */
    extern int user_ptr_valid(const void *ptr, uint32_t size);
    if (!user_ptr_valid(buf, len)) {
        vga_printf("[SOCKET] recv: invalid buf ptr 0x%x len %d\n", (uint32_t)buf, len);
        return -1;
    }

    socket_t *s = &sockets[fd];
    if (s->recv_len == 0) return 0;  /* 无数据 */

    uint32_t available = s->recv_len - s->recv_pos;
    uint32_t copy = len < available ? len : available;
    memcpy(buf, s->recv_buf + s->recv_pos, copy);
    s->recv_pos += copy;

    /* 缓冲区读完后重置 */
    if (s->recv_pos >= s->recv_len) {
        s->recv_len = 0;
        s->recv_pos = 0;
    }

    return copy;
}

int sys_close_socket(int fd)
{
    if (fd < 0 || fd >= MAX_SOCKETS || !sockets[fd].used) return -1;
    sockets[fd].used = 0;
    sockets[fd].state = SS_CLOSED;
    vga_printf("[SOCKET] close: fd=%d\n", fd);
    return 0;
}
