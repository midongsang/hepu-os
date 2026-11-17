/* ============================================================================
 * 赫普OS - IP协议层
 * IPv4协议实现，支持ICMP/UDP/TCP
 * V7.98: 初始实现
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "eth.h"
#include "arp.h"
#include "vga.h"
#include "serial.h"

/* IP协议类型 */
#define IP_PROTO_ICMP  1
#define IP_PROTO_TCP   6
#define IP_PROTO_UDP   17

/* IP头结构 */
typedef struct {
    uint8_t  version_ihl;    /* 版本(4位) + 头长度(4位) */
    uint8_t  tos;            /* 服务类型 */
    uint16_t total_length;   /* 总长度 */
    uint16_t id;             /* 标识 */
    uint16_t flags_frag;     /* 标志(3位) + 片偏移(13位) */
    uint8_t  ttl;            /* 生存时间 */
    uint8_t  protocol;       /* 协议 */
    uint16_t checksum;       /* 校验和 */
    uint32_t src_ip;         /* 源IP */
    uint32_t dest_ip;        /* 目的IP */
} __attribute__((packed)) ip_header_t;

/* 以太网头结构 */
typedef struct {
    uint8_t  dest_mac[6];
    uint8_t  src_mac[6];
    uint16_t ethertype;
} __attribute__((packed)) eth_header_t;

/* 网络配置 */
static uint32_t local_ip = 0;
static uint32_t netmask = 0;
static uint32_t gateway = 0;
static int ip_initialized = 0;

/* 计算IP校验和: 按 RFC1071 把头部当作大端 16 位字求和。
 * 旧实现直接按原生 uint16_t 解引用, 小端机上每个字被字节翻转, 累加出来的
 * 校验和本身就是错的; 与 arch/x86_64/net64_ip.c 的 ip_checksum 对齐, 显式
 * 用 data[i]<<8 | data[i+1] 拼大端字。 */
static uint16_t ip_checksum(const uint8_t *data, uint32_t length)
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i + 1 < length; i += 2) {
        uint16_t word = ((uint16_t)data[i] << 8) | data[i + 1];
        sum += word;
    }
    if (length & 1) {
        sum += (uint16_t)data[length - 1] << 8;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

/* 主机序 <-> 网络序 (与 arch/x86_64/net64_ip.c 同约定)。
 * 以太网/IP/UDP 头里的 16/32 位整数字段线上一律大端; 本机小端直写会被
 * 对端按大端解析成颠倒值(total_length/id/端口/长度离谱), 帧在对端协议层
 * 即被丢弃, 表现为本机 RX 环永远等不到完成位、MMC PRC 不涨。 */
static inline uint16_t htons16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
static inline uint16_t ntohs16(uint16_t v) { return htons16(v); }
static inline uint32_t htonl32(uint32_t v)
{
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
           ((v & 0xFF0000) >> 8) | ((v & 0xFF000000) >> 24);
}
static inline uint32_t ntohl32(uint32_t v) { return htonl32(v); }

/* 初始化IP层 */
void ip_init(void)
{
    local_ip = (192 << 24) | (168 << 16) | (1 << 8) | 100;
    netmask = 0xFFFFFF00;
    gateway = (192 << 24) | (168 << 16) | (1 << 8) | 1;
    ip_initialized = 1;
    serial_puts(0x3F8, "[IP] initialized, IP=192.168.1.100\n");
}

/* 设置IP配置 */
void ip_set_config(uint32_t ip, uint32_t mask, uint32_t gw)
{
    local_ip = ip;
    netmask = mask;
    gateway = gw;
    serial_puts(0x3F8, "[IP] config updated\n");
}

/* 获取本机IP */
uint32_t ip_get_local(void)
{
    return local_ip;
}

/* 发送IP包 */
int ip_send(uint32_t dest_ip, uint8_t protocol, const uint8_t *data, uint32_t data_len)
{
    if (!ip_initialized || !data || data_len == 0) return -1;

    /* 检查是否为广播地址 */
    int is_broadcast = (dest_ip == 0xFFFFFFFF);

    /* 计算下一跳IP */
    uint32_t next_hop;
    if (is_broadcast) {
        next_hop = 0xFFFFFFFF;  /* 广播 */
    } else if ((dest_ip & netmask) == (local_ip & netmask)) {
        next_hop = dest_ip;  /* 同一子网 */
    } else {
        next_hop = gateway;  /* 通过网关 */
    }

    /* 查找下一跳的MAC地址 */
    uint8_t dest_mac[6];
    if (is_broadcast) {
        memset(dest_mac, 0xFF, 6);  /* 广播MAC */
    } else if (arp_lookup(next_hop, dest_mac) != 0) {
        /* ARP缓存未命中，发送ARP请求 */
        arp_send_request(next_hop);
        serial_puts(0x3F8, "[IP] ARP miss, sending request\n");
        return -1;  /* 暂时无法发送，等ARP回复后重试 */
    }

    /* 获取本机MAC */
    uint8_t src_mac[6];
    eth_get_mac("eth0", src_mac);

    /* 构造完整数据包：以太网头 + IP头 + 数据 */
    uint32_t packet_len = sizeof(eth_header_t) + sizeof(ip_header_t) + data_len;
    uint8_t packet[1514];  /* 最大以太网帧 */
    if (packet_len > sizeof(packet)) return -1;
    memset(packet, 0, sizeof(packet));

    /* 以太网头 */
    eth_header_t *eth_hdr = (eth_header_t *)packet;
    memcpy(eth_hdr->dest_mac, dest_mac, 6);
    memcpy(eth_hdr->src_mac, src_mac, 6);
    eth_hdr->ethertype = 0x0008;  /* IPv4 (大端序: 0x0800) */

    /* IP头 */
    ip_header_t *ip_hdr = (ip_header_t *)(packet + sizeof(eth_header_t));
    ip_hdr->version_ihl = 0x45;  /* IPv4, 头长20字节 */
    ip_hdr->tos = 0;
    ip_hdr->total_length = htons16((uint16_t)(sizeof(ip_header_t) + data_len));
    ip_hdr->id = htons16(0x1234);
    ip_hdr->flags_frag = 0;
    ip_hdr->ttl = 64;
    ip_hdr->protocol = protocol;
    ip_hdr->checksum = 0;
    ip_hdr->src_ip = htonl32(local_ip);
    ip_hdr->dest_ip = htonl32(dest_ip);

    /* 计算IP校验和: 字段已按网络序落内存, 校验和结果再转网络序写入。 */
    ip_hdr->checksum = htons16(ip_checksum((uint8_t *)ip_hdr, sizeof(ip_header_t)));

    /* 复制数据 */
    memcpy(packet + sizeof(eth_header_t) + sizeof(ip_header_t), data, data_len);

    /* 发送 */
    int ret = eth_send("eth0", packet, packet_len);
    if (ret > 0) {
        serial_puts(0x3F8, "[IP] packet sent\n");
    }

    return ret;
}

/* 处理接收到的IP包 */
int ip_process_packet(const uint8_t *packet, uint32_t length)
{
    if (!ip_initialized || !packet || length < sizeof(eth_header_t) + sizeof(ip_header_t)) {
        return -1;
    }

    /* 解析以太网头 */
    const eth_header_t *eth_hdr = (const eth_header_t *)packet;
    if (eth_hdr->ethertype != 0x0008) {  /* 不是IPv4 */
        return -1;
    }

    /* 解析IP头 */
    ip_header_t *ip_hdr = (ip_header_t *)(packet + sizeof(eth_header_t));

    /* 接收侧字段是网络序, 转主机序再解析。 */
    uint32_t ip_header_len = (ip_hdr->version_ihl & 0x0F) * 4;
    if (ip_header_len < 20) return -1;
    uint32_t total_len = ntohs16(ip_hdr->total_length);
    uint32_t data_len = total_len - ip_header_len;
    /* 安全: 对端自报长度必须钳制到实际帧长, 避免越界读。 */
    if (data_len > length - sizeof(eth_header_t) - ip_header_len)
        data_len = length - sizeof(eth_header_t) - ip_header_len;
    if (data_len > 1500) return -1;
    uint32_t src_ip = ntohl32(ip_hdr->src_ip);
    uint32_t dst_ip = ntohl32(ip_hdr->dest_ip);
    const uint8_t *data = packet + sizeof(eth_header_t) + ip_header_len;

    /* 根据协议分发 */
    switch (ip_hdr->protocol) {
        case IP_PROTO_ICMP:
            serial_puts(0x3F8, "[IP] ICMP packet received\n");
            /* 调用ICMP处理 */
            extern int icmp_process_packet(const uint8_t *data, uint32_t length);
            icmp_process_packet(data, data_len);
            break;
        case IP_PROTO_UDP:
            serial_puts(0x3F8, "[IP] UDP packet received\n");
            /* 调用UDP处理 */
            extern int udp_process_packet(uint32_t src_ip, const uint8_t *data, uint32_t length);
            udp_process_packet(src_ip, data, data_len);
            break;
        case IP_PROTO_TCP:
            serial_puts(0x3F8, "[IP] TCP packet received\n");
            /* 调用TCP处理 */
            extern int tcp_process_packet(uint32_t src_ip, const uint8_t *data, uint32_t length);
            tcp_process_packet(src_ip, data, data_len);
            break;
        default:
            serial_puts(0x3F8, "[IP] unknown protocol\n");
            break;
    }

    return 0;
}

/* 网络轮询：从网卡读取数据包并处理 */
void net_poll(void)
{
    if (!ip_initialized) return;

    uint8_t packet[1514];
    int len = eth_receive("eth0", packet, sizeof(packet));
    if (len > 0) {
        /* 解析以太网头，判断协议类型 */
        if (len >= (int)sizeof(eth_header_t)) {
            const eth_header_t *eth_hdr = (const eth_header_t *)packet;
            if (eth_hdr->ethertype == 0x0008) {  /* IPv4 */
                ip_process_packet(packet, len);
            } else if (eth_hdr->ethertype == 0x0608) {  /* ARP */
                extern int arp_process_packet(const uint8_t *data, uint32_t length);
                arp_process_packet(packet + sizeof(eth_header_t), len - sizeof(eth_header_t));
            }
        }
    }
}
