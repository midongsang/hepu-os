/* ============================================================================
 * 赫普OS - QUIC丢包恢复与拥塞控制
 * RFC 9002: 丢包检测与拥塞控制
 * ========================================================================== */

#include <stdint.h>
#include "types.h"
#include "quic_recovery.h"
#include "vga.h"
#include "string.h"

/* 初始化恢复模块 */
void quic_recovery_init(quic_recovery_t *recovery)
{
    if (!recovery) return;

    memset(recovery, 0, sizeof(quic_recovery_t));

    /* RTT初始化 */
    recovery->min_rtt = QUIC_RECOVERY_INITIAL_RTT;
    recovery->smoothed_rtt = QUIC_RECOVERY_INITIAL_RTT;
    recovery->rtt_var = QUIC_RECOVERY_INITIAL_RTT / 2;
    recovery->latest_rtt = QUIC_RECOVERY_INITIAL_RTT;
    recovery->rto = QUIC_RECOVERY_INITIAL_RTT * 2;

    /* 丢包检测 */
    recovery->loss_mode = QUIC_LOSS_PACKET;
    recovery->loss_threshold = QUIC_RECOVERY_LOSS_THRESHOLD;

    /* 拥塞控制 */
    recovery->cc_state = QUIC_CC_SLOW_START;
    recovery->initial_window = 10 * QUIC_RECOVERY_MAX_DATAGRAM_SIZE;
    recovery->minimum_window = 2 * QUIC_RECOVERY_MAX_DATAGRAM_SIZE;
    recovery->congestion_window = recovery->initial_window;
    recovery->ssthresh = UINT64_MAX;

    recovery->initialized = 1;

    vga_puts("[QUIC-RECOV] loss recovery & congestion ctrl init \n");
    vga_printf("[QUIC-RECOV] initial RTT=%dms, initial CWND=%llu\n",
               recovery->smoothed_rtt,
               (unsigned long long)recovery->congestion_window);
}

/* 记录发送的包 */
int quic_recovery_on_packet_sent(quic_recovery_t *recovery, u64 packet_num,
                                  u32 size, const u8 *data,
                                  u32 data_len)
{
    if (!recovery || !recovery->initialized) return -1;

    /* 查找空闲槽位 */
    int slot = -1;
    for (int i = 0; i < QUIC_RECOVERY_MAX_PACKETS; i++) {
        if (!recovery->sent_packets[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        /* 没有空闲槽位，移除最旧的已确认包 */
        for (int i = 0; i < QUIC_RECOVERY_MAX_PACKETS; i++) {
            if (recovery->sent_packets[i].acked) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) return -1;

    quic_sent_packet_t *pkt = &recovery->sent_packets[slot];
    pkt->packet_number = packet_num;
    pkt->send_time = 0;  /* 简化：不记录实际时间 */
    pkt->size = size;
    pkt->data_len = data_len > QUIC_RECOVERY_MAX_DATAGRAM_SIZE ?
                    QUIC_RECOVERY_MAX_DATAGRAM_SIZE : data_len;
    if (data && pkt->data_len > 0) {
        memcpy(pkt->data, data, pkt->data_len);
    }
    pkt->acked = 0;
    pkt->lost = 0;
    pkt->in_use = 1;

    recovery->sent_count++;
    recovery->bytes_in_flight += size;
    recovery->packets_in_flight++;
    recovery->total_sent++;

    return 0;
}

/* 处理ACK */
int quic_recovery_on_ack(quic_recovery_t *recovery, u64 largest_acked,
                          u64 ack_delay, const u8 *ack_ranges,
                          u32 ranges_len)
{
    (void)ranges_len;
    (void)ack_ranges;
    if (!recovery || !recovery->initialized) return -1;

    recovery->largest_acked = largest_acked;

    /* 标记已确认的包 */
    u64 bytes_acked = 0;
    u32 packets_acked = 0;

    for (int i = 0; i < QUIC_RECOVERY_MAX_PACKETS; i++) {
        quic_sent_packet_t *pkt = &recovery->sent_packets[i];
        if (pkt->in_use && !pkt->acked && pkt->packet_number <= largest_acked) {
            /* 简化：假设所有<=largest_acked的包都被确认 */
            pkt->acked = 1;
            bytes_acked += pkt->size;
            packets_acked++;
            recovery->total_acked++;
        }
    }

    recovery->bytes_in_flight -= bytes_acked;
    if (recovery->packets_in_flight >= packets_acked) {
        recovery->packets_in_flight -= packets_acked;
    }

    /* 更新RTT */
    if (ack_delay > 0) {
        quic_recovery_update_rtt(recovery, recovery->latest_rtt,
                                 (u32)ack_delay);
    }

    /* 慢启动：每确认一个包，CWND增加一个包大小 */
    if (recovery->cc_state == QUIC_CC_SLOW_START) {
        recovery->congestion_window += QUIC_RECOVERY_MAX_DATAGRAM_SIZE;
        if (recovery->congestion_window >= recovery->ssthresh) {
            recovery->cc_state = QUIC_CC_CONGESTION_AVOIDANCE;
        }
    } else if (recovery->cc_state == QUIC_CC_CONGESTION_AVOIDANCE) {
        /* 拥塞避免：每RTT增加一个MSS */
        recovery->congestion_window +=
            (QUIC_RECOVERY_MAX_DATAGRAM_SIZE * QUIC_RECOVERY_MAX_DATAGRAM_SIZE) /
            recovery->congestion_window;
    }

    vga_printf("[QUIC-RECOV] ACK: largest=%llu, acked=%u, cwnd=%llu\n",
               (unsigned long long)largest_acked, packets_acked,
               (unsigned long long)recovery->congestion_window);

    return 0;
}

/* 丢包检测 */
int quic_recovery_detect_loss(quic_recovery_t *recovery,
                               u64 *lost_packets, u32 *lost_count)
{
    if (!recovery || !recovery->initialized || !lost_packets) return -1;

    u32 count = 0;

    /* 基于包号的丢包检测：
     * 如果一个包的包号比largest_acked小loss_threshold以上，且未被确认，则判定为丢失 */
    for (int i = 0; i < QUIC_RECOVERY_MAX_PACKETS; i++) {
        quic_sent_packet_t *pkt = &recovery->sent_packets[i];
        if (pkt->in_use && !pkt->acked && !pkt->lost) {
            if (pkt->packet_number + recovery->loss_threshold <
                recovery->largest_acked) {
                pkt->lost = 1;
                lost_packets[count++] = pkt->packet_number;
                recovery->total_lost++;
                recovery->loss_events++;

                if (recovery->bytes_in_flight >= pkt->size) {
                    recovery->bytes_in_flight -= pkt->size;
                }
                if (recovery->packets_in_flight > 0) {
                    recovery->packets_in_flight--;
                }
            }
        }
    }

    if (lost_count) *lost_count = count;

    if (count > 0) {
        vga_printf("[QUIC-RECOV] detected %u lost packages \n", count);
        quic_recovery_on_congestion_event(recovery);
    }

    return count;
}

/* 重传丢失的包 */
int quic_recovery_retransmit(quic_recovery_t *recovery, u64 packet_num,
                              u8 *data, u32 *data_len)
{
    if (!recovery || !recovery->initialized || !data) return -1;

    for (int i = 0; i < QUIC_RECOVERY_MAX_PACKETS; i++) {
        quic_sent_packet_t *pkt = &recovery->sent_packets[i];
        if (pkt->in_use && pkt->lost && pkt->packet_number == packet_num) {
            u32 len = pkt->data_len;
            if (data_len && *data_len < len) len = *data_len;
            memcpy(data, pkt->data, len);
            if (data_len) *data_len = len;

            pkt->lost = 0;
            pkt->acked = 0;
            recovery->total_retransmitted++;

            vga_printf("[QUIC-RECOV] retransmit pkt %llu(%u bytes)\n",
                       (unsigned long long)packet_num, len);
            return 0;
        }
    }
    return -1;
}

/* 更新RTT估计 */
int quic_recovery_update_rtt(quic_recovery_t *recovery, u32 latest_rtt,
                              u32 ack_delay)
{
    if (!recovery || !recovery->initialized) return -1;

    /* 调整ack_delay（不超过smoothed_rtt的一半） */
    if (ack_delay > recovery->smoothed_rtt / 2) {
        ack_delay = recovery->smoothed_rtt / 2;
    }

    /* 计算adjusted_rtt */
    u32 adjusted_rtt = latest_rtt;
    if (adjusted_rtt > ack_delay) {
        adjusted_rtt -= ack_delay;
    }

    recovery->latest_rtt = latest_rtt;

    /* 更新min_rtt */
    if (recovery->min_rtt == 0 || adjusted_rtt < recovery->min_rtt) {
        recovery->min_rtt = adjusted_rtt;
    }

    if (recovery->smoothed_rtt == 0) {
        /* 首次测量 */
        recovery->smoothed_rtt = adjusted_rtt;
        recovery->rtt_var = adjusted_rtt / 2;
    } else {
        /* RTTVAR = (3/4) * RTTVAR + (1/4) * |SRTT - adjusted_rtt| */
        u32 rtt_diff = (adjusted_rtt > recovery->smoothed_rtt) ?
                            adjusted_rtt - recovery->smoothed_rtt :
                            recovery->smoothed_rtt - adjusted_rtt;
        recovery->rtt_var = (3 * recovery->rtt_var + rtt_diff) / 4;

        /* SRTT = (7/8) * SRTT + (1/8) * adjusted_rtt */
        recovery->smoothed_rtt = (7 * recovery->smoothed_rtt + adjusted_rtt) / 8;
    }

    /* RTO = SRTT + 4 * RTTVAR + granularity */
    recovery->rto = recovery->smoothed_rtt + 4 * recovery->rtt_var +
                    QUIC_RECOVERY_GRANULARITY;

    return 0;
}

/* 检查是否可以发送 */
int quic_recovery_can_send(const quic_recovery_t *recovery, u32 packet_size)
{
    if (!recovery || !recovery->initialized) return 0;
    return (recovery->bytes_in_flight + packet_size <= recovery->congestion_window);
}

/* 拥塞事件处理 */
int quic_recovery_on_congestion_event(quic_recovery_t *recovery)
{
    if (!recovery || !recovery->initialized) return -1;

    /* 进入恢复状态，ssthresh = cwnd / 2, cwnd = ssthresh */
    recovery->ssthresh = recovery->congestion_window / 2;
    if (recovery->ssthresh < recovery->minimum_window) {
        recovery->ssthresh = recovery->minimum_window;
    }
    recovery->congestion_window = recovery->ssthresh;
    recovery->cc_state = QUIC_CC_RECOVERY;

    vga_printf("[QUIC-RECOV] congestion event: ssthresh=%llu, cwnd=%llu\n",
               (unsigned long long)recovery->ssthresh,
               (unsigned long long)recovery->congestion_window);
    return 0;
}

/* 获取拥塞窗口 */
int quic_recovery_get_cwnd(const quic_recovery_t *recovery)
{
    return recovery ? (int)recovery->congestion_window : 0;
}

/* 获取统计信息 */
int quic_recovery_get_stats(const quic_recovery_t *recovery,
                             u32 *rtt, u32 *rtt_var,
                             u64 *cwnd, u64 *in_flight,
                             u64 *lost, u64 *retransmitted)
{
    if (!recovery) return -1;

    if (rtt) *rtt = recovery->smoothed_rtt;
    if (rtt_var) *rtt_var = recovery->rtt_var;
    if (cwnd) *cwnd = recovery->congestion_window;
    if (in_flight) *in_flight = recovery->bytes_in_flight;
    if (lost) *lost = recovery->total_lost;
    if (retransmitted) *retransmitted = recovery->total_retransmitted;

    return 0;
}

/* 拥塞状态名称 */
const char *quic_cc_state_name(quic_congestion_state_t state)
{
    switch (state) {
        case QUIC_CC_SLOW_START: return "SLOW_START";
        case QUIC_CC_CONGESTION_AVOIDANCE: return "CONGESTION_AVOIDANCE";
        case QUIC_CC_RECOVERY: return "RECOVERY";
        case QUIC_CC_APPLICATION_LIMITED: return "APPLICATION_LIMITED";
        default: return "UNKNOWN";
    }
}
