/* ============================================================================
 * 赫普OS - QUIC不可靠数据报(DATAGRAM)
 * RFC 9221: 不可靠数据报扩展
 * ========================================================================== */

#include <stdint.h>
#include "types.h"
#include "quic_datagram.h"
#include "vga.h"
#include "string.h"

/* 初始化DATAGRAM */
void quic_datagram_init(quic_datagram_t *dgram)
{
    if (!dgram) return;

    memset(dgram, 0, sizeof(quic_datagram_t));
    dgram->max_datagram_size = QUIC_DATAGRAM_MAX_SIZE;
    dgram->enabled = 0;

    vga_puts("[QUIC-DGRAM] unreliable datagram init \n");
    vga_puts("[QUIC-DGRAM] RFC 9221, max 1200 bytes \n");
}

/* 启用/禁用DATAGRAM */
int quic_datagram_enable(quic_datagram_t *dgram, int enable)
{
    if (!dgram) return -1;
    dgram->enabled = enable ? 1 : 0;
    vga_printf("[QUIC-DGRAM] %s\n", enable ? " enabled " : " disabled ");
    return 0;
}

/* 检查是否启用 */
int quic_datagram_is_enabled(const quic_datagram_t *dgram)
{
    return dgram && dgram->enabled;
}

/* 发送数据报 */
int quic_datagram_send(quic_datagram_t *dgram, const u8 *data,
                        u32 len)
{
    if (!dgram || !data || len == 0) return -1;
    if (!dgram->enabled) return -1;
    if (len > dgram->max_datagram_size) return -1;

    /* 检查发送队列是否满 */
    if (dgram->send_count >= QUIC_DATAGRAM_QUEUE_SIZE) {
        dgram->total_dropped++;
        vga_puts("[QUIC-DGRAM] send queue full , drop datagram \n");
        return -1;
    }

    /* 入队 */
    memcpy(dgram->send_queue[dgram->send_tail], data, len);
    dgram->send_lens[dgram->send_tail] = len;
    dgram->send_tail = (dgram->send_tail + 1) % QUIC_DATAGRAM_QUEUE_SIZE;
    dgram->send_count++;
    dgram->total_sent++;

    vga_printf("[QUIC-DGRAM] enqueue: %u bytes(queue depth: %u)\n",
               len, dgram->send_count);
    return len;
}

/* 接收数据报 */
int quic_datagram_receive(quic_datagram_t *dgram, u8 *buffer,
                           u32 buf_len, u32 *received)
{
    if (!dgram || !buffer) return -1;

    if (dgram->recv_count == 0) {
        if (received) *received = 0;
        return 0;
    }

    u32 len = dgram->recv_lens[dgram->recv_head];
    if (len > buf_len) len = buf_len;

    memcpy(buffer, dgram->recv_queue[dgram->recv_head], len);
    dgram->recv_head = (dgram->recv_head + 1) % QUIC_DATAGRAM_QUEUE_SIZE;
    dgram->recv_count--;

    if (received) *received = len;

    vga_printf("[QUIC-DGRAM] dequeue: %u bytes\n", len);
    return len;
}

/* 处理收到的DATAGRAM帧 */
int quic_datagram_handle_frame(quic_datagram_t *dgram, const u8 *frame,
                                u32 frame_len)
{
    if (!dgram || !frame || frame_len == 0) return -1;
    if (!dgram->enabled) return -1;

    u32 offset = 0;
    u32 data_len = 0;

    /* 解析帧类型 */
    u8 frame_type = frame[offset++];

    if (frame_type == QUIC_DGRAM_WITH_LEN) {
        /* 有长度字段：读取可变长度整数 */
        if (offset >= frame_len) return -1;

        u8 len_byte = frame[offset];
        u8 len_prefix = (len_byte >> 6) & 0x03;

        switch (len_prefix) {
            case 0:
                data_len = len_byte & 0x3F;
                offset += 1;
                break;
            case 1:
                if (offset + 1 >= frame_len) return -1;
                data_len = ((len_byte & 0x3F) << 8) | frame[offset + 1];
                offset += 2;
                break;
            case 2:
                if (offset + 3 >= frame_len) return -1;
                data_len = ((len_byte & 0x3F) << 24) |
                           (frame[offset + 1] << 16) |
                           (frame[offset + 2] << 8) |
                           frame[offset + 3];
                offset += 4;
                break;
            case 3:
                if (offset + 7 >= frame_len) return -1;
                data_len = ((u64)(len_byte & 0x3F) << 56) |
                           ((u64)frame[offset + 1] << 48) |
                           ((u64)frame[offset + 2] << 40) |
                           ((u64)frame[offset + 3] << 32) |
                           ((u64)frame[offset + 4] << 24) |
                           ((u64)frame[offset + 5] << 16) |
                           ((u64)frame[offset + 6] << 8) |
                           frame[offset + 7];
                offset += 8;
                break;
        }
    } else {
        /* 无长度字段：剩余数据即为数据报内容 */
        data_len = frame_len - offset;
    }

    if (offset + data_len > frame_len) return -1;
    if (data_len > dgram->max_datagram_size) {
        dgram->total_dropped++;
        return -1;
    }

    /* 检查接收队列是否满 */
    if (dgram->recv_count >= QUIC_DATAGRAM_QUEUE_SIZE) {
        dgram->total_dropped++;
        vga_puts("[QUIC-DGRAM] recv queue full , drop datagram \n");
        return -1;
    }

    /* 入队 */
    memcpy(dgram->recv_queue[dgram->recv_tail], frame + offset, data_len);
    dgram->recv_lens[dgram->recv_tail] = data_len;
    dgram->recv_tail = (dgram->recv_tail + 1) % QUIC_DATAGRAM_QUEUE_SIZE;
    dgram->recv_count++;
    dgram->total_received++;

    /* 调用回调 */
    if (dgram->recv_cb) {
        dgram->recv_cb(dgram->recv_queue[dgram->recv_tail == 0 ?
                       QUIC_DATAGRAM_QUEUE_SIZE - 1 : dgram->recv_tail - 1],
                       data_len, dgram->recv_cb_data);
    }

    vga_printf("[QUIC-DGRAM] recv: %u bytes(queue depth: %u)\n",
               data_len, dgram->recv_count);
    return 0;
}

/* 构建DATAGRAM帧 */
int quic_datagram_build_frame(quic_datagram_t *dgram, u8 *buffer,
                               u32 buf_len, u32 *frame_len)
{
    if (!dgram || !buffer) return -1;
    if (dgram->send_count == 0) return 0;

    u32 offset = 0;
    u32 data_len = dgram->send_lens[dgram->send_head];

    /* 帧类型：使用带长度字段的格式 */
    buffer[offset++] = QUIC_DGRAM_WITH_LEN;

    /* 写入长度（可变长度整数） */
    if (data_len < 64) {
        buffer[offset++] = (u8)data_len;
    } else if (data_len < 16384) {
        buffer[offset++] = 0x40 | (u8)(data_len >> 8);
        buffer[offset++] = (u8)(data_len & 0xFF);
    } else if (data_len < 1073741824) {
        buffer[offset++] = 0x80 | (u8)(data_len >> 24);
        buffer[offset++] = (u8)(data_len >> 16);
        buffer[offset++] = (u8)(data_len >> 8);
        buffer[offset++] = (u8)(data_len & 0xFF);
    } else {
        /* 8字节长度 */
        buffer[offset++] = 0xC0;
        for (int i = 7; i >= 0; i--) {
            buffer[offset++] = (u8)((data_len >> (i * 8)) & 0xFF);
        }
    }

    /* 检查缓冲区是否足够 */
    if (offset + data_len > buf_len) return -1;

    /* 复制数据 */
    memcpy(buffer + offset, dgram->send_queue[dgram->send_head], data_len);
    offset += data_len;

    /* 出队 */
    dgram->send_head = (dgram->send_head + 1) % QUIC_DATAGRAM_QUEUE_SIZE;
    dgram->send_count--;

    if (frame_len) *frame_len = offset;

    return 0;
}

/* 设置接收回调 */
int quic_datagram_set_recv_cb(quic_datagram_t *dgram,
                               quic_datagram_recv_cb_t cb, void *user_data)
{
    if (!dgram) return -1;
    dgram->recv_cb = cb;
    dgram->recv_cb_data = user_data;
    return 0;
}

/* 获取队列深度 */
int quic_datagram_get_queue_depth(const quic_datagram_t *dgram,
                                   u32 *send_depth,
                                   u32 *recv_depth)
{
    if (!dgram) return -1;
    if (send_depth) *send_depth = dgram->send_count;
    if (recv_depth) *recv_depth = dgram->recv_count;
    return 0;
}

/* 获取统计信息 */
int quic_datagram_get_stats(const quic_datagram_t *dgram,
                             u64 *sent, u64 *received,
                             u64 *dropped, u32 *max_size)
{
    if (!dgram) return -1;
    if (sent) *sent = dgram->total_sent;
    if (received) *received = dgram->total_received;
    if (dropped) *dropped = dgram->total_dropped;
    if (max_size) *max_size = dgram->max_datagram_size;
    return 0;
}

/* 清除状态 */
int quic_datagram_clear(quic_datagram_t *dgram)
{
    if (!dgram) return -1;
    dgram->send_head = 0;
    dgram->send_tail = 0;
    dgram->send_count = 0;
    dgram->recv_head = 0;
    dgram->recv_tail = 0;
    dgram->recv_count = 0;
    vga_puts("[QUIC-DGRAM] queue clear \n");
    return 0;
}
