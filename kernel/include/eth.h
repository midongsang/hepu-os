#ifndef ETH_H
#define ETH_H

#include <stdint.h>

#define ETH_MAC_ADDR_LEN 6
#define ETH_FRAME_MAX    1518
#define ETH_FRAME_MIN    64
#define ETH_HEADER_LEN   14

/* 以太网帧头 */
typedef struct {
    uint8_t  dest[ETH_MAC_ADDR_LEN];
    uint8_t  src[ETH_MAC_ADDR_LEN];
    uint16_t type;
} __attribute__((packed)) hepu_eth_header_t;

/* 以太网类型 */
#define ETH_TYPE_IPV4   0x0800
#define ETH_TYPE_ARP    0x0806
#define ETH_TYPE_IPV6   0x86DD

/* 网络设备状态 */
typedef enum {
    ETH_LINK_DOWN = 0,
    ETH_LINK_UP   = 1,
    ETH_LINK_FULL_DUPLEX = 2,
    ETH_LINK_HALF_DUPLEX = 3
} hepu_eth_link_status_t;

/* 网络设备统计 */
typedef struct {
    uint64_t tx_packets;
    uint64_t rx_packets;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint64_t tx_errors;
    uint64_t rx_errors;
    uint64_t collisions;
} hepu_eth_stats_t;

/* 网络设备 */
typedef struct {
    char     name[16];
    uint8_t  mac[ETH_MAC_ADDR_LEN];
    uint32_t ip_addr;
    uint32_t netmask;
    uint32_t gateway;
    hepu_eth_link_status_t link_status;
    int      speed_mbps;
    int      mtu;
    hepu_eth_stats_t stats;
    int      initialized;
} hepu_eth_device_t;

void eth_init(void);
int eth_open(const char *name);
int eth_close(const char *name);
int eth_send(const char *name, const uint8_t *data, uint32_t length);
int eth_receive(const char *name, uint8_t *buffer, uint32_t max_length);
int eth_get_mac(const char *name, uint8_t *mac);
int eth_set_mac(const char *name, const uint8_t *mac);
int eth_get_link_status(const char *name, hepu_eth_link_status_t *status);
int eth_get_stats(const char *name, hepu_eth_stats_t *stats);
int eth_list_devices(hepu_eth_device_t *devices, int max);
int eth_set_ip(const char *name, uint32_t ip, uint32_t netmask, uint32_t gateway);

#endif /* ETH_H */
