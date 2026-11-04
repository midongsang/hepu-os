/* ============================================================================
 * 赫普OS - 网络诊断工具（V8.74新增）
 * ping、traceroute、DNS查询、端口扫描、网络配置
 * ========================================================================== */

#ifndef _NET_TOOLS_H
#define _NET_TOOLS_H

#include <stdint.h>

/* Ping结果 */
typedef struct {
    uint32_t ip_addr;
    int      seq;
    int      ttl;
    float    rtt_ms;       /* 往返时间（毫秒） */
    int      received;
    char     error[64];
} ping_result_t;

/* Ping统计 */
typedef struct {
    uint32_t target_ip;
    char     target_host[64];
    int      packets_sent;
    int      packets_received;
    float    min_rtt;
    float    max_rtt;
    float    avg_rtt;
    float    loss_percent;
} ping_stats_t;

/* Traceroute跳数 */
typedef struct {
    int      hop;
    uint32_t ip_addr;
    char     hostname[64];
    float    rtt_ms[3];
    int      probes_received;
} traceroute_hop_t;

/* 端口扫描结果 */
typedef struct {
    uint16_t port;
    char     service[32];
    int      state;  /* 0=关闭, 1=开放, 2=过滤 */
} port_scan_result_t;

/* 网络接口信息 */
typedef struct {
    char     name[16];
    uint8_t  mac[6];
    uint32_t ip_addr;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns1;
    uint32_t dns2;
    int      mtu;
    int      state;  /* 0=down, 1=up */
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint64_t tx_packets;
    uint64_t rx_packets;
} net_interface_t;

/* 网络工具状态 */
typedef struct {
    int      initialized;
    int      current_tool;  /* 0=ping, 1=traceroute, 2=端口扫描, 3=网络配置 */
    char     target[64];
    ping_stats_t ping_stats;
    ping_result_t ping_results[10];
    int      ping_result_count;
    traceroute_hop_t traceroute_results[30];
    int      traceroute_hop_count;
    port_scan_result_t port_results[100];
    int      port_count;
    net_interface_t interfaces[4];
    int      interface_count;
} net_tools_t;

/* 初始化网络工具 */
int net_tools_init(void);

/* 关闭 */
void net_tools_shutdown(void);

/* Ping */
int net_tools_ping(const char *host, int count, int interval_ms);

/* Traceroute */
int net_tools_traceroute(const char *host, int max_hops);

/* 端口扫描 */
int net_tools_port_scan(const char *host, uint16_t start_port,
                        uint16_t end_port);

/* DNS查询 */
int net_tools_dns_lookup(const char *hostname, uint32_t *result_ip);

/* 获取网络接口 */
int net_tools_get_interfaces(net_interface_t *ifaces, int max_count);

/* 刷新网络状态 */
int net_tools_refresh(void);

/* 绘制UI */
void net_tools_draw_ui(int x, int y, int width, int height);

/* 处理键盘 */
void net_tools_handle_key(char key);

/* 获取状态 */
int net_tools_get_state(net_tools_t *state);

#endif /* _NET_TOOLS_H */
