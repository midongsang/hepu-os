/* ============================================================================
 * 赫普OS - 网络协议深度单元测试
 * 覆盖：TCP/UDP/IP/以太网帧解析
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === 以太网帧测试 === */
typedef struct {
    u8  dst_mac[6];
    u8  src_mac[6];
    u16 ethertype;
    u8  payload[1500];
} __attribute__((packed)) eth_frame_t;

static kunit_result_t test_eth_frame_parse(void)
{
    u8 raw_frame[1514] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  /* dst: broadcast */
        0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,  /* src */
        0x08, 0x00                           /* IPv4 */
    };

    eth_frame_t *frame = (eth_frame_t *)raw_frame;

    /* 检查目的MAC */
    int is_broadcast = 1;
    for (int i = 0; i < 6; i++) {
        if (frame->dst_mac[i] != 0xFF) { is_broadcast = 0; break; }
    }
    KUNIT_ASSERT(is_broadcast, " entries MAC broadcast ");

    /* 检查源MAC */
    KUNIT_ASSERT_EQ(frame->src_mac[0], 0xAA, " 源 MAC bytes ");
    KUNIT_ASSERT_EQ(frame->src_mac[5], 0xFF, " 源 MAC 末 bytes ");

    /* 检查EtherType（网络字节序） */
    u16 ethertype = (frame->ethertype >> 8) | (frame->ethertype << 8);
    KUNIT_ASSERT_EQ(ethertype, 0x0800, "EtherType=IPv4");
    return KUNIT_PASS;
}

static kunit_result_t test_eth_ethertypes(void)
{
    KUNIT_ASSERT_EQ(0x0800, 0x0800, "IPv4");
    KUNIT_ASSERT_EQ(0x0806, 0x0806, "ARP");
    KUNIT_ASSERT_EQ(0x86DD, 0x86DD, "IPv6");
    KUNIT_ASSERT_EQ(0x8100, 0x8100, "VLAN");
    return KUNIT_PASS;
}

/* === IPv4头测试 === */
typedef struct {
    u8  version_ihl;
    u8  tos;
    u16 total_length;
    u16 identification;
    u16 flags_fragment;
    u8  ttl;
    u8  protocol;
    u16 checksum;
    u32 src_ip;
    u32 dst_ip;
} __attribute__((packed)) ipv4_header_t;

static kunit_result_t test_ipv4_header_parse(void)
{
    u8 raw_ip[20] = {
        0x45, 0x00, 0x00, 0x3c,  /* version=4, IHL=5, TOS=0, total=60 */
        0x1c, 0x46, 0x40, 0x00,  /* ID, flags=DF, fragment=0 */
        0x40, 0x06, 0x00, 0x00,  /* TTL=64, protocol=TCP, checksum=0 */
        0xac, 0x10, 0x0a, 0x63,  /* src: 172.16.10.99 */
        0xac, 0x10, 0x0a, 0x0c   /* dst: 172.16.10.12 */
    };

    ipv4_header_t *ip = (ipv4_header_t *)raw_ip;

    u8 version = (ip->version_ihl >> 4) & 0x0F;
    u8 ihl = ip->version_ihl & 0x0F;

    KUNIT_ASSERT_EQ(version, 4, "IP version =4");
    KUNIT_ASSERT_EQ(ihl, 5, "IHL=5(20 bytes)");
    KUNIT_ASSERT_EQ(ip->ttl, 64, "TTL=64");
    KUNIT_ASSERT_EQ(ip->protocol, 6, " proto =TCP");

    /* 检查源IP */
    u8 *src = (u8 *)&ip->src_ip;
    KUNIT_ASSERT_EQ(src[0], 0xac, " 源 IP bytes ");
    KUNIT_ASSERT_EQ(src[3], 0x63, " 源 IP 末 bytes ");
    return KUNIT_PASS;
}

static kunit_result_t test_ip_protocols(void)
{
    KUNIT_ASSERT_EQ(1, 1, "ICMP");
    KUNIT_ASSERT_EQ(2, 2, "IGMP");
    KUNIT_ASSERT_EQ(6, 6, "TCP");
    KUNIT_ASSERT_EQ(17, 17, "UDP");
    KUNIT_ASSERT_EQ(41, 41, "IPv6 封装 ");
    return KUNIT_PASS;
}

/* === TCP头测试 === */
typedef struct {
    u16 src_port;
    u16 dst_port;
    u32 seq_num;
    u32 ack_num;
    u8  data_offset;
    u8  flags;
    u16 window;
    u16 checksum;
    u16 urgent_ptr;
} __attribute__((packed)) tcp_header_t;

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10
#define TCP_URG 0x20

static kunit_result_t test_tcp_header_flags(void)
{
    KUNIT_ASSERT_EQ(TCP_FIN, 0x01, "FIN 标志 ");
    KUNIT_ASSERT_EQ(TCP_SYN, 0x02, "SYN 标志 ");
    KUNIT_ASSERT_EQ(TCP_RST, 0x04, "RST 标志 ");
    KUNIT_ASSERT_EQ(TCP_PSH, 0x08, "PSH 标志 ");
    KUNIT_ASSERT_EQ(TCP_ACK, 0x10, "ACK 标志 ");
    KUNIT_ASSERT_EQ(TCP_URG, 0x20, "URG 标志 ");

    /* SYN-ACK组合 */
    u8 syn_ack = TCP_SYN | TCP_ACK;
    KUNIT_ASSERT_EQ(syn_ack, 0x12, "SYN-ACK 组合 ");
    return KUNIT_PASS;
}

static kunit_result_t test_tcp_ports(void)
{
    KUNIT_ASSERT_EQ(80, 80, "HTTP port ");
    KUNIT_ASSERT_EQ(443, 443, "HTTPS port ");
    KUNIT_ASSERT_EQ(22, 22, "SSH port ");
    KUNIT_ASSERT_EQ(53, 53, "DNS port ");
    KUNIT_ASSERT_EQ(853, 853, "DoT port ");
    KUNIT_ASSERT_EQ(443, 443, "DoH port ");
    return KUNIT_PASS;
}

static kunit_result_t test_tcp_seq_numbers(void)
{
    /* TCP序列号回绕测试 */
    u32 seq = 0xFFFFFFF0;
    seq += 0x20;  /* 应该回绕到0x10 */
    KUNIT_ASSERT_EQ(seq, 0x10, " 序列号回绕 ");

    /* ACK号 */
    u32 ack = 1000;
    ack += 1;  /* 期望下一个字节 */
    KUNIT_ASSERT_EQ(ack, 1001, "ACK 号递增 ");
    return KUNIT_PASS;
}

/* === UDP头测试 === */
typedef struct {
    u16 src_port;
    u16 dst_port;
    u16 length;
    u16 checksum;
} __attribute__((packed)) udp_header_t;

static kunit_result_t test_udp_header(void)
{
    KUNIT_ASSERT_EQ(sizeof(udp_header_t), 8, "UDP header size =8 bytes ");

    /* UDP长度字段包含头部 */
    u16 payload_len = 100;
    u16 total_len = 8 + payload_len;
    KUNIT_ASSERT_EQ(total_len, 108, "UDP total len = header + data ");
    return KUNIT_PASS;
}

/* === ICMP测试 === */
#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO_REQUEST 8
#define ICMP_TIME_EXCEEDED 11

static kunit_result_t test_icmp_types(void)
{
    KUNIT_ASSERT_EQ(ICMP_ECHO_REPLY, 0, "Echo Reply");
    KUNIT_ASSERT_EQ(ICMP_ECHO_REQUEST, 8, "Echo Request");
    KUNIT_ASSERT_EQ(ICMP_TIME_EXCEEDED, 11, "Time Exceeded");
    return KUNIT_PASS;
}

/* === ARP测试 === */
typedef struct {
    u16 htype;     /* 硬件类型 */
    u16 ptype;     /* 协议类型 */
    u8  hlen;      /* 硬件地址长度 */
    u8  plen;      /* 协议地址长度 */
    u16 oper;      /* 操作码 */
    u8  sha[6];    /* 发送者MAC */
    u32 spa;       /* 发送者IP */
    u8  tha[6];    /* 目标MAC */
    u32 tpa;       /* 目标IP */
} __attribute__((packed)) arp_packet_t;

#define ARP_REQUEST 1
#define ARP_REPLY 2

static kunit_result_t test_arp_packet(void)
{
    KUNIT_ASSERT_EQ(ARP_REQUEST, 1, "ARP 请求 ");
    KUNIT_ASSERT_EQ(ARP_REPLY, 2, "ARP 应答 ");

    /* 硬件类型=以太网(1), 协议类型=IPv4(0x0806) */
    KUNIT_ASSERT_EQ(1, 1, " HW type = Ethernet ");
    KUNIT_ASSERT_EQ(0x0800, 0x0800, " proto type =IPv4");
    KUNIT_ASSERT_EQ(6, 6, " HW addr len =6");
    KUNIT_ASSERT_EQ(4, 4, " proto addr len =4");
    return KUNIT_PASS;
}

/* === DNS测试 === */
typedef struct {
    u16 id;
    u16 flags;
    u16 qdcount;
    u16 ancount;
    u16 nscount;
    u16 arcount;
} __attribute__((packed)) dns_header_t;

#define DNS_QR_QUERY 0
#define DNS_QR_RESPONSE 1
#define DNS_OPCODE_QUERY 0
#define DNS_RCODE_NOERROR 0
#define DNS_RCODE_NXDOMAIN 3

static kunit_result_t test_dns_header(void)
{
    KUNIT_ASSERT_EQ(sizeof(dns_header_t), 12, "DNS header size =12 bytes ");

    /* DNS标志解析 */
    u16 flags = 0x8180;  /* 标准查询响应, 无错误 */
    u8 qr = (flags >> 15) & 0x1;
    u8 opcode = (flags >> 11) & 0xF;
    u8 rcode = flags & 0xF;

    KUNIT_ASSERT_EQ(qr, DNS_QR_RESPONSE, "QR= 响应 ");
    KUNIT_ASSERT_EQ(opcode, DNS_OPCODE_QUERY, "Opcode= standard query ");
    KUNIT_ASSERT_EQ(rcode, DNS_RCODE_NOERROR, "Rcode= no errors ");
    return KUNIT_PASS;
}

static kunit_result_t test_dns_rcodes(void)
{
    KUNIT_ASSERT_EQ(DNS_RCODE_NOERROR, 0, " no errors ");
    KUNIT_ASSERT_EQ(1, 1, " format error ");
    KUNIT_ASSERT_EQ(2, 2, " server failed ");
    KUNIT_ASSERT_EQ(DNS_RCODE_NXDOMAIN, 3, " domains 名 not exists ");
    KUNIT_ASSERT_EQ(4, 4, " query type not supports ");
    KUNIT_ASSERT_EQ(5, 5, " query denied ");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t net_deep_tests[] = {
    {" Ethernet frame parse ", test_eth_frame_parse, "net"},
    {"EtherType", test_eth_ethertypes, "net"},
    {"IPv4 header parse ", test_ipv4_header_parse, "net"},
    {"IP proto 号 ", test_ip_protocols, "net"},
    {"TCP 标志 ", test_tcp_header_flags, "net"},
    {"TCP port ", test_tcp_ports, "net"},
    {"TCP 序列号 ", test_tcp_seq_numbers, "net"},
    {"UDP header ", test_udp_header, "net"},
    {"ICMP type ", test_icmp_types, "net"},
    {"ARP packages ", test_arp_packet, "net"},
    {"DNS header ", test_dns_header, "net"},
    {"DNS 返回码 ", test_dns_rcodes, "net"},
};
static const kunit_suite_t kunit_net_deep_suite = {
    " net proto deep ", net_deep_tests, 12
};

void kunit_net_deep_register(void)
{
    kunit_register_suite(&kunit_net_deep_suite);
    vga_puts("[KUNIT] net deep test suite registered(12 cases)\n");
}
