/* ============================================================================
 * 赫普OS - DHCP客户端
 * 动态主机配置协议，自动获取IP地址
 * ========================================================================== */

#include <stdint.h>
#include "dhcp.h"
#include "eth.h"
#include "vga.h"
#include "string.h"
extern void serial_puts(uint16_t port, const char *s);

#define DHCP_MAX_INTERFACES 2

typedef struct {
    char interface[16];
    hepu_dhcp_state_t state;
    hepu_dhcp_config_t config;
    uint32_t xid;
    int enabled;
} dhcp_interface_t;

static dhcp_interface_t dhcp_ifaces[DHCP_MAX_INTERFACES];
static int dhcp_initialized = 0;

/* 初始化DHCP */
void dhcp_init(void)
{
    memset(dhcp_ifaces, 0, sizeof(dhcp_ifaces));
    dhcp_initialized = 1;
    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[DHCP] client init done\n");
}

/* 查找接口 */
static dhcp_interface_t *dhcp_find_iface(const char *interface)
{
    if (!interface) return NULL;
    for (int i = 0; i < DHCP_MAX_INTERFACES; i++) {
        if (dhcp_ifaces[i].enabled &&
            strcmp(dhcp_ifaces[i].interface, interface) == 0) {
            return &dhcp_ifaces[i];
        }
    }
    return NULL;
}

/* 生成事务ID */
static uint32_t dhcp_generate_xid(void)
{
    static uint32_t counter = 0x12345678;
    counter += 0x9E3779B9;
    return counter;
}

/* 构造DHCP发现包 */
static int dhcp_build_discover(hepu_dhcp_msg_t *msg, uint8_t *mac)
{
    memset(msg, 0, sizeof(hepu_dhcp_msg_t));
    msg->op = 1;  /* 请求 */
    msg->htype = 1;  /* 以太网 */
    msg->hlen = 6;
    msg->xid = dhcp_generate_xid();
    msg->flags = 0x8000;  /* 广播 */
    memcpy(msg->chaddr, mac, 6);
    msg->magic_cookie = DHCP_MAGIC_COOKIE;

    /* 选项：消息类型=Discover */
    int opt_idx = 0;
    msg->options[opt_idx++] = DHCP_OPT_MESSAGE_TYPE;
    msg->options[opt_idx++] = 1;
    msg->options[opt_idx++] = DHCP_DISCOVER;

    /* 参数请求列表 */
    msg->options[opt_idx++] = DHCP_OPT_PARAM_REQUEST;
    msg->options[opt_idx++] = 4;
    msg->options[opt_idx++] = DHCP_OPT_SUBNET_MASK;
    msg->options[opt_idx++] = DHCP_OPT_ROUTER;
    msg->options[opt_idx++] = DHCP_OPT_DNS_SERVER;
    msg->options[opt_idx++] = DHCP_OPT_LEASE_TIME;

    msg->options[opt_idx++] = DHCP_OPT_END;
    return 0;
}

/* V8.01: 解析DHCP选项 */
static void dhcp_parse_options(hepu_dhcp_msg_t *msg, hepu_dhcp_config_t *config,
                                uint8_t *msg_type, uint32_t *server_id)
{
    uint8_t *opts = msg->options;
    int idx = 0;

    while (idx < 312 && opts[idx] != DHCP_OPT_END) {
        uint8_t opt_type = opts[idx];
        if (opt_type == DHCP_OPT_PAD) {
            idx++;
            continue;
        }
        uint8_t opt_len = opts[idx + 1];
        uint8_t *opt_data = &opts[idx + 2];

        switch (opt_type) {
            case DHCP_OPT_MESSAGE_TYPE:
                if (opt_len >= 1) *msg_type = opt_data[0];
                break;
            case DHCP_OPT_SUBNET_MASK:
                if (opt_len >= 4) {
                    config->netmask = (opt_data[0] << 24) | (opt_data[1] << 16) |
                                      (opt_data[2] << 8) | opt_data[3];
                }
                break;
            case DHCP_OPT_ROUTER:
                if (opt_len >= 4) {
                    config->gateway = (opt_data[0] << 24) | (opt_data[1] << 16) |
                                      (opt_data[2] << 8) | opt_data[3];
                }
                break;
            case DHCP_OPT_DNS_SERVER:
                if (opt_len >= 4) {
                    config->dns1 = (opt_data[0] << 24) | (opt_data[1] << 16) |
                                   (opt_data[2] << 8) | opt_data[3];
                }
                if (opt_len >= 8) {
                    config->dns2 = (opt_data[4] << 24) | (opt_data[5] << 16) |
                                   (opt_data[6] << 8) | opt_data[7];
                }
                break;
            case DHCP_OPT_LEASE_TIME:
                if (opt_len >= 4) {
                    config->lease_time = (opt_data[0] << 24) | (opt_data[1] << 16) |
                                         (opt_data[2] << 8) | opt_data[3];
                }
                break;
            case DHCP_OPT_SERVER_ID:
                if (opt_len >= 4 && server_id) {
                    *server_id = (opt_data[0] << 24) | (opt_data[1] << 16) |
                                 (opt_data[2] << 8) | opt_data[3];
                }
                break;
        }
        idx += 2 + opt_len;
    }
}

/* 构造DHCP Request包 */
static int dhcp_build_request(hepu_dhcp_msg_t *msg, uint8_t *mac,
                               uint32_t requested_ip, uint32_t server_id,
                               uint32_t xid)
{
    memset(msg, 0, sizeof(hepu_dhcp_msg_t));
    msg->op = 1;
    msg->htype = 1;
    msg->hlen = 6;
    msg->xid = xid;
    msg->flags = 0x8000;
    memcpy(msg->chaddr, mac, 6);
    msg->magic_cookie = DHCP_MAGIC_COOKIE;

    int opt_idx = 0;
    /* 消息类型=Request */
    msg->options[opt_idx++] = DHCP_OPT_MESSAGE_TYPE;
    msg->options[opt_idx++] = 1;
    msg->options[opt_idx++] = 3;  /* DHCP Request */

    /* 请求的IP地址 */
    msg->options[opt_idx++] = DHCP_OPT_REQUESTED_IP;
    msg->options[opt_idx++] = 4;
    msg->options[opt_idx++] = (requested_ip >> 24) & 0xFF;
    msg->options[opt_idx++] = (requested_ip >> 16) & 0xFF;
    msg->options[opt_idx++] = (requested_ip >> 8) & 0xFF;
    msg->options[opt_idx++] = requested_ip & 0xFF;

    /* DHCP服务器标识 */
    msg->options[opt_idx++] = DHCP_OPT_SERVER_ID;
    msg->options[opt_idx++] = 4;
    msg->options[opt_idx++] = (server_id >> 24) & 0xFF;
    msg->options[opt_idx++] = (server_id >> 16) & 0xFF;
    msg->options[opt_idx++] = (server_id >> 8) & 0xFF;
    msg->options[opt_idx++] = server_id & 0xFF;

    msg->options[opt_idx++] = DHCP_OPT_END;
    return 0;
}

/* 启动DHCP */
int dhcp_start(const char *interface)
{
    if (!dhcp_initialized || !interface) return -1;

    /* 查找空闲接口槽 */
    dhcp_interface_t *iface = NULL;
    for (int i = 0; i < DHCP_MAX_INTERFACES; i++) {
        if (!dhcp_ifaces[i].enabled) {
            iface = &dhcp_ifaces[i];
            break;
        }
    }
    if (!iface) return -1;

    strncpy(iface->interface, interface, 15);
    iface->state = DHCP_STATE_INIT;
    iface->enabled = 1;
    iface->xid = dhcp_generate_xid();

    /* 获取MAC */
    uint8_t mac[6];
    eth_get_mac(interface, mac);

    /* V8.01: 完整DHCP状态机 */
    extern int udp_socket(uint16_t local_port);
    extern int udp_sendto(int sockfd, uint32_t dest_ip, uint16_t dest_port,
                          const uint8_t *data, uint32_t data_len);
    extern int udp_recvfrom(int sockfd, uint8_t *buf, uint32_t max_len,
                            uint32_t *src_ip, uint16_t *src_port);
    extern void udp_close(int sockfd);
    extern void net_poll(void);
    extern uint32_t pit_get_ticks(void);

    int sock = udp_socket(68);
    if (sock < 0) {
        vga_puts("[DHCP] UDP socket create failed\n");
        return -1;
    }

    uint8_t reply[512];
    uint32_t offered_ip = 0;
    uint32_t server_id = 0;
    int got_offer = 0;
    int got_ack = 0;

    /* === 阶段1: 发送Discover，等待Offer === */
    hepu_dhcp_msg_t discover;
    dhcp_build_discover(&discover, mac);
    iface->state = DHCP_STATE_SELECTING;
    vga_printf("[DHCP] send Discover(xid=0x%x)\n", iface->xid);
    udp_sendto(sock, 0xFFFFFFFF, 67, (uint8_t *)&discover, sizeof(discover));

    uint32_t start_time = pit_get_ticks();
    while (pit_get_ticks() - start_time < 3000) {
        net_poll();
        int len = udp_recvfrom(sock, reply, sizeof(reply), NULL, NULL);
        if (len > 0) {
            hepu_dhcp_msg_t *offer = (hepu_dhcp_msg_t *)reply;
            if (offer->xid == iface->xid && offer->op == 2) {
                uint8_t msg_type = 0;
                dhcp_parse_options(offer, &iface->config, &msg_type, &server_id);
                if (msg_type == 2) {  /* DHCP Offer */
                    offered_ip = offer->yiaddr;
                    got_offer = 1;
                    vga_printf("[DHCP] got Offer: IP=%d.%d.%d.%d, server=%d.%d.%d.%d\n",
                               (offered_ip >> 24) & 0xFF, (offered_ip >> 16) & 0xFF,
                               (offered_ip >> 8) & 0xFF, offered_ip & 0xFF,
                               (server_id >> 24) & 0xFF, (server_id >> 16) & 0xFF,
                               (server_id >> 8) & 0xFF, server_id & 0xFF);
                    break;
                }
            }
        }
    }

    /* === 阶段2: 发送Request，等待ACK === */
    if (got_offer) {
        hepu_dhcp_msg_t request;
        dhcp_build_request(&request, mac, offered_ip, server_id, iface->xid);
        iface->state = DHCP_STATE_REQUESTING;
        vga_puts("[DHCP] send Request\n");
        udp_sendto(sock, 0xFFFFFFFF, 67, (uint8_t *)&request, sizeof(request));

        start_time = pit_get_ticks();
        while (pit_get_ticks() - start_time < 3000) {
            net_poll();
            int len = udp_recvfrom(sock, reply, sizeof(reply), NULL, NULL);
            if (len > 0) {
                hepu_dhcp_msg_t *ack = (hepu_dhcp_msg_t *)reply;
                if (ack->xid == iface->xid && ack->op == 2) {
                    uint8_t msg_type = 0;
                    dhcp_parse_options(ack, &iface->config, &msg_type, NULL);
                    if (msg_type == 5) {  /* DHCP ACK */
                        iface->config.ip_addr = ack->yiaddr;
                        iface->config.acquired = 1;
                        iface->state = DHCP_STATE_BOUND;
                        got_ack = 1;
                        vga_printf("[DHCP] got ACK: IP=%d.%d.%d.%d\n",
                                   (ack->yiaddr >> 24) & 0xFF,
                                   (ack->yiaddr >> 16) & 0xFF,
                                   (ack->yiaddr >> 8) & 0xFF,
                                   ack->yiaddr & 0xFF);
                        break;
                    }
                }
            }
        }
    }

    udp_close(sock);

    /* === 回退处理 === */
    if (!got_ack) {
        if (got_offer) {
            /* 收到Offer但没收到ACK，使用Offer中的配置 */
            iface->config.ip_addr = offered_ip;
            iface->config.acquired = 1;
            iface->state = DHCP_STATE_BOUND;
            vga_puts("[DHCP] no ACK, using Offer config\n");
        } else {
            vga_puts("[DHCP] timeout, using fallback IP\n");
            iface->config.ip_addr = (192 << 24) | (168 << 16) | (1 << 8) | 100;
            iface->config.netmask = 0xFFFFFF00;
            iface->config.gateway = (192 << 24) | (168 << 16) | (1 << 8) | 1;
            iface->config.dns1 = (8 << 24) | (8 << 16) | (8 << 8) | 8;
            iface->config.acquired = 1;
            iface->state = DHCP_STATE_BOUND;
        }
    }

    /* 设置到网络接口和IP层 */
    eth_set_ip(interface, iface->config.ip_addr,
               iface->config.netmask, iface->config.gateway);
    extern void ip_set_config(uint32_t ip, uint32_t mask, uint32_t gw);
    ip_set_config(iface->config.ip_addr, iface->config.netmask, iface->config.gateway);

    vga_printf("[DHCP] IP=%d.%d.%d.%d Mask=%d.%d.%d.%d GW=%d.%d.%d.%d\n",
               (iface->config.ip_addr >> 24) & 0xFF,
               (iface->config.ip_addr >> 16) & 0xFF,
               (iface->config.ip_addr >> 8) & 0xFF,
               iface->config.ip_addr & 0xFF,
               (iface->config.netmask >> 24) & 0xFF,
               (iface->config.netmask >> 16) & 0xFF,
               (iface->config.netmask >> 8) & 0xFF,
               iface->config.netmask & 0xFF,
               (iface->config.gateway >> 24) & 0xFF,
               (iface->config.gateway >> 16) & 0xFF,
               (iface->config.gateway >> 8) & 0xFF,
               iface->config.gateway & 0xFF);
    return 0;
}

/* 停止DHCP */
int dhcp_stop(const char *interface)
{
    dhcp_interface_t *iface = dhcp_find_iface(interface);
    if (!iface) return -1;

    iface->enabled = 0;
    iface->state = DHCP_STATE_INIT;
    return 0;
}

/* 获取配置 */
int dhcp_get_config(const char *interface, hepu_dhcp_config_t *config)
{
    dhcp_interface_t *iface = dhcp_find_iface(interface);
    if (!iface || !config) return -1;
    *config = iface->config;
    return 0;
}

/* 续租 */
int dhcp_renew(const char *interface)
{
    dhcp_interface_t *iface = dhcp_find_iface(interface);
    if (!iface) return -1;

    iface->state = DHCP_STATE_RENEWING;
    vga_printf("[DHCP] renew lease %s\n", interface);
    iface->state = DHCP_STATE_BOUND;
    return 0;
}

/* 释放 */
int dhcp_release(const char *interface)
{
    dhcp_interface_t *iface = dhcp_find_iface(interface);
    if (!iface) return -1;

    iface->state = DHCP_STATE_INIT;
    iface->config.acquired = 0;
    vga_printf("[DHCP] release %s IP\n", interface);
    return 0;
}

/* 获取状态 */
int dhcp_get_state(const char *interface, hepu_dhcp_state_t *state)
{
    dhcp_interface_t *iface = dhcp_find_iface(interface);
    if (!iface || !state) return -1;
    *state = iface->state;
    return 0;
}

/* 处理DHCP包 */
int dhcp_process_packet(const uint8_t *data, uint32_t length)
{
    if (!data || length < sizeof(hepu_dhcp_msg_t)) return -1;

    const hepu_dhcp_msg_t *msg = (const hepu_dhcp_msg_t *)data;
    if (msg->magic_cookie != DHCP_MAGIC_COOKIE) return -1;

    vga_printf("[DHCP] recv msg, op=%d, xid=0x%x\n", msg->op, msg->xid);
    return 0;
}
