/* ============================================================================
 * 赫普OS - 网络配置管理头文件
 * V8.44
 * ========================================================================== */

#ifndef _NET_CONFIG_H
#define _NET_CONFIG_H

#include <stdint.h>

/* 网络配置结构 */
typedef struct {
    int enabled;           /* 网络是否启用 */
    int dhcp_enabled;      /* DHCP自动获取 */
    uint32_t ip_addr;      /* IP地址 */
    uint32_t netmask;      /* 子网掩码 */
    uint32_t gateway;      /* 网关 */
    uint32_t dns1;         /* 主DNS */
    uint32_t dns2;         /* 备DNS */
    uint8_t mac[6];        /* MAC地址 */
    int mtu;               /* MTU */
    char hostname[64];     /* 主机名 */
    int wifi_enabled;      /* WiFi启用 */
    int bluetooth_enabled; /* 蓝牙启用 */
    int airplane_mode;     /* 飞行模式 */
} net_config_t;

/* 函数声明 */
void net_config_init(void);
int net_config_set_static(uint32_t ip, uint32_t mask, uint32_t gw);
int net_config_set_dhcp(int enable);
int net_config_set_dns(uint32_t dns1, uint32_t dns2);
int net_config_set_hostname(const char *name);
int net_config_set_airplane_mode(int enable);
int net_config_get(net_config_t *cfg);
void net_config_print(void);

#endif /* _NET_CONFIG_H */
