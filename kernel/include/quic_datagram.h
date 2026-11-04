#ifndef QUIC_DATAGRAM_H
#include "types.h"
#define QUIC_DATAGRAM_H

#include <stdint.h>
#include "quic.h"

#define QUIC_DATAGRAM_MAX_SIZE 1200
#define QUIC_DATAGRAM_QUEUE_SIZE 32
#define QUIC_DATAGRAM_FRAME_TYPE 0x30
#define QUIC_DATAGRAM_FRAME_TYPE_LEN 0x31

/* DATAGRAM帧类型 */
typedef enum {
    QUIC_DGRAM_NO_LEN = 0x30,  /* 无长度字段 */
    QUIC_DGRAM_WITH_LEN = 0x31 /* 有长度字段 */
} quic_datagram_frame_type_t;

/* DATAGRAM接收回调 */
typedef void (*quic_datagram_recv_cb_t)(const u8 *data, u32 len,
                                         void *user_data);

/* DATAGRAM状态 */
typedef struct {
    /* 发送队列 */
    u8  send_queue[QUIC_DATAGRAM_QUEUE_SIZE][QUIC_DATAGRAM_MAX_SIZE];
    u32 send_lens[QUIC_DATAGRAM_QUEUE_SIZE];
    u32 send_head;
    u32 send_tail;
    u32 send_count;

    /* 接收队列 */
    u8  recv_queue[QUIC_DATAGRAM_QUEUE_SIZE][QUIC_DATAGRAM_MAX_SIZE];
    u32 recv_lens[QUIC_DATAGRAM_QUEUE_SIZE];
    u32 recv_head;
    u32 recv_tail;
    u32 recv_count;

    /* 统计 */
    u64 total_sent;
    u64 total_received;
    u64 total_dropped;
    u32 max_datagram_size;

    /* 回调 */
    quic_datagram_recv_cb_t recv_cb;
    void    *recv_cb_data;

    int      enabled;
} quic_datagram_t;

void quic_datagram_init(quic_datagram_t *dgram);
int quic_datagram_enable(quic_datagram_t *dgram, int enable);
int quic_datagram_is_enabled(const quic_datagram_t *dgram);
int quic_datagram_send(quic_datagram_t *dgram, const u8 *data,
                        u32 len);
int quic_datagram_receive(quic_datagram_t *dgram, u8 *buffer,
                           u32 buf_len, u32 *received);
int quic_datagram_handle_frame(quic_datagram_t *dgram, const u8 *frame,
                                u32 frame_len);
int quic_datagram_build_frame(quic_datagram_t *dgram, u8 *buffer,
                               u32 buf_len, u32 *frame_len);
int quic_datagram_set_recv_cb(quic_datagram_t *dgram,
                               quic_datagram_recv_cb_t cb, void *user_data);
int quic_datagram_get_queue_depth(const quic_datagram_t *dgram,
                                   u32 *send_depth,
                                   u32 *recv_depth);
int quic_datagram_get_stats(const quic_datagram_t *dgram,
                             u64 *sent, u64 *received,
                             u64 *dropped, u32 *max_size);
int quic_datagram_clear(quic_datagram_t *dgram);

#endif /* QUIC_DATAGRAM_H */
