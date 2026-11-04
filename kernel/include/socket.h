#ifndef SOCKET_H
#define SOCKET_H

#include <stdint.h>

#define MAX_SOCKETS 32
#define SOCKET_BUF_SIZE 4096

/* 地址族 */
#define AF_INET  2
#define AF_UNIX  1

/* 套接字类型 */
#define SOCK_STREAM 1  /* TCP */
#define SOCK_DGRAM  2  /* UDP */

/* 协议 */
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

/* 套接字状态 */
typedef enum {
    SS_CLOSED = 0,
    SS_CREATED,
    SS_BOUND,
    SS_LISTENING,
    SS_CONNECTED,
    SS_ACCEPTED
} sk_state_t;

/* IPv4地址 */
typedef struct {
    uint16_t family;
    uint16_t port;
    uint32_t addr;
} sockaddr_in_t;

/* 套接字结构 */
typedef struct {
    int used;
    int domain;
    int type;
    int protocol;
    sk_state_t state;
    sockaddr_in_t local_addr;
    sockaddr_in_t remote_addr;
    uint8_t recv_buf[SOCKET_BUF_SIZE];
    uint32_t recv_len;
    uint32_t recv_pos;
    int backlog;
    int peer_fd;  /* 对端socket（loopback） */
    int is_server_side;  /* V6.69: 是否为服务器端连接socket */
    int listen_fd;  /* V6.69: 服务器端连接socket所属的监听socket */
} socket_t;

/* API */
void socket_init(void);
int sys_socket_impl(int domain, int type, int protocol);
int sys_bind_impl(int fd, sockaddr_in_t *addr);
int sys_listen_impl(int fd, int backlog);
int sys_accept_impl(int fd, sockaddr_in_t *addr);
int sys_connect_impl(int fd, sockaddr_in_t *addr);
int sys_send_impl(int fd, const void *buf, uint32_t len, int flags);
int sys_recv_impl(int fd, void *buf, uint32_t len, int flags);
int sys_close_socket(int fd);

#endif /* SOCKET_H */
