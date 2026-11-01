/* ============================================================================
 * 赫普OS 64位 - IP协议（IPv4）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

/* IP头结构 */
typedef struct {
    uint8_t  ver_ihl;     /* 版本(4位) + IHL(4位) */
    uint8_t  tos;         /* 服务类型 */
    uint16_t total_len;   /* 总长度 */
    uint16_t id;          /* 标识 */
    uint16_t flags_frag;  /* 标志+片偏移 */
    uint8_t  ttl;         /* 生存时间 */
    uint8_t  protocol;    /* 协议 */
    uint16_t checksum;    /* 校验和 */
    uint32_t src_ip;      /* 源IP */
    uint32_t dst_ip;      /* 目标IP */
} __attribute__((packed)) ip_header_t;

extern void vga_puts(const char *s);
extern int eth64_send(const uint8_t *data, uint32_t length);
extern int eth64_get_mac(uint8_t *mac);
extern uint32_t eth64_get_ip(void);
extern uint32_t eth64_get_gateway(void);
extern uint32_t eth64_get_netmask(void);
extern int arp64_lookup(uint32_t ip, uint8_t *mac);
extern int arp64_request(uint32_t ip);

/* 协议处理函数 */
extern void icmp64_handle(const uint8_t *packet, int length, uint32_t src_ip);
extern void udp64_handle(const uint8_t *packet, int length, uint32_t src_ip, uint32_t dst_ip);
extern void tcp64_handle(const uint8_t *packet, int length, uint32_t src_ip, uint32_t dst_ip);

static void ip_memcpy(void *dst, const void *src, int n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}

/* IP校验和 */
static uint16_t ip_checksum(const uint8_t *data, int length)
{
    uint32_t sum = 0;
    for (int i = 0; i < length; i += 2) {
        uint16_t word = data[i] << 8;
        if (i + 1 < length) word |= data[i + 1];
        sum += word;
    }
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

/* 大端/小端转换 */
static uint16_t htons16(uint16_t v) { return (v >> 8) | (v << 8); }
static uint32_t htonl32(uint32_t v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
           ((v & 0xFF0000) >> 8) | ((v & 0xFF000000) >> 24);
}

static int ip_initialized = 0;
static uint16_t ip_id_counter = 0;

/* 初始化IP */
void ip64_init(void)
{
    ip_id_counter = 0;
    ip_initialized = 1;
    vga_puts("[IP64] IPv4 protocol initialized\n");
}

/* 发送IP包 */
int ip64_send(uint32_t dst_ip, uint8_t protocol, const uint8_t *data, int data_len)
{
    if (!ip_initialized) return -1;

    uint8_t packet[1500];
    ip_header_t *ip = (ip_header_t *)packet;
    int total_len = 20 + data_len;

    if (total_len > 1500) return -1;

    /* 填充IP头 */
    ip->ver_ihl = 0x45;  /* IPv4, 20字节头 */
    ip->tos = 0;
    ip->total_len = htons16(total_len);
    ip->id = htons16(ip_id_counter++);
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->checksum = 0;
    ip->src_ip = htonl32(eth64_get_ip());
    ip->dst_ip = htonl32(dst_ip);

    /* 计算校验和 */
    ip->checksum = ip_checksum((uint8_t *)ip, 20);

    /* 复制数据 */
    ip_memcpy(packet + 20, data, data_len);

    /* 确定下一跳IP */
    uint32_t next_hop;
    uint32_t my_ip = eth64_get_ip();
    uint32_t netmask = eth64_get_netmask();
    if ((dst_ip & netmask) == (my_ip & netmask)) {
        next_hop = dst_ip;  /* 同一子网 */
    } else {
        next_hop = eth64_get_gateway();  /* 通过网关 */
    }

    if (next_hop == 0) return -1;

    /* 查找目标MAC */
    uint8_t dst_mac[6];
    if (arp64_lookup(next_hop, dst_mac) != 0) {
        /* ARP表中没有，发送ARP请求 */
        arp64_request(next_hop);
        return -2;  /* 需要等待ARP应答 */
    }

    /* 构建以太网帧并发送 */
    uint8_t frame[1514];
    uint8_t my_mac[6];
    eth64_get_mac(my_mac);

    ip_memcpy(frame, dst_mac, 6);
    ip_memcpy(frame + 6, my_mac, 6);
    frame[12] = 0x08; frame[13] = 0x00;  /* IP类型 */
    ip_memcpy(frame + 14, packet, total_len);

    return eth64_send(frame, 14 + total_len);
}

/* 处理收到的IP包 */
void ip64_handle(const uint8_t *frame, int length)
{
    if (length < 14 + 20) return;

    ip_header_t *ip = (ip_header_t *)(frame + 14);
    int ip_len = (ip->ver_ihl & 0x0F) * 4;

    if (ip_len < 20 || 14 + ip_len > length) return;

    uint32_t src_ip = htonl32(ip->src_ip);
    uint32_t dst_ip = htonl32(ip->dst_ip);
    int data_len = htons16(ip->total_len) - ip_len;

    if (data_len < 0) return;

    const uint8_t *data = frame + 14 + ip_len;

    switch (ip->protocol) {
        case IP_PROTO_ICMP:
            icmp64_handle(data, data_len, src_ip);
            break;
        case IP_PROTO_UDP:
            udp64_handle(data, data_len, src_ip, dst_ip);
            break;
        case IP_PROTO_TCP:
            tcp64_handle(data, data_len, src_ip, dst_ip);
            break;
    }
}
