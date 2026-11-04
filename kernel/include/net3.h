#ifndef NET3_H
#define NET3_H

#include <stdint.h>

#define NET3_MAX_INTERFACES 8
#define NET3_MAX_ROUTES 64
#define NET3_MAX_SOCKETS 128
#define NET3_MAX_TCP_CONNECTIONS 64
#define NET3_MAX_UDP_PORTS 32
#define NET3_MAX_DNS_CACHE 128
#define NET3_MAX_FIREWALL_RULES 256
#define NET3_MAX_NAT_RULES 64
#define NET3_MAX_QOS_CLASSES 8

/* 网络接口类型 */
typedef enum {
    NET_IF_LOOPBACK = 0,
    NET_IF_ETHERNET,
    NET_IF_WIFI,
    NET_IF_BLUETOOTH,
    NET_IF_USB,
    NET_IF_CELLULAR,
    NET_IF_VPN,
    NET_IF_TUN,
    NET_IF_TAP,
    NET_IF_VIRTUAL,
} net_if_type_t;

/* 网络接口状态 */
typedef enum {
    NET_IF_DOWN = 0,
    NET_IF_UP,
    NET_IF_RUNNING,
    NET_IF_DORMANT,
    NET_IF_ERROR,
} net_if_state_t;

/* IPv4地址 */
typedef struct {
    uint8_t addr[4];
} ipv4_addr_t;

/* IPv6地址 */
typedef struct {
    uint8_t addr[16];
} ipv6_addr_t;

/* MAC地址 */
typedef struct {
    uint8_t addr[6];
} mac_addr_t;

/* 网络接口 */
typedef struct {
    int used;
    int id;
    char name[32];
    net_if_type_t type;
    net_if_state_t state;
    mac_addr_t mac;
    ipv4_addr_t ipv4;
    ipv4_addr_t netmask;
    ipv4_addr_t gateway;
    ipv6_addr_t ipv6;
    ipv6_addr_t ipv6_gateway;
    int mtu;
    int speed_mbps;
    int duplex;  /* 0=half,1=full */
    int promiscuous;
    /* 统计 */
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_errors;
    uint64_t tx_errors;
    uint64_t rx_dropped;
    uint64_t tx_dropped;
} net_interface_t;

/* 路由表条目 */
typedef struct {
    int used;
    int id;
    ipv4_addr_t destination;
    ipv4_addr_t netmask;
    ipv4_addr_t gateway;
    int interface_id;
    int metric;
    int protocol;  /* 0=static,1=dhcp,2=rip,3=ospf,4=bgp */
    int scope;  /* 0=global,1=link,2=host */
} route_entry_t;

/* TCP连接状态 */
typedef enum {
    TCP_CLOSED = 0,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_CLOSING,
    TCP_LAST_ACK,
    TCP_TIME_WAIT,
} tcp_state_t;

/* TCP连接 */
typedef struct {
    int used;
    int id;
    int socket_id;
    tcp_state_t state;
    ipv4_addr_t local_addr;
    uint16_t local_port;
    ipv4_addr_t remote_addr;
    uint16_t remote_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t window_size;
    int mss;
    int ssthresh;
    int cwnd;
    int retransmit_count;
    uint64_t last_activity;
    int keepalive_enabled;
    int keepalive_idle;
    int keepalive_interval;
    int keepalive_count;
    /* 缓冲 */
    void *rx_buffer;
    uint32_t rx_buffer_size;
    uint32_t rx_data_len;
    void *tx_buffer;
    uint32_t tx_buffer_size;
    uint32_t tx_data_len;
} tcp_connection_t;

/* UDP端口 */
typedef struct {
    int used;
    int id;
    uint16_t port;
    int socket_id;
    ipv4_addr_t local_addr;
    int connected;
    ipv4_addr_t remote_addr;
    uint16_t remote_port;
} udp_port_t;

/* Socket类型 */
typedef enum {
    NET3_SOCK_STREAM = 0,  /* TCP */
    NET3_SOCK_DGRAM,  /* UDP */
    NET3_SOCK_RAW,  /* RAW */
    NET3_SOCK_PACKET,  /* 数据包 */
} net3_socket_type_t;

/* Socket */
typedef struct {
    int used;
    int id;
    int fd;
    net3_socket_type_t type;
    int protocol;  /* IPPROTO_TCP/UDP/ICMP */
    int domain;  /* AF_INET/AF_INET6 */
    int state;  /* 0=closed,1=bound,2=listening,3=connected */
    ipv4_addr_t local_addr;
    uint16_t local_port;
    ipv4_addr_t remote_addr;
    uint16_t remote_port;
    int options;  /* SO_REUSEADDR/SO_KEEPALIVE等 */
    int recv_timeout;
    int send_timeout;
    int recv_buffer_size;
    int send_buffer_size;
    int error;
} net3_socket_t;

/* DNS缓存条目 */
typedef struct {
    int used;
    int id;
    char hostname[256];
    ipv4_addr_t ipv4;
    ipv6_addr_t ipv6;
    int ttl;
    uint64_t expires;
    int query_type;  /* A/AAAA/CNAME/MX/TXT/SRV */
    int resolved;
} dns_cache_entry_t;

/* 防火墙规则 */
typedef struct {
    int used;
    int id;
    int chain;  /* 0=INPUT,1=OUTPUT,2=FORWARD */
    int action;  /* 0=ACCEPT,1=DROP,2=REJECT,3=LOG */
    int protocol;  /* 0=any,1=TCP,2=UDP,3=ICMP */
    ipv4_addr_t src_addr;
    ipv4_addr_t src_mask;
    uint16_t src_port;
    ipv4_addr_t dst_addr;
    ipv4_addr_t dst_mask;
    uint16_t dst_port;
    int in_interface;
    int out_interface;
    int state;  /* connection tracking state */
    int priority;
    uint64_t packets;
    uint64_t bytes;
} net3_firewall_rule_t;

/* NAT规则 */
typedef struct {
    int used;
    int id;
    int type;  /* 0=SNAT,1=DNAT,2=MASQUERADE,3=REDIRECT */
    ipv4_addr_t original_src;
    uint16_t original_src_port;
    ipv4_addr_t original_dst;
    uint16_t original_dst_port;
    ipv4_addr_t new_src;
    uint16_t new_src_port;
    ipv4_addr_t new_dst;
    uint16_t new_dst_port;
    int protocol;
    int interface_id;
} nat_rule_t;

/* VPN类型 */
typedef enum {
    NET3_VPN_TYPE_PPTP = 0,
    NET3_VPN_TYPE_L2TP,
    NET3_VPN_TYPE_IPSEC,
    NET3_VPN_TYPE_OPENVPN,
    NET3_VPN_TYPE_WIREGUARD,
    NET3_VPN_TYPE_SSL,
    NET3_VPN_TYPE_SSTP,
} net3_vpn_type_t;

/* VPN配置 */
typedef struct {
    int used;
    int id;
    char name[64];
    net3_vpn_type_t type;
    char server[128];
    int port;
    char username[64];
    int mtu;
    int connected;
    ipv4_addr_t local_ip;
    ipv4_addr_t remote_ip;
    ipv4_addr_t dns1;
    ipv4_addr_t dns2;
    int encryption;  /* 0=none,1=AES-128,2=AES-256,3=ChaCha20 */
    int compression;
    uint64_t connect_time;
    uint64_t bytes_sent;
    uint64_t bytes_received;
} vpn_config_t;

/* 代理类型 */
typedef enum {
    NET3_PROXY_HTTP = 0,
    NET3_PROXY_HTTPS,
    NET3_PROXY_SOCKS4,
    NET3_PROXY_SOCKS5,
    NET3_PROXY_TRANSPARENT,
} net3_proxy_type_t;

/* 代理配置 */
typedef struct {
    int enabled;
    net3_proxy_type_t type;
    char server[128];
    int port;
    char username[64];
    int auth_required;
    int bypass_local;
    char bypass_list[256];
    int pac_enabled;
    char pac_url[256];
} proxy_config_t;

/* QoS类别 */
typedef struct {
    int used;
    int id;
    char name[32];
    int priority;  /* 0-7 */
    int bandwidth_percent;
    int max_bandwidth_kbps;
    int min_bandwidth_kbps;
    int latency_ms;
    int packet_loss_percent;
    int match_protocol;
    int match_port;
    uint64_t bytes_sent;
    uint64_t packets_sent;
} qos_class_t;

/* 网络完善状态 */
typedef struct {
    int initialized;
    /* 接口 */
    net_interface_t interfaces[NET3_MAX_INTERFACES];
    int interface_count;
    /* 路由 */
    route_entry_t routes[NET3_MAX_ROUTES];
    int route_count;
    /* Socket */
    net3_socket_t sockets[NET3_MAX_SOCKETS];
    int socket_count;
    /* TCP */
    tcp_connection_t tcp_connections[NET3_MAX_TCP_CONNECTIONS];
    int tcp_count;
    /* UDP */
    udp_port_t udp_ports[NET3_MAX_UDP_PORTS];
    int udp_count;
    /* DNS */
    dns_cache_entry_t dns_cache[NET3_MAX_DNS_CACHE];
    int dns_count;
    ipv4_addr_t dns_servers[4];
    int dns_server_count;
    /* 防火墙 */
    net3_firewall_rule_t firewall_rules[NET3_MAX_FIREWALL_RULES];
    int firewall_count;
    int firewall_enabled;
    int default_input_policy;
    int default_output_policy;
    int default_forward_policy;
    /* NAT */
    nat_rule_t nat_rules[NET3_MAX_NAT_RULES];
    int nat_count;
    int nat_enabled;
    /* VPN */
    vpn_config_t vpns[4];
    int vpn_count;
    int active_vpn;
    /* 代理 */
    proxy_config_t proxy;
    /* QoS */
    qos_class_t qos_classes[NET3_MAX_QOS_CLASSES];
    int qos_count;
    int qos_enabled;
    /* 统计 */
    uint64_t total_rx_bytes;
    uint64_t total_tx_bytes;
    uint64_t total_packets;
    uint64_t tcp_connections_total;
    uint64_t dns_queries;
    uint64_t firewall_drops;
} net3_t;

/* 初始化网络完善 */
void net3_init(void);

/* 接口管理 */
int net3_if_add(net_interface_t *iface);
int net3_if_remove(int if_id);
int net3_if_up(int if_id);
int net3_if_down(int if_id);
int net3_if_get(int if_id, net_interface_t *iface);
int net3_if_list(net_interface_t *ifaces, int max);
int net3_if_set_ip(int if_id, ipv4_addr_t addr, ipv4_addr_t netmask, ipv4_addr_t gateway);
int net3_if_set_mtu(int if_id, int mtu);
int net3_if_get_stats(int if_id, uint64_t *rx, uint64_t *tx, uint64_t *rx_pkts, uint64_t *tx_pkts);

/* 路由管理 */
int net3_route_add(ipv4_addr_t dest, ipv4_addr_t mask, ipv4_addr_t gateway, int if_id, int metric);
int net3_route_remove(int route_id);
int net3_route_list(route_entry_t *routes, int max);
int net3_route_lookup(ipv4_addr_t dest, route_entry_t *route);

/* Socket */
int net3_socket_create(net3_socket_type_t type, int protocol);
int net3_socket_close(int sock_id);
int net3_socket_bind(int sock_id, ipv4_addr_t addr, uint16_t port);
int net3_socket_listen(int sock_id, int backlog);
int net3_socket_accept(int sock_id, net3_socket_t *client);
int net3_socket_connect(int sock_id, ipv4_addr_t addr, uint16_t port);
int net3_socket_send(int sock_id, const void *data, int len, int flags);
int net3_socket_recv(int sock_id, void *buf, int len, int flags);
int net3_socket_sendto(int sock_id, const void *data, int len, ipv4_addr_t addr, uint16_t port);
int net3_socket_recvfrom(int sock_id, void *buf, int len, ipv4_addr_t *addr, uint16_t *port);
int net3_socket_get(int sock_id, net3_socket_t *sock);
int net3_socket_list(net3_socket_t *socks, int max);
int net3_socket_setsockopt(int sock_id, int option, int value);

/* TCP */
int net3_tcp_list(tcp_connection_t *conns, int max);
int net3_tcp_get(int conn_id, tcp_connection_t *conn);
int net3_tcp_close(int conn_id);

/* DNS */
int net3_dns_resolve(const char *hostname, ipv4_addr_t *result);
int net3_dns_resolve6(const char *hostname, ipv6_addr_t *result);
int net3_dns_add_server(ipv4_addr_t server);
int net3_dns_get_cache(dns_cache_entry_t *cache, int max);
int net3_dns_flush(void);

/* 防火墙 */
int net3_fw_enable(int enable);
int net3_fw_add_rule(net3_firewall_rule_t *rule);
int net3_fw_remove_rule(int rule_id);
int net3_fw_list_rules(net3_firewall_rule_t *rules, int max);
int net3_fw_set_default(int chain, int policy);
int net3_fw_get_stats(int rule_id, uint64_t *packets, uint64_t *bytes);

/* NAT */
int net3_nat_enable(int enable);
int net3_nat_add_rule(nat_rule_t *rule);
int net3_nat_remove_rule(int rule_id);
int net3_nat_list_rules(nat_rule_t *rules, int max);

/* VPN */
int net3_vpn_add(vpn_config_t *vpn);
int net3_vpn_remove(int vpn_id);
int net3_vpn_connect(int vpn_id);
int net3_vpn_disconnect(int vpn_id);
int net3_vpn_list(vpn_config_t *vpns, int max);
int net3_vpn_get_status(int vpn_id, vpn_config_t *vpn);

/* 代理 */
int net3_proxy_set(proxy_config_t *proxy);
int net3_proxy_get(proxy_config_t *proxy);
int net3_proxy_enable(int enable);

/* QoS */
int net3_qos_enable(int enable);
int net3_qos_add_class(qos_class_t *qos);
int net3_qos_remove_class(int class_id);
int net3_qos_list(qos_class_t *classes, int max);

/* 统计 */
int net3_get_stats(uint64_t *rx, uint64_t *tx, uint64_t *packets, uint64_t *tcp_conns, uint64_t *dns);

#endif /* NET3_H */
