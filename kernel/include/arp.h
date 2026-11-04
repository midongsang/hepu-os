#ifndef ARP_H
#define ARP_H

#include <stdint.h>

#define ARP_HW_TYPE_ETHERNET 1
#define ARP_PROTO_TYPE_IPV4  0x0800
#define ARP_HW_ADDR_LEN      6
#define ARP_PROTO_ADDR_LEN   4
#define ARP_OP_REQUEST       1
#define ARP_OP_REPLY         2
#define ARP_CACHE_SIZE       32
#define ARP_CACHE_TIMEOUT    300  /* 秒 */

/* ARP数据包 */
typedef struct {
    uint16_t hw_type;       /* 硬件类型 */
    uint16_t proto_type;    /* 协议类型 */
    uint8_t  hw_addr_len;   /* 硬件地址长度 */
    uint8_t  proto_addr_len;/* 协议地址长度 */
    uint16_t opcode;        /* 操作码 */
    uint8_t  sender_hw[6];  /* 发送方硬件地址 */
    uint32_t sender_ip;     /* 发送方IP地址 */
    uint8_t  target_hw[6];  /* 目标硬件地址 */
    uint32_t target_ip;     /* 目标IP地址 */
} __attribute__((packed)) hepu_arp_packet_t;

/* ARP缓存条目 */
typedef struct {
    uint32_t ip_addr;
    uint8_t  mac_addr[6];
    uint32_t timestamp;
    int      valid;
} hepu_arp_cache_entry_t;

void arp_init(void);
int arp_send_request(uint32_t target_ip);
int arp_send_reply(uint32_t target_ip, const uint8_t *target_mac);
int arp_process_packet(const uint8_t *data, uint32_t length);
int arp_lookup(uint32_t ip_addr, uint8_t *mac_addr);
int arp_add_entry(uint32_t ip_addr, const uint8_t *mac_addr);
int arp_get_cache(hepu_arp_cache_entry_t *cache, int max);
void arp_tick(void);

#endif /* ARP_H */
