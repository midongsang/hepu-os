/* ============================================================================
 * 赫普OS - sys/socket.h 头文件
 * 套接字相关定义和函数声明
 * V8.20: 初始实现
 * ========================================================================== */

#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include <sys/types.h>

/* 套接字地址族 */
#define AF_UNSPEC    0       /* 未指定 */
#define AF_UNIX      1       /* Unix域套接字 */
#define AF_INET      2       /* IPv4 */
#define AF_AX25      3       /* AX.25 */
#define AF_IPX       4       /* IPX */
#define AF_APPLETALK 5       /* AppleTalk */
#define AF_NETROM    6       /* NETROM */
#define AF_BRIDGE    7       /* 桥接 */
#define AF_ATMPVC    8       /* ATM PVC */
#define AF_X25       9       /* X.25 */
#define AF_INET6     10      /* IPv6 */
#define AF_ROSE      11      /* ROSE */
#define AF_DECnet    12      /* DECnet */
#define AF_NETBEUI   13      /* NetBEUI */
#define AF_SECURITY  14      /* Security */
#define AF_KEY       15      /* Key */
#define AF_NETLINK   16      /* Netlink */
#define AF_PACKET    17      /* 数据包 */
#define AF_ASH       18      /* Ash */
#define AF_ECONET    19      /* Econet */
#define AF_ATMSVC    20      /* ATM SVC */
#define AF_RDS       21      /* RDS */
#define AF_SNA       22      /* SNA */
#define AF_IRDA      23      /* IrDA */
#define AF_PPPOX     24      /* PPPoX */
#define AF_WANPIPE   25      /* WANpipe */
#define AF_LLC       26      /* LLC */
#define AF_IB        27      /* InfiniBand */
#define AF_MPLS      28      /* MPLS */
#define AF_CAN       29      /* CAN */
#define AF_TIPC      30      /* TIPC */
#define AF_BLUETOOTH 31      /* 蓝牙 */
#define AF_IUCV      32      /* IUCV */
#define AF_RXRPC     33      /* RxRPC */
#define AF_ISDN      34      /* ISDN */
#define AF_PHONET    35      /* Phonet */
#define AF_IEEE802154 36     /* IEEE 802.15.4 */
#define AF_CAIF      37      /* CAIF */
#define AF_ALG       38      /* 算法 */
#define AF_NFC       39      /* NFC */
#define AF_VSOCK     40      /* vsock */
#define AF_KCM       41      /* KCM */
#define AF_QIPCRTR   42      /* QIPCRTR */
#define AF_SMC       43      /* SMC */
#define AF_XDP       44      /* XDP */
#define AF_MCTP      45      /* MCTP */
#define AF_MAX       46      /* 最大值 */

/* 协议族（与地址族相同） */
#define PF_UNSPEC    AF_UNSPEC
#define PF_UNIX      AF_UNIX
#define PF_INET      AF_INET
#define PF_INET6     AF_INET6
#define PF_PACKET    AF_PACKET
#define PF_MAX       AF_MAX

/* 套接字类型 */
#define SOCK_STREAM    1     /* 流式套接字（TCP） */
#define SOCK_DGRAM     2     /* 数据报套接字（UDP） */
#define SOCK_RAW       3     /* 原始套接字 */
#define SOCK_RDM       4     /* 可靠数据报 */
#define SOCK_SEQPACKET 5     /* 序列包 */
#define SOCK_DCCP      6     /* DCCP */
#define SOCK_PACKET    10    /* 数据包 */

/* 套接字标志 */
#define SOCK_CLOEXEC   02000000  /* 执行时关闭 */
#define SOCK_NONBLOCK  04000      /* 非阻塞 */

/* 消息标志 */
#define MSG_OOB        0x0001  /* 带外数据 */
#define MSG_PEEK       0x0002  /* 偷看 */
#define MSG_DONTROUTE  0x0004  /* 不路由 */
#define MSG_TRYHARD    0x0004  /* 尝试 */
#define MSG_CTRUNC     0x0008  /* 控制截断 */
#define MSG_PROBE      0x0010  /* 探测 */
#define MSG_TRUNC      0x0020  /* 截断 */
#define MSG_DONTWAIT   0x0040  /* 不等待 */
#define MSG_EOR        0x0080  /* 记录结束 */
#define MSG_WAITALL    0x0100  /* 等待全部 */
#define MSG_FIN        0x0200  /* 完成 */
#define MSG_SYN        0x0400  /* 同步 */
#define MSG_CONFIRM    0x0800  /* 确认 */
#define MSG_RST        0x1000  /* 重置 */
#define MSG_ERRQUEUE   0x2000  /* 错误队列 */
#define MSG_NOSIGNAL   0x4000  /* 无信号 */
#define MSG_MORE       0x8000  /* 更多 */
#define MSG_WAITFORONE 0x10000 /* 等待一个 */
#define MSG_BATCH      0x40000 /* 批量 */
#define MSG_FASTOPEN   0x20000000 /* 快速打开 */
#define MSG_CMSG_CLOEXEC 0x40000000 /* 控制消息关闭执行 */

/* 套接字选项级别 */
#define SOL_SOCKET     1       /* 套接字级别 */
#define SOL_IP         0       /* IP级别 */
#define SOL_IPV6       41      /* IPv6级别 */
#define SOL_TCP        6       /* TCP级别 */
#define SOL_UDP        17      /* UDP级别 */

/* 套接字选项 */
#define SO_DEBUG        0x0001  /* 调试 */
#define SO_REUSEADDR    0x0002  /* 重用地址 */
#define SO_TYPE         0x0003  /* 类型 */
#define SO_ERROR        0x0004  /* 错误 */
#define SO_DONTROUTE    0x0005  /* 不路由 */
#define SO_BROADCAST    0x0006  /* 广播 */
#define SO_SNDBUF       0x0007  /* 发送缓冲区 */
#define SO_RCVBUF       0x0008  /* 接收缓冲区 */
#define SO_SNDBUFFORCE  0x001F  /* 强制发送缓冲区 */
#define SO_RCVBUFFORCE  0x001D  /* 强制接收缓冲区 */
#define SO_KEEPALIVE    0x0009  /* 保持连接 */
#define SO_OOBINLINE    0x000A  /* 带外内联 */
#define SO_NO_CHECK     0x000B  /* 无校验 */
#define SO_PRIORITY     0x000C  /* 优先级 */
#define SO_LINGER       0x000D  /* 拖延 */
#define SO_BSDCOMPAT    0x000E  /* BSD兼容 */
#define SO_REUSEPORT    0x000F  /* 重用端口 */
#define SO_PASSCRED     0x0010  /* 传递凭证 */
#define SO_PEERCRED     0x0011  /* 对等凭证 */
#define SO_RCVLOWAT     0x0012  /* 接收低水位 */
#define SO_SNDLOWAT     0x0013  /* 发送低水位 */
#define SO_RCVTIMEO     0x0014  /* 接收超时 */
#define SO_SNDTIMEO     0x0015  /* 发送超时 */
#define SO_ACCEPTCONN   0x0016  /* 接受连接 */
#define SO_BINDTODEVICE 0x0019  /* 绑定到设备 */
#define SO_ATTACH_FILTER 0x001A /* 附加过滤器 */
#define SO_DETACH_FILTER 0x001B /* 分离过滤器 */
#define SO_PEERNAME     0x001C  /* 对等名称 */
#define SO_TIMESTAMP    0x001D  /* 时间戳 */
#define SO_NOSIGPIPE    0x0800  /* 无SIGPIPE */

/* TCP选项 */
#define TCP_NODELAY     0x0001  /* 无延迟 */
#define TCP_MAXSEG      0x0002  /* 最大段大小 */
#define TCP_CORK        0x0003  /* 阻塞 */
#define TCP_KEEPIDLE    0x0004  /* 保持空闲 */
#define TCP_KEEPINTVL   0x0005  /* 保持间隔 */
#define TCP_KEEPCNT     0x0006  /* 保持计数 */
#define TCP_SYNCNT      0x0007  /* SYN计数 */
#define TCP_LINGER2     0x0008  /* 拖延2 */
#define TCP_DEFER_ACCEPT 0x0009 /* 延迟接受 */
#define TCP_WINDOW_CLAMP 0x000A /* 窗口钳制 */
#define TCP_INFO        0x000B  /* 信息 */
#define TCP_QUICKACK    0x000C  /* 快速ACK */
#define TCP_CONGESTION  0x000D  /* 拥塞控制 */
#define TCP_MD5SIG      0x000E  /* MD5签名 */
#define TCP_COOKIE_TRANSACTIONS 0x000F /* Cookie事务 */
#define TCP_THIN_LINEAR_TIMEOUTS 0x0010 /* 细线性超时 */
#define TCP_THIN_DUPACK 0x0011  /* 细重复ACK */
#define TCP_USER_TIMEOUT 0x0012 /* 用户超时 */
#define TCP_REPAIR       0x0013 /* 修复 */
#define TCP_REPAIR_QUEUE 0x0014 /* 修复队列 */
#define TCP_QUEUE_SEQ    0x0015 /* 队列序号 */
#define TCP_PROBE        0x0016 /* 探测 */

/* IP选项 */
#define IP_OPTIONS      0x0001  /* 选项 */
#define IP_HDRINCL      0x0002  /* 包含头部 */
#define IP_TOS          0x0003  /* 服务类型 */
#define IP_TTL          0x0004  /* 生存时间 */
#define IP_RECVOPTS     0x0005  /* 接收选项 */
#define IP_RECVRETOPTS  0x0006  /* 接收回复选项 */
#define IP_RECVERR      0x0007  /* 接收错误 */
#define IP_RECVTTL      0x0008  /* 接收TTL */
#define IP_RECVTOS      0x0009  /* 接收TOS */
#define IP_MULTICAST_IF 0x000A  /* 多播接口 */
#define IP_MULTICAST_TTL 0x000B /* 多播TTL */
#define IP_MULTICAST_LOOP 0x000C /* 多播回环 */
#define IP_ADD_MEMBERSHIP 0x000D /* 添加成员 */
#define IP_DROP_MEMBERSHIP 0x000E /* 删除成员 */
#define IP_UNBLOCK_SOURCE 0x000F /* 解除源阻塞 */
#define IP_BLOCK_SOURCE 0x0010  /* 阻塞源 */
#define IP_ADD_SOURCE_MEMBERSHIP 0x0011 /* 添加源成员 */
#define IP_DROP_SOURCE_MEMBERSHIP 0x0012 /* 删除源成员 */
#define IP_MSFILTER     0x0013  /* 多播源过滤器 */
#define IP_MULTICAST_ALL 0x0014 /* 多播全部 */
#define IP_UNICAST_IF   0x0015  /* 单播接口 */

/* 套接字地址结构（通用） */
struct sockaddr {
    sa_family_t sa_family;  /* 地址族 */
    char        sa_data[14]; /* 地址数据 */
};

/* IPv4套接字地址结构 */
struct sockaddr_in {
    sa_family_t    sin_family;  /* 地址族（AF_INET） */
    in_port_t      sin_port;    /* 端口号（网络字节序） */
    struct in_addr sin_addr;    /* IPv4地址 */
    unsigned char  sin_zero[8]; /* 填充（与struct sockaddr对齐） */
};

/* IPv4地址结构 */
struct in_addr {
    in_addr_t s_addr;  /* IPv4地址（网络字节序） */
};

/* 套接字地址存储结构（足够大以容纳任何地址族） */
struct sockaddr_storage {
    sa_family_t ss_family;       /* 地址族 */
    unsigned long __ss_align;    /* 对齐 */
    char __ss_padding[128 - sizeof(sa_family_t) - sizeof(unsigned long)]; /* 填充 */
};

/* 消息头结构（用于recvmsg/sendmsg） */
struct iovec {
    void *iov_base;    /* 缓冲区基地址 */
    size_t iov_len;    /* 缓冲区长度 */
};

struct msghdr {
    void         *msg_name;       /* 地址 */
    socklen_t     msg_namelen;    /* 地址长度 */
    struct iovec *msg_iov;        /* 数据块数组 */
    size_t        msg_iovlen;     /* 数据块数量 */
    void         *msg_control;    /* 辅助数据 */
    size_t        msg_controllen; /* 辅助数据长度 */
    int           msg_flags;      /* 消息标志 */
};

/* 控制消息结构 */
struct cmsghdr {
    socklen_t cmsg_len;    /* 消息长度（包含头部） */
    int       cmsg_level;  /* 协议级别 */
    int       cmsg_type;   /* 协议类型 */
};

/* 拖延结构 */
struct linger {
    int l_onoff;   /* 开启拖延 */
    int l_linger;  /* 拖延时间（秒） */
};

/* 函数声明 */
int socket(int domain, int type, int protocol);
int socketpair(int domain, int type, int protocol, int sv[2]);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags);
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
                const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                  struct sockaddr *src_addr, socklen_t *addrlen);
ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags);
ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags);
int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int shutdown(int sockfd, int how);

/* 关闭方向 */
#define SHUT_RD   0  /* 关闭读 */
#define SHUT_WR   1  /* 关闭写 */
#define SHUT_RDWR 2  /* 关闭读写 */

/* 字节序转换函数 */
uint32_t htonl(uint32_t hostlong);
uint16_t htons(uint16_t hostshort);
uint32_t ntohl(uint32_t netlong);
uint16_t ntohs(uint16_t netshort);

/* 地址转换函数 */
in_addr_t inet_addr(const char *cp);
int inet_aton(const char *cp, struct in_addr *inp);
char *inet_ntoa(struct in_addr in);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);
int inet_pton(int af, const char *src, void *dst);

#endif /* _SYS_SOCKET_H */
