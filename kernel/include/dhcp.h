#ifndef DHCP_H
#define DHCP_H

#include <stdint.h>

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DHCP_MAX_OPTIONS 312
#define DHCP_MAGIC_COOKIE 0x63825363

/* DHCP消息类型 */
#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_DECLINE  4
#define DHCP_ACK      5
#define DHCP_NAK      6
#define DHCP_RELEASE  7
#define DHCP_INFORM   8

/* DHCP选项类型 */
#define DHCP_OPT_PAD              0
#define DHCP_OPT_SUBNET_MASK      1
#define DHCP_OPT_ROUTER           3
#define DHCP_OPT_DNS_SERVER       6
#define DHCP_OPT_HOST_NAME        12
#define DHCP_OPT_REQUESTED_IP     50
#define DHCP_OPT_LEASE_TIME       51
#define DHCP_OPT_MESSAGE_TYPE     53
#define DHCP_OPT_SERVER_ID        54
#define DHCP_OPT_PARAM_REQUEST    55
#define DHCP_OPT_END              255

/* DHCP消息 */
typedef struct {
    uint8_t  op;             /* 操作码：1=请求, 2=回复 */
    uint8_t  htype;          /* 硬件类型：1=以太网 */
    uint8_t  hlen;           /* 硬件地址长度：6 */
    uint8_t  hops;           /* 跳数 */
    uint32_t xid;            /* 事务ID */
    uint16_t secs;           /* 秒数 */
    uint16_t flags;          /* 标志 */
    uint32_t ciaddr;         /* 客户端IP */
    uint32_t yiaddr;         /* 你的IP */
    uint32_t siaddr;         /* 服务器IP */
    uint32_t giaddr;         /* 网关IP */
    uint8_t  chaddr[16];     /* 客户端硬件地址 */
    uint8_t  sname[64];      /* 服务器名称 */
    uint8_t  file[128];      /* 引导文件名 */
    uint32_t magic_cookie;   /* 魔术字 */
    uint8_t  options[DHCP_MAX_OPTIONS]; /* 选项 */
} __attribute__((packed)) hepu_dhcp_msg_t;

/* DHCP状态 */
typedef enum {
    DHCP_STATE_INIT = 0,
    DHCP_STATE_SELECTING = 1,
    DHCP_STATE_REQUESTING = 2,
    DHCP_STATE_BOUND = 3,
    DHCP_STATE_RENEWING = 4,
    DHCP_STATE_REBINDING = 5
} hepu_dhcp_state_t;

/* DHCP配置结果 */
typedef struct {
    uint32_t ip_addr;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns1;
    uint32_t dns2;
    uint32_t server_id;
    uint32_t lease_time;
    char     server_name[64];
    int      acquired;
} hepu_dhcp_config_t;

void dhcp_init(void);
int dhcp_start(const char *interface);
int dhcp_stop(const char *interface);
int dhcp_get_config(const char *interface, hepu_dhcp_config_t *config);
int dhcp_renew(const char *interface);
int dhcp_release(const char *interface);
int dhcp_get_state(const char *interface, hepu_dhcp_state_t *state);
int dhcp_process_packet(const uint8_t *data, uint32_t length);

#endif /* DHCP_H */
