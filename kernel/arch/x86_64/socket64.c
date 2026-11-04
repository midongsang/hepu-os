/* ============================================================================
 * 赫普OS 64位 - BSD Socket API实现
 * 支持TCP/UDP socket，基于内核TCP/IP协议栈
 * V0.10.104: P0-4 Socket API初始实现
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 外部函数 */
extern void vga_puts(const char *str);
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

/* Socket常量 */
#define AF_INET     2
#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

/* Socket状态 */
typedef enum {
    SOCKET_CLOSED = 0,
    SOCKET_OPEN,
    SOCKET_BOUND,
    SOCKET_LISTENING,
    SOCKET_CONNECTING,
    SOCKET_CONNECTED,
    SOCKET_ERROR
} socket_state_t;

/* Socket结构 */
typedef struct {
    int fd;                 /* 文件描述符 */
    int domain;             /* 地址族（AF_INET） */
    int type;               /* 类型（SOCK_STREAM/SOCK_DGRAM） */
    int protocol;           /* 协议（IPPROTO_TCP/IPPROTO_UDP） */
    socket_state_t state;   /* 状态 */
    uint32_t local_ip;      /* 本地IP */
    uint16_t local_port;    /* 本地端口 */
    uint32_t remote_ip;     /* 远程IP */
    uint16_t remote_port;   /* 远程端口 */
    uint8_t *rx_buffer;     /* 接收缓冲区 */
    uint32_t rx_size;       /* 接收缓冲区大小 */
    uint32_t rx_used;       /* 已使用接收缓冲区 */
    int backlog;            /* 监听backlog */
    int error;              /* 错误码 */
    int tcp_sock;           /* TCP协议栈连接索引 */
} socket_t;

/* 最大Socket数 */
#define MAX_SOCKETS 32
static socket_t *sockets[MAX_SOCKETS];
static int socket_count = 0;
static int next_fd = 100;  /* Socket fd从100开始 */

/* 初始化Socket子系统 */
int socket64_init(void)
{
    vga_puts("[SOCKET64] Initializing BSD Socket API...\n");
    for (int i = 0; i < MAX_SOCKETS; i++) {
        sockets[i] = NULL;
    }
    socket_count = 0;
    next_fd = 100;
    vga_puts("[SOCKET64] Socket API initialized\n");
    return 0;
}

/* 查找空闲Socket槽位 */
static int find_free_slot(void)
{
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (sockets[i] == NULL) {
            return i;
        }
    }
    return -1;
}

/* 查找Socket */
static socket_t *find_socket(int fd)
{
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (sockets[i] && sockets[i]->fd == fd) {
            return sockets[i];
        }
    }
    return NULL;
}

/* socket() - 创建Socket */
int sys_socket(int domain, int type, int protocol)
{
    vga_puts("[SOCKET64] socket(domain=");
    /* 简化输出 */
    if (domain != AF_INET) {
        vga_puts("unsupported)\n");
        return -1;  /* EAFNOSUPPORT */
    }

    if (type != SOCK_STREAM && type != SOCK_DGRAM) {
        vga_puts("bad type)\n");
        return -1;  /* EINVAL */
    }

    int slot = find_free_slot();
    if (slot < 0) {
        vga_puts("no slots)\n");
        return -1;  /* EMFILE */
    }

    socket_t *sock = (socket_t *)kmalloc(sizeof(socket_t));
    if (!sock) {
        return -1;  /* ENOMEM */
    }

    sock->fd = next_fd++;
    sock->domain = domain;
    sock->type = type;
    sock->protocol = (protocol == 0) ?
        (type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP) : protocol;
    sock->state = SOCKET_OPEN;
    sock->local_ip = 0;
    sock->local_port = 0;
    sock->remote_ip = 0;
    sock->remote_port = 0;
    sock->rx_buffer = NULL;
    sock->rx_size = 0;
    sock->rx_used = 0;
    sock->backlog = 0;
    sock->error = 0;
    sock->tcp_sock = -1;

    sockets[slot] = sock;
    socket_count++;

    vga_puts("ok, fd=");
    /* 输出fd（简化） */
    vga_puts(")\n");
    return sock->fd;
}

/* bind() - 绑定地址 */
int sys_bind(int fd, const void *addr, int addrlen)
{
    socket_t *sock = find_socket(fd);
    if (!sock) {
        return -1;  /* EBADF */
    }

    if (sock->state != SOCKET_OPEN) {
        return -1;  /* EINVAL */
    }

    /* 简化：sockaddr_in结构 */
    const uint8_t *addr_bytes = (const uint8_t *)addr;
    if (addrlen >= 8) {
        sock->local_port = (addr_bytes[2] << 8) | addr_bytes[3];
        sock->local_ip = (addr_bytes[4] << 24) | (addr_bytes[5] << 16) |
                         (addr_bytes[6] << 8) | addr_bytes[7];
    }

    sock->state = SOCKET_BOUND;
    return 0;
}

/* listen() - 监听 */
int sys_listen(int fd, int backlog)
{
    socket_t *sock = find_socket(fd);
    if (!sock) {
        return -1;  /* EBADF */
    }

    if (sock->type != SOCK_STREAM) {
        return -1;  /* EOPNOTSUPP */
    }

    if (sock->state != SOCKET_BOUND && sock->state != SOCKET_OPEN) {
        return -1;  /* EINVAL */
    }

    sock->backlog = backlog > 0 ? backlog : 1;
    sock->state = SOCKET_LISTENING;
    return 0;
}

/* accept() - 接受连接（简化版，返回新fd） */
int sys_accept(int fd, void *addr, int *addrlen)
{
    socket_t *sock = find_socket(fd);
    if (!sock) {
        return -1;  /* EBADF */
    }

    if (sock->state != SOCKET_LISTENING) {
        return -1;  /* EINVAL */
    }

    /* 简化：创建新socket表示连接 */
    int slot = find_free_slot();
    if (slot < 0) {
        return -1;  /* EMFILE */
    }

    socket_t *new_sock = (socket_t *)kmalloc(sizeof(socket_t));
    if (!new_sock) {
        return -1;  /* ENOMEM */
    }

    new_sock->fd = next_fd++;
    new_sock->domain = sock->domain;
    new_sock->type = sock->type;
    new_sock->protocol = sock->protocol;
    new_sock->state = SOCKET_CONNECTED;
    new_sock->local_ip = sock->local_ip;
    new_sock->local_port = sock->local_port;
    new_sock->remote_ip = 0;  /* 待填充 */
    new_sock->remote_port = 0;
    new_sock->rx_buffer = NULL;
    new_sock->rx_size = 0;
    new_sock->rx_used = 0;
    new_sock->backlog = 0;
    new_sock->error = 0;

    sockets[slot] = new_sock;
    socket_count++;

    return new_sock->fd;
}

/* connect() - 连接（调用TCP协议栈） */
int sys_connect(int fd, const void *addr, int addrlen)
{
    socket_t *sock = find_socket(fd);
    if (!sock) {
        return -1;  /* EBADF */
    }

    if (sock->state != SOCKET_OPEN && sock->state != SOCKET_BOUND) {
        return -1;  /* EINVAL */
    }

    /* 解析sockaddr_in结构 */
    const uint8_t *addr_bytes = (const uint8_t *)addr;
    if (addrlen >= 8) {
        sock->remote_port = (addr_bytes[2] << 8) | addr_bytes[3];
        sock->remote_ip = (addr_bytes[4] << 24) | (addr_bytes[5] << 16) |
                          (addr_bytes[6] << 8) | addr_bytes[7];
    }

    /* TCP socket：调用TCP协议栈连接 */
    if (sock->type == SOCK_STREAM) {
        extern int tcp64_connect(uint32_t remote_ip, uint16_t remote_port, uint16_t local_port);
        int tcp_fd = tcp64_connect(sock->remote_ip, sock->remote_port, sock->local_port);
        if (tcp_fd < 0) {
            return -1;  /* ECONNREFUSED */
        }
        sock->tcp_sock = tcp_fd;
        sock->state = SOCKET_CONNECTING;
        /* 简化：立即标记为已连接（实际应等待SYN-ACK） */
        sock->state = SOCKET_CONNECTED;
    } else {
        /* UDP socket：无连接，直接标记 */
        sock->state = SOCKET_CONNECTED;
    }

    return 0;
}

/* send() - 发送数据（调用TCP协议栈） */
int sys_send(int fd, const void *buf, size_t len, int flags)
{
    socket_t *sock = find_socket(fd);
    if (!sock) {
        return -1;  /* EBADF */
    }

    if (sock->state != SOCKET_CONNECTED) {
        return -1;  /* ENOTCONN */
    }

    /* TCP socket：调用TCP协议栈发送 */
    if (sock->type == SOCK_STREAM && sock->tcp_sock >= 0) {
        extern int tcp64_send(int sock, const uint8_t *data, int length);
        return tcp64_send(sock->tcp_sock, (const uint8_t *)buf, (int)len);
    }

    /* UDP或未连接TCP：简化返回 */
    return (int)len;
}

/* recv() - 接收数据（调用TCP协议栈） */
int sys_recv(int fd, void *buf, size_t len, int flags)
{
    socket_t *sock = find_socket(fd);
    if (!sock) {
        return -1;  /* EBADF */
    }

    if (sock->state != SOCKET_CONNECTED) {
        return -1;  /* ENOTCONN */
    }

    /* TCP socket：调用TCP协议栈接收 */
    if (sock->type == SOCK_STREAM && sock->tcp_sock >= 0) {
        extern int tcp64_recv(int sock, uint8_t *buffer, int max_len);
        return tcp64_recv(sock->tcp_sock, (uint8_t *)buf, (int)len);
    }

    /* UDP或未连接TCP：简化返回0 */
    return 0;
}

/* socket close */
int socket64_close(int fd)
{
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (sockets[i] && sockets[i]->fd == fd) {
            if (sockets[i]->rx_buffer) {
                kfree(sockets[i]->rx_buffer);
            }
            kfree(sockets[i]);
            sockets[i] = NULL;
            socket_count--;
            return 0;
        }
    }
    return -1;
}

/* 获取Socket统计 */
int socket64_get_count(void)
{
    return socket_count;
}

/* setsockopt() - 设置socket选项 */
int sys_setsockopt(int fd, int level, int optname, const void *optval, int optlen)
{
    socket_t *sock = find_socket(fd);
    if (!sock) {
        return -1;  /* EBADF */
    }
    /* 简化：接受所有选项（实际应存储） */
    return 0;
}

/* getsockopt() - 获取socket选项 */
int sys_getsockopt(int fd, int level, int optname, void *optval, int *optlen)
{
    socket_t *sock = find_socket(fd);
    if (!sock) {
        return -1;  /* EBADF */
    }
    /* 简化：返回0 */
    if (optval && optlen) {
        *(int *)optval = 0;
        *optlen = 4;
    }
    return 0;
}

/* shutdown() - 关闭socket部分连接 */
int sys_shutdown(int fd, int how)
{
    socket_t *sock = find_socket(fd);
    if (!sock) {
        return -1;  /* EBADF */
    }
    /* how: 0=SHUT_RD, 1=SHUT_WR, 2=SHUT_RDWR */
    if (how == 2) {
        sock->state = SOCKET_CLOSED;
    }
    return 0;
}

/* getpeername() - 获取对端地址 */
int sys_getpeername(int fd, void *addr, int *addrlen)
{
    socket_t *sock = find_socket(fd);
    if (!sock) {
        return -1;  /* EBADF */
    }
    if (sock->state != SOCKET_CONNECTED) {
        return -1;  /* ENOTCONN */
    }
    /* 简化：填充sockaddr_in */
    if (addr && addrlen) {
        uint8_t *a = (uint8_t *)addr;
        a[0] = AF_INET;
        a[1] = 0;
        a[2] = (sock->remote_port >> 8) & 0xFF;
        a[3] = sock->remote_port & 0xFF;
        a[4] = (sock->remote_ip >> 24) & 0xFF;
        a[5] = (sock->remote_ip >> 16) & 0xFF;
        a[6] = (sock->remote_ip >> 8) & 0xFF;
        a[7] = sock->remote_ip & 0xFF;
        *addrlen = 8;
    }
    return 0;
}

/* getsockname() - 获取本地地址 */
int sys_getsockname(int fd, void *addr, int *addrlen)
{
    socket_t *sock = find_socket(fd);
    if (!sock) {
        return -1;  /* EBADF */
    }
    /* 简化：填充sockaddr_in */
    if (addr && addrlen) {
        uint8_t *a = (uint8_t *)addr;
        a[0] = AF_INET;
        a[1] = 0;
        a[2] = (sock->local_port >> 8) & 0xFF;
        a[3] = sock->local_port & 0xFF;
        a[4] = (sock->local_ip >> 24) & 0xFF;
        a[5] = (sock->local_ip >> 16) & 0xFF;
        a[6] = (sock->local_ip >> 8) & 0xFF;
        a[7] = sock->local_ip & 0xFF;
        *addrlen = 8;
    }
    return 0;
}
