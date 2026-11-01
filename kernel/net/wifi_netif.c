/* ============================================================================
 * 赫普OS - WiFi网络接口集成（V8.70新增）
 * 802.11帧与以太网帧转换、netif注册、与TCP/IP栈集成
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../include/wifi.h"
#include "../include/ieee80211.h"
#include "../include/vga.h"
#include "../include/kheap.h"

/* 以太网帧头大小 */
#define ETH_HEADER_SIZE 14

/* 802.11数据帧头大小（QoS数据帧） */
#define WIFI_DATA_HEADER_SIZE 26

/* LLC/SNAP头大小 */
#define LLC_SNAP_SIZE 8

/* 网络接口状态 */
static uint8_t netif_is_up = 0;
static uint8_t wifi_netif_registered = 0;
static uint8_t wifi_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};

/* 统计 */
static uint32_t stat_tx_packets = 0;
static uint32_t stat_rx_packets = 0;
static uint32_t stat_tx_bytes = 0;
static uint32_t stat_rx_bytes = 0;
static uint32_t stat_errors = 0;

/* 外部函数 */
extern int eth_process_packet(const uint8_t *data, int len);
extern int eth_send_packet(const uint8_t *data, int len);

/* 注册WiFi网络接口 */
int wifi_netif_register(void)
{
    if (wifi_netif_registered) return 0;

    wifi_netif_registered = 1;
    netif_is_up = 0;

    vga_puts("[WIFI-NETIF] wlan0 interface registered\n");
    vga_printf("[WIFI-NETIF] MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
               wifi_mac[0], wifi_mac[1], wifi_mac[2],
               wifi_mac[3], wifi_mac[4], wifi_mac[5]);
    return 0;
}

/* 启用网络接口 */
int wifi_netif_up(void)
{
    if (!wifi_netif_registered) return -1;
    netif_is_up = 1;
    vga_puts("[WIFI-NETIF] wlan0 interface up\n");
    return 0;
}

/* 禁用网络接口 */
int wifi_netif_down(void)
{
    netif_is_up = 0;
    vga_puts("[WIFI-NETIF] wlan0 interface down\n");
    return 0;
}

/* 获取接口状态 */
int wifi_netif_is_up(void)
{
    return netif_is_up;
}

/* 获取MAC地址 */
int wifi_netif_get_mac(uint8_t *mac)
{
    if (!mac) return -1;
    memcpy(mac, wifi_mac, 6);
    return 0;
}

/* 设置MAC地址 */
int wifi_netif_set_mac(const uint8_t *mac)
{
    if (!mac) return -1;
    memcpy(wifi_mac, mac, 6);
    return 0;
}

/* 802.11数据帧 → 以太网帧转换
 * 输入：802.11数据帧（含帧头+LLC/SNAP+以太网负载）
 * 输出：以太网帧（含以太网头+负载） */
int wifi_80211_to_eth(const uint8_t *wifi_frame, int wifi_len,
                       uint8_t *eth_frame, int eth_buf_size)
{
    if (!wifi_frame || !eth_frame || wifi_len < WIFI_DATA_HEADER_SIZE + LLC_SNAP_SIZE)
        return -1;

    /* 解析802.11帧头 */
    /* uint16_t frame_control = wifi_frame[0] | (wifi_frame[1] << 8); */
    /* uint16_t duration = wifi_frame[2] | (wifi_frame[3] << 8); */
    const uint8_t *addr1 = wifi_frame + 4;   /* 接收地址 (BSSID) */
    const uint8_t *addr2 = wifi_frame + 10;  /* 发送地址 (TA) */
    const uint8_t *addr3 = wifi_frame + 16;  /* 目标地址 (DA) */
    /* uint16_t seq_ctrl = wifi_frame[22] | (wifi_frame[23] << 8); */

    /* LLC/SNAP头 */
    const uint8_t *llc = wifi_frame + WIFI_DATA_HEADER_SIZE;
    /* llc[0]=0xAA, llc[1]=0xAA, llc[2]=0x03 (SNAP) */
    /* llc[3..5]=OUI, llc[6..7]=EtherType */

    uint16_t ethertype = llc[6] | (llc[7] << 8);

    /* 以太网负载 */
    const uint8_t *payload = wifi_frame + WIFI_DATA_HEADER_SIZE + LLC_SNAP_SIZE;
    int payload_len = wifi_len - WIFI_DATA_HEADER_SIZE - LLC_SNAP_SIZE;

    if (payload_len < 0) return -2;
    if (ETH_HEADER_SIZE + payload_len > eth_buf_size) return -3;

    /* 构建以太网帧头 */
    eth_frame[0] = addr3[0]; eth_frame[1] = addr3[1]; eth_frame[2] = addr3[2];
    eth_frame[3] = addr3[3]; eth_frame[4] = addr3[4]; eth_frame[5] = addr3[5];
    eth_frame[6] = addr2[0]; eth_frame[7] = addr2[1]; eth_frame[8] = addr2[2];
    eth_frame[9] = addr2[3]; eth_frame[10] = addr2[4]; eth_frame[11] = addr2[5];
    eth_frame[12] = ethertype & 0xFF;
    eth_frame[13] = (ethertype >> 8) & 0xFF;

    /* 复制负载 */
    memcpy(eth_frame + ETH_HEADER_SIZE, payload, payload_len);

    return ETH_HEADER_SIZE + payload_len;
}

/* 以太网帧 → 802.11数据帧转换
 * 输入：以太网帧（含以太网头+负载）
 * 输出：802.11数据帧（含帧头+LLC/SNAP+以太网负载） */
int wifi_eth_to_80211(const uint8_t *eth_frame, int eth_len,
                       uint8_t *wifi_frame, int wifi_buf_size,
                       const uint8_t *bssid, const uint8_t *sa)
{
    if (!eth_frame || !wifi_frame || eth_len < ETH_HEADER_SIZE)
        return -1;
    if (!bssid || !sa) return -2;

    /* 解析以太网帧头 */
    const uint8_t *da = eth_frame;      /* 目标地址 */
    const uint8_t *ta = eth_frame + 6;  /* 源地址 */
    uint16_t ethertype = eth_frame[12] | (eth_frame[13] << 8);

    /* 以太网负载 */
    const uint8_t *payload = eth_frame + ETH_HEADER_SIZE;
    int payload_len = eth_len - ETH_HEADER_SIZE;

    if (payload_len < 0) return -3;

    int total_len = WIFI_DATA_HEADER_SIZE + LLC_SNAP_SIZE + payload_len;
    if (total_len > wifi_buf_size) return -4;

    /* 构建802.11数据帧头 */
    /* Frame Control: 数据帧, FromDS=1, ToDS=0 */
    wifi_frame[0] = 0x08;  /* 版本=0, 类型=2(数据), 子类型=0 */
    wifi_frame[1] = 0x02;  /* FromDS=1 */
    wifi_frame[2] = 0x00; wifi_frame[3] = 0x00;  /* Duration */

    /* Address 1: 接收地址 (BSSID) */
    memcpy(wifi_frame + 4, bssid, 6);
    /* Address 2: 发送地址 (SA) */
    memcpy(wifi_frame + 10, sa, 6);
    /* Address 3: 目标地址 (DA) */
    memcpy(wifi_frame + 16, da, 6);

    /* Sequence Control */
    wifi_frame[22] = 0x00; wifi_frame[23] = 0x00;

    /* LLC/SNAP头 */
    uint8_t *llc = wifi_frame + WIFI_DATA_HEADER_SIZE;
    llc[0] = 0xAA; llc[1] = 0xAA; llc[2] = 0x03;  /* SNAP */
    llc[3] = 0x00; llc[4] = 0x00; llc[5] = 0x00;  /* OUI (以太网) */
    llc[6] = ethertype & 0xFF;
    llc[7] = (ethertype >> 8) & 0xFF;

    /* 复制负载 */
    memcpy(wifi_frame + WIFI_DATA_HEADER_SIZE + LLC_SNAP_SIZE,
           payload, payload_len);

    return total_len;
}

/* 发送以太网数据包（通过WiFi） */
int wifi_netif_send(const uint8_t *eth_data, int eth_len)
{
    if (!eth_data || eth_len < ETH_HEADER_SIZE) return -1;
    if (!netif_is_up) return -2;

    /* 简化：直接通过eth层发送
     * 实际应转换为802.11帧后通过WiFi驱动发送 */
    stat_tx_packets++;
    stat_tx_bytes += eth_len;

    return eth_send_packet(eth_data, eth_len);
}

/* 处理接收到的802.11帧 */
int wifi_netif_receive(const uint8_t *wifi_frame, int wifi_len)
{
    if (!wifi_frame || wifi_len < WIFI_DATA_HEADER_SIZE) return -1;

    /* 转换为以太网帧 */
    uint8_t eth_frame[1514];
    int eth_len = wifi_80211_to_eth(wifi_frame, wifi_len,
                                     eth_frame, sizeof(eth_frame));
    if (eth_len < 0) {
        stat_errors++;
        return eth_len;
    }

    stat_rx_packets++;
    stat_rx_bytes += eth_len;

    /* 传递给TCP/IP协议栈 */
    return eth_process_packet(eth_frame, eth_len);
}

/* 获取网络接口统计 */
void wifi_netif_get_stats(uint32_t *tx_packets, uint32_t *rx_packets,
                           uint32_t *tx_bytes, uint32_t *rx_bytes,
                           uint32_t *errors)
{
    if (tx_packets) *tx_packets = stat_tx_packets;
    if (rx_packets) *rx_packets = stat_rx_packets;
    if (tx_bytes) *tx_bytes = stat_tx_bytes;
    if (rx_bytes) *rx_bytes = stat_rx_bytes;
    if (errors) *errors = stat_errors;
}

/* 重置统计 */
void wifi_netif_reset_stats(void)
{
    stat_tx_packets = 0;
    stat_rx_packets = 0;
    stat_tx_bytes = 0;
    stat_rx_bytes = 0;
    stat_errors = 0;
}
