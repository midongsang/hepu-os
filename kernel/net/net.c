/* ============================================================================
 * 赫普OS - 网络协议栈
 * 以太网 + ARP + IP + ICMP + TCP + UDP + Socket API
 * ========================================================================== */

#include <stdint.h>
#include "net.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"

/* 字节序转换 */
static inline uint16_t htons(uint16_t x) {
    return ((x & 0xFF) << 8) | ((x >> 8) & 0xFF);
}
static inline uint16_t ntohs(uint16_t x) { return htons(x); }
static inline uint32_t htonl(uint32_t x) {
    return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) |
           ((x >> 8) & 0xFF00) | ((x >> 24) & 0xFF);
}
static inline uint32_t ntohl(uint32_t x) { return htonl(x); }

static net_iface_t iface;
static net_socket_t sockets[NET_MAX_SOCKETS];

/* ARP缓存 */
typedef struct {
    uint8_t ip[NET_IP_LEN];
    uint8_t mac[NET_MAC_LEN];
    int used;
} arp_entry_t;
static arp_entry_t arp_cache[16];

void net_init(void)
{
    /* 默认MAC: 52:54:00:12:34:56 (QEMU默认) */
    iface.mac[0] = 0x52; iface.mac[1] = 0x54; iface.mac[2] = 0x00;
    iface.mac[3] = 0x12; iface.mac[4] = 0x34; iface.mac[5] = 0x56;
    /* 默认IP: 10.0.2.15 (QEMU用户网络) */
    iface.ip[0] = 10; iface.ip[1] = 0; iface.ip[2] = 2; iface.ip[3] = 15;
    iface.gateway[0] = 10; iface.gateway[1] = 0; iface.gateway[2] = 2; iface.gateway[3] = 2;
    iface.netmask[0] = 255; iface.netmask[1] = 255; iface.netmask[2] = 255; iface.netmask[3] = 0;
    iface.up = 1;

    for (int i = 0; i < NET_MAX_SOCKETS; i++) sockets[i].used = 0;
    for (int i = 0; i < 16; i++) arp_cache[i].used = 0;

    vga_puts("[NET] net stack init done \n");
    vga_printf("      MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
               iface.mac[0], iface.mac[1], iface.mac[2],
               iface.mac[3], iface.mac[4], iface.mac[5]);
    vga_printf("      IP:  %d.%d.%d.%d\n",
               iface.ip[0], iface.ip[1], iface.ip[2], iface.ip[3]);
}

uint16_t net_checksum(void *data, uint32_t len)
{
    uint32_t sum = 0;
    uint16_t *ptr = (uint16_t *)data;
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    if (len == 1) sum += *(uint8_t *)ptr;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum;
}

uint32_t net_ip_to_int(uint8_t *ip)
{
    return (ip[0] << 24) | (ip[1] << 16) | (ip[2] << 8) | ip[3];
}

void net_int_to_ip(uint32_t val, uint8_t *ip)
{
    ip[0] = (val >> 24) & 0xFF;
    ip[1] = (val >> 16) & 0xFF;
    ip[2] = (val >> 8) & 0xFF;
    ip[3] = val & 0xFF;
}

/* ---- ARP ---- */

int net_arp_resolve(uint8_t *ip, uint8_t *mac)
{
    for (int i = 0; i < 16; i++) {
        if (arp_cache[i].used && memcmp(arp_cache[i].ip, ip, 4) == 0) {
            memcpy(mac, arp_cache[i].mac, 6);
            return 0;
        }
    }
    return -1;
}

static void arp_add(uint8_t *ip, uint8_t *mac)
{
    for (int i = 0; i < 16; i++) {
        if (!arp_cache[i].used) {
            memcpy(arp_cache[i].ip, ip, 4);
            memcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].used = 1;
            return;
        }
    }
}

void net_arp_request(uint8_t *ip)
{
    uint8_t buf[64];
    eth_header_t *eth = (eth_header_t *)buf;
    memset(eth->dst_mac, 0xFF, 6);
    memcpy(eth->src_mac, iface.mac, 6);
    eth->type = htons(ETH_TYPE_ARP);

    arp_packet_t *arp = (arp_packet_t *)(buf + sizeof(eth_header_t));
    arp->hw_type = htons(1);
    arp->proto_type = htons(ETH_TYPE_IP);
    arp->hw_len = 6;
    arp->proto_len = 4;
    arp->opcode = htons(ARP_REQUEST);
    memcpy(arp->sender_mac, iface.mac, 6);
    memcpy(arp->sender_ip, iface.ip, 4);
    memset(arp->target_mac, 0, 6);
    memcpy(arp->target_ip, ip, 4);

    net_tx(buf, sizeof(eth_header_t) + sizeof(arp_packet_t));
}

static void handle_arp(uint8_t *data, uint32_t len)
{
    (void)len;
    arp_packet_t *arp = (arp_packet_t *)data;
    if (ntohs(arp->opcode) == ARP_REPLY) {
        arp_add(arp->sender_ip, arp->sender_mac);
    }
}

/* ---- ICMP ---- */

void net_icmp_echo_reply(ip_header_t *ip_hdr, uint8_t *data, uint32_t len)
{
    icmp_header_t *icmp = (icmp_header_t *)data;
    icmp->type = ICMP_ECHO_REPLY;
    icmp->checksum = 0;
    icmp->checksum = net_checksum(icmp, len);

    /* 构造IP包回复 */
    uint8_t buf[NET_MTU];
    ip_header_t *reply_ip = (ip_header_t *)buf;
    memcpy(reply_ip, ip_hdr, sizeof(ip_header_t));
    memcpy(reply_ip->src_ip, iface.ip, 4);
    memcpy(reply_ip->dst_ip, ip_hdr->src_ip, 4);
    reply_ip->total_len = htons(sizeof(ip_header_t) + len);
    reply_ip->checksum = 0;
    reply_ip->checksum = net_checksum(reply_ip, sizeof(ip_header_t));

    memcpy(buf + sizeof(ip_header_t), icmp, len);

    /* 查找目标MAC并发送 */
    uint8_t dst_mac[6];
    if (net_arp_resolve(ip_hdr->src_ip, dst_mac) == 0) {
        uint8_t frame[NET_MTU + 14];
        eth_header_t *eth = (eth_header_t *)frame;
        memcpy(eth->dst_mac, dst_mac, 6);
        memcpy(eth->src_mac, iface.mac, 6);
        eth->type = htons(ETH_TYPE_IP);
        memcpy(frame + 14, buf, sizeof(ip_header_t) + len);
        net_tx(frame, 14 + sizeof(ip_header_t) + len);
    }
}

/* ---- IP ---- */

static void handle_ip(uint8_t *data, uint32_t len)
{
    /* 最小IP头20字节 */
    if (len < sizeof(ip_header_t)) return;

    ip_header_t *ip = (ip_header_t *)data;
    uint32_t ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;
    if (ip_hdr_len < 20 || ip_hdr_len > len) return;

    uint32_t total_len = ntohs(ip->total_len);
    if (total_len < ip_hdr_len || total_len > len) return;

    uint8_t proto = ip->protocol;
    uint8_t *payload = data + ip_hdr_len;
    uint32_t payload_len = total_len - ip_hdr_len;

    if (proto == IP_PROTO_ICMP) {
        if (payload_len < sizeof(icmp_header_t)) return;
        icmp_header_t *icmp = (icmp_header_t *)payload;
        if (icmp->type == ICMP_ECHO) {
            net_icmp_echo_reply(ip, payload, payload_len);
        }
    }
    else if (proto == IP_PROTO_TCP) {
        if (payload_len < sizeof(tcp_header_t)) return;
        tcp_header_t *tcp = (tcp_header_t *)payload;
        uint32_t data_offset = (tcp->data_offset >> 4) * 4;
        if (data_offset < 20 || data_offset > payload_len) return;

        for (int i = 0; i < NET_MAX_SOCKETS; i++) {
            if (sockets[i].used &&
                sockets[i].local_port == ntohs(tcp->dst_port)) {
                uint8_t *tcp_data = payload + data_offset;
                uint32_t tcp_data_len = payload_len - data_offset;
                uint32_t space = sizeof(sockets[i].rx_buf) - sockets[i].rx_len;
                if (tcp_data_len > 0 && tcp_data_len <= space) {
                    memcpy(sockets[i].rx_buf + sockets[i].rx_len, tcp_data, tcp_data_len);
                    sockets[i].rx_len += tcp_data_len;
                }
                break;
            }
        }
    }
    else if (proto == IP_PROTO_UDP) {
        if (payload_len < sizeof(udp_header_t)) return;
        /* UDP处理 */
    }
}

/* ---- 接收处理 ---- */

void net_rx(uint8_t *data, uint32_t len)
{
    if (!data || len < sizeof(eth_header_t)) return;
    eth_header_t *eth = (eth_header_t *)data;
    uint16_t type = ntohs(eth->type);
    uint32_t payload_len = len - sizeof(eth_header_t);

    if (type == ETH_TYPE_ARP) {
        if (payload_len >= sizeof(arp_packet_t))
            handle_arp(data + sizeof(eth_header_t), payload_len);
    }
    else if (type == ETH_TYPE_IP) {
        handle_ip(data + sizeof(eth_header_t), payload_len);
    }
}

int net_tx(uint8_t *data, uint32_t len)
{
    /* 原型阶段: 由网卡驱动实际发送 */
    (void)data; (void)len;
    return len;
}

/* ---- Socket API ---- */

int net_socket(int domain, int type, int protocol)
{
    (void)domain; (void)protocol;
    for (int i = 0; i < NET_MAX_SOCKETS; i++) {
        if (!sockets[i].used) {
            memset(&sockets[i], 0, sizeof(net_socket_t));
            sockets[i].used = 1;
            sockets[i].type = type;
            sockets[i].state = SOCK_CLOSED;
            return i;
        }
    }
    return -1;
}

int net_bind(int sockfd, uint16_t port)
{
    if (sockfd < 0 || sockfd >= NET_MAX_SOCKETS || !sockets[sockfd].used) return -1;
    sockets[sockfd].local_port = port;
    return 0;
}

int net_listen(int sockfd, int backlog)
{
    (void)backlog;
    if (sockfd < 0 || sockfd >= NET_MAX_SOCKETS || !sockets[sockfd].used) return -1;
    sockets[sockfd].state = SOCK_LISTEN;
    return 0;
}

int net_accept(int sockfd)
{
    if (sockfd < 0 || sockfd >= NET_MAX_SOCKETS || !sockets[sockfd].used) return -1;
    /* 原型阶段: 返回新socket */
    int newfd = net_socket(0, sockets[sockfd].type, 0);
    if (newfd >= 0) {
        sockets[newfd].state = SOCK_ESTABLISHED;
        sockets[newfd].local_port = sockets[sockfd].local_port;
    }
    return newfd;
}

int net_connect(int sockfd, uint8_t *ip, uint16_t port)
{
    if (sockfd < 0 || sockfd >= NET_MAX_SOCKETS || !sockets[sockfd].used) return -1;
    memcpy(sockets[sockfd].remote_ip, ip, 4);
    sockets[sockfd].remote_port = port;
    sockets[sockfd].state = SOCK_ESTABLISHED;  /* 简化: 直接建立 */
    return 0;
}

int net_send(int sockfd, const void *buf, uint32_t len)
{
    if (sockfd < 0 || sockfd >= NET_MAX_SOCKETS || !sockets[sockfd].used) return -1;
    if (sockets[sockfd].state != SOCK_ESTABLISHED) return -1;
    /* 原型阶段: 构造TCP包并发送 */
    (void)buf;
    return len;
}

int net_recv(int sockfd, void *buf, uint32_t len)
{
    if (sockfd < 0 || sockfd >= NET_MAX_SOCKETS || !sockets[sockfd].used) return -1;
    net_socket_t *s = &sockets[sockfd];
    if (s->rx_len == 0) return 0;
    int n = s->rx_len - s->rx_pos;
    if (n > (int)len) n = len;
    memcpy(buf, s->rx_buf + s->rx_pos, n);
    s->rx_pos += n;
    if (s->rx_pos >= s->rx_len) {
        s->rx_len = 0;
        s->rx_pos = 0;
    }
    return n;
}

int net_close(int sockfd)
{
    if (sockfd < 0 || sockfd >= NET_MAX_SOCKETS || !sockets[sockfd].used) return -1;
    sockets[sockfd].used = 0;
    return 0;
}
