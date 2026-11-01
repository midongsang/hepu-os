/* ============================================================================
 * 赫普OS - DNS over QUIC (DoQ)
 * RFC 9250: 基于QUIC的DNS解析
 * ========================================================================== */

#include <stdint.h>
#include "types.h"
#include "doq.h"
#include "quic.h"
#include "csprng.h"
#include "vga.h"
#include "string.h"

static int doq_initialized = 0;
static u32 doq_total_queries = 0;
static u32 doq_success_count = 0;

/* DNS查询类型 */
#define DNS_TYPE_A 1
#define DNS_TYPE_AAAA 28
#define DNS_CLASS_IN 1

/* 构建DNS查询消息 */
static int doq_build_query(const char *hostname, u16 qid,
                           u16 qtype, u8 *buf, u32 *buf_len)
{
    int offset = 0;

    /* 头部: 12字节 */
    buf[offset++] = (qid >> 8) & 0xFF;  /* ID高字节 */
    buf[offset++] = qid & 0xFF;         /* ID低字节 */
    buf[offset++] = 0x01;               /* 标志: QR=0, RD=1 */
    buf[offset++] = 0x00;               /* 标志 */
    buf[offset++] = 0x00; buf[offset++] = 0x01;  /* QDCOUNT=1 */
    buf[offset++] = 0x00; buf[offset++] = 0x00;  /* ANCOUNT=0 */
    buf[offset++] = 0x00; buf[offset++] = 0x00;  /* NSCOUNT=0 */
    buf[offset++] = 0x00; buf[offset++] = 0x00;  /* ARCOUNT=0 */

    /* QNAME: 标签编码 */
    const char *p = hostname;
    while (*p) {
        const char *label_start = p;
        while (*p && *p != '.') p++;
        u8 label_len = (u8)(p - label_start);
        buf[offset++] = label_len;
        memcpy(buf + offset, label_start, label_len);
        offset += label_len;
        if (*p == '.') p++;
    }
    buf[offset++] = 0x00;  /* 根标签 */

    /* QTYPE */
    buf[offset++] = (qtype >> 8) & 0xFF;
    buf[offset++] = qtype & 0xFF;

    /* QCLASS */
    buf[offset++] = (DNS_CLASS_IN >> 8) & 0xFF;
    buf[offset++] = DNS_CLASS_IN & 0xFF;

    *buf_len = offset;
    return 0;
}

/* 解析DNS名称（处理压缩指针） */
static int doq_parse_name(const u8 *msg, u32 msg_len,
                          u32 offset, char *name, u32 name_len)
{
    u32 pos = offset;
    u32 name_pos = 0;
    int jumped = 0;
    u32 end_offset = 0;

    while (pos < msg_len) {
        u8 len = msg[pos];

        if (len == 0) {
            pos++;
            break;
        }

        if ((len & 0xC0) == 0xC0) {
            /* 压缩指针 */
            if (!jumped) end_offset = pos + 2;
            pos = ((len & 0x3F) << 8) | msg[pos + 1];
            jumped = 1;
            continue;
        }

        pos++;
        if (name_pos > 0 && name_pos < name_len - 1) {
            name[name_pos++] = '.';
        }
        for (int i = 0; i < len && pos < msg_len && name_pos < name_len - 1; i++) {
            name[name_pos++] = (char)msg[pos++];
        }
    }

    name[name_pos] = '\0';
    return jumped ? end_offset : pos;
}

/* 初始化DoQ */
void doq_init(void)
{
    doq_initialized = 1;
    doq_total_queries = 0;
    doq_success_count = 0;
    vga_puts("[DoQ] DNS over QUIC init \n");
    vga_puts("[DoQ] RFC 9250, default port 853\n");
}

/* 连接到DoQ服务器 */
int doq_connect(doq_connection_t *conn, const char *server, u16 port)
{
    if (!doq_initialized || !conn || !server) return -1;

    memset(conn, 0, sizeof(doq_connection_t));
    strncpy(conn->server_host, server, sizeof(conn->server_host) - 1);
    conn->server_port = port ? port : DOQ_DEFAULT_PORT;
    conn->next_query_id = (u16)csprng_get_uint32();

    if (quic_connect(&conn->quic, server, conn->server_port) != 0) {
        return -1;
    }

    conn->connected = 1;
    vga_printf("[DoQ] connect to %s:%d\n", server, conn->server_port);
    return 0;
}

/* 关闭DoQ连接 */
int doq_close(doq_connection_t *conn)
{
    if (!conn) return -1;
    quic_close(&conn->quic, 0, "");
    conn->connected = 0;
    vga_puts("[DoQ] conn closed \n");
    return 0;
}

/* 异步DNS查询 */
int doq_resolve_async(doq_connection_t *conn, const char *hostname,
                      u16 qtype, u16 *query_id)
{
    if (!conn || !conn->connected || !hostname) return -1;

    /* 查找空闲查询槽 */
    doq_query_t *query = NULL;
    for (int i = 0; i < DOQ_MAX_QUERIES; i++) {
        if (!conn->queries[i].in_use) {
            query = &conn->queries[i];
            break;
        }
    }
    if (!query) return -1;

    memset(query, 0, sizeof(doq_query_t));
    query->query_id = conn->next_query_id++;
    query->query_type = qtype;
    query->query_class = DNS_CLASS_IN;
    strncpy(query->hostname, hostname, sizeof(query->hostname) - 1);
    query->state = DOQ_QUERY_IDLE;
    query->in_use = 1;

    /* 创建QUIC流 */
    if (quic_create_stream(&conn->quic, &query->stream_id) != 0) {
        query->in_use = 0;
        return -1;
    }

    /* 构建DNS查询消息 */
    u8 dns_msg[DOQ_MAX_DNS_MESSAGE];
    u32 msg_len;
    doq_build_query(hostname, query->query_id, qtype, dns_msg, &msg_len);

    /* 发送（DoQ要求每个查询在独立的流上，且流必须以FIN结束） */
    quic_stream_send(&conn->quic, query->stream_id, dns_msg, msg_len, 1);

    query->state = DOQ_QUERY_SENT;
    query->timestamp = 0;  /* 简化：不记录时间戳 */
    doq_total_queries++;

    if (query_id) *query_id = query->query_id;

    vga_printf("[DoQ] query: %s(id=%d, type=%d)\n", hostname, query->query_id, qtype);
    return 0;
}

/* 同步DNS查询（A记录） */
int doq_resolve(doq_connection_t *conn, const char *hostname,
                u16 qtype, u32 *ip)
{
    if (!conn || !hostname || !ip) return -1;

    u16 query_id;
    if (doq_resolve_async(conn, hostname, qtype, &query_id) != 0) {
        return -1;
    }

    /* 简化：直接模拟响应 */
    for (int i = 0; i < DOQ_MAX_QUERIES; i++) {
        if (conn->queries[i].in_use && conn->queries[i].query_id == query_id) {
            conn->queries[i].state = DOQ_QUERY_RECEIVED;
            conn->queries[i].response_len = 0;
            doq_success_count++;

            /* 简化：返回模拟IP */
            *ip = 0x08080808;  /* 8.8.8.8 */
            return 0;
        }
    }

    return -1;
}

/* 获取查询响应 */
int doq_get_response(doq_connection_t *conn, u16 query_id,
                     u8 *response, u32 *resp_len)
{
    if (!conn || !response) return -1;

    for (int i = 0; i < DOQ_MAX_QUERIES; i++) {
        if (conn->queries[i].in_use && conn->queries[i].query_id == query_id) {
            if (conn->queries[i].state != DOQ_QUERY_RECEIVED) return -1;

            u32 len = conn->queries[i].response_len;
            memcpy(response, conn->queries[i].response, len);
            if (resp_len) *resp_len = len;

            conn->queries[i].in_use = 0;
            return 0;
        }
    }
    return -1;
}

/* 解析A记录（IPv4） */
int doq_parse_a_record(const u8 *response, u32 len, u32 *ip)
{
    if (!response || len < 12 || !ip) return -1;

    /* 跳过头部(12字节)和问题部分 */
    u32 offset = 12;
    char name[256];
    offset = doq_parse_name(response, len, offset, name, sizeof(name));
    offset += 4;  /* QTYPE + QCLASS */

    /* 解析回答部分 */
    u16 ancount = (response[6] << 8) | response[7];
    for (int i = 0; i < ancount; i++) {
        offset = doq_parse_name(response, len, offset, name, sizeof(name));
        if (offset + 10 > len) return -1;

        u16 rtype = (response[offset] << 8) | response[offset + 1];
        u16 rdlen = (response[offset + 8] << 8) | response[offset + 9];
        offset += 10;

        if (rtype == DNS_TYPE_A && rdlen == 4 && offset + 4 <= len) {
            *ip = (response[offset] << 24) |
                  (response[offset + 1] << 16) |
                  (response[offset + 2] << 8) |
                  response[offset + 3];
            return 0;
        }
        offset += rdlen;
    }

    return -1;
}

/* 解析AAAA记录（IPv6） */
int doq_parse_aaaa_record(const u8 *response, u32 len, u8 *ip)
{
    if (!response || len < 12 || !ip) return -1;

    u32 offset = 12;
    char name[256];
    offset = doq_parse_name(response, len, offset, name, sizeof(name));
    offset += 4;

    u16 ancount = (response[6] << 8) | response[7];
    for (int i = 0; i < ancount; i++) {
        offset = doq_parse_name(response, len, offset, name, sizeof(name));
        if (offset + 10 > len) return -1;

        u16 rtype = (response[offset] << 8) | response[offset + 1];
        u16 rdlen = (response[offset + 8] << 8) | response[offset + 9];
        offset += 10;

        if (rtype == DNS_TYPE_AAAA && rdlen == 16 && offset + 16 <= len) {
            memcpy(ip, response + offset, 16);
            return 0;
        }
        offset += rdlen;
    }

    return -1;
}

/* 获取统计信息 */
int doq_get_stats(doq_connection_t *conn, u32 *active_queries,
                  u32 *total_queries, u32 *success_count)
{
    if (!conn) return -1;

    u32 active = 0;
    for (int i = 0; i < DOQ_MAX_QUERIES; i++) {
        if (conn->queries[i].in_use) active++;
    }

    if (active_queries) *active_queries = active;
    if (total_queries) *total_queries = doq_total_queries;
    if (success_count) *success_count = doq_success_count;

    return 0;
}

/* 查询状态名称 */
const char *doq_query_state_name(doq_query_state_t state)
{
    switch (state) {
        case DOQ_QUERY_IDLE: return "IDLE";
        case DOQ_QUERY_SENT: return "SENT";
        case DOQ_QUERY_RECEIVED: return "RECEIVED";
        case DOQ_QUERY_TIMEOUT: return "TIMEOUT";
        case DOQ_QUERY_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}
