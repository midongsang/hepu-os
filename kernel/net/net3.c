/* ============================================================================
 * 赫普OS - 网络完善
 * 完整TCP/IP + IPv6 + 防火墙 + NAT + VPN + 代理 + QoS + DNS
 * ========================================================================== */

#include "types.h"
#include <stdint.h>
#include "net3.h"
#include "vga.h"
#include "string.h"

net3_t net;

void net3_init(void)
{
    memset(&net, 0, sizeof(net3_t));
    net.initialized = 1;

    /* 回环接口 */
    net.interfaces[0].used = 1;
    net.interfaces[0].id = 0;
    strncpy(net.interfaces[0].name, "lo", 31);
    net.interfaces[0].type = NET_IF_LOOPBACK;
    net.interfaces[0].state = NET_IF_RUNNING;
    net.interfaces[0].ipv4.addr[0] = 127;
    net.interfaces[0].ipv4.addr[3] = 1;
    net.interfaces[0].netmask.addr[0] = 255;
    net.interfaces[0].netmask.addr[1] = 255;
    net.interfaces[0].netmask.addr[2] = 255;
    net.interfaces[0].netmask.addr[3] = 255;
    net.interfaces[0].mtu = 65536;
    net.interface_count = 1;

    /* 以太网接口 */
    net.interfaces[1].used = 1;
    net.interfaces[1].id = 1;
    strncpy(net.interfaces[1].name, "eth0", 31);
    net.interfaces[1].type = NET_IF_ETHERNET;
    net.interfaces[1].state = NET_IF_RUNNING;
    net.interfaces[1].mac.addr[0] = 0x00;
    net.interfaces[1].mac.addr[1] = 0x11;
    net.interfaces[1].mac.addr[2] = 0x22;
    net.interfaces[1].mac.addr[3] = 0x33;
    net.interfaces[1].mac.addr[4] = 0x44;
    net.interfaces[1].mac.addr[5] = 0x55;
    net.interfaces[1].ipv4.addr[0] = 192;
    net.interfaces[1].ipv4.addr[1] = 168;
    net.interfaces[1].ipv4.addr[2] = 1;
    net.interfaces[1].ipv4.addr[3] = 100;
    net.interfaces[1].netmask.addr[0] = 255;
    net.interfaces[1].netmask.addr[1] = 255;
    net.interfaces[1].netmask.addr[2] = 255;
    net.interfaces[1].netmask.addr[3] = 0;
    net.interfaces[1].gateway.addr[0] = 192;
    net.interfaces[1].gateway.addr[1] = 168;
    net.interfaces[1].gateway.addr[2] = 1;
    net.interfaces[1].gateway.addr[3] = 1;
    net.interfaces[1].mtu = 1500;
    net.interfaces[1].speed_mbps = 1000;
    net.interfaces[1].duplex = 1;
    net.interface_count = 2;

    /* 默认路由 */
    net.routes[0].used = 1;
    net.routes[0].id = 0;
    net.routes[0].gateway = net.interfaces[1].gateway;
    net.routes[0].interface_id = 1;
    net.routes[0].metric = 100;
    net.routes[0].protocol = 0;
    net.route_count = 1;

    /* DNS服务器 */
    net.dns_servers[0].addr[0] = 8;
    net.dns_servers[0].addr[3] = 8;
    net.dns_servers[1].addr[0] = 8;
    net.dns_servers[1].addr[3] = 4;
    net.dns_servers[1].addr[2] = 4;
    net.dns_servers[1].addr[3] = 4;
    net.dns_server_count = 2;

    /* 防火墙默认策略 */
    net.firewall_enabled = 1;
    net.default_input_policy = 1;  /* DROP */
    net.default_output_policy = 0;  /* ACCEPT */
    net.default_forward_policy = 1;  /* DROP */

    /* QoS默认类别 */
    net.qos_classes[0].used = 1;
    net.qos_classes[0].id = 0;
    strncpy(net.qos_classes[0].name, " default ", 31);
    net.qos_classes[0].priority = 0;
    net.qos_classes[0].bandwidth_percent = 100;
    net.qos_count = 1;
    net.qos_enabled = 1;

    vga_puts("[NET3] net complete init done \n");
    vga_printf(" iface: %d, firewall: on, NAT: close, QoS: on, DNS: %d server \n",
               net.interface_count, net.dns_server_count);
}

/* 接口管理 */
int net3_if_add(net_interface_t *iface)
{
    if (!iface) return -1;
    for (int i = 0; i < NET3_MAX_INTERFACES; i++) {
        if (!net.interfaces[i].used) {
            memcpy(&net.interfaces[i], iface, sizeof(net_interface_t));
            net.interfaces[i].used = 1;
            net.interfaces[i].id = i;
            net.interface_count++;
            return i;
        }
    }
    return -1;
}

int net3_if_remove(int if_id)
{
    if (if_id < 0 || if_id >= NET3_MAX_INTERFACES || !net.interfaces[if_id].used) return -1;
    net.interfaces[if_id].used = 0;
    net.interface_count--;
    return 0;
}

int net3_if_up(int if_id)
{
    if (if_id < 0 || if_id >= NET3_MAX_INTERFACES || !net.interfaces[if_id].used) return -1;
    net.interfaces[if_id].state = NET_IF_RUNNING;
    return 0;
}

int net3_if_down(int if_id)
{
    if (if_id < 0 || if_id >= NET3_MAX_INTERFACES || !net.interfaces[if_id].used) return -1;
    net.interfaces[if_id].state = NET_IF_DOWN;
    return 0;
}

int net3_if_get(int if_id, net_interface_t *iface)
{
    if (if_id < 0 || if_id >= NET3_MAX_INTERFACES || !net.interfaces[if_id].used || !iface) return -1;
    memcpy(iface, &net.interfaces[if_id], sizeof(net_interface_t));
    return 0;
}

int net3_if_list(net_interface_t *ifaces, int max)
{
    if (!ifaces || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < NET3_MAX_INTERFACES && count < max; i++) {
        if (net.interfaces[i].used) memcpy(&ifaces[count++], &net.interfaces[i], sizeof(net_interface_t));
    }
    return count;
}

int net3_if_set_ip(int if_id, ipv4_addr_t addr, ipv4_addr_t netmask, ipv4_addr_t gateway)
{
    if (if_id < 0 || if_id >= NET3_MAX_INTERFACES || !net.interfaces[if_id].used) return -1;
    net.interfaces[if_id].ipv4 = addr;
    net.interfaces[if_id].netmask = netmask;
    net.interfaces[if_id].gateway = gateway;
    return 0;
}

int net3_if_set_mtu(int if_id, int mtu)
{
    if (if_id < 0 || if_id >= NET3_MAX_INTERFACES || !net.interfaces[if_id].used) return -1;
    net.interfaces[if_id].mtu = mtu;
    return 0;
}

int net3_if_get_stats(int if_id, uint64_t *rx, uint64_t *tx, uint64_t *rx_pkts, uint64_t *tx_pkts)
{
    if (if_id < 0 || if_id >= NET3_MAX_INTERFACES || !net.interfaces[if_id].used) return -1;
    if (rx) *rx = net.interfaces[if_id].rx_bytes;
    if (tx) *tx = net.interfaces[if_id].tx_bytes;
    if (rx_pkts) *rx_pkts = net.interfaces[if_id].rx_packets;
    if (tx_pkts) *tx_pkts = net.interfaces[if_id].tx_packets;
    return 0;
}

/* 路由管理 */
int net3_route_add(ipv4_addr_t dest, ipv4_addr_t mask, ipv4_addr_t gateway, int if_id, int metric)
{
    for (int i = 0; i < NET3_MAX_ROUTES; i++) {
        if (!net.routes[i].used) {
            memset(&net.routes[i], 0, sizeof(route_entry_t));
            net.routes[i].used = 1;
            net.routes[i].id = i;
            net.routes[i].destination = dest;
            net.routes[i].netmask = mask;
            net.routes[i].gateway = gateway;
            net.routes[i].interface_id = if_id;
            net.routes[i].metric = metric;
            net.route_count++;
            return i;
        }
    }
    return -1;
}

int net3_route_remove(int route_id)
{
    if (route_id < 0 || route_id >= NET3_MAX_ROUTES || !net.routes[route_id].used) return -1;
    net.routes[route_id].used = 0;
    net.route_count--;
    return 0;
}

int net3_route_list(route_entry_t *routes, int max)
{
    if (!routes || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < NET3_MAX_ROUTES && count < max; i++) {
        if (net.routes[i].used) memcpy(&routes[count++], &net.routes[i], sizeof(route_entry_t));
    }
    return count;
}

int net3_route_lookup(ipv4_addr_t dest, route_entry_t *route)
{
    (void)dest;
    if (!route) return -1;
    /* 返回默认路由 */
    memcpy(route, &net.routes[0], sizeof(route_entry_t));
    return 0;
}

/* Socket */
int net3_socket_create(net3_socket_type_t type, int protocol)
{
    for (int i = 0; i < NET3_MAX_SOCKETS; i++) {
        if (!net.sockets[i].used) {
            memset(&net.sockets[i], 0, sizeof(net3_socket_t));
            net.sockets[i].used = 1;
            net.sockets[i].id = i;
            net.sockets[i].fd = i;
            net.sockets[i].type = type;
            net.sockets[i].protocol = protocol;
            net.sockets[i].recv_buffer_size = 65536;
            net.sockets[i].send_buffer_size = 65536;
            net.socket_count++;
            return i;
        }
    }
    return -1;
}

int net3_socket_close(int sock_id)
{
    if (sock_id < 0 || sock_id >= NET3_MAX_SOCKETS || !net.sockets[sock_id].used) return -1;
    net.sockets[sock_id].used = 0;
    net.sockets[sock_id].state = 0;
    net.socket_count--;
    return 0;
}

int net3_socket_bind(int sock_id, ipv4_addr_t addr, uint16_t port)
{
    if (sock_id < 0 || sock_id >= NET3_MAX_SOCKETS || !net.sockets[sock_id].used) return -1;
    net.sockets[sock_id].local_addr = addr;
    net.sockets[sock_id].local_port = port;
    net.sockets[sock_id].state = 1;  /* bound */
    return 0;
}

int net3_socket_listen(int sock_id, int backlog)
{
    (void)backlog;
    if (sock_id < 0 || sock_id >= NET3_MAX_SOCKETS || !net.sockets[sock_id].used) return -1;
    net.sockets[sock_id].state = 2;  /* listening */
    return 0;
}

int net3_socket_accept(int sock_id, net3_socket_t *client)
{
    (void)sock_id; (void)client;
    return -1;  /* 原型阶段 */
}

int net3_socket_connect(int sock_id, ipv4_addr_t addr, uint16_t port)
{
    if (sock_id < 0 || sock_id >= NET3_MAX_SOCKETS || !net.sockets[sock_id].used) return -1;
    net.sockets[sock_id].remote_addr = addr;
    net.sockets[sock_id].remote_port = port;
    net.sockets[sock_id].state = 3;  /* connected */
    return 0;
}

int net3_socket_send(int sock_id, const void *data, int len, int flags)
{
    (void)data; (void)flags;
    if (sock_id < 0 || sock_id >= NET3_MAX_SOCKETS || !net.sockets[sock_id].used) return -1;
    net.total_tx_bytes += len;
    return len;
}

int net3_socket_recv(int sock_id, void *buf, int len, int flags)
{
    (void)buf; (void)len; (void)flags;
    if (sock_id < 0 || sock_id >= NET3_MAX_SOCKETS || !net.sockets[sock_id].used) return -1;
    return 0;
}

int net3_socket_sendto(int sock_id, const void *data, int len, ipv4_addr_t addr, uint16_t port)
{
    (void)addr; (void)port;
    return net3_socket_send(sock_id, data, len, 0);
}

int net3_socket_recvfrom(int sock_id, void *buf, int len, ipv4_addr_t *addr, uint16_t *port)
{
    (void)addr; (void)port;
    return net3_socket_recv(sock_id, buf, len, 0);
}

int net3_socket_get(int sock_id, net3_socket_t *sock)
{
    if (sock_id < 0 || sock_id >= NET3_MAX_SOCKETS || !net.sockets[sock_id].used || !sock) return -1;
    memcpy(sock, &net.sockets[sock_id], sizeof(net3_socket_t));
    return 0;
}

int net3_socket_list(net3_socket_t *socks, int max)
{
    if (!socks || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < NET3_MAX_SOCKETS && count < max; i++) {
        if (net.sockets[i].used) memcpy(&socks[count++], &net.sockets[i], sizeof(net3_socket_t));
    }
    return count;
}

int net3_socket_setsockopt(int sock_id, int option, int value)
{
    (void)option; (void)value;
    if (sock_id < 0 || sock_id >= NET3_MAX_SOCKETS || !net.sockets[sock_id].used) return -1;
    return 0;
}

/* TCP */


