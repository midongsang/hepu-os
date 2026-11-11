/* ============================================================================
 * 赫普OS 64位 - 以太网驱动接口（eth0）
 * 连接RTL8139驱动与TCP/IP协议栈
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

#define ETH_MAC_LEN 6
#define ETH_FRAME_MAX 1514
#define ETH_MAX_DEVICES 2

typedef struct {
    char name[16];
    uint8_t mac[ETH_MAC_LEN];
    uint32_t ip_addr;
    uint32_t netmask;
    uint32_t gateway;
    int link_up;
    int mtu;
    uint64_t tx_packets;
    uint64_t tx_bytes;
    uint64_t rx_packets;
    uint64_t rx_bytes;
    int initialized;
} eth_device_t;

static eth_device_t eth_devices[ETH_MAX_DEVICES];
static int eth_initialized = 0;

/* RTL8139驱动接口 */
extern int e1000_send(const uint8_t *data, uint32_t length);
extern int e1000_recv(uint8_t *buffer, uint32_t max_len);
extern int e1000_get_mac(uint8_t *mac);

/* 串口输出 */
extern void vga_puts(const char *s);

/* V0.13.65: e1000 初始化完成后回写真实 MAC (eth64_init 在 e1000_init 前调用,
 * 当时 e1000 未初始化导致拿到全 0 MAC, 单播应答会被网卡硬件过滤) */
void eth64_update_mac(void)
{
    if (!eth_initialized) return;
    uint8_t mac[6];
    if (e1000_get_mac(mac) != 0) return;
    int valid = 0;
    for (int i = 0; i < 6; i++) if (mac[i] != 0) valid = 1;
    if (!valid) return;
    for (int i = 0; i < 6; i++) eth_devices[0].mac[i] = mac[i];
    vga_puts("[ETH64] MAC updated from e1000: ");
    for (int i = 0; i < 6; i++) { vga_put_hex(mac[i]); if (i < 5) vga_puts(":"); }
    vga_puts("\n");
}

/* 初始化以太网驱动 */
void eth64_init(void)
{
    /* V0.13.9: 幂等保护 — network_hal 会二次调用本函数, 防止重置 IP/gateway */
    if (eth_initialized) return;

    vga_puts("[ETH64] Initializing ethernet driver...\n");

    for (int i = 0; i < ETH_MAX_DEVICES; i++) {
        eth_devices[i].initialized = 0;
    }

    /* 创建eth0设备 */
    eth_device_t *dev = &eth_devices[0];
    dev->initialized = 1;
    dev->name[0] = 'e'; dev->name[1] = 't'; dev->name[2] = 'h'; dev->name[3] = '0'; dev->name[4] = 0;
    dev->ip_addr = 0;
    dev->netmask = 0xFFFFFF00;
    dev->gateway = 0;
    dev->link_up = 0;
    dev->mtu = 1500;
    dev->tx_packets = 0;
    dev->tx_bytes = 0;
    dev->rx_packets = 0;
    dev->rx_bytes = 0;

    /* 从RTL8139获取MAC地址 */
    uint8_t mac[6];
    e1000_get_mac(mac);
    /* 检查MAC是否有效（非全0） */
    int mac_valid = 0;
    for (int i = 0; i < 6; i++) if (mac[i] != 0) mac_valid = 1;
    if (mac_valid) {
        for (int i = 0; i < 6; i++) dev->mac[i] = mac[i];
        vga_puts("[ETH64] MAC from RTL8139: ");
    } else {
        /* 默认MAC */
        dev->mac[0] = 0x00; dev->mac[1] = 0x1A; dev->mac[2] = 0x36;
        dev->mac[3] = 0x5D; dev->mac[4] = 0xA7; dev->mac[5] = 0xC9;
        vga_puts("[ETH64] Using default MAC: ");
    }

    /* 输出MAC地址 */
    char macstr[18];
    const char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < 6; i++) {
        macstr[i*3] = hex[dev->mac[i] >> 4];
        macstr[i*3+1] = hex[dev->mac[i] & 0xF];
        macstr[i*3+2] = (i < 5) ? ':' : 0;
    }
    vga_puts(macstr);
    vga_puts("\n");

    eth_initialized = 1;
    vga_puts("[OK] Ethernet driver initialized\n");
}

/* 查找设备 */
/* V0.55.1: 按名字查找网卡设备(预留, 当前接口选择走默认网卡) */
static __attribute__((unused)) eth_device_t *eth64_find(const char *name)
{
    if (!name) return &eth_devices[0];
    for (int i = 0; i < ETH_MAX_DEVICES; i++) {
        if (eth_devices[i].initialized) {
            int match = 1;
            for (int j = 0; j < 16; j++) {
                if (eth_devices[i].name[j] != name[j]) { match = 0; break; }
                if (name[j] == 0) break;
            }
            if (match) return &eth_devices[i];
        }
    }
    return &eth_devices[0];
}

/* 发送以太网帧 */
int eth64_send(const uint8_t *data, uint32_t length)
{
    if (!eth_initialized || !data || length == 0) return -1;
    if (length > ETH_FRAME_MAX) return -1;

    int ret = e1000_send(data, length);
    if (ret > 0) {
        eth_devices[0].tx_packets++;
        eth_devices[0].tx_bytes += ret;
        return ret;
    }
    return -1;
}

/* 接收以太网帧 */
int eth64_recv(uint8_t *buffer, uint32_t max_length)
{
    if (!eth_initialized || !buffer || max_length == 0) return 0;

    int ret = e1000_recv(buffer, max_length);
    if (ret > 0) {
        eth_devices[0].rx_packets++;
        eth_devices[0].rx_bytes += ret;
    }
    return ret;
}

/* 获取网络字节统计 (透出给 net64/任务管理器) */
int eth64_get_bytes(uint64_t *rx, uint64_t *tx)
{
    if (!rx || !tx || !eth_initialized) return -1;
    *rx = eth_devices[0].rx_bytes;
    *tx = eth_devices[0].tx_bytes;
    return 0;
}

/* 获取MAC地址 */
int eth64_get_mac(uint8_t *mac)
{
    if (!mac || !eth_initialized) return -1;
    for (int i = 0; i < 6; i++) mac[i] = eth_devices[0].mac[i];
    return 0;
}

/* 设置IP地址 */
int eth64_set_ip(uint32_t ip, uint32_t netmask, uint32_t gateway)
{
    if (!eth_initialized) return -1;
    eth_devices[0].ip_addr = ip;
    eth_devices[0].netmask = netmask;
    eth_devices[0].gateway = gateway;
    eth_devices[0].link_up = 1;
    return 0;
}

/* 获取IP地址 */
uint32_t eth64_get_ip(void)
{
    return eth_initialized ? eth_devices[0].ip_addr : 0;
}

/* 获取网关 */
uint32_t eth64_get_gateway(void)
{
    return eth_initialized ? eth_devices[0].gateway : 0;
}

/* 获取子网掩码 */
uint32_t eth64_get_netmask(void)
{
    return eth_initialized ? eth_devices[0].netmask : 0xFFFFFF00;
}
