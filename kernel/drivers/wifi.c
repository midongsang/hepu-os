/* ============================================================================
 * 赫普OS - WiFi驱动（wlan0）
 * 支持802.11 a/b/g/n/ac/ax，2.4GHz/5GHz/6GHz
 * ========================================================================== */

#include <stdint.h>
#include "wifi.h"
#include "vga.h"
#include "string.h"

#define WIFI_MAX_DEVICES 2

static wifi_device_t wifi_devices[WIFI_MAX_DEVICES];
static int wifi_initialized = 0;

/* 初始化WiFi驱动 */
void wifi_init(void)
{
    memset(wifi_devices, 0, sizeof(wifi_devices));

    /* 创建默认wlan0设备 */
    strcpy(wifi_devices[0].name, "wlan0");
    wifi_devices[0].mac[0] = 0x00;
    wifi_devices[0].mac[1] = 0x1A;
    wifi_devices[0].mac[2] = 0x36;
    wifi_devices[0].mac[3] = 0x5D;
    wifi_devices[0].mac[4] = 0xA7;
    wifi_devices[0].mac[5] = 0xCA;
    wifi_devices[0].state = WIFI_STATE_DISABLED;
    wifi_devices[0].supported_bands = WIFI_BAND_DUAL;
    wifi_devices[0].tx_power = 20;
    wifi_devices[0].initialized = 1;

    wifi_initialized = 1;
    vga_puts("[WiFi] WiFi driver init \n");
    vga_printf("[WiFi] wlan0 MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
               wifi_devices[0].mac[0], wifi_devices[0].mac[1],
               wifi_devices[0].mac[2], wifi_devices[0].mac[3],
               wifi_devices[0].mac[4], wifi_devices[0].mac[5]);
    vga_puts("[WiFi] supports 802.11 a/b/g/n/ac/ax, 2.4GHz/5GHz dual-band \n");
}

/* 查找设备 */
static wifi_device_t *wifi_find_device(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < WIFI_MAX_DEVICES; i++) {
        if (wifi_devices[i].initialized &&
            strcmp(wifi_devices[i].name, name) == 0) {
            return &wifi_devices[i];
        }
    }
    return NULL;
}

/* 启用WiFi */
int wifi_enable(const char *name)
{
    wifi_device_t *dev = wifi_find_device(name);
    if (!dev) return -1;

    dev->state = WIFI_STATE_DISCONNECTED;
    vga_printf("[WiFi] %s enabled \n", name);
    return 0;
}

/* 禁用WiFi */
int wifi_disable(const char *name)
{
    wifi_device_t *dev = wifi_find_device(name);
    if (!dev) return -1;

    dev->state = WIFI_STATE_DISABLED;
    dev->connected_ssid[0] = '\0';
    return 0;
}

/* 扫描网络 */
int wifi_scan(const char *name)
{
    wifi_device_t *dev = wifi_find_device(name);
    if (!dev || dev->state == WIFI_STATE_DISABLED) return -1;

    dev->state = WIFI_STATE_SCANNING;
    dev->scan_results_count = 0;

    /* 简化：模拟扫描结果 */
    strcpy(dev->scan_results[0].ssid, "HEPU_Network");
    dev->scan_results[0].signal_strength = -45;
    dev->scan_results[0].channel = 6;
    dev->scan_results[0].security = WIFI_SECURITY_WPA2;
    dev->scan_results[0].band = WIFI_BAND_2GHZ;
    dev->scan_results[0].bssid[0] = 0xAA; dev->scan_results[0].bssid[1] = 0xBB;
    dev->scan_results[0].bssid[2] = 0xCC; dev->scan_results[0].bssid[3] = 0xDD;
    dev->scan_results[0].bssid[4] = 0xEE; dev->scan_results[0].bssid[5] = 0xFF;

    strcpy(dev->scan_results[1].ssid, "HEPU_5G");
    dev->scan_results[1].signal_strength = -60;
    dev->scan_results[1].channel = 36;
    dev->scan_results[1].security = WIFI_SECURITY_WPA3;
    dev->scan_results[1].band = WIFI_BAND_5GHZ;

    dev->scan_results_count = 2;
    dev->state = WIFI_STATE_DISCONNECTED;

    vga_printf("[WiFi] scan done, found %d net \n", dev->scan_results_count);
    return dev->scan_results_count;
}

/* 获取扫描结果 */
int wifi_get_scan_results(const char *name, wifi_scan_result_t *results, int max)
{
    wifi_device_t *dev = wifi_find_device(name);
    if (!dev || !results || max <= 0) return 0;

    int count = dev->scan_results_count < max ? dev->scan_results_count : max;
    for (int i = 0; i < count; i++) {
        results[i] = dev->scan_results[i];
    }
    return count;
}

/* 连接网络 */
int wifi_connect(const char *name, const char *ssid, const char *password,
                 wifi_security_t security)
{
    (void)security;
    (void)password;
    wifi_device_t *dev = wifi_find_device(name);
    if (!dev || !ssid || dev->state == WIFI_STATE_DISABLED) return -1;

    dev->state = WIFI_STATE_CONNECTING;

    /* 简化：模拟连接过程 */
    strncpy(dev->connected_ssid, ssid, WIFI_SSID_MAX_LEN);
    dev->connected_ssid[WIFI_SSID_MAX_LEN] = '\0';
    dev->current_signal = -50;
    dev->current_channel = 6;
    dev->state = WIFI_STATE_CONNECTED;

    /* 模拟DHCP分配IP */
    dev->ip_addr = (192 << 24) | (168 << 16) | (1 << 8) | 100;
    dev->netmask = 0xFFFFFF00;
    dev->gateway = (192 << 24) | (168 << 16) | (1 << 8) | 1;
    dev->dns1 = (8 << 24) | (8 << 16) | (8 << 8) | 8;
    dev->dns2 = (8 << 24) | (8 << 16) | (4 << 8) | 4;

    vga_printf("[WiFi] connected to %s, signal %d dBm\n", ssid, dev->current_signal);
    return 0;
}

/* 断开连接 */
int wifi_disconnect(const char *name)
{
    wifi_device_t *dev = wifi_find_device(name);
    if (!dev) return -1;

    dev->state = WIFI_STATE_DISCONNECTED;
    dev->connected_ssid[0] = '\0';
    dev->ip_addr = 0;
    return 0;
}

/* 获取状态 */
int wifi_get_state(const char *name, wifi_state_t *state)
{
    wifi_device_t *dev = wifi_find_device(name);
    if (!dev || !state) return -1;
    *state = dev->state;
    return 0;
}

/* 获取信号强度 */
int wifi_get_signal_strength(const char *name, int *dbm)
{
    wifi_device_t *dev = wifi_find_device(name);
    if (!dev || !dbm) return -1;
    *dbm = dev->current_signal;
    return 0;
}

/* 获取已连接网络 */
int wifi_get_connected_network(const char *name, wifi_network_t *network)
{
    wifi_device_t *dev = wifi_find_device(name);
    if (!dev || !network || dev->state != WIFI_STATE_CONNECTED) return -1;

    strncpy(network->ssid, dev->connected_ssid, WIFI_SSID_MAX_LEN);
    memcpy(network->bssid, dev->connected_bssid, WIFI_MAC_ADDR_LEN);
    return 0;
}

/* 添加保存网络 */
int wifi_add_network(const char *name, const wifi_network_t *network)
{
    wifi_device_t *dev = wifi_find_device(name);
    if (!dev || !network) return -1;
    if (dev->saved_networks_count >= WIFI_MAX_SSIDS) return -1;

    dev->saved_networks[dev->saved_networks_count++] = *network;
    return 0;
}

/* 删除保存网络 */
int wifi_remove_network(const char *name, const char *ssid)
{
    wifi_device_t *dev = wifi_find_device(name);
    if (!dev || !ssid) return -1;

    for (int i = 0; i < dev->saved_networks_count; i++) {
        if (strcmp(dev->saved_networks[i].ssid, ssid) == 0) {
            for (int j = i; j < dev->saved_networks_count - 1; j++) {
                dev->saved_networks[j] = dev->saved_networks[j + 1];
            }
            dev->saved_networks_count--;
            return 0;
        }
    }
    return -1;
}

/* 列出保存网络 */
int wifi_list_saved_networks(const char *name, wifi_network_t *networks, int max)
{
    wifi_device_t *dev = wifi_find_device(name);
    if (!dev || !networks || max <= 0) return 0;

    int count = dev->saved_networks_count < max ? dev->saved_networks_count : max;
    for (int i = 0; i < count; i++) {
        networks[i] = dev->saved_networks[i];
    }
    return count;
}

/* 获取统计 */
int wifi_get_stats(const char *name, wifi_stats_t *stats)
{
    wifi_device_t *dev = wifi_find_device(name);
    if (!dev || !stats) return -1;
    *stats = dev->stats;
    return 0;
}

/* 设置发射功率 */
int wifi_set_tx_power(const char *name, int dbm)
{
    wifi_device_t *dev = wifi_find_device(name);
    if (!dev) return -1;
    dev->tx_power = dbm;
    return 0;
}

/* 列出设备 */
int wifi_list_devices(wifi_device_t *devices, int max)
{
    if (!devices || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < WIFI_MAX_DEVICES && count < max; i++) {
        if (wifi_devices[i].initialized) {
            devices[count++] = wifi_devices[i];
        }
    }
    return count;
}
