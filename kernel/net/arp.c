/* ============================================================================
 * 赫普OS - ARP地址解析协议
 * 实现IPv4到MAC地址的解析
 * ========================================================================== */

#include <stdint.h>
#include "arp.h"
#include "eth.h"
#include "vga.h"
#include "string.h"
extern void serial_puts(uint16_t port, const char *s);

static hepu_arp_cache_entry_t arp_cache[ARP_CACHE_SIZE];
static uint32_t arp_tick_count = 0;
static int arp_initialized = 0;

/* 初始化ARP */
void arp_init(void)
{
    memset(arp_cache, 0, sizeof(arp_cache));
    arp_tick_count = 0;
    arp_initialized = 1;
    vga_puts("[ARP] addr parse proto init \n");
    vga_printf("[ARP] cache size: %d entries, timeout: %d s \n", ARP_CACHE_SIZE, ARP_CACHE_TIMEOUT);
}

/* 发送ARP请求 */
int arp_send_request(uint32_t target_ip)
{
    if (!arp_initialized) return -1;

    uint8_t packet[sizeof(hepu_arp_packet_t) + 14];  /* 以太网头+ARP */
    memset(packet, 0, sizeof(packet));

    /* 构造以太网头 */
    uint8_t *eth_hdr = packet;
    memset(eth_hdr, 0xFF, 6);  /* 广播地址 */
    eth_hdr[12] = 0x08; eth_hdr[13] = 0x06;  /* ARP类型 */

    /* 构造ARP包 */
    hepu_arp_packet_t *arp = (hepu_arp_packet_t *)(packet + 14);
    arp->hw_type = ARP_HW_TYPE_ETHERNET;
    arp->proto_type = ARP_PROTO_TYPE_IPV4;
    arp->hw_addr_len = ARP_HW_ADDR_LEN;
    arp->proto_addr_len = ARP_PROTO_ADDR_LEN;
    arp->opcode = ARP_OP_REQUEST;

    /* 获取本机MAC和IP */
    uint8_t local_mac[6];
    eth_get_mac("eth0", local_mac);
    memcpy(arp->sender_hw, local_mac, 6);
    arp->sender_ip = 0;  /* 待设置 */
    memset(arp->target_hw, 0, 6);
    arp->target_ip = target_ip;

    /* 发送 */
    eth_send("eth0", packet, sizeof(packet));
    vga_printf("[ARP] send request: ask IP %d.%d.%d.%d\n",
               (target_ip >> 24) & 0xFF, (target_ip >> 16) & 0xFF,
               (target_ip >> 8) & 0xFF, target_ip & 0xFF);
    return 0;
}

/* 发送ARP回复 */
int arp_send_reply(uint32_t target_ip, const uint8_t *target_mac)
{
    if (!arp_initialized || !target_mac) return -1;

    uint8_t packet[sizeof(hepu_arp_packet_t) + 14];
    memset(packet, 0, sizeof(packet));

    /* 以太网头 */
    uint8_t *eth_hdr = packet;
    memcpy(eth_hdr, target_mac, 6);
    eth_hdr[12] = 0x08; eth_hdr[13] = 0x06;

    /* ARP包 */
    hepu_arp_packet_t *arp = (hepu_arp_packet_t *)(packet + 14);
    arp->hw_type = ARP_HW_TYPE_ETHERNET;
    arp->proto_type = ARP_PROTO_TYPE_IPV4;
    arp->hw_addr_len = ARP_HW_ADDR_LEN;
    arp->proto_addr_len = ARP_PROTO_ADDR_LEN;
    arp->opcode = ARP_OP_REPLY;

    uint8_t local_mac[6];
    eth_get_mac("eth0", local_mac);
    memcpy(arp->sender_hw, local_mac, 6);
    arp->sender_ip = 0;
    memcpy(arp->target_hw, target_mac, 6);
    arp->target_ip = target_ip;

    eth_send("eth0", packet, sizeof(packet));
    return 0;
}

/* 处理ARP包 */
int arp_process_packet(const uint8_t *data, uint32_t length)
{
    if (!arp_initialized || !data || length < sizeof(hepu_arp_packet_t)) return -1;

    const hepu_arp_packet_t *arp = (const hepu_arp_packet_t *)data;

    if (arp->hw_type != ARP_HW_TYPE_ETHERNET ||
        arp->proto_type != ARP_PROTO_TYPE_IPV4) {
        return -1;
    }

    /* 添加到缓存 */
    arp_add_entry(arp->sender_ip, arp->sender_hw);

    /* 如果是请求，回复 */
    if (arp->opcode == ARP_OP_REQUEST) {
        vga_printf("[ARP] got request: %d.%d.%d.%d ask \n",
                   (arp->sender_ip >> 24) & 0xFF, (arp->sender_ip >> 16) & 0xFF,
                   (arp->sender_ip >> 8) & 0xFF, arp->sender_ip & 0xFF);
        /* arp_send_reply(arp->sender_ip, arp->sender_hw); */
    } else if (arp->opcode == ARP_OP_REPLY) {
        vga_printf("[ARP] got reply: %d.%d.%d.%d -> %02x:%02x:%02x:%02x:%02x:%02x\n",
                   (arp->sender_ip >> 24) & 0xFF, (arp->sender_ip >> 16) & 0xFF,
                   (arp->sender_ip >> 8) & 0xFF, arp->sender_ip & 0xFF,
                   arp->sender_hw[0], arp->sender_hw[1], arp->sender_hw[2],
                   arp->sender_hw[3], arp->sender_hw[4], arp->sender_hw[5]);
    }

    return 0;
}

/* 查找ARP缓存 */
int arp_lookup(uint32_t ip_addr, uint8_t *mac_addr)
{
    if (!mac_addr) return -1;

    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip_addr == ip_addr) {
            memcpy(mac_addr, arp_cache[i].mac_addr, 6);
            return 0;
        }
    }
    return -1;
}

/* 添加ARP条目 */
int arp_add_entry(uint32_t ip_addr, const uint8_t *mac_addr)
{
    if (!mac_addr) return -1;

    /* 查找已有条目 */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip_addr == ip_addr) {
            memcpy(arp_cache[i].mac_addr, mac_addr, 6);
            arp_cache[i].timestamp = arp_tick_count;
            return 0;
        }
    }

    /* 找空闲条目 */
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            arp_cache[i].ip_addr = ip_addr;
            memcpy(arp_cache[i].mac_addr, mac_addr, 6);
            arp_cache[i].timestamp = arp_tick_count;
            arp_cache[i].valid = 1;
            return 0;
        }
    }

    /* 替换最旧的条目 */
    int oldest = 0;
    for (int i = 1; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].timestamp < arp_cache[oldest].timestamp) {
            oldest = i;
        }
    }
    arp_cache[oldest].ip_addr = ip_addr;
    memcpy(arp_cache[oldest].mac_addr, mac_addr, 6);
    arp_cache[oldest].timestamp = arp_tick_count;
    arp_cache[oldest].valid = 1;
    return 0;
}

/* 获取缓存 */
int arp_get_cache(hepu_arp_cache_entry_t *cache, int max)
{
    if (!cache || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < ARP_CACHE_SIZE && count < max; i++) {
        if (arp_cache[i].valid) {
            cache[count++] = arp_cache[i];
        }
    }
    return count;
}

/* 定时处理（清理过期条目） */
void arp_tick(void)
{
    arp_tick_count++;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid &&
            (arp_tick_count - arp_cache[i].timestamp) > ARP_CACHE_TIMEOUT) {
            arp_cache[i].valid = 0;
        }
    }
}
