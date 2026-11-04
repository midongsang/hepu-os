#ifndef WIFI_H
#define WIFI_H

#include <stdint.h>

#define WIFI_MAC_ADDR_LEN 6
#define WIFI_MAX_SSIDS 32
#define WIFI_SSID_MAX_LEN 32
#define WIFI_MAX_BSSIDS 16

/* WiFi安全类型 */
typedef enum {
    WIFI_SECURITY_OPEN = 0,
    WIFI_SECURITY_WEP = 1,
    WIFI_SECURITY_WPA = 2,
    WIFI_SECURITY_WPA2 = 3,
    WIFI_SECURITY_WPA3 = 4,
    WIFI_SECURITY_ENTERPRISE = 5
} wifi_security_t;

/* WiFi频段 */
typedef enum {
    WIFI_BAND_2GHZ = 0,
    WIFI_BAND_5GHZ = 1,
    WIFI_BAND_6GHZ = 2,
    WIFI_BAND_DUAL = 3
} wifi_band_t;

/* WiFi状态 */
typedef enum {
    WIFI_STATE_DISABLED = 0,
    WIFI_STATE_DISCONNECTED = 1,
    WIFI_STATE_SCANNING = 2,
    WIFI_STATE_CONNECTING = 3,
    WIFI_STATE_CONNECTED = 4,
    WIFI_STATE_AUTHENTICATING = 5,
    WIFI_STATE_ASSOCIATING = 6
} wifi_state_t;

/* WiFi扫描结果 */
typedef struct {
    uint8_t  bssid[WIFI_MAC_ADDR_LEN];
    char     ssid[WIFI_SSID_MAX_LEN + 1];
    int      signal_strength;  /* dBm */
    int      channel;
    wifi_security_t security;
    wifi_band_t band;
    int      wps_enabled;
    int      rsn_capable;
} wifi_scan_result_t;

/* WiFi网络配置 */
typedef struct {
    char     ssid[WIFI_SSID_MAX_LEN + 1];
    uint8_t  bssid[WIFI_MAC_ADDR_LEN];
    wifi_security_t security;
    char     password[64];
    int      auto_connect;
    int      priority;
    int      hidden;
} wifi_network_t;

/* WiFi设备统计 */
typedef struct {
    uint64_t tx_packets;
    uint64_t rx_packets;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint64_t tx_retries;
    uint64_t rx_errors;
    uint32_t beacon_loss;
    uint32_t roam_count;
} wifi_stats_t;

/* WiFi设备 */
typedef struct {
    char     name[16];
    uint8_t  mac[WIFI_MAC_ADDR_LEN];
    wifi_state_t state;
    wifi_band_t supported_bands;
    int      tx_power;       /* dBm */
    int      current_channel;
    int      current_signal; /* dBm */
    char     connected_ssid[WIFI_SSID_MAX_LEN + 1];
    uint8_t  connected_bssid[WIFI_MAC_ADDR_LEN];
    uint32_t ip_addr;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns1;
    uint32_t dns2;
    wifi_stats_t stats;
    int      initialized;
    int      scan_results_count;
    wifi_scan_result_t scan_results[WIFI_MAX_BSSIDS];
    int      saved_networks_count;
    wifi_network_t saved_networks[WIFI_MAX_SSIDS];
} wifi_device_t;

void wifi_init(void);
int wifi_enable(const char *name);
int wifi_disable(const char *name);
int wifi_scan(const char *name);
int wifi_get_scan_results(const char *name, wifi_scan_result_t *results, int max);
int wifi_connect(const char *name, const char *ssid, const char *password,
                 wifi_security_t security);
int wifi_disconnect(const char *name);
int wifi_get_state(const char *name, wifi_state_t *state);
int wifi_get_signal_strength(const char *name, int *dbm);
int wifi_get_connected_network(const char *name, wifi_network_t *network);
int wifi_add_network(const char *name, const wifi_network_t *network);
int wifi_remove_network(const char *name, const char *ssid);
int wifi_list_saved_networks(const char *name, wifi_network_t *networks, int max);
int wifi_get_stats(const char *name, wifi_stats_t *stats);
int wifi_set_tx_power(const char *name, int dbm);
int wifi_list_devices(wifi_device_t *devices, int max);

#endif /* WIFI_H */
