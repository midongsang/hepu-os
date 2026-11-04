#ifndef QUIC_RECOVERY_H
#include "types.h"
#define QUIC_RECOVERY_H

#include <stdint.h>
#include "quic.h"

#define QUIC_RECOVERY_MAX_PACKETS 256
#define QUIC_RECOVERY_INITIAL_RTT 333  /* 初始RTT 333ms */
#define QUIC_RECOVERY_MIN_RTT 1
#define QUIC_RECOVERY_GRANULARITY 1  /* 1ms */
#define QUIC_RECOVERY_PERSISTENT_CONGESTION_THRESHOLD 3
#define QUIC_RECOVERY_MAX_DATAGRAM_SIZE 1200
#define QUIC_RECOVERY_LOSS_THRESHOLD 3  /* 包号差距阈值 */

/* 发送的包记录 */
typedef struct {
    u64 packet_number;
    u64 send_time;
    u32 size;
    u8  data[QUIC_RECOVERY_MAX_DATAGRAM_SIZE];
    u32 data_len;
    int      acked;
    int      lost;
    int      in_use;
} quic_sent_packet_t;

/* 丢包检测模式 */
typedef enum {
    QUIC_LOSS_TIME = 0,      /* 基于时间 */
    QUIC_LOSS_PACKET = 1     /* 基于包号 */
} quic_loss_detection_mode_t;

/* 拥塞控制状态 */
typedef enum {
    QUIC_CC_SLOW_START = 0,
    QUIC_CC_CONGESTION_AVOIDANCE = 1,
    QUIC_CC_RECOVERY = 2,
    QUIC_CC_APPLICATION_LIMITED = 3
} quic_congestion_state_t;

/* QUIC恢复模块状态 */
typedef struct {
    /* RTT估计 */
    u32 min_rtt;
    u32 smoothed_rtt;
    u32 rtt_var;
    u32 latest_rtt;
    u32 rto;  /* 重传超时 */

    /* 丢包检测 */
    quic_loss_detection_mode_t loss_mode;
    u64 loss_time;
    u64 largest_acked;
    u32 loss_threshold;

    /* 拥塞控制 */
    quic_congestion_state_t cc_state;
    u64 congestion_window;
    u64 bytes_in_flight;
    u64 ssthresh;
    u64 initial_window;
    u64 minimum_window;
    u32 packets_in_flight;

    /* 发送包记录 */
    quic_sent_packet_t sent_packets[QUIC_RECOVERY_MAX_PACKETS];
    u32 sent_count;

    /* 统计 */
    u64 total_sent;
    u64 total_acked;
    u64 total_lost;
    u64 total_retransmitted;
    u32 loss_events;

    int      initialized;
} quic_recovery_t;

void quic_recovery_init(quic_recovery_t *recovery);
int quic_recovery_on_packet_sent(quic_recovery_t *recovery, u64 packet_num,
                                  u32 size, const u8 *data,
                                  u32 data_len);
int quic_recovery_on_ack(quic_recovery_t *recovery, u64 largest_acked,
                          u64 ack_delay, const u8 *ack_ranges,
                          u32 ranges_len);
int quic_recovery_detect_loss(quic_recovery_t *recovery,
                               u64 *lost_packets, u32 *lost_count);
int quic_recovery_retransmit(quic_recovery_t *recovery, u64 packet_num,
                              u8 *data, u32 *data_len);
int quic_recovery_update_rtt(quic_recovery_t *recovery, u32 latest_rtt,
                              u32 ack_delay);
int quic_recovery_can_send(const quic_recovery_t *recovery, u32 packet_size);
int quic_recovery_on_congestion_event(quic_recovery_t *recovery);
int quic_recovery_get_cwnd(const quic_recovery_t *recovery);
int quic_recovery_get_stats(const quic_recovery_t *recovery,
                             u32 *rtt, u32 *rtt_var,
                             u64 *cwnd, u64 *in_flight,
                             u64 *lost, u64 *retransmitted);
const char *quic_cc_state_name(quic_congestion_state_t state);

#endif /* QUIC_RECOVERY_H */
