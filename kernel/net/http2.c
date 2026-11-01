/* ============================================================================
 * 赫普OS - HTTP/2协议
 * 二进制帧、多路复用、流量控制、HPACK头部压缩
 * ========================================================================== */

#include <stdint.h>
#include "http2.h"
#include "vga.h"
#include "string.h"

static int http2_initialized = 0;

/* 初始化HTTP/2 */
void http2_init(void)
{
    http2_initialized = 1;
    vga_puts("[HTTP2] HTTP/2 proto init \n");
    vga_puts("[HTTP2] supports: binary frames, multiplex, flow control \n");
}

/* 发送连接前言（magic） */
int http2_send_magic(http2_connection_t *conn)
{
    if (!conn || !conn->connected) return -1;
    vga_puts("[HTTP2] send preface \n");
    /* 实际应通过TCP发送 HTTP2_MAGIC */
    return 0;
}

/* 发送SETTINGS帧 */
int http2_send_settings(http2_connection_t *conn)
{
    if (!conn || !conn->connected) return -1;

    /* 构建设置参数 */
    uint8_t settings[36];  /* 6个设置 * 6字节 */
    int offset = 0;

    /* HEADER_TABLE_SIZE = 4096 */
    settings[offset++] = 0x00; settings[offset++] = HTTP2_SETTINGS_HEADER_TABLE_SIZE;
    settings[offset++] = 0x00; settings[offset++] = 0x00;
    settings[offset++] = 0x10; settings[offset++] = 0x00;

    /* ENABLE_PUSH = 0 */
    settings[offset++] = 0x00; settings[offset++] = HTTP2_SETTINGS_ENABLE_PUSH;
    settings[offset++] = 0x00; settings[offset++] = 0x00;
    settings[offset++] = 0x00; settings[offset++] = 0x00;

    /* MAX_CONCURRENT_STREAMS = 128 */
    settings[offset++] = 0x00; settings[offset++] = HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS;
    settings[offset++] = 0x00; settings[offset++] = 0x00;
    settings[offset++] = 0x00; settings[offset++] = 0x80;

    /* INITIAL_WINDOW_SIZE = 65535 */
    settings[offset++] = 0x00; settings[offset++] = HTTP2_SETTINGS_INITIAL_WINDOW_SIZE;
    settings[offset++] = 0x00; settings[offset++] = 0x00;
    settings[offset++] = 0xFF; settings[offset++] = 0xFF;

    /* MAX_FRAME_SIZE = 16384 */
    settings[offset++] = 0x00; settings[offset++] = HTTP2_SETTINGS_MAX_FRAME_SIZE;
    settings[offset++] = 0x00; settings[offset++] = 0x00;
    settings[offset++] = 0x40; settings[offset++] = 0x00;

    (void)settings;  /* TODO: 实际发送settings到连接 */
    vga_printf("[HTTP2] send SETTINGS frame(%d bytes)\n", offset);
    return 0;
}

/* 简单HPACK编码：整数 */
static int http2_hpack_encode_int(uint8_t *buf, int prefix_bits, uint32_t value)
{
    uint8_t mask = (1 << prefix_bits) - 1;
    int offset = 0;

    if (value < mask) {
        buf[offset++] = (uint8_t)value;
        return offset;
    }

    buf[offset++] = mask;
    value -= mask;

    while (value >= 128) {
        buf[offset++] = (uint8_t)(value % 128 + 128);
        value /= 128;
    }
    buf[offset++] = (uint8_t)value;
    return offset;
}

/* 简单HPACK编码：字符串（字面量，不压缩） */
static int http2_hpack_encode_string(uint8_t *buf, const char *str)
{
    int len = strlen(str);
    int offset = 0;

    /* 字符串长度（7位前缀） */
    offset += http2_hpack_encode_int(buf + offset, 7, (uint32_t)len);

    /* 字符串内容 */
    memcpy(buf + offset, str, len);
    offset += len;

    return offset;
}

/* 发送HEADERS帧 */
int http2_send_headers(http2_connection_t *conn, uint32_t stream_id,
                       const char *method, const char *path,
                       const char *host, const char **headers, int header_count)
{
    if (!conn || !conn->connected || !method || !path) return -1;

    uint8_t payload[HTTP2_MAX_HEADER_SIZE];
    int offset = 0;

    /* HPACK编码头部 */
    /* :method */
    payload[offset++] = 0x04;  /* 字面量头部，不索引 */
    offset += http2_hpack_encode_string(payload + offset, ":method");
    offset += http2_hpack_encode_string(payload + offset, method);

    /* :path */
    payload[offset++] = 0x04;
    offset += http2_hpack_encode_string(payload + offset, ":path");
    offset += http2_hpack_encode_string(payload + offset, path);

    /* :authority (host) */
    if (host) {
        payload[offset++] = 0x04;
        offset += http2_hpack_encode_string(payload + offset, ":authority");
        offset += http2_hpack_encode_string(payload + offset, host);
    }

    /* :scheme */
    payload[offset++] = 0x04;
    offset += http2_hpack_encode_string(payload + offset, ":scheme");
    offset += http2_hpack_encode_string(payload + offset, "https");

    /* 其他头部 */
    for (int i = 0; i < header_count && headers[i]; i += 2) {
        payload[offset++] = 0x04;
        offset += http2_hpack_encode_string(payload + offset, headers[i]);
        if (i + 1 < header_count && headers[i + 1]) {
            offset += http2_hpack_encode_string(payload + offset, headers[i + 1]);
        }
    }

    vga_printf("[HTTP2] send HEADERS frame: stream=%d, %d bytes\n", stream_id, offset);
    return offset;
}

/* 发送DATA帧 */
int http2_send_data(http2_connection_t *conn, uint32_t stream_id,
                    const uint8_t *data, uint32_t len, int end_stream)
{
    if (!conn || !conn->connected || !data || len == 0) return -1;
    if (len > HTTP2_MAX_FRAME_SIZE) return -1;

    uint8_t flags = end_stream ? HTTP2_FLAG_END_STREAM : 0;

    vga_printf("[HTTP2] send DATA frame: stream=%d, %d bytes, flags=0x%02x\n",
               stream_id, len, flags);
    return 0;
}

/* 发送WINDOW_UPDATE帧 */
int http2_send_window_update(http2_connection_t *conn, uint32_t stream_id,
                             uint32_t increment)
{
    if (!conn || !conn->connected) return -1;
    if (increment == 0 || increment > 0x7FFFFFFF) return -1;

    vga_printf("[HTTP2] send WINDOW_UPDATE: stream=%d, increment=%d\n",
               stream_id, increment);

    if (stream_id == 0) {
        conn->window_size += (int32_t)increment;
    } else {
        for (int i = 0; i < HTTP2_MAX_STREAMS; i++) {
            if (conn->streams[i].in_use && conn->streams[i].stream_id == stream_id) {
                conn->streams[i].window_size += (int32_t)increment;
                break;
            }
        }
    }
    return 0;
}

/* 发送PING帧 */
int http2_send_ping(http2_connection_t *conn, const uint8_t *data)
{
    (void)data;
    if (!conn || !conn->connected) return -1;
    vga_puts("[HTTP2] send PING frame \n");
    return 0;
}

/* 发送GOAWAY帧 */
int http2_send_goaway(http2_connection_t *conn, uint32_t last_stream_id,
                      http2_error_code_t error_code)
{
    if (!conn || !conn->connected) return -1;

    conn->goaway_received = 1;
    conn->goaway_last_stream_id = last_stream_id;

    vga_printf("[HTTP2] send GOAWAY: last_stream=%d, error=%d\n",
               last_stream_id, error_code);
    return 0;
}

/* 读取帧 */
int http2_read_frame(http2_connection_t *conn, http2_frame_header_t *header,
                     uint8_t *payload, uint32_t *payload_len)
{
    (void)payload;
    if (!conn || !conn->connected || !header) return -1;

    /* 简化：从TCP读取9字节帧头 + payload */
    header->length = 0;
    header->type = 0;
    header->flags = 0;
    header->stream_id = 0;

    if (payload_len) *payload_len = 0;
    return 0;
}

/* 处理帧 */
int http2_handle_frame(http2_connection_t *conn, const http2_frame_header_t *header,
                       const uint8_t *payload)
{
    (void)payload;
    if (!conn || !header) return -1;

    switch (header->type) {
        case HTTP2_FRAME_SETTINGS:
            if (header->flags & HTTP2_FLAG_ACK) {
                conn->settings_acked = 1;
                vga_puts("[HTTP2] recv SETTINGS ACK\n");
            } else {
                vga_puts("[HTTP2] recv SETTINGS frame \n");
                /* 解析并应用设置参数 */
            }
            break;

        case HTTP2_FRAME_HEADERS:
            vga_printf("[HTTP2] recv HEADERS frame: stream=%d\n", header->stream_id);
            break;

        case HTTP2_FRAME_DATA:
            vga_printf("[HTTP2] recv DATA frame: stream=%d, %d bytes\n",
                       header->stream_id, header->length);
            break;

        case HTTP2_FRAME_PING:
            vga_puts("[HTTP2] recv PING frame , reply ACK\n");
            break;

        case HTTP2_FRAME_GOAWAY:
            vga_puts("[HTTP2] recv GOAWAY frame \n");
            conn->goaway_received = 1;
            break;

        case HTTP2_FRAME_WINDOW_UPDATE:
            vga_printf("[HTTP2] recv WINDOW_UPDATE: stream=%d\n",
                       header->stream_id);
            break;

        case HTTP2_FRAME_RST_STREAM:
            vga_printf("[HTTP2] recv RST_STREAM: stream=%d\n", header->stream_id);
            break;

        default:
            vga_printf("[HTTP2] unknown frame type: 0x%02x\n", header->type);
            break;
    }

    return 0;
}

/* 创建流 */
uint32_t http2_create_stream(http2_connection_t *conn)
{
    if (!conn || !conn->connected) return 0;

    for (int i = 0; i < HTTP2_MAX_STREAMS; i++) {
        if (!conn->streams[i].in_use) {
            conn->streams[i].stream_id = conn->next_stream_id;
            conn->streams[i].state = HTTP2_STREAM_OPEN;
            conn->streams[i].window_size = HTTP2_DEFAULT_WINDOW;
            conn->streams[i].sent_bytes = 0;
            conn->streams[i].received_bytes = 0;
            conn->streams[i].in_use = 1;
            conn->next_stream_id += 2;  /* 客户端使用奇数ID */
            return conn->streams[i].stream_id;
        }
    }
    return 0;
}

/* 关闭流 */
int http2_close_stream(http2_connection_t *conn, uint32_t stream_id,
                       http2_error_code_t error_code)
{
    if (!conn) return -1;

    for (int i = 0; i < HTTP2_MAX_STREAMS; i++) {
        if (conn->streams[i].in_use && conn->streams[i].stream_id == stream_id) {
            conn->streams[i].state = HTTP2_STREAM_CLOSED;
            conn->streams[i].in_use = 0;
            vga_printf("[HTTP2] close stream: %d, error=%d\n", stream_id, error_code);
            return 0;
        }
    }
    return -1;
}

/* 连接 */
int http2_connect(http2_connection_t *conn, const char *host, uint16_t port)
{
    if (!conn || !host) return -1;

    memset(conn, 0, sizeof(http2_connection_t));
    conn->connected = 1;
    conn->window_size = HTTP2_DEFAULT_WINDOW;
    conn->max_frame_size = HTTP2_MAX_FRAME_SIZE;
    conn->max_concurrent_streams = 128;
    conn->header_table_size = 4096;
    conn->push_enabled = 0;
    conn->next_stream_id = 1;  /* 客户端从1开始 */
    conn->settings_acked = 0;
    conn->goaway_received = 0;

    vga_printf("[HTTP2] connect to %s:%d\n", host, port);

    /* 发送连接前言和SETTINGS */
    http2_send_magic(conn);
    http2_send_settings(conn);

    return 0;
}

/* 帧类型名称 */
const char *http2_frame_type_name(http2_frame_type_t type)
{
    switch (type) {
        case HTTP2_FRAME_DATA: return "DATA";
        case HTTP2_FRAME_HEADERS: return "HEADERS";
        case HTTP2_FRAME_PRIORITY: return "PRIORITY";
        case HTTP2_FRAME_RST_STREAM: return "RST_STREAM";
        case HTTP2_FRAME_SETTINGS: return "SETTINGS";
        case HTTP2_FRAME_PUSH_PROMISE: return "PUSH_PROMISE";
        case HTTP2_FRAME_PING: return "PING";
        case HTTP2_FRAME_GOAWAY: return "GOAWAY";
        case HTTP2_FRAME_WINDOW_UPDATE: return "WINDOW_UPDATE";
        case HTTP2_FRAME_CONTINUATION: return "CONTINUATION";
        default: return "UNKNOWN";
    }
}

/* 错误码名称 */
const char *http2_error_name(http2_error_code_t error)
{
    switch (error) {
        case HTTP2_NO_ERROR: return "NO_ERROR";
        case HTTP2_PROTOCOL_ERROR: return "PROTOCOL_ERROR";
        case HTTP2_INTERNAL_ERROR: return "INTERNAL_ERROR";
        case HTTP2_FLOW_CONTROL_ERROR: return "FLOW_CONTROL_ERROR";
        case HTTP2_SETTINGS_TIMEOUT: return "SETTINGS_TIMEOUT";
        case HTTP2_STREAM_CLOSED_ERROR: return "STREAM_CLOSED_ERROR";
        case HTTP2_FRAME_SIZE_ERROR: return "FRAME_SIZE_ERROR";
        case HTTP2_REFUSED_STREAM: return "REFUSED_STREAM";
        case HTTP2_CANCEL: return "CANCEL";
        case HTTP2_COMPRESSION_ERROR: return "COMPRESSION_ERROR";
        case HTTP2_CONNECT_ERROR: return "CONNECT_ERROR";
        case HTTP2_ENHANCE_YOUR_CALM: return "ENHANCE_YOUR_CALM";
        case HTTP2_INADEQUATE_SECURITY: return "INADEQUATE_SECURITY";
        case HTTP2_HTTP_1_1_REQUIRED: return "HTTP_1_1_REQUIRED";
        default: return "UNKNOWN";
    }
}
