/* ============================================================================
 * 赫普OS 64位 - 网络协议栈主入口
 * 负责数据包接收分发和协议栈初始化
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 以太网类型 */
#define ETH_TYPE_IP   0x0800
#define ETH_TYPE_ARP  0x0806

/* 以太网头 */
typedef struct {
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    uint16_t type;
} __attribute__((packed)) eth_header_t;

extern void vga_puts(const char *s);
extern int eth64_recv(uint8_t *buffer, uint32_t max_length);
extern void arp64_handle(const uint8_t *packet, int length);
extern void ip64_handle(const uint8_t *frame, int length);

/* 协议栈初始化 */
extern void eth64_init(void);
extern void arp64_init(void);
extern void ip64_init(void);
extern void icmp64_init(void);
extern void udp64_init(void);
extern void tcp64_init(void);

static uint16_t ntohs16(uint16_t v) { return (v >> 8) | (v << 8); }

static int net_initialized = 0;
static uint64_t net_rx_packets = 0;
static uint64_t net_tx_packets = 0;

/* 初始化整个网络协议栈 */
void net64_init(void)
{
    vga_puts("[NET64] Initializing network stack...\n");

    eth64_init();
    arp64_init();
    ip64_init();
    icmp64_init();
    udp64_init();
    tcp64_init();

    net_initialized = 1;
    vga_puts("[OK] Network stack initialized (ETH/ARP/IP/ICMP/UDP/TCP)\n");
}

/* 网络轮询（处理接收数据包） */
void net64_poll(void)
{
    if (!net_initialized) return;

    uint8_t buffer[1514];
    int len = eth64_recv(buffer, sizeof(buffer));

    if (len <= 0) return;

    net_rx_packets++;

    if (len < 14) return;

    eth_header_t *eth = (eth_header_t *)buffer;
    uint16_t type = ntohs16(eth->type);

    switch (type) {
        case ETH_TYPE_ARP:
            arp64_handle(buffer + 14, len - 14);
            break;
        case ETH_TYPE_IP:
            ip64_handle(buffer, len);
            break;
    }
}

/* 获取网络统计 */
void net64_get_stats(uint64_t *rx, uint64_t *tx)
{
    if (rx) *rx = net_rx_packets;
    if (tx) *tx = net_tx_packets;
}
