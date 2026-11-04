/* ============================================================================
 * 赫普OS 64位 - DHCP客户端（动态主机配置协议）
 * 自动获取IP地址、子网掩码、网关、DNS服务器
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68

#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_DECLINE  4
#define DHCP_ACK      5
#define DHCP_NAK      6
#define DHCP_RELEASE  7
#define DHCP_INFORM   8

/* DHCP消息结构 */
typedef struct {
    uint8_t  op;          /* 1=请求, 2=应答 */
    uint8_t  htype;       /* 硬件类型 (1=以太网) */
    uint8_t  hlen;        /* 硬件地址长度 (6) */
    uint8_t  hops;        /* 跳数 */
    uint32_t xid;         /* 事务ID */
    uint16_t secs;        /* 秒数 */
    uint16_t flags;       /* 标志 */
    uint32_t ciaddr;      /* 客户端IP */
    uint32_t yiaddr;      /* 你的IP */
    uint32_t siaddr;      /* 服务器IP */
    uint32_t giaddr;      /* 网关IP */
    uint8_t  chaddr[16];  /* 客户端硬件地址 */
    uint8_t  sname[64];   /* 服务器名 */
    uint8_t  file[128];   /* 引导文件名 */
    uint32_t magic;       /* 魔术字 (0x63825363) */
    uint8_t  options[308]; /* 选项 */
} __attribute__((packed)) dhcp_packet_t;

/* DHCP选项 */
#define DHCP_OPT_PAD          0
#define DHCP_OPT_SUBNET_MASK  1
#define DHCP_OPT_ROUTER       3
#define DHCP_OPT_DNS          6
#define DHCP_OPT_HOSTNAME     12
#define DHCP_OPT_REQUESTED_IP 50
#define DHCP_OPT_LEASE_TIME   51
#define DHCP_OPT_DHCP_MSG     53
#define DHCP_OPT_SERVER_ID    54
#define DHCP_OPT_PARAM_LIST   55
#define DHCP_OPT_END          255

/* DHCP状态 */
#define DHCP_STATE_INIT       0
#define DHCP_STATE_SELECTING  1
#define DHCP_STATE_REQUESTING 2
#define DHCP_STATE_BOUND      3
#define DHCP_STATE_RENEWING   4

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);
extern int udp64_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                       const uint8_t *data, int data_len);
extern int udp64_bind(uint16_t port);
extern int udp64_recv(int sock, uint8_t *buffer, int max_len,
                      uint32_t *src_ip, uint16_t *src_port);
extern int eth64_get_mac(uint8_t *mac);
extern int eth64_set_ip(uint32_t ip, uint32_t netmask, uint32_t gateway);

static void dhcp_memcpy(void *dst, const void *src, int n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}

static void dhcp_memset(void *dst, uint8_t val, int n) {
    uint8_t *d = (uint8_t*)dst;
    for (int i = 0; i < n; i++) d[i] = val;
}

static uint32_t htonl32(uint32_t v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
           ((v & 0xFF0000) >> 8) | ((v & 0xFF000000) >> 24);
}
static uint32_t ntohl32(uint32_t v) { return htonl32(v); }

static int dhcp_state = DHCP_STATE_INIT;
static uint32_t dhcp_xid = 0x12345678;
static uint32_t dhcp_offered_ip = 0;
static uint32_t dhcp_server_ip = 0;
static uint32_t dhcp_netmask = 0;
static uint32_t dhcp_gateway = 0;
static uint32_t dhcp_dns = 0;
static int dhcp_udp_sock = -1;

/* 初始化DHCP */
void dhcp64_init(void)
{
    dhcp_state = DHCP_STATE_INIT;
    dhcp_xid++;
    vga_puts("[DHCP64] DHCP client initialized\n");
}

/* 发送DHCP Discover */
static int dhcp_send_discover(void)
{
    uint8_t packet[sizeof(dhcp_packet_t)];
    dhcp_packet_t *dhcp = (dhcp_packet_t *)packet;
    dhcp_memset(packet, 0, sizeof(packet));

    dhcp->op = 1;  /* 请求 */
    dhcp->htype = 1;  /* 以太网 */
    dhcp->hlen = 6;
    dhcp->xid = htonl32(dhcp_xid);
    dhcp->flags = htonl32(0x8000) >> 16;  /* 广播标志 */
    eth64_get_mac(dhcp->chaddr);
    dhcp->magic = htonl32(0x63825363);

    /* 选项 */
    int opt_idx = 0;
    /* DHCP消息类型: Discover */
    dhcp->options[opt_idx++] = DHCP_OPT_DHCP_MSG;
    dhcp->options[opt_idx++] = 1;
    dhcp->options[opt_idx++] = DHCP_DISCOVER;
    /* 参数请求列表 */
    dhcp->options[opt_idx++] = DHCP_OPT_PARAM_LIST;
    dhcp->options[opt_idx++] = 3;
    dhcp->options[opt_idx++] = DHCP_OPT_SUBNET_MASK;
    dhcp->options[opt_idx++] = DHCP_OPT_ROUTER;
    dhcp->options[opt_idx++] = DHCP_OPT_DNS;
    /* 结束 */
    dhcp->options[opt_idx++] = DHCP_OPT_END;

    /* 广播发送 */
    int rc2 = udp64_send(0xFFFFFFFF, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, packet, 240 + opt_idx);
    vga_puts("[DHCP64] discover rc=");
    vga_put_hex((uint64_t)rc2);
    vga_puts("\n");
    return rc2;
}

/* 发送DHCP Request */
static int dhcp_send_request(uint32_t requested_ip, uint32_t server_ip)
{
    uint8_t packet[sizeof(dhcp_packet_t)];
    dhcp_packet_t *dhcp = (dhcp_packet_t *)packet;
    dhcp_memset(packet, 0, sizeof(packet));

    dhcp->op = 1;
    dhcp->htype = 1;
    dhcp->hlen = 6;
    dhcp->xid = htonl32(dhcp_xid);
    dhcp->flags = htonl32(0x8000) >> 16;
    eth64_get_mac(dhcp->chaddr);
    dhcp->magic = htonl32(0x63825363);

    int opt_idx = 0;
    /* DHCP消息类型: Request */
    dhcp->options[opt_idx++] = DHCP_OPT_DHCP_MSG;
    dhcp->options[opt_idx++] = 1;
    dhcp->options[opt_idx++] = DHCP_REQUEST;
    /* 请求的IP */
    dhcp->options[opt_idx++] = DHCP_OPT_REQUESTED_IP;
    dhcp->options[opt_idx++] = 4;
    uint32_t ip = htonl32(requested_ip);
    dhcp_memcpy(&dhcp->options[opt_idx], &ip, 4);
    opt_idx += 4;
    /* 服务器ID */
    dhcp->options[opt_idx++] = DHCP_OPT_SERVER_ID;
    dhcp->options[opt_idx++] = 4;
    uint32_t sip = htonl32(server_ip);
    dhcp_memcpy(&dhcp->options[opt_idx], &sip, 4);
    opt_idx += 4;
    /* 结束 */
    dhcp->options[opt_idx++] = DHCP_OPT_END;

    int rc2 = udp64_send(0xFFFFFFFF, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, packet, 240 + opt_idx);
    vga_puts("[DHCP64] discover rc=");
    vga_put_hex((uint64_t)rc2);
    vga_puts("\n");
    return rc2;
}

/* 解析DHCP选项 */
static void dhcp_parse_options(const uint8_t *options, int len)
{
    int i = 0;
    while (i < len) {
        uint8_t opt = options[i];
        if (opt == DHCP_OPT_PAD) { i++; continue; }
        if (opt == DHCP_OPT_END) break;
        if (i + 1 >= len) break;
        uint8_t opt_len = options[i + 1];
        if (i + 2 + opt_len > len) break;

        switch (opt) {
            case DHCP_OPT_SUBNET_MASK:
                if (opt_len == 4) {
                    dhcp_memcpy(&dhcp_netmask, &options[i + 2], 4);
                    dhcp_netmask = ntohl32(dhcp_netmask);
                }
                break;
            case DHCP_OPT_ROUTER:
                if (opt_len >= 4) {
                    dhcp_memcpy(&dhcp_gateway, &options[i + 2], 4);
                    dhcp_gateway = ntohl32(dhcp_gateway);
                }
                break;
            case DHCP_OPT_DNS:
                if (opt_len >= 4) {
                    dhcp_memcpy(&dhcp_dns, &options[i + 2], 4);
                    dhcp_dns = ntohl32(dhcp_dns);
                }
                break;
            case DHCP_OPT_DHCP_MSG:
                /* 消息类型在处理函数中判断 */
                break;
        }
        i += 2 + opt_len;
    }
}

/* 处理DHCP Offer */
static void dhcp_handle_offer(const uint8_t *packet, int len)
{
    if (len < 240) return;
    dhcp_packet_t *dhcp = (dhcp_packet_t *)packet;

    if (ntohl32(dhcp->xid) != dhcp_xid) return;

    dhcp_offered_ip = ntohl32(dhcp->yiaddr);
    dhcp_server_ip = ntohl32(dhcp->siaddr);

    /* 解析选项获取服务器ID */
    const uint8_t *opts = dhcp->options;
    int i = 0;
    while (i < 308) {
        if (opts[i] == DHCP_OPT_END) break;
        if (opts[i] == DHCP_OPT_PAD) { i++; continue; }
        if (opts[i] == DHCP_OPT_SERVER_ID && opts[i+1] == 4) {
            dhcp_memcpy(&dhcp_server_ip, &opts[i+2], 4);
            dhcp_server_ip = ntohl32(dhcp_server_ip);
            break;
        }
        i += 2 + opts[i+1];
    }

    dhcp_parse_options(dhcp->options, len - 240);

    vga_puts("[DHCP64] Offer received, IP: ");
    char ipstr[16];
    uint32_t ip_net = htonl32(dhcp_offered_ip);
    uint8_t *ip = (uint8_t*)&ip_net;
    int pos = 0;
    for (int j = 0; j < 4; j++) {
        if (ip[j] >= 100) ipstr[pos++] = '0' + ip[j]/100;
        if (ip[j] >= 10) ipstr[pos++] = '0' + (ip[j]/10)%10;
        ipstr[pos++] = '0' + ip[j]%10;
        if (j < 3) ipstr[pos++] = '.';
    }
    ipstr[pos] = 0;
    vga_puts(ipstr);
    vga_puts("\n");
}

/* 处理DHCP Ack */
static int dhcp_handle_ack(const uint8_t *packet, int len)
{
    if (len < 240) return -1;
    dhcp_packet_t *dhcp = (dhcp_packet_t *)packet;

    if (ntohl32(dhcp->xid) != dhcp_xid) return -1;

    uint32_t assigned_ip = ntohl32(dhcp->yiaddr);
    dhcp_parse_options(dhcp->options, len - 240);

    /* 设置网络配置 */
    eth64_set_ip(assigned_ip, dhcp_netmask, dhcp_gateway);
    dhcp_state = DHCP_STATE_BOUND;

    vga_puts("[DHCP64] IP assigned: ");
    char ipstr[16];
    uint32_t ip_net2 = htonl32(assigned_ip);
    uint8_t *ip = (uint8_t*)&ip_net2;
    int pos = 0;
    for (int j = 0; j < 4; j++) {
        if (ip[j] >= 100) ipstr[pos++] = '0' + ip[j]/100;
        if (ip[j] >= 10) ipstr[pos++] = '0' + (ip[j]/10)%10;
        ipstr[pos++] = '0' + ip[j]%10;
        if (j < 3) ipstr[pos++] = '.';
    }
    ipstr[pos] = 0;
    vga_puts(ipstr);
    vga_puts("\n");

    /* V0.13.33: 网络连接通知 */
    {
        extern int hs_notification_send(const char *title, const char *body, int urgency, int timeout);
        extern int l10n64_get_lang(void);
        char nb[64];
        if (l10n64_get_lang()) {
            strcpy(nb, "IP: ");
            strcpy(nb + strlen(nb), ipstr);
            hs_notification_send("Network connected", nb, 0, 6000);
        } else {
            strcpy(nb, "IP: ");
            strcpy(nb + strlen(nb), ipstr);
            hs_notification_send("Network connected", nb, 0, 6000);
        }
    }

    return 0;
}

/* V0.13.33: 查询 DHCP 是否已获取地址(供桌面层检查) */
int dhcp64_is_bound(void)
{
    return (dhcp_state == DHCP_STATE_BOUND) ? 1 : 0;
}

/* V0.13.38: 获取 DHCP 分配的 IP (网络字节序转主机序) */
uint32_t dhcp64_get_ip(void)
{
    return dhcp_offered_ip;
}

/* DHCP状态机轮询（应在主循环中调用） */
int dhcp64_poll(void)
{
    if (dhcp_udp_sock < 0) {
        dhcp_udp_sock = udp64_bind(DHCP_CLIENT_PORT);
        if (dhcp_udp_sock < 0) return -1;
    }

    uint8_t buffer[1500];
    uint32_t src_ip;
    uint16_t src_port;

    switch (dhcp_state) {
        case DHCP_STATE_INIT:
            vga_puts("[DHCP64] Sending Discover...\n");
            dhcp_send_discover();
            dhcp_state = DHCP_STATE_SELECTING;
            break;

        case DHCP_STATE_SELECTING: {
            int len = udp64_recv(dhcp_udp_sock, buffer, sizeof(buffer), &src_ip, &src_port);
            if (len > 0 && src_port == DHCP_SERVER_PORT) {
                /* 检查消息类型 */
                dhcp_packet_t *dhcp = (dhcp_packet_t *)buffer;
                if (len >= 240 && ntohl32(dhcp->xid) == dhcp_xid) {
                    /* 查找DHCP消息类型选项 */
                    const uint8_t *opts = dhcp->options;
                    int i = 0;
                    while (i < 308) {
                        if (opts[i] == DHCP_OPT_END) break;
                        if (opts[i] == DHCP_OPT_PAD) { i++; continue; }
                        if (opts[i] == DHCP_OPT_DHCP_MSG && opts[i+1] == 1) {
                            if (opts[i+2] == DHCP_OFFER) {
                                dhcp_handle_offer(buffer, len);
                                dhcp_send_request(dhcp_offered_ip, dhcp_server_ip);
                                dhcp_state = DHCP_STATE_REQUESTING;
                                vga_puts("[DHCP64] Sending Request...\n");
                            }
                            break;
                        }
                        i += 2 + opts[i+1];
                    }
                }
            }
            break;
        }

        case DHCP_STATE_REQUESTING: {
            int len = udp64_recv(dhcp_udp_sock, buffer, sizeof(buffer), &src_ip, &src_port);
            if (len > 0 && src_port == DHCP_SERVER_PORT) {
                dhcp_packet_t *dhcp = (dhcp_packet_t *)buffer;
                if (len >= 240 && ntohl32(dhcp->xid) == dhcp_xid) {
                    const uint8_t *opts = dhcp->options;
                    int i = 0;
                    while (i < 308) {
                        if (opts[i] == DHCP_OPT_END) break;
                        if (opts[i] == DHCP_OPT_PAD) { i++; continue; }
                        if (opts[i] == DHCP_OPT_DHCP_MSG && opts[i+1] == 1) {
                            if (opts[i+2] == DHCP_ACK) {
                                dhcp_handle_ack(buffer, len);
                                return 1;  /* 成功 */
                            } else if (opts[i+2] == DHCP_NAK) {
                                dhcp_state = DHCP_STATE_INIT;
                                vga_puts("[DHCP64] NAK received, restarting\n");
                            }
                            break;
                        }
                        i += 2 + opts[i+1];
                    }
                }
            }
            break;
        }

        case DHCP_STATE_BOUND:
            return 1;  /* 已绑定 */
    }

    return 0;  /* 进行中 */
}

/* 获取DHCP分配的DNS服务器 */
uint32_t dhcp64_get_dns(void)
{
    return dhcp_dns;
}

/* 获取DHCP状态 */
int dhcp64_get_state(void)
{
    return dhcp_state;
}
