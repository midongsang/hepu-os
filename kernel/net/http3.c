/* ============================================================================
 * 赫普OS - HTTP/3协议
 * RFC 9114: 基于QUIC的HTTP协议
 * ========================================================================== */

#include <stdint.h>
#include "types.h"
#include "http3.h"
#include "quic.h"
#include "vga.h"
#include "string.h"

static int http3_initialized = 0;

/* QPACK静态表索引（部分常用头部） */
static __attribute__((unused)) const char *http3_static_table[][2] = {
    {":authority", ""},           /* 0 */
    {":path", "/"},               /* 1 */
    {"age", "0"},                 /* 2 */
    {"content-disposition", ""},  /* 3 */
    {"content-length", "0"},      /* 4 */
    {"cookie", ""},               /* 5 */
    {"date", ""},                 /* 6 */
    {"etag", ""},                 /* 7 */
    {"if-modified-since", ""},    /* 8 */
    {"if-none-match", ""},        /* 9 */
    {"last-modified", ""},        /* 10 */
    {"link", ""},                 /* 11 */
    {"location", ""},             /* 12 */
    {"referer", ""},              /* 13 */
    {"set-cookie", ""},           /* 14 */
    {":method", "CONNECT"},       /* 15 */
    {":method", "DELETE"},        /* 16 */
    {":method", "GET"},           /* 17 */
    {":method", "HEAD"},          /* 18 */
    {":method", "OPTIONS"},       /* 19 */
    {":method", "POST"},          /* 20 */
    {":method", "PUT"},           /* 21 */
    {":scheme", "http"},          /* 22 */
    {":scheme", "https"},         /* 23 */
    {":status", "103"},           /* 24 */
    {":status", "200"},           /* 25 */
    {":status", "304"},           /* 26 */
    {":status", "404"},           /* 27 */
    {":status", "503"},           /* 28 */
    {"accept", "*/*"},            /* 29 */
    {"accept", "application/dns-message"}, /* 30 */
    {"accept-encoding", "gzip, deflate, br"}, /* 31 */
    {"accept-ranges", "bytes"},   /* 32 */
    {"access-control-allow-headers", "*"}, /* 33 */
    {"access-control-allow-origin", "*"}, /* 34 */
    {"cache-control", "max-age=0"}, /* 35 */
    {"cache-control", "no-cache"}, /* 36 */
    {"cache-control", "no-store"}, /* 37 */
    {"cache-control", "public, max-age=31536000"}, /* 38 */
    {"content-encoding", "br"},   /* 39 */
    {"content-encoding", "gzip"}, /* 40 */
    {"content-type", "application/dns-message"}, /* 41 */
    {"content-type", "application/javascript"}, /* 42 */
    {"content-type", "application/json"}, /* 43 */
    {"content-type", "application/x-www-form-urlencoded"}, /* 44 */
    {"content-type", "image/gif"}, /* 45 */
    {"content-type", "image/jpeg"}, /* 46 */
    {"content-type", "image/png"}, /* 47 */
    {"content-type", "text/css"}, /* 48 */
    {"content-type", "text/html; charset=utf-8"}, /* 49 */
    {"content-type", "text/plain"}, /* 50 */
    {"content-type", "text/plain;charset=utf-8"}, /* 51 */
    {"range", "bytes=0-"},        /* 52 */
    {"strict-transport-security", "max-age=31536000"}, /* 53 */
    {"strict-transport-security", "max-age=31536000; includesubdomains"}, /* 54 */
    {"strict-transport-security", "max-age=31536000; includesubdomains; preload"}, /* 55 */
    {"vary", "accept-encoding"},  /* 56 */
    {"vary", "origin"},           /* 57 */
    {"x-content-type-options", "nosniff"}, /* 58 */
    {"x-xss-protection", "1; mode=block"}, /* 59 */
    {":status", "100"},           /* 60 */
    {":status", "204"},           /* 61 */
    {":status", "206"},           /* 62 */
    {":status", "302"},           /* 63 */
    {":status", "400"},           /* 64 */
    {":status", "403"},           /* 65 */
    {":status", "421"},           /* 66 */
    {":status", "425"},           /* 67 */
    {":status", "500"},           /* 68 */
    {"accept-language", ""},      /* 69 */
    {"access-control-allow-credentials", "FALSE"}, /* 70 */
    {"access-control-allow-credentials", "TRUE"}, /* 71 */
    {"access-control-allow-headers", "cache-control"}, /* 72 */
    {"access-control-allow-headers", "content-type"}, /* 73 */
    {"access-control-allow-methods", "get"}, /* 74 */
    {"access-control-allow-methods", "get, post, options"}, /* 75 */
    {"access-control-expose-headers", "content-length"}, /* 76 */
    {"access-control-request-headers", "content-type"}, /* 77 */
    {"access-control-request-method", "get"}, /* 78 */
    {"access-control-request-method", "post"}, /* 79 */
    {"alt-svc", "clear"},         /* 80 */
    {"authorization", ""},        /* 81 */
    {"cache-control", "no-transform"}, /* 82 */
    {"connection", "keep-alive"}, /* 83 */
    {"content-security-policy", "script-src 'none'; object-src 'none'; base-uri 'none'"}, /* 84 */
    {"cross-origin-embedder-policy", "require-corp"}, /* 85 */
    {"cross-origin-opener-policy", "same-origin"}, /* 86 */
    {"cross-origin-resource-policy", "same-origin"}, /* 87 */
    {"expect-ct", ""},            /* 88 */
    {"forwarded", ""},            /* 89 */
    {"if-range", ""},             /* 90 */
    {"origin", ""},               /* 91 */
    {"purpose", "prefetch"},      /* 92 */
    {"server", ""},               /* 93 */
    {"timing-allow-origin", "*"}, /* 94 */
    {"upgrade-insecure-requests", "1"}, /* 95 */
    {"user-agent", ""},           /* 96 */
    {"x-forwarded-for", ""},      /* 97 */
    {"x-frame-options", "deny"},  /* 98 */
    {"x-frame-options", "sameorigin"} /* 99 */
};

#define HTTP3_STATIC_TABLE_SIZE 100

/* 初始化HTTP/3 */
void http3_init(void)
{
    http3_initialized = 1;
    vga_puts("[HTTP3] HTTP/3 proto init \n");
    vga_puts("[HTTP3] RFC 9114, based on QUIC, QPACK static table(100 entries)\n");
}

/* QPACK简单编码：字面量头部（不索引） */
static int http3_qpack_encode_literal(u8 *buf, const char *name, const char *value)
{
    int offset = 0;
    int name_len = strlen(name);
    int value_len = strlen(value);

    /* 字面量头部，不索引: 0010xxxx (4位前缀) */
    buf[offset++] = 0x20 | (name_len & 0x0F);
    memcpy(buf + offset, name, name_len);
    offset += name_len;

    /* 值长度 (7位前缀) */
    buf[offset++] = (u8)(value_len & 0x7F);
    memcpy(buf + offset, value, value_len);
    offset += value_len;

    return offset;
}

/* QPACK简单编码：静态表引用 */
static int http3_qpack_encode_static(u8 *buf, int index, const char *value)
{
    int offset = 0;
    int value_len = value ? strlen(value) : 0;

    if (value && value_len > 0) {
        /* 静态表索引 + 字面量值: 0101xxxx (4位前缀) */
        buf[offset++] = 0x50 | (index & 0x0F);
        buf[offset++] = (u8)(value_len & 0x7F);
        memcpy(buf + offset, value, value_len);
        offset += value_len;
    } else {
        /* 静态表索引: 11xxxxxx (6位前缀) */
        buf[offset++] = 0xC0 | (index & 0x3F);
    }

    return offset;
}

/* 连接到HTTP/3服务器 */
int http3_connect(http3_connection_t *conn, const char *host, u16 port)
{
    if (!http3_initialized || !conn || !host) return -1;

    memset(conn, 0, sizeof(http3_connection_t));

    /* 建立QUIC连接 */
    if (quic_connect(&conn->quic, host, port) != 0) {
        return -1;
    }

    conn->next_stream_id = 0;
    conn->max_field_section_size = HTTP3_MAX_HEADER_SIZE;
    conn->qpack_max_table_capacity = 4096;
    conn->qpack_blocked_streams = 0;
    conn->settings_received = 0;
    conn->goaway_received = 0;
    conn->connected = 1;

    /* 发送SETTINGS帧（在控制流上） */
    http3_send_settings(conn);

    vga_printf("[HTTP3] connect to %s:%d\n", host, port);
    return 0;
}

/* 关闭HTTP/3连接 */
int http3_close(http3_connection_t *conn)
{
    if (!conn) return -1;

    http3_send_goaway(conn, conn->next_stream_id);
    quic_close(&conn->quic, HTTP3_NO_ERROR, "");
    conn->connected = 0;

    vga_puts("[HTTP3] conn closed \n");
    return 0;
}

/* 发送SETTINGS帧 */
int http3_send_settings(http3_connection_t *conn)
{
    if (!conn) return -1;

    u8 frame[64];
    int offset = 0;

    /* 帧类型: SETTINGS (0x4) */
    frame[offset++] = HTTP3_FRAME_SETTINGS;

    /* 帧长度 */
    frame[offset++] = 12;  /* 3个设置 * 4字节每个 */

    /* QPACK_MAX_TABLE_CAPACITY = 4096 */
    frame[offset++] = HTTP3_SETTINGS_QPACK_MAX_TABLE_CAPACITY;
    frame[offset++] = 0x00; frame[offset++] = 0x00;
    frame[offset++] = 0x10; frame[offset++] = 0x00;

    /* MAX_FIELD_SECTION_SIZE = 8192 */
    frame[offset++] = HTTP3_SETTINGS_MAX_FIELD_SECTION_SIZE;
    frame[offset++] = 0x00; frame[offset++] = 0x00;
    frame[offset++] = 0x20; frame[offset++] = 0x00;

    /* QPACK_BLOCKED_STREAMS = 0 */
    frame[offset++] = HTTP3_SETTINGS_QPACK_BLOCKED_STREAMS;
    frame[offset++] = 0x00; frame[offset++] = 0x00;
    frame[offset++] = 0x00; frame[offset++] = 0x00;

    (void)frame;  /* TODO: 实际发送frame到连接 */
    vga_puts("[HTTP3] send SETTINGS frame \n");
    return offset;
}

/* 发送GOAWAY帧 */
int http3_send_goaway(http3_connection_t *conn, u64 id)
{
    if (!conn) return -1;

    conn->goaway_received = 1;
    conn->goaway_id = id;
    vga_printf("[HTTP3] send GOAWAY: id=%u\n", (uint32_t)id);
    return 0;
}

/* 发送HTTP请求 */
int http3_send_request(http3_connection_t *conn, http3_method_t method,
                       const char *path, const char **headers, int header_count,
                       const u8 *body, u32 body_len, u64 *stream_id)
{
    if (!conn || !conn->connected || !path) return -1;

    /* 创建QUIC流 */
    u64 q_stream_id;
    if (quic_create_stream(&conn->quic, &q_stream_id) != 0) {
        return -1;
    }

    /* 查找请求结构 */
    http3_request_t *req = NULL;
    for (int i = 0; i < HTTP3_MAX_REQUESTS; i++) {
        if (!conn->requests[i].in_use) {
            req = &conn->requests[i];
            break;
        }
    }
    if (!req) return -1;

    memset(req, 0, sizeof(http3_request_t));
    req->stream_id = q_stream_id;
    req->method = method;
    strncpy(req->path, path, sizeof(req->path) - 1);
    req->in_use = 1;

    /* 构建HEADERS帧 */
    u8 header_block[HTTP3_MAX_HEADER_SIZE];
    int hdr_offset = 0;

    /* QPACK前缀: Required Insert Count=0, Delta Base=0 */
    header_block[hdr_offset++] = 0x00;
    header_block[hdr_offset++] = 0x00;

    /* :method */
    int method_index = 17;  /* GET默认 */
    switch (method) {
        case HTTP3_METHOD_GET: method_index = 17; break;
        case HTTP3_METHOD_POST: method_index = 20; break;
        case HTTP3_METHOD_PUT: method_index = 21; break;
        case HTTP3_METHOD_DELETE: method_index = 16; break;
        case HTTP3_METHOD_PATCH: method_index = 20; break;  /* PATCH用POST索引 */
        case HTTP3_METHOD_HEAD: method_index = 18; break;
        case HTTP3_METHOD_OPTIONS: method_index = 19; break;
    }
    hdr_offset += http3_qpack_encode_static(header_block + hdr_offset, method_index, NULL);

    /* :scheme = https */
    hdr_offset += http3_qpack_encode_static(header_block + hdr_offset, 23, NULL);

    /* :path */
    hdr_offset += http3_qpack_encode_static(header_block + hdr_offset, 1, path);

    /* :authority (host) */
    hdr_offset += http3_qpack_encode_literal(header_block + hdr_offset, ":authority", "localhost");

    /* 其他头部 */
    for (int i = 0; i < header_count && headers[i]; i += 2) {
        if (i + 1 < header_count && headers[i + 1]) {
            hdr_offset += http3_qpack_encode_literal(header_block + hdr_offset,
                                                      headers[i], headers[i + 1]);
        }
    }

    /* 发送HEADERS帧 */
    quic_stream_send(&conn->quic, q_stream_id, header_block, hdr_offset, body_len == 0);

    /* 发送DATA帧（如果有body） */
    if (body && body_len > 0) {
        u8 data_frame[HTTP3_MAX_BODY_SIZE + 16];
        int data_offset = 0;
        data_frame[data_offset++] = HTTP3_FRAME_DATA;
        /* 长度（简化：1字节） */
        data_frame[data_offset++] = (u8)(body_len & 0x7F);
        memcpy(data_frame + data_offset, body, body_len);
        data_offset += body_len;

        quic_stream_send(&conn->quic, q_stream_id, data_frame, data_offset, 1);
    }

    if (stream_id) *stream_id = q_stream_id;

    vga_printf("[HTTP3] send request: %s %s(stream=%u)\n",
               http3_method_name(method), path, (uint32_t)q_stream_id);
    return 0;
}

/* 接收响应 */
int http3_receive_response(http3_connection_t *conn, u64 stream_id,
                           u8 *headers, u32 *header_len,
                           u8 *body, u32 *body_len)
{
    if (!conn || !headers || !body) return -1;

    for (int i = 0; i < HTTP3_MAX_REQUESTS; i++) {
        if (conn->requests[i].in_use && conn->requests[i].stream_id == stream_id) {
            if (!conn->requests[i].complete) return -1;

            u32 h_len = conn->requests[i].header_len;
            u32 b_len = conn->requests[i].body_len;

            if (header_len) *header_len = h_len;
            if (body_len) *body_len = b_len;

            memcpy(headers, conn->requests[i].headers, h_len);
            memcpy(body, conn->requests[i].body, b_len);

            conn->requests[i].in_use = 0;
            return 0;
        }
    }
    return -1;
}

/* 处理HTTP/3帧 */
int http3_handle_frame(http3_connection_t *conn, u64 stream_id,
                       const u8 *data, u32 len)
{
    if (!conn || !data || len < 1) return -1;

    u8 frame_type = data[0];
    u32 offset = 1;

    switch (frame_type) {
        case HTTP3_FRAME_DATA:
            vga_printf("[HTTP3] recv DATA frame: stream=%u, len=%d\n",
                       (uint32_t)(unsigned long long)stream_id, len - offset);
            break;

        case HTTP3_FRAME_HEADERS:
            vga_printf("[HTTP3] recv HEADERS frame: stream=%u\n",
                       (uint32_t)(unsigned long long)stream_id);
            break;

        case HTTP3_FRAME_SETTINGS:
            conn->settings_received = 1;
            vga_puts("[HTTP3] recv SETTINGS frame \n");
            break;

        case HTTP3_FRAME_GOAWAY:
            conn->goaway_received = 1;
            vga_puts("[HTTP3] recv GOAWAY frame \n");
            break;

        default:
            vga_printf("[HTTP3] unknown frame type: 0x%02x\n", frame_type);
            break;
    }

    return 0;
}

/* 获取统计信息 */
int http3_get_stats(http3_connection_t *conn, u32 *active_requests,
                    u64 *bytes_sent, u64 *bytes_received)
{
    if (!conn) return -1;

    u32 active = 0;
    for (int i = 0; i < HTTP3_MAX_REQUESTS; i++) {
        if (conn->requests[i].in_use) active++;
    }

    if (active_requests) *active_requests = active;
    quic_get_stats(&conn->quic, bytes_sent, bytes_received, NULL, NULL);

    return 0;
}

/* 请求方法名称 */
const char *http3_method_name(http3_method_t method)
{
    switch (method) {
        case HTTP3_METHOD_GET: return "GET";
        case HTTP3_METHOD_POST: return "POST";
        case HTTP3_METHOD_PUT: return "PUT";
        case HTTP3_METHOD_DELETE: return "DELETE";
        case HTTP3_METHOD_PATCH: return "PATCH";
        case HTTP3_METHOD_HEAD: return "HEAD";
        case HTTP3_METHOD_OPTIONS: return "OPTIONS";
        default: return "UNKNOWN";
    }
}

/* 错误码名称 */
const char *http3_error_name(http3_error_code_t error)
{
    switch (error) {
        case HTTP3_NO_ERROR: return "NO_ERROR";
        case HTTP3_GENERAL_PROTOCOL_ERROR: return "GENERAL_PROTOCOL_ERROR";
        case HTTP3_INTERNAL_ERROR: return "INTERNAL_ERROR";
        case HTTP3_STREAM_CREATION_ERROR: return "STREAM_CREATION_ERROR";
        case HTTP3_CLOSED_CRITICAL_STREAM: return "CLOSED_CRITICAL_STREAM";
        case HTTP3_FRAME_UNEXPECTED: return "FRAME_UNEXPECTED";
        case HTTP3_FRAME_ERROR: return "FRAME_ERROR";
        case HTTP3_EXCESSIVE_LOAD: return "EXCESSIVE_LOAD";
        case HTTP3_ID_ERROR: return "ID_ERROR";
        case HTTP3_SETTINGS_ERROR: return "SETTINGS_ERROR";
        case HTTP3_MISSING_SETTINGS: return "MISSING_SETTINGS";
        case HTTP3_REQUEST_REJECTED: return "REQUEST_REJECTED";
        case HTTP3_REQUEST_CANCELLED: return "REQUEST_CANCELLED";
        case HTTP3_REQUEST_INCOMPLETE: return "REQUEST_INCOMPLETE";
        case HTTP3_MESSAGE_ERROR: return "MESSAGE_ERROR";
        case HTTP3_CONNECT_ERROR: return "CONNECT_ERROR";
        case HTTP3_VERSION_FALLBACK: return "VERSION_FALLBACK";
        case HTTP3_QPACK_DECOMPRESSION_FAILED: return "QPACK_DECOMPRESSION_FAILED";
        default: return "UNKNOWN";
    }
}
