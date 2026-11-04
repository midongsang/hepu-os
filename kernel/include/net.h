#ifndef NET_H
#define NET_H

#include <stdint.h>

#define NET_MAC_LEN 6
#define NET_IP_LEN 4
#define NET_MTU 1500
#define NET_MAX_SOCKETS 16
#define NET_TCP_BACKLOG 4

/* 以太网帧头 */
typedef struct {
    uint8_t  dst_mac[NET_MAC_LEN];
    uint8_t  src_mac[NET_MAC_LEN];
    uint16_t type;
} __attribute__((packed)) eth_header_t;

#define ETH_TYPE_IP   0x0800
#define ETH_TYPE_ARP  0x0806

/* ARP包 */
typedef struct {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t opcode;
    uint8_t  sender_mac[NET_MAC_LEN];
    uint8_t  sender_ip[NET_IP_LEN];
    uint8_t  target_mac[NET_MAC_LEN];
    uint8_t  target_ip[NET_IP_LEN];
} __attribute__((packed)) arp_packet_t;

#define ARP_REQUEST 1
#define ARP_REPLY   2

/* IP头 */
typedef struct {
    uint8_t  ver_ihl;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint8_t  src_ip[NET_IP_LEN];
    uint8_t  dst_ip[NET_IP_LEN];
} __attribute__((packed)) ip_header_t;

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

/* ICMP头 */
typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint32_t rest;
} __attribute__((packed)) icmp_header_t;

#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO       8

/* TCP头 */
typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_offset;
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed)) tcp_header_t;

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

/* UDP头 */
typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

/* Socket类型 */
#define SOCK_STREAM 1
#define SOCK_DGRAM  2

/* Socket状态 */
typedef enum {
    SOCK_CLOSED = 0,
    SOCK_LISTEN,
    SOCK_SYN_SENT,
    SOCK_SYN_RECV,
    SOCK_ESTABLISHED,
    SOCK_FIN_WAIT,
    SOCK_CLOSE_WAIT,
} sock_state_t;

/* Socket结构 */
typedef struct {
    int used;
    int type;
    sock_state_t state;
    uint16_t local_port;
    uint16_t remote_port;
    uint8_t  remote_ip[NET_IP_LEN];
    uint8_t  remote_mac[NET_MAC_LEN];
    uint32_t seq_num;
    uint32_t ack_num;
    /* 接收缓冲区 */
    uint8_t  rx_buf[4096];
    int      rx_len;
    int      rx_pos;
} net_socket_t;

/* 网络接口 */
typedef struct {
    uint8_t mac[NET_MAC_LEN];
    uint8_t ip[NET_IP_LEN];
    uint8_t gateway[NET_IP_LEN];
    uint8_t netmask[NET_IP_LEN];
    int up;
} net_iface_t;

/* 初始化网络子系统 */
void net_init(void);

/* 处理收到的数据包 */
void net_rx(uint8_t *data, uint32_t len);

/* 发送数据包 */
int net_tx(uint8_t *data, uint32_t len);

/* ARP */
int net_arp_resolve(uint8_t *ip, uint8_t *mac);
void net_arp_request(uint8_t *ip);

/* ICMP */
void net_icmp_echo_reply(ip_header_t *ip_hdr, uint8_t *data, uint32_t len);

/* Socket API */
int net_socket(int domain, int type, int protocol);
int net_bind(int sockfd, uint16_t port);
int net_listen(int sockfd, int backlog);
int net_accept(int sockfd);
int net_connect(int sockfd, uint8_t *ip, uint16_t port);
int net_send(int sockfd, const void *buf, uint32_t len);
int net_recv(int sockfd, void *buf, uint32_t len);
int net_close(int sockfd);

/* 工具函数 */
uint16_t net_checksum(void *data, uint32_t len);
uint32_t net_ip_to_int(uint8_t *ip);
void net_int_to_ip(uint32_t val, uint8_t *ip);

#endif /* NET_H */
