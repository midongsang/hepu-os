/* ============================================================================
 * 赫普OS - QUIC协议
 * RFC 9000: 基于UDP的低延迟传输协议
 * ========================================================================== */

#include <stdint.h>
#include "quic.h"
#include "csprng.h"
#include "vga.h"
#include "string.h"

static int quic_initialized = 0;
static quic_connection_t connections[QUIC_MAX_CONNECTIONS];

/* QUIC可变长度整数编码 */
static int quic_encode_varint(uint64_t value, uint8_t *buf)
{
    if (value < 64) {
        buf[0] = (uint8_t)value;
        return 1;
    } else if (value < 16384) {
        buf[0] = (uint8_t)(0x40 | (value >> 8));
        buf[1] = (uint8_t)(value & 0xFF);
        return 2;
    } else if (value < 1073741824) {
        buf[0] = (uint8_t)(0x80 | (value >> 24));
        buf[1] = (uint8_t)((value >> 16) & 0xFF);
        buf[2] = (uint8_t)((value >> 8) & 0xFF);
        buf[3] = (uint8_t)(value & 0xFF);
        return 4;
    } else {
        buf[0] = (uint8_t)(0xC0 | (value >> 56));
        buf[1] = (uint8_t)((value >> 48) & 0xFF);
        buf[2] = (uint8_t)((value >> 40) & 0xFF);
        buf[3] = (uint8_t)((value >> 32) & 0xFF);
        buf[4] = (uint8_t)((value >> 24) & 0xFF);
        buf[5] = (uint8_t)((value >> 16) & 0xFF);
        buf[6] = (uint8_t)((value >> 8) & 0xFF);
        buf[7] = (uint8_t)(value & 0xFF);
        return 8;
    }
}

/* QUIC可变长度整数解码 */
static int __attribute__((unused)) quic_decode_varint(const uint8_t *buf, uint64_t *value)
{
    uint8_t first = buf[0];
    uint8_t prefix = first >> 6;
    int len = 1 << prefix;

    *value = first & 0x3F;
    for (int i = 1; i < len; i++) {
        *value = (*value << 8) | buf[i];
    }
    return len;
}

/* 初始化QUIC */
void quic_init(void)
{
    memset(connections, 0, sizeof(connections));
    quic_initialized = 1;
    vga_puts("[QUIC] proto init \n");
    vga_puts("[QUIC] RFC 9000, based on UDP, multiplex \n");
}

/* 连接到服务器 */
int quic_connect(quic_connection_t *conn, const char *host, uint16_t port)
{
    if (!quic_initialized || !conn || !host) return -1;

    memset(conn, 0, sizeof(quic_connection_t));

    /* 生成连接ID */
    conn->scid_len = 8;
    csprng_get_bytes(conn->scid, conn->scid_len);
    conn->dcid_len = 8;
    csprng_get_bytes(conn->dcid, conn->dcid_len);

    /* 设置传输参数 */
    conn->max_data = QUIC_INITIAL_MAX_DATA;
    conn->max_stream_data_bidi = QUIC_INITIAL_MAX_STREAM_DATA;
    conn->max_stream_data_uni = QUIC_INITIAL_MAX_STREAM_DATA;
    conn->max_streams_bidi = QUIC_INITIAL_MAX_STREAMS_BIDI;
    conn->max_streams_uni = QUIC_INITIAL_MAX_STREAMS_UNI;
    conn->idle_timeout = QUIC_IDLE_TIMEOUT;
    conn->max_ack_delay = QUIC_MAX_ACK_DELAY;
    conn->packet_number = 0;
    conn->largest_acked = 0;
    conn->next_stream_id = 0;
    conn->rtt = 100;  /* 初始RTT 100ms */
    conn->rtt_var = 50;
    conn->state = QUIC_CONN_INITIAL;

    vga_printf("[QUIC] connect to %s:%d\n", host, port);
    vga_printf("[QUIC] SCID: %02x%02x%02x%02x...\n",
               conn->scid[0], conn->scid[1], conn->scid[2], conn->scid[3]);

    return 0;
}

/* 关闭连接 */
int quic_close(quic_connection_t *conn, uint64_t error_code, const char *reason)
{
    if (!conn) return -1;

    conn->state = QUIC_CONN_CLOSING;
    vga_printf("[QUIC] close connect: error=%llu, reason=%s\n",
               (unsigned long long)error_code, reason ? reason : "");

    conn->state = QUIC_CONN_CLOSED;
    return 0;
}

/* 创建流 */
int quic_create_stream(quic_connection_t *conn, uint64_t *stream_id)
{
    if (!conn || !stream_id) return -1;
    if (conn->state != QUIC_CONN_ESTABLISHED) return -1;

    for (int i = 0; i < QUIC_MAX_STREAMS; i++) {
        if (!conn->streams[i].in_use) {
            conn->streams[i].stream_id = conn->next_stream_id;
            conn->streams[i].state = QUIC_STREAM_OPEN;
            conn->streams[i].offset = 0;
            conn->streams[i].max_data = conn->max_stream_data_bidi;
            conn->streams[i].buffer_len = 0;
            conn->streams[i].in_use = 1;
            *stream_id = conn->next_stream_id;
            conn->next_stream_id += 4;  /* 客户端发起的双向流: 0,4,8... */
            vga_printf("[QUIC] create stream: %llu\n", (unsigned long long)*stream_id);
            return 0;
        }
    }
    return -1;
}

/* 发送流数据 */
int quic_stream_send(quic_connection_t *conn, uint64_t stream_id,
                     const uint8_t *data, uint32_t len, int fin)
{
    if (!conn || !data || len == 0) return -1;

    for (int i = 0; i < QUIC_MAX_STREAMS; i++) {
        if (conn->streams[i].in_use && conn->streams[i].stream_id == stream_id) {
            if (conn->streams[i].state != QUIC_STREAM_OPEN &&
                conn->streams[i].state != QUIC_STREAM_HALF_CLOSED_REMOTE) {
                return -1;
            }

            /* 流量控制检查 */
            if (conn->streams[i].offset + len > conn->streams[i].max_data) {
                return -1;  /* 流级流量控制限制 */
            }

            conn->streams[i].offset += len;
            conn->bytes_sent += len;

            if (fin) {
                conn->streams[i].state = QUIC_STREAM_HALF_CLOSED_LOCAL;
            }

            vga_printf("[QUIC] streams %llu send %d bytes%s\n",
                       (unsigned long long)stream_id, len, fin ? " (FIN)" : "");
            return len;
        }
    }
    return -1;
}

/* 接收流数据 */
int quic_stream_receive(quic_connection_t *conn, uint64_t stream_id,
                        uint8_t *buffer, uint32_t buf_len, uint32_t *received)
{
    if (!conn || !buffer) return -1;

    for (int i = 0; i < QUIC_MAX_STREAMS; i++) {
        if (conn->streams[i].in_use && conn->streams[i].stream_id == stream_id) {
            uint32_t copy_len = conn->streams[i].buffer_len;
            if (copy_len > buf_len) copy_len = buf_len;
            memcpy(buffer, conn->streams[i].buffer, copy_len);
            if (received) *received = copy_len;

            /* 移除已读取数据 */
            if (copy_len < conn->streams[i].buffer_len) {
                memmove(conn->streams[i].buffer,
                        conn->streams[i].buffer + copy_len,
                        conn->streams[i].buffer_len - copy_len);
            }
            conn->streams[i].buffer_len -= copy_len;

            return 0;
        }
    }
    return -1;
}

/* 重置流 */
int quic_stream_reset(quic_connection_t *conn, uint64_t stream_id, uint64_t error_code)
{
    if (!conn) return -1;

    for (int i = 0; i < QUIC_MAX_STREAMS; i++) {
        if (conn->streams[i].in_use && conn->streams[i].stream_id == stream_id) {
            conn->streams[i].state = QUIC_STREAM_CLOSED;
            vga_printf("[QUIC] reset stream %llu: error=%llu\n",
                       (unsigned long long)stream_id, (unsigned long long)error_code);
            return 0;
        }
    }
    return -1;
}

/* 发送PING */
int quic_send_ping(quic_connection_t *conn)
{
    if (!conn) return -1;
    vga_puts("[QUIC] send PING\n");
    return 0;
}

/* 处理收到的包 */
int quic_handle_packet(quic_connection_t *conn, const uint8_t *packet, uint32_t len)
{
    if (!conn || !packet || len < 1) return -1;

    conn->packets_received++;
    conn->bytes_received += len;

    /* 解析包头 */
    uint8_t first_byte = packet[0];
    int is_long_header = (first_byte & 0x80) != 0;

    if (is_long_header) {
        uint8_t packet_type = (first_byte >> 4) & 0x03;
        vga_printf("[QUIC] recv long packages header: type=%d, len=%d\n", packet_type, len);

        if (packet_type == QUIC_PACKET_INITIAL) {
            conn->state = QUIC_CONN_HANDSHAKE;
        } else if (packet_type == QUIC_PACKET_HANDSHAKE) {
            conn->state = QUIC_CONN_ESTABLISHED;
            vga_puts("[QUIC] handshake done, established \n");
        }
    } else {
        /* 短包头 (1-RTT) */
        vga_printf("[QUIC] recv short packages header: len=%d\n", len);
    }

    return 0;
}

/* 构建包 */
int quic_build_packet(quic_connection_t *conn, uint8_t *buffer, uint32_t *buf_len)
{
    if (!conn || !buffer || !buf_len) return -1;

    uint32_t offset = 0;

    /* 长包头: 固定位(1) + 包类型(2) + 保留位(2) + 包号长度(2) */
    uint8_t first_byte = 0xC0 | (QUIC_PACKET_INITIAL << 4) | 0x00;
    buffer[offset++] = first_byte;

    /* 版本 */
    buffer[offset++] = 0x00;
    buffer[offset++] = 0x00;
    buffer[offset++] = 0x00;
    buffer[offset++] = 0x01;  /* draft-01? 实际应为0xff00001d */

    /* DCID长度和DCID */
    buffer[offset++] = conn->dcid_len;
    memcpy(buffer + offset, conn->dcid, conn->dcid_len);
    offset += conn->dcid_len;

    /* SCID长度和SCID */
    buffer[offset++] = conn->scid_len;
    memcpy(buffer + offset, conn->scid, conn->scid_len);
    offset += conn->scid_len;

    /* 令牌长度(0) */
    buffer[offset++] = 0;

    /* 包长度 */
    offset += quic_encode_varint(10, buffer + offset);

    /* 包号 */
    buffer[offset++] = (uint8_t)(conn->packet_number & 0xFF);
    conn->packet_number++;

    /* PING帧 */
    buffer[offset++] = QUIC_FRAME_PING;

    *buf_len = offset;
    conn->packets_sent++;
    conn->bytes_sent += offset;

    return 0;
}

/* 获取统计信息 */
int quic_get_stats(quic_connection_t *conn, uint64_t *sent, uint64_t *received,
                   uint32_t *rtt, uint32_t *streams)
{
    if (!conn) return -1;

    if (sent) *sent = conn->bytes_sent;
    if (received) *received = conn->bytes_received;
    if (rtt) *rtt = conn->rtt;
    if (streams) {
        uint32_t count = 0;
        for (int i = 0; i < QUIC_MAX_STREAMS; i++) {
            if (conn->streams[i].in_use) count++;
        }
        *streams = count;
    }
    return 0;
}

/* 连接状态名称 */
const char *quic_state_name(quic_conn_state_t state)
{
    switch (state) {
        case QUIC_CONN_INITIAL: return "INITIAL";
        case QUIC_CONN_HANDSHAKE: return "HANDSHAKE";
        case QUIC_CONN_ESTABLISHED: return "ESTABLISHED";
        case QUIC_CONN_CLOSING: return "CLOSING";
        case QUIC_CONN_DRAINING: return "DRAINING";
        case QUIC_CONN_CLOSED: return "CLOSED";
        default: return "UNKNOWN";
    }
}

/* 帧类型名称 */
const char *quic_frame_name(quic_frame_type_t type)
{
    switch (type) {
        case QUIC_FRAME_PADDING: return "PADDING";
        case QUIC_FRAME_PING: return "PING";
        case QUIC_FRAME_ACK: return "ACK";
        case QUIC_FRAME_RESET_STREAM: return "RESET_STREAM";
        case QUIC_FRAME_STOP_SENDING: return "STOP_SENDING";
        case QUIC_FRAME_CRYPTO: return "CRYPTO";
        case QUIC_FRAME_NEW_TOKEN: return "NEW_TOKEN";
        case QUIC_FRAME_STREAM: return "STREAM";
        case QUIC_FRAME_MAX_DATA: return "MAX_DATA";
        case QUIC_FRAME_MAX_STREAM_DATA: return "MAX_STREAM_DATA";
        case QUIC_FRAME_MAX_STREAMS_BIDI: return "MAX_STREAMS_BIDI";
        case QUIC_FRAME_MAX_STREAMS_UNI: return "MAX_STREAMS_UNI";
        case QUIC_FRAME_NEW_CONNECTION_ID: return "NEW_CONNECTION_ID";
        case QUIC_FRAME_CONNECTION_CLOSE: return "CONNECTION_CLOSE";
        case QUIC_FRAME_HANDSHAKE_DONE: return "HANDSHAKE_DONE";
        default: return "UNKNOWN";
    }
}
