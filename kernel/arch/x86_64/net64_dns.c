/* ============================================================================
 * 赫普OS 64位 - DNS客户端（域名系统）
 * 支持A记录查询（域名→IP地址）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

#define DNS_SERVER_PORT 53
#define DNS_CLIENT_PORT 5353

/* DNS头结构 */
typedef struct {
    uint16_t id;          /* 事务ID */
    uint16_t flags;       /* 标志 */
    uint16_t qdcount;     /* 问题数 */
    uint16_t ancount;     /* 回答数 */
    uint16_t nscount;     /* 权威记录数 */
    uint16_t arcount;     /* 附加记录数 */
} __attribute__((packed)) dns_header_t;

/* DNS问题结构 */
typedef struct {
    uint16_t qtype;       /* 查询类型 */
    uint16_t qclass;      /* 查询类 */
} __attribute__((packed)) dns_question_t;

/* DNS资源记录 */
typedef struct {
    uint16_t type;        /* 记录类型 */
    uint16_t class;       /* 记录类 */
    uint32_t ttl;         /* 生存时间 */
    uint16_t rdlength;    /* 数据长度 */
} __attribute__((packed)) dns_rr_t;

#define DNS_TYPE_A     1
#define DNS_TYPE_CNAME 5
#define DNS_CLASS_IN   1

#define DNS_MAX_CACHE 8

typedef struct {
    char name[64];
    uint32_t ip;
    int valid;
    int ttl;
} dns_cache_t;

extern void vga_puts(const char *s);
extern int udp64_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                       const uint8_t *data, int data_len);
extern int udp64_bind(uint16_t port);
extern int udp64_recv(int sock, uint8_t *buffer, int max_len,
                      uint32_t *src_ip, uint16_t *src_port);
extern uint32_t dhcp64_get_dns(void);

static void dns_memcpy(void *dst, const void *src, int n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}

static int dns_strlen(const char *s) {
    int i = 0;
    while (s[i]) i++;
    return i;
}

static int dns_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

static uint16_t htons16(uint16_t v) { return (v >> 8) | (v << 8); }
static uint32_t htonl32(uint32_t v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
           ((v & 0xFF0000) >> 8) | ((v & 0xFF000000) >> 24);
}
static uint16_t ntohs16(uint16_t v) { return htons16(v); }
static uint32_t ntohl32(uint32_t v) { return htonl32(v); }

static dns_cache_t dns_cache[DNS_MAX_CACHE];
static int dns_initialized = 0;
static uint16_t dns_id = 0x1234;
static int dns_udp_sock = -1;

/* 初始化DNS */
void dns64_init(void)
{
    for (int i = 0; i < DNS_MAX_CACHE; i++) dns_cache[i].valid = 0;
    dns_initialized = 1;
    vga_puts("[DNS64] DNS resolver initialized\n");
}

/* 将域名转换为DNS格式（长度前缀） */
static int dns_encode_name(const char *name, uint8_t *buf)
{
    int pos = 0;
    int label_start = 0;
    int len = dns_strlen(name);

    for (int i = 0; i <= len; i++) {
        if (name[i] == '.' || i == len) {
            int label_len = i - label_start;
            if (label_len > 0 && label_len <= 63) {
                buf[pos++] = label_len;
                for (int j = 0; j < label_len; j++) {
                    buf[pos++] = name[label_start + j];
                }
            }
            label_start = i + 1;
        }
    }
    buf[pos++] = 0;  /* 结束 */
    return pos;
}

/* 解析DNS名称（处理压缩指针） */
static int dns_parse_name(const uint8_t *packet, int packet_len, int offset,
                           char *name, int name_max)
{
    int pos = offset;
    int name_pos = 0;
    int jumped = 0;
    int new_offset = offset;

    while (pos < packet_len) {
        uint8_t len = packet[pos];

        if (len == 0) {
            pos++;
            if (!jumped) new_offset = pos;
            break;
        }

        if ((len & 0xC0) == 0xC0) {
            /* 压缩指针 */
            if (pos + 1 >= packet_len) break;
            uint16_t ptr = ((len & 0x3F) << 8) | packet[pos + 1];
            if (!jumped) new_offset = pos + 2;
            pos = ptr;
            jumped = 1;
            continue;
        }

        pos++;
        if (pos + len > packet_len) break;

        if (name_pos > 0 && name_pos < name_max - 1) {
            name[name_pos++] = '.';
        }
        for (int i = 0; i < len && name_pos < name_max - 1; i++) {
            name[name_pos++] = packet[pos + i];
        }
        pos += len;
    }

    name[name_pos] = 0;
    return jumped ? new_offset : pos;
}

/* DNS查询（同步，简化版） */
uint32_t dns64_resolve(const char *hostname)
{
    if (!dns_initialized || !hostname) return 0;

    /* 检查缓存 */
    for (int i = 0; i < DNS_MAX_CACHE; i++) {
        if (dns_cache[i].valid && dns_strcmp(dns_cache[i].name, hostname) == 0) {
            return dns_cache[i].ip;
        }
    }

    uint32_t dns_server = dhcp64_get_dns();
    if (dns_server == 0) return 0;

    if (dns_udp_sock < 0) {
        dns_udp_sock = udp64_bind(DNS_CLIENT_PORT);
        if (dns_udp_sock < 0) return 0;
    }

    /* 构建DNS查询 */
    uint8_t packet[512];
    dns_header_t *dns = (dns_header_t *)packet;
    dns->id = htons16(dns_id++);
    dns->flags = htons16(0x0100);  /* 标准查询, 递归请求 */
    dns->qdcount = htons16(1);
    dns->ancount = 0;
    dns->nscount = 0;
    dns->arcount = 0;

    int pos = sizeof(dns_header_t);
    pos += dns_encode_name(hostname, packet + pos);

    dns_question_t *q = (dns_question_t *)(packet + pos);
    q->qtype = htons16(DNS_TYPE_A);
    q->qclass = htons16(DNS_CLASS_IN);
    pos += sizeof(dns_question_t);

    /* 发送查询 */
    if (udp64_send(dns_server, DNS_CLIENT_PORT, DNS_SERVER_PORT, packet, pos) < 0) {
        return 0;
    }

    /* 等待响应（简化：轮询几次） */
    for (int retry = 0; retry < 10; retry++) {
        uint8_t response[512];
        uint32_t src_ip;
        uint16_t src_port;
        int len = udp64_recv(dns_udp_sock, response, sizeof(response), &src_ip, &src_port);

        if (len > 0 && src_port == DNS_SERVER_PORT) {
            dns_header_t *resp = (dns_header_t *)response;
            if (ntohs16(resp->id) != dns_id - 1) continue;

            int answer_count = ntohs16(resp->ancount);
            int offset = sizeof(dns_header_t);

            /* 跳过问题部分 */
            char qname[128];
            offset = dns_parse_name(response, len, offset, qname, sizeof(qname));
            offset += sizeof(dns_question_t);

            /* 解析回答 */
            for (int i = 0; i < answer_count && offset < len; i++) {
                char aname[128];
                offset = dns_parse_name(response, len, offset, aname, sizeof(aname));

                if (offset + sizeof(dns_rr_t) > len) break;
                dns_rr_t *rr = (dns_rr_t *)(response + offset);
                offset += sizeof(dns_rr_t);

                uint16_t rtype = ntohs16(rr->type);
                uint16_t rdlen = ntohs16(rr->rdlength);

                if (rtype == DNS_TYPE_A && rdlen == 4 && offset + 4 <= len) {
                    uint32_t ip;
                    dns_memcpy(&ip, response + offset, 4);
                    ip = ntohl32(ip);

                    /* 加入缓存 */
                    for (int j = 0; j < DNS_MAX_CACHE; j++) {
                        if (!dns_cache[j].valid) {
                            int nlen = dns_strlen(hostname);
                            if (nlen > 63) nlen = 63;
                            dns_memcpy(dns_cache[j].name, hostname, nlen);
                            dns_cache[j].name[nlen] = 0;
                            dns_cache[j].ip = ip;
                            dns_cache[j].valid = 1;
                            dns_cache[j].ttl = ntohl32(rr->ttl);
                            break;
                        }
                    }
                    return ip;
                }
                offset += rdlen;
            }
            return 0;  /* 没有A记录 */
        }
    }

    return 0;  /* 超时 */
}
