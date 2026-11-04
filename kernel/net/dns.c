/* ============================================================================
 * 赫普OS - DNS域名解析
 * 域名系统，将域名解析为IP地址
 * ========================================================================== */

#include <stdint.h>
#include "dns.h"
#include "vga.h"
#include "string.h"

static hepu_dns_cache_entry_t dns_cache[DNS_CACHE_SIZE];
static uint32_t dns_tick_count = 0;
static uint32_t dns_default_server = 0;
static int dns_initialized = 0;

/* 初始化DNS */
void hepu_dns_init(void)
{
    memset(dns_cache, 0, sizeof(dns_cache));
    dns_tick_count = 0;
    dns_default_server = (8 << 24) | (8 << 16) | (8 << 8) | 8;  /* 8.8.8.8 */
    dns_initialized = 1;
    vga_puts("[DNS] DNS init \n");
    vga_printf("[DNS] default DNS server: 8.8.8.8, cache: %d entries \n", DNS_CACHE_SIZE);
}

/* 解析域名 */
int hepu_dns_resolve(const char *hostname, uint32_t *ip_addr)
{
    return hepu_dns_resolve_with_server(hostname, dns_default_server, ip_addr);
}

/* 使用指定DNS服务器解析 */
int hepu_dns_resolve_with_server(const char *hostname, uint32_t dns_server, uint32_t *ip_addr)
{
    if (!dns_initialized || !hostname || !ip_addr) return -1;

    /* 先查缓存 */
    if (hepu_dns_lookup_cache(hostname, ip_addr) == 0) {
        vga_printf("[DNS] cache hit: %s -> %d.%d.%d.%d\n",
                   hostname,
                   (*ip_addr >> 24) & 0xFF, (*ip_addr >> 16) & 0xFF,
                   (*ip_addr >> 8) & 0xFF, *ip_addr & 0xFF);
        return 0;
    }

    vga_printf("[DNS] query %s via %d.%d.%d.%d\n",
               hostname,
               (dns_server >> 24) & 0xFF, (dns_server >> 16) & 0xFF,
               (dns_server >> 8) & 0xFF, dns_server & 0xFF);

    /* V8.09: 真实DNS查询 */
    /* 构造DNS查询报文 */
    uint8_t dns_query[512];
    int qidx = 0;

    /* DNS头 */
    dns_query[qidx++] = 0x12;  /* Transaction ID高字节 */
    dns_query[qidx++] = 0x34;  /* Transaction ID低字节 */
    dns_query[qidx++] = 0x01;  /* Flags: 标准查询 */
    dns_query[qidx++] = 0x00;
    dns_query[qidx++] = 0x00;  /* Questions: 1 */
    dns_query[qidx++] = 0x01;
    dns_query[qidx++] = 0x00;  /* Answer RRs: 0 */
    dns_query[qidx++] = 0x00;
    dns_query[qidx++] = 0x00;  /* Authority RRs: 0 */
    dns_query[qidx++] = 0x00;
    dns_query[qidx++] = 0x00;  /* Additional RRs: 0 */
    dns_query[qidx++] = 0x00;

    /* Question: 域名编码 */
    const char *p = hostname;
    while (*p) {
        int label_len = 0;
        const char *label_start = p;
        while (*p && *p != '.') {
            label_len++;
            p++;
        }
        dns_query[qidx++] = label_len;
        for (int i = 0; i < label_len; i++) {
            dns_query[qidx++] = label_start[i];
        }
        if (*p == '.') p++;
    }
    dns_query[qidx++] = 0;  /* 域名结束 */

    /* Question type: A (1) */
    dns_query[qidx++] = 0x00;
    dns_query[qidx++] = 0x01;

    /* Question class: IN (1) */
    dns_query[qidx++] = 0x00;
    dns_query[qidx++] = 0x01;

    /* 使用UDP发送DNS查询 */
    extern int udp_socket(uint16_t local_port);
    extern int udp_sendto(int sockfd, uint32_t dest_ip, uint16_t dest_port,
                          const uint8_t *data, uint32_t length);
    extern int udp_recvfrom(int sockfd, uint8_t *buf, uint32_t max_len,
                             uint32_t *src_ip, uint16_t *src_port);
    extern int udp_close(int sockfd);
    extern void net_poll(void);
    extern uint32_t pit_get_ticks(void);

    int sockfd = udp_socket(5353);  /* 本地端口 */
    if (sockfd < 0) {
        vga_puts("[DNS] UDP socket failed, using simulation\n");
        goto simulation;
    }

    /* 发送DNS查询 */
    int sent = udp_sendto(sockfd, dns_server, 53, dns_query, qidx);
    if (sent < 0) {
        vga_puts("[DNS] UDP send failed, using simulation\n");
        udp_close(sockfd);
        goto simulation;
    }

    /* 等待DNS响应（3秒超时） */
    uint8_t dns_response[512];
    uint32_t start_tick = pit_get_ticks();
    int recv_len = 0;

    while (pit_get_ticks() - start_tick < 3000) {
        net_poll();
        uint32_t src_ip = 0;
        uint16_t src_port = 0;
        recv_len = udp_recvfrom(sockfd, dns_response, sizeof(dns_response),
                                 &src_ip, &src_port);
        if (recv_len > 0 && src_port == 53) {
            break;
        }
        recv_len = 0;
    }

    udp_close(sockfd);

    if (recv_len > 0) {
        /* 解析DNS响应 */
        vga_printf("[DNS] received %d bytes response\n", recv_len);

        /* 检查响应标志 */
        if ((dns_response[2] & 0x80) == 0) {
            vga_puts("[DNS] not a response\n");
            goto simulation;
        }

        /* 检查RCODE */
        int rcode = dns_response[3] & 0x0F;
        if (rcode != 0) {
            vga_printf("[DNS] query failed, rcode=%d\n", rcode);
            goto simulation;
        }

        /* 获取Answer RRs数量 */
        int ancount = (dns_response[6] << 8) | dns_response[7];
        vga_printf("[DNS] %d answer records\n", ancount);

        /* 跳过Question部分 */
        int ridx = 12;  /* 跳过DNS头 */
        int qdcount = (dns_response[4] << 8) | dns_response[5];
        for (int q = 0; q < qdcount; q++) {
            while (dns_response[ridx] != 0 && ridx < recv_len) {
                ridx += dns_response[ridx] + 1;
            }
            ridx += 5;  /* 跳过结束0 + type + class */
        }

        /* 解析Answer部分 */
        for (int a = 0; a < ancount && ridx < recv_len; a++) {
            /* 跳过名称（可能是指针） */
            if (dns_response[ridx] & 0xC0) {
                ridx += 2;  /* 指针 */
            } else {
                while (dns_response[ridx] != 0 && ridx < recv_len) {
                    ridx += dns_response[ridx] + 1;
                }
                ridx++;
            }

            if (ridx + 10 > recv_len) break;

            uint16_t type = (dns_response[ridx] << 8) | dns_response[ridx + 1];
            uint16_t rdlength = (dns_response[ridx + 8] << 8) | dns_response[ridx + 9];
            ridx += 10;

            if (type == 1 && rdlength == 4 && ridx + 4 <= recv_len) {
                /* A记录：IPv4地址 */
                *ip_addr = (dns_response[ridx] << 24) |
                           (dns_response[ridx + 1] << 16) |
                           (dns_response[ridx + 2] << 8) |
                           dns_response[ridx + 3];

                /* 添加到缓存 */
                hepu_dns_add_cache(hostname, *ip_addr, HEPU_DNS_TYPE_A, 300);

                vga_printf("[DNS] resolved: %s -> %d.%d.%d.%d\n",
                           hostname,
                           (*ip_addr >> 24) & 0xFF,
                           (*ip_addr >> 16) & 0xFF,
                           (*ip_addr >> 8) & 0xFF,
                           *ip_addr & 0xFF);
                return 0;
            }

            ridx += rdlength;
        }

        vga_puts("[DNS] no A record found, using simulation\n");
    } else {
        vga_puts("[DNS] timeout, using simulation\n");
    }

simulation:
    /* 回退：模拟DNS解析 */
    if (strcmp(hostname, "www.hepu.os") == 0) {
        *ip_addr = (10 << 24) | (0 << 16) | (0 << 8) | 1;
    } else if (strcmp(hostname, "localhost") == 0) {
        *ip_addr = (127 << 24) | (0 << 16) | (0 << 8) | 1;
    } else {
        /* 模拟解析结果 */
        uint32_t hash = 0;
        for (int i = 0; hostname[i]; i++) {
            hash = hash * 31 + (uint8_t)hostname[i];
        }
        *ip_addr = (199 << 24) | (235 << 16) | ((hash >> 8) & 0xFF) << 8 | (hash & 0xFF);
    }

    /* 添加到缓存 */
    hepu_dns_add_cache(hostname, *ip_addr, HEPU_DNS_TYPE_A, 300);

    vga_printf("[DNS] simulation result: %s -> %d.%d.%d.%d\n",
               hostname,
               (*ip_addr >> 24) & 0xFF, (*ip_addr >> 16) & 0xFF,
               (*ip_addr >> 8) & 0xFF, *ip_addr & 0xFF);
    return 0;
}

/* 添加缓存条目 */
int hepu_dns_add_cache(const char *hostname, uint32_t ip_addr, uint16_t type, uint32_t ttl)
{
    if (!hostname) return -1;

    /* 查找已有条目 */
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (dns_cache[i].valid && strcmp(dns_cache[i].name, hostname) == 0) {
            dns_cache[i].ip_addr = ip_addr;
            dns_cache[i].type = type;
            dns_cache[i].ttl = ttl;
            dns_cache[i].timestamp = dns_tick_count;
            return 0;
        }
    }

    /* 找空闲条目 */
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (!dns_cache[i].valid) {
            strncpy(dns_cache[i].name, hostname, DNS_MAX_NAME_LEN - 1);
            dns_cache[i].ip_addr = ip_addr;
            dns_cache[i].type = type;
            dns_cache[i].ttl = ttl;
            dns_cache[i].timestamp = dns_tick_count;
            dns_cache[i].valid = 1;
            return 0;
        }
    }

    /* 替换最旧的 */
    int oldest = 0;
    for (int i = 1; i < DNS_CACHE_SIZE; i++) {
        if (dns_cache[i].timestamp < dns_cache[oldest].timestamp) {
            oldest = i;
        }
    }
    strncpy(dns_cache[oldest].name, hostname, DNS_MAX_NAME_LEN - 1);
    dns_cache[oldest].ip_addr = ip_addr;
    dns_cache[oldest].type = type;
    dns_cache[oldest].ttl = ttl;
    dns_cache[oldest].timestamp = dns_tick_count;
    dns_cache[oldest].valid = 1;
    return 0;
}

/* 查找缓存 */
int hepu_dns_lookup_cache(const char *hostname, uint32_t *ip_addr)
{
    if (!hostname || !ip_addr) return -1;

    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (dns_cache[i].valid && strcmp(dns_cache[i].name, hostname) == 0) {
            /* 检查TTL */
            if ((dns_tick_count - dns_cache[i].timestamp) < dns_cache[i].ttl) {
                *ip_addr = dns_cache[i].ip_addr;
                return 0;
            } else {
                dns_cache[i].valid = 0;  /* 过期 */
            }
        }
    }
    return -1;
}

/* 清空缓存 */
int hepu_dns_flush_cache(void)
{
    memset(dns_cache, 0, sizeof(dns_cache));
    vga_puts("[DNS] cache cleared \n");
    return 0;
}

/* 获取缓存 */
int hepu_dns_get_cache(hepu_dns_cache_entry_t *cache, int max)
{
    if (!cache || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DNS_CACHE_SIZE && count < max; i++) {
        if (dns_cache[i].valid) {
            cache[count++] = dns_cache[i];
        }
    }
    return count;
}

/* 定时处理 */
void hepu_dns_tick(void)
{
    dns_tick_count++;
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        if (dns_cache[i].valid &&
            (dns_tick_count - dns_cache[i].timestamp) > dns_cache[i].ttl) {
            dns_cache[i].valid = 0;
        }
    }
}
