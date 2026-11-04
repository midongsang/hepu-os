#ifndef DNS_H
#define DNS_H

#include <stdint.h>

#define DNS_SERVER_PORT 53
#define DNS_MAX_NAME_LEN 255
#define DNS_CACHE_SIZE 32
#define DNS_CACHE_TIMEOUT 300  /* 秒 */

/* DNS查询类型 */
#define HEPU_DNS_TYPE_A     1   /* IPv4地址 */
#define HEPU_DNS_TYPE_NS    2   /* 名称服务器 */
#define HEPU_DNS_TYPE_CNAME 5   /* 规范名称 */
#define HEPU_DNS_TYPE_SOA   6   /* 起始授权 */
#define HEPU_DNS_TYPE_PTR   12  /* 指针 */
#define HEPU_DNS_TYPE_MX    15  /* 邮件交换 */
#define HEPU_DNS_TYPE_TXT   16  /* 文本 */
#define HEPU_DNS_TYPE_AAAA  28  /* IPv6地址 */

/* DNS查询类 */
#define HEPU_DNS_CLASS_IN 1  /* 互联网 */

/* DNS标志 */
#define DNS_FLAG_QR     0x8000  /* 查询/响应 */
#define DNS_FLAG_AA     0x0400  /* 权威回答 */
#define DNS_FLAG_TC     0x0200  /* 截断 */
#define DNS_FLAG_RD     0x0100  /* 期望递归 */
#define DNS_FLAG_RA     0x0080  /* 递归可用 */
#define DNS_FLAG_RCODE_MASK 0x000F  /* 响应码 */

/* DNS响应码 */
#define DNS_RCODE_NOERROR  0
#define DNS_RCODE_FORMERR  1
#define DNS_RCODE_SERVFAIL 2
#define DNS_RCODE_NXDOMAIN 3
#define DNS_RCODE_NOTIMP   4
#define DNS_RCODE_REFUSED  5

/* DNS头部 */
typedef struct {
    uint16_t id;          /* 事务ID */
    uint16_t flags;       /* 标志 */
    uint16_t qdcount;     /* 问题数 */
    uint16_t ancount;     /* 回答数 */
    uint16_t nscount;     /* 权威数 */
    uint16_t arcount;     /* 附加数 */
} __attribute__((packed)) hepu_dns_header_t;

/* DNS缓存条目 */
typedef struct {
    char     name[DNS_MAX_NAME_LEN];
    uint32_t ip_addr;
    uint16_t type;
    uint32_t ttl;
    uint32_t timestamp;
    int      valid;
} hepu_dns_cache_entry_t;

void hepu_dns_init(void);
int hepu_dns_resolve(const char *hostname, uint32_t *ip_addr);
int hepu_dns_resolve_with_server(const char *hostname, uint32_t dns_server, uint32_t *ip_addr);
int hepu_dns_add_cache(const char *hostname, uint32_t ip_addr, uint16_t type, uint32_t ttl);
int hepu_dns_lookup_cache(const char *hostname, uint32_t *ip_addr);
int hepu_dns_flush_cache(void);
int hepu_dns_get_cache(hepu_dns_cache_entry_t *cache, int max);
void hepu_dns_tick(void);

#endif /* DNS_H */
