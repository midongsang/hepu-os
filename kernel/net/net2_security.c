/* ============================================================================
 * 赫普OS - 网络安全功能（从net2.c拆分）
 * 防火墙 / Cookie管理 / 网络共享
 * ========================================================================== */

#include <stdint.h>
#include "net2.h"
#include "vga.h"
#include "string.h"

extern net2_t net2;

int firewall_enable(int enable)
{
    net2.firewall_enabled = enable ? 1 : 0;
    vga_printf("[NET2] firewall %s\n", enable ? " enabled " : " disabled ");
    return 0;
}

int firewall_add_rule(fw_action_t action, fw_direction_t direction,
                      uint32_t src_ip, uint32_t src_mask, int src_port,
                      uint32_t dst_ip, uint32_t dst_mask, int dst_port,
                      int protocol, int priority, const char *desc)
{
    for (int i = 0; i < NET2_MAX_FIREWALL_RULES; i++) {
        if (!net2.firewall_rules[i].used) {
            memset(&net2.firewall_rules[i], 0, sizeof(firewall_rule_t));
            net2.firewall_rules[i].used = 1;
            net2.firewall_rules[i].id = i;
            net2.firewall_rules[i].action = action;
            net2.firewall_rules[i].direction = direction;
            net2.firewall_rules[i].src_ip = src_ip;
            net2.firewall_rules[i].src_mask = src_mask;
            net2.firewall_rules[i].src_port = src_port;
            net2.firewall_rules[i].dst_ip = dst_ip;
            net2.firewall_rules[i].dst_mask = dst_mask;
            net2.firewall_rules[i].dst_port = dst_port;
            net2.firewall_rules[i].protocol = protocol;
            net2.firewall_rules[i].priority = priority;
            net2.firewall_rules[i].enabled = 1;
            if (desc) strncpy(net2.firewall_rules[i].description, desc, 127);
            net2.firewall_rule_count++;
            return i;
        }
    }
    return -1;
}

int firewall_remove_rule(int rule_id)
{
    if (rule_id < 0 || rule_id >= NET2_MAX_FIREWALL_RULES || !net2.firewall_rules[rule_id].used) return -1;
    net2.firewall_rules[rule_id].used = 0;
    net2.firewall_rule_count--;
    return 0;
}

int firewall_enable_rule(int rule_id, int enable)
{
    if (rule_id < 0 || rule_id >= NET2_MAX_FIREWALL_RULES || !net2.firewall_rules[rule_id].used) return -1;
    net2.firewall_rules[rule_id].enabled = enable ? 1 : 0;
    return 0;
}

int firewall_list_rules(firewall_rule_t *rules, int max)
{
    if (!rules || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < NET2_MAX_FIREWALL_RULES && count < max; i++) {
        if (net2.firewall_rules[i].used) memcpy(&rules[count++], &net2.firewall_rules[i], sizeof(firewall_rule_t));
    }
    return count;
}

int firewall_check(fw_direction_t direction, uint32_t src_ip, int src_port,
                   uint32_t dst_ip, int dst_port, int protocol)
{
    (void)src_ip; (void)src_port; (void)dst_ip; (void)dst_port;
    if (!net2.firewall_enabled) return FW_ACTION_ACCEPT;
    /* 检查规则 */
    for (int i = 0; i < NET2_MAX_FIREWALL_RULES; i++) {
        firewall_rule_t *r = &net2.firewall_rules[i];
        if (!r->used || !r->enabled) continue;
        if (r->direction != direction && r->direction != FW_DIR_BOTH) continue;
        if (r->protocol != 0 && r->protocol != protocol) continue;
        r->hit_count++;
        return r->action;
    }
    /* 默认策略 */
    return direction == FW_DIR_INBOUND ? net2.default_inbound : net2.default_outbound;
}

int firewall_set_default(int inbound, int outbound)
{
    net2.default_inbound = inbound;
    net2.default_outbound = outbound;
    return 0;
}

int cookie_set(const char *domain, const char *path, const char *name, const char *value,
               uint64_t expires, int secure, int http_only)
{
    if (!domain || !name || !value) return -1;
    for (int i = 0; i < NET2_MAX_COOKIES; i++) {
        if (net2.cookies[i].used && strcmp(net2.cookies[i].domain, domain) == 0 &&
            strcmp(net2.cookies[i].name, name) == 0) {
            strncpy(net2.cookies[i].value, value, 255);
            net2.cookies[i].expires = expires;
            return 0;
        }
    }
    for (int i = 0; i < NET2_MAX_COOKIES; i++) {
        if (!net2.cookies[i].used) {
            memset(&net2.cookies[i], 0, sizeof(cookie_t));
            net2.cookies[i].used = 1;
            strncpy(net2.cookies[i].domain, domain, 127);
            strncpy(net2.cookies[i].path, path ? path : "/", 127);
            strncpy(net2.cookies[i].name, name, 63);
            strncpy(net2.cookies[i].value, value, 255);
            net2.cookies[i].expires = expires;
            net2.cookies[i].secure = secure;
            net2.cookies[i].http_only = http_only;
            net2.cookie_count++;
            return 0;
        }
    }
    return -1;
}

int cookie_get(const char *domain, const char *path, const char *name, char *value, int len)
{
    (void)path;
    if (!domain || !name || !value || len <= 0) return -1;
    for (int i = 0; i < NET2_MAX_COOKIES; i++) {
        if (net2.cookies[i].used && strcmp(net2.cookies[i].domain, domain) == 0 &&
            strcmp(net2.cookies[i].name, name) == 0) {
            strncpy(value, net2.cookies[i].value, len - 1);
            value[len - 1] = '\0';
            return 0;
        }
    }
    return -1;
}

int cookie_delete(const char *domain, const char *name)
{
    (void)name;
    if (!domain) return -1;
    for (int i = 0; i < NET2_MAX_COOKIES; i++) {
        if (net2.cookies[i].used && strcmp(net2.cookies[i].domain, domain) == 0 &&
            strcmp(net2.cookies[i].name, name) == 0) {
            net2.cookies[i].used = 0;
            net2.cookie_count--;
            return 0;
        }
    }
    return -1;
}

int cookie_list(cookie_t *cookies, int max)
{
    if (!cookies || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < NET2_MAX_COOKIES && count < max; i++) {
        if (net2.cookies[i].used) memcpy(&cookies[count++], &net2.cookies[i], sizeof(cookie_t));
    }
    return count;
}

int cookie_clear(void)
{
    for (int i = 0; i < NET2_MAX_COOKIES; i++) net2.cookies[i].used = 0;
    net2.cookie_count = 0;
    return 0;
}

int net2_tethering_enable(int enable)
{
    (void)enable;
    vga_printf("[NET2] net sharing %s\n", enable ? " enabled " : " disabled ");
    return 0;
}

int net2_tethering_get_clients(int *clients, int max)
{
    (void)clients; (void)max;
    return 0;
}

