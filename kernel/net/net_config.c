/* ============================================================================
 * 赫普OS - 网络配置管理
 * V8.44
 * 统一管理IP/网关/DNS/DHCP配置
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "vga.h"

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

static net_config_t net_cfg;

/* IP地址转换工具 */
static uint32_t ip_to_u32(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | d;
}

static void u32_to_ip(uint32_t ip, uint8_t *a, uint8_t *b, uint8_t *c, uint8_t *d) {
    *a = (ip >> 24) & 0xFF;
    *b = (ip >> 16) & 0xFF;
    *c = (ip >> 8) & 0xFF;
    *d = ip & 0xFF;
}

/* 初始化网络配置（默认值） */
void net_config_init(void)
{
    memset(&net_cfg, 0, sizeof(net_cfg));
    net_cfg.enabled = 1;
    net_cfg.dhcp_enabled = 1;  /* 默认DHCP */
    net_cfg.ip_addr = ip_to_u32(192, 168, 1, 100);
    net_cfg.netmask = ip_to_u32(255, 255, 255, 0);
    net_cfg.gateway = ip_to_u32(192, 168, 1, 1);
    net_cfg.dns1 = ip_to_u32(8, 8, 8, 8);
    net_cfg.dns2 = ip_to_u32(114, 114, 114, 114);
    net_cfg.mtu = 1500;
    strncpy(net_cfg.hostname, "hepu-os", 63);
    net_cfg.wifi_enabled = 0;
    net_cfg.bluetooth_enabled = 0;
    net_cfg.airplane_mode = 0;

    vga_puts("[NETCFG] Network config initialized\n");
    vga_printf("  DHCP: %s, IP: 192.168.1.100\n",
               net_cfg.dhcp_enabled ? "enabled" : "disabled");
}

/* 设置静态IP */
int net_config_set_static(uint32_t ip, uint32_t mask, uint32_t gw)
{
    net_cfg.dhcp_enabled = 0;
    net_cfg.ip_addr = ip;
    net_cfg.netmask = mask;
    net_cfg.gateway = gw;

    /* 应用到网络栈 */
    extern int eth_set_ip(const char*, uint32_t, uint32_t, uint32_t);
    eth_set_ip("eth0", ip, mask, gw);

    vga_puts("[NETCFG] Static IP configured\n");
    return 0;
}

/* 启用DHCP */
int net_config_set_dhcp(int enable)
{
    net_cfg.dhcp_enabled = enable ? 1 : 0;

    if (enable) {
        vga_puts("[NETCFG] DHCP enabled\n");
        /* 触发DHCP客户端 */
        extern int dhcp_start(const char*);
        dhcp_start("eth0");
    } else {
        vga_puts("[NETCFG] DHCP disabled, using static config\n");
    }

    return 0;
}

/* 设置DNS */
int net_config_set_dns(uint32_t dns1, uint32_t dns2)
{
    net_cfg.dns1 = dns1;
    net_cfg.dns2 = dns2;

    /* 应用到DNS解析器 */
    extern void dns_set_server(uint32_t);
    dns_set_server(dns1);

    vga_puts("[NETCFG] DNS updated\n");
    return 0;
}

/* 设置主机名 */
int net_config_set_hostname(const char *name)
{
    if (!name || strlen(name) > 63) return -1;
    strncpy(net_cfg.hostname, name, 63);
    vga_printf("[NETCFG] Hostname: %s\n", name);
    return 0;
}

/* 设置飞行模式 */
int net_config_set_airplane_mode(int enable)
{
    net_cfg.airplane_mode = enable ? 1 : 0;
    if (enable) {
        net_cfg.wifi_enabled = 0;
        net_cfg.bluetooth_enabled = 0;
        vga_puts("[NETCFG] Airplane mode ON (all wireless disabled)\n");
    } else {
        vga_puts("[NETCFG] Airplane mode OFF\n");
    }
    return 0;
}

/* 获取当前配置 */
int net_config_get(net_config_t *cfg)
{
    if (!cfg) return -1;
    memcpy(cfg, &net_cfg, sizeof(net_config_t));
    return 0;
}

/* 打印网络配置 */
void net_config_print(void)
{
    uint8_t a, b, c, d;

    vga_puts("=== Network Configuration ===\n");
    vga_printf("Hostname: %s\n", net_cfg.hostname);
    vga_printf("Enabled: %s\n", net_cfg.enabled ? "yes" : "no");
    vga_printf("DHCP: %s\n", net_cfg.dhcp_enabled ? "enabled" : "disabled");

    u32_to_ip(net_cfg.ip_addr, &a, &b, &c, &d);
    vga_printf("IP: %d.%d.%d.%d\n", a, b, c, d);

    u32_to_ip(net_cfg.netmask, &a, &b, &c, &d);
    vga_printf("Netmask: %d.%d.%d.%d\n", a, b, c, d);

    u32_to_ip(net_cfg.gateway, &a, &b, &c, &d);
    vga_printf("Gateway: %d.%d.%d.%d\n", a, b, c, d);

    u32_to_ip(net_cfg.dns1, &a, &b, &c, &d);
    vga_printf("DNS1: %d.%d.%d.%d\n", a, b, c, d);

    u32_to_ip(net_cfg.dns2, &a, &b, &c, &d);
    vga_printf("DNS2: %d.%d.%d.%d\n", a, b, c, d);

    vga_printf("MTU: %d\n", net_cfg.mtu);
    vga_printf("WiFi: %s\n", net_cfg.wifi_enabled ? "on" : "off");
    vga_printf("Bluetooth: %s\n", net_cfg.bluetooth_enabled ? "on" : "off");
    vga_printf("Airplane: %s\n", net_cfg.airplane_mode ? "on" : "off");
}
