/* ============================================================================
 * 赫普OS - TCP连接管理（从net3.c拆分）
 * ========================================================================== */

#include <stdint.h>
#include "net3.h"
#include "vga.h"
#include "string.h"

extern net3_t net;

int net3_tcp_list(tcp_connection_t *conns, int max)
{
    if (!conns || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < NET3_MAX_TCP_CONNECTIONS && count < max; i++) {
        if (net.tcp_connections[i].used) memcpy(&conns[count++], &net.tcp_connections[i], sizeof(tcp_connection_t));
    }
    return count;
}

int net3_tcp_get(int conn_id, tcp_connection_t *conn)
{
    if (conn_id < 0 || conn_id >= NET3_MAX_TCP_CONNECTIONS || !net.tcp_connections[conn_id].used || !conn) return -1;
    memcpy(conn, &net.tcp_connections[conn_id], sizeof(tcp_connection_t));
    return 0;
}

int net3_tcp_close(int conn_id)
{
    if (conn_id < 0 || conn_id >= NET3_MAX_TCP_CONNECTIONS || !net.tcp_connections[conn_id].used) return -1;
    net.tcp_connections[conn_id].used = 0;
    net.tcp_connections[conn_id].state = TCP_CLOSED;
    net.tcp_count--;
    return 0;
}

/* DNS */
int net3_dns_resolve(const char *hostname, ipv4_addr_t *result)
{
    if (!hostname || !result) return -1;
    /* 检查缓存 */
    for (int i = 0; i < NET3_MAX_DNS_CACHE; i++) {
        if (net.dns_cache[i].used && strcmp(net.dns_cache[i].hostname, hostname) == 0) {
            *result = net.dns_cache[i].ipv4;
            return 0;
        }
    }
    /* 原型阶段: 返回模拟结果 */
    result->addr[0] = 93;
    result->addr[1] = 184;
    result->addr[2] = 216;
    result->addr[3] = 34;
    net.dns_queries++;
    return 0;
}

int net3_dns_resolve6(const char *hostname, ipv6_addr_t *result)
{
    (void)hostname;
    if (!result) return -1;
    memset(result, 0, sizeof(ipv6_addr_t));
    return 0;
}

int net3_dns_add_server(ipv4_addr_t server)
{
    if (net.dns_server_count >= 4) return -1;
    net.dns_servers[net.dns_server_count++] = server;
    return 0;
}

int net3_dns_get_cache(dns_cache_entry_t *cache, int max)
{
    if (!cache || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < NET3_MAX_DNS_CACHE && count < max; i++) {
        if (net.dns_cache[i].used) memcpy(&cache[count++], &net.dns_cache[i], sizeof(dns_cache_entry_t));
    }
    return count;
}

int net3_dns_flush(void)
{
    for (int i = 0; i < NET3_MAX_DNS_CACHE; i++) net.dns_cache[i].used = 0;
    net.dns_count = 0;
    return 0;
}

/* 防火墙 */
int net3_fw_enable(int enable)
{
    net.firewall_enabled = enable ? 1 : 0;
    return 0;
}

int net3_fw_add_rule(net3_firewall_rule_t *rule)
{
    if (!rule) return -1;
    for (int i = 0; i < NET3_MAX_FIREWALL_RULES; i++) {
        if (!net.firewall_rules[i].used) {
            memcpy(&net.firewall_rules[i], rule, sizeof(net3_firewall_rule_t));
            net.firewall_rules[i].used = 1;
            net.firewall_rules[i].id = i;
            net.firewall_count++;
            return i;
        }
    }
    return -1;
}

int net3_fw_remove_rule(int rule_id)
{
    if (rule_id < 0 || rule_id >= NET3_MAX_FIREWALL_RULES || !net.firewall_rules[rule_id].used) return -1;
    net.firewall_rules[rule_id].used = 0;
    net.firewall_count--;
    return 0;
}

int net3_fw_list_rules(net3_firewall_rule_t *rules, int max)
{
    if (!rules || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < NET3_MAX_FIREWALL_RULES && count < max; i++) {
        if (net.firewall_rules[i].used) memcpy(&rules[count++], &net.firewall_rules[i], sizeof(net3_firewall_rule_t));
    }
    return count;
}

int net3_fw_set_default(int chain, int policy)
{
    if (chain == 0) net.default_input_policy = policy;
    else if (chain == 1) net.default_output_policy = policy;
    else if (chain == 2) net.default_forward_policy = policy;
    else return -1;
    return 0;
}

int net3_fw_get_stats(int rule_id, uint64_t *packets, uint64_t *bytes)
{
    if (rule_id < 0 || rule_id >= NET3_MAX_FIREWALL_RULES || !net.firewall_rules[rule_id].used) return -1;
    if (packets) *packets = net.firewall_rules[rule_id].packets;
    if (bytes) *bytes = net.firewall_rules[rule_id].bytes;
    return 0;
}

/* NAT */
int net3_nat_enable(int enable)
{
    net.nat_enabled = enable ? 1 : 0;
    return 0;
}

int net3_nat_add_rule(nat_rule_t *rule)
{
    if (!rule) return -1;
    for (int i = 0; i < NET3_MAX_NAT_RULES; i++) {
        if (!net.nat_rules[i].used) {
            memcpy(&net.nat_rules[i], rule, sizeof(nat_rule_t));
            net.nat_rules[i].used = 1;
            net.nat_rules[i].id = i;
            net.nat_count++;
            return i;
        }
    }
    return -1;
}

int net3_nat_remove_rule(int rule_id)
{
    if (rule_id < 0 || rule_id >= NET3_MAX_NAT_RULES || !net.nat_rules[rule_id].used) return -1;
    net.nat_rules[rule_id].used = 0;
    net.nat_count--;
    return 0;
}

int net3_nat_list_rules(nat_rule_t *rules, int max)
{
    if (!rules || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < NET3_MAX_NAT_RULES && count < max; i++) {
        if (net.nat_rules[i].used) memcpy(&rules[count++], &net.nat_rules[i], sizeof(nat_rule_t));
    }
    return count;
}

/* VPN */
int net3_vpn_add(vpn_config_t *vpn)
{
    if (!vpn) return -1;
    for (int i = 0; i < 4; i++) {
        if (!net.vpns[i].used) {
            memcpy(&net.vpns[i], vpn, sizeof(vpn_config_t));
            net.vpns[i].used = 1;
            net.vpns[i].id = i;
            net.vpn_count++;
            return i;
        }
    }
    return -1;
}

int net3_vpn_remove(int vpn_id)
{
    if (vpn_id < 0 || vpn_id >= 4 || !net.vpns[vpn_id].used) return -1;
    net.vpns[vpn_id].used = 0;
    net.vpn_count--;
    return 0;
}

int net3_vpn_connect(int vpn_id)
{
    if (vpn_id < 0 || vpn_id >= 4 || !net.vpns[vpn_id].used) return -1;
    net.vpns[vpn_id].connected = 1;
    net.active_vpn = vpn_id;
    vga_printf("[NET3] VPN connected: %s\n", net.vpns[vpn_id].name);
    return 0;
}

int net3_vpn_disconnect(int vpn_id)
{
    if (vpn_id < 0 || vpn_id >= 4 || !net.vpns[vpn_id].used) return -1;
    net.vpns[vpn_id].connected = 0;
    if (net.active_vpn == vpn_id) net.active_vpn = -1;
    return 0;
}

int net3_vpn_list(vpn_config_t *vpns, int max)
{
    if (!vpns || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < 4 && count < max; i++) {
        if (net.vpns[i].used) memcpy(&vpns[count++], &net.vpns[i], sizeof(vpn_config_t));
    }
    return count;
}

int net3_vpn_get_status(int vpn_id, vpn_config_t *vpn)
{
    if (vpn_id < 0 || vpn_id >= 4 || !net.vpns[vpn_id].used || !vpn) return -1;
    memcpy(vpn, &net.vpns[vpn_id], sizeof(vpn_config_t));
    return 0;
}

/* 代理 */
int net3_proxy_set(proxy_config_t *proxy)
{
    if (!proxy) return -1;
    memcpy(&net.proxy, proxy, sizeof(proxy_config_t));
    return 0;
}

int net3_proxy_get(proxy_config_t *proxy)
{
    if (!proxy) return -1;
    memcpy(proxy, &net.proxy, sizeof(proxy_config_t));
    return 0;
}

int net3_proxy_enable(int enable)
{
    net.proxy.enabled = enable ? 1 : 0;
    return 0;
}

/* QoS */
