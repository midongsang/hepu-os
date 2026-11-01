/* ============================================================================
 * 赫普OS - 网络协议栈共享（架构无关）
 * 32位和64位共用的网络数据结构、常量和接口
 * V0.10.85: P2-2网络协议栈共享，统一源码树
 * ========================================================================== */

#ifndef SHARED_NET_H
#define SHARED_NET_H

#include <stdint.h>

/* 以太网MAC地址 */
typedef struct {
    uint8_t addr[6];
} mac_addr_t;

/* IPv4地址 */
typedef struct {
    uint8_t addr[4];
} ipv4_addr_t;

/* 以太网帧头 */
typedef struct {
    mac_addr_t  dst_mac;
    mac_addr_t  src_mac;
    uint16_t    ethertype;
} __attribute__((packed)) eth_header_t;

/* ARP数据包 */
typedef struct {
    uint16_t    htype;      /* 硬件类型 */
    uint16_t    ptype;      /* 协议类型 */
    uint8_t     hlen;       /* 硬件地址长度 */
    uint8_t     plen;       /* 协议地址长度 */
    uint16_t    oper;       /* 操作码 */
    mac_addr_t  sha;        /* 发送端MAC */
    ipv4_addr_t spa;        /* 发送端IP */
    mac_addr_t  tha;        /* 目标MAC */
    ipv4_addr_t tpa;        /* 目标IP */
} __attribute__((packed)) arp_packet_t;

/* IPv4头 */
typedef struct {
    uint8_t     version_ihl;    /* 版本(4) + IHL(4) */
    uint8_t     tos;            /* 服务类型 */
    uint16_t    total_length;   /* 总长度 */
    uint16_t    identification; /* 标识 */
    uint16_t    flags_frag;     /* 标志+片偏移 */
    uint8_t     ttl;            /* 生存时间 */
    uint8_t     protocol;       /* 协议 */
    uint16_t    checksum;       /* 校验和 */
    ipv4_addr_t src_ip;         /* 源IP */
    ipv4_addr_t dst_ip;         /* 目标IP */
} __attribute__((packed)) ipv4_header_t;

/* ICMP头 */
typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
} __attribute__((packed)) icmp_header_t;

/* UDP头 */
typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

/* TCP头 */
typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset;   /* 数据偏移(4) + 保留(4) */
    uint8_t  flags;         /* 标志位 */
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed)) tcp_header_t;

/* DHCP头 */
typedef struct {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    ipv4_addr_t ciaddr;
    ipv4_addr_t yiaddr;
    ipv4_addr_t siaddr;
    ipv4_addr_t giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic_cookie;
} __attribute__((packed)) dhcp_header_t;

/* DNS头 */
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed)) dns_header_t;

/* 以太网类型 */
#define ETH_TYPE_IPV4   0x0800
#define ETH_TYPE_ARP    0x0806

/* IP协议号 */
#define IP_PROTO_ICMP   1
#define IP_PROTO_TCP    6
#define IP_PROTO_UDP    17

/* TCP标志位 */
#define TCP_FIN  0x01
#define TCP_SYN  0x02
#define TCP_RST  0x04
#define TCP_PSH  0x08
#define TCP_ACK  0x10
#define TCP_URG  0x20

/* ARP操作码 */
#define ARP_REQUEST 1
#define ARP_REPLY   2

/* 网络状态 */
typedef enum {
    NET_STATE_DOWN = 0,
    NET_STATE_UP,
    NET_STATE_CONFIGURING,
    NET_STATE_CONFIGURED,
    NET_STATE_ERROR
} net_state_t;

/* 网络接口信息 */
typedef struct {
    mac_addr_t  mac;
    ipv4_addr_t ip;
    ipv4_addr_t netmask;
    ipv4_addr_t gateway;
    ipv4_addr_t dns_server;
    net_state_t state;
    uint32_t    rx_packets;
    uint32_t    tx_packets;
    uint32_t    rx_bytes;
    uint32_t    tx_bytes;
} net_interface_t;

/* 工具函数：字节序转换 */
static inline uint16_t net_htons(uint16_t v) {
    return (v >> 8) | (v << 8);
}

static inline uint16_t net_ntohs(uint16_t v) {
    return net_htons(v);
}

static inline uint32_t net_htonl(uint32_t v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
           ((v & 0xFF0000) >> 8) | ((v & 0xFF000000) >> 24);
}

static inline uint32_t net_ntohl(uint32_t v) {
    return net_htonl(v);
}

/* IP地址转字符串 */
static inline void net_ip_to_str(ipv4_addr_t ip, char *buf) {
    int i = 0;
    for (int j = 0; j < 4; j++) {
        uint8_t b = ip.addr[j];
        if (b >= 100) buf[i++] = '0' + b / 100;
        if (b >= 10)  buf[i++] = '0' + (b / 10) % 10;
        buf[i++] = '0' + b % 10;
        if (j < 3) buf[i++] = '.';
    }
    buf[i] = 0;
}

/* MAC地址转字符串 */
static inline void net_mac_to_str(mac_addr_t mac, char *buf) {
    static const char hex[] = "0123456789ABCDEF";
    int i = 0;
    for (int j = 0; j < 6; j++) {
        buf[i++] = hex[mac.addr[j] >> 4];
        buf[i++] = hex[mac.addr[j] & 0xF];
        if (j < 5) buf[i++] = ':';
    }
    buf[i] = 0;
}

/* 计算IP校验和 */
static inline uint16_t net_checksum(const uint8_t *data, int length) {
    uint32_t sum = 0;
    for (int i = 0; i < length - 1; i += 2) {
        sum += (data[i] << 8) | data[i + 1];
    }
    if (length & 1) {
        sum += data[length - 1] << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

#endif /* SHARED_NET_H */
