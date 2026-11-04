/* ============================================================================
 * 赫普OS - MP-QUIC多路径并发
 * RFC 9308: 多路径QUIC
 * ========================================================================== */

#include <stdint.h>
#include "types.h"
#include "mp_quic.h"
#include "csprng.h"
#include "vga.h"
#include "string.h"

static int mp_quic_initialized = 0;

/* 初始化MP-QUIC */
void mp_quic_init(void)
{
    mp_quic_initialized = 1;
    vga_puts("[MP-QUIC] multipath QUIC init \n");
    vga_puts("[MP-QUIC] RFC 9308, supports 4 path concurrent \n");
}

/* 连接 */
int mp_quic_connect(mp_quic_connection_t *conn, const char *host, u16 port)
{
    if (!mp_quic_initialized || !conn || !host) return -1;

    memset(conn, 0, sizeof(mp_quic_connection_t));

    /* 建立基础QUIC连接 */
    if (quic_connect(&conn->base_quic, host, port) != 0) {
        return -1;
    }

    conn->scheduler = MP_QUIC_SCHED_MIN_RTT;
    conn->enabled = 1;
    conn->initialized = 1;

    /* 添加主路径 */
    mp_quic_add_path(conn, NULL, 0, NULL, 0, 100, 1);

    vga_printf("[MP-QUIC] connect to %s:%d\n", host, port);
    return 0;
}

/* 添加路径 */
int mp_quic_add_path(mp_quic_connection_t *conn, const u8 *local_addr,
                     u16 local_port, const u8 *remote_addr,
                     u16 remote_port, u32 priority, u32 weight)
{
    if (!conn || !conn->initialized) return -1;

    for (int i = 0; i < MP_QUIC_MAX_PATHS; i++) {
        if (!conn->paths[i].in_use) {
            mp_quic_path_t *path = &conn->paths[i];
            memset(path, 0, sizeof(mp_quic_path_t));

            if (local_addr) memcpy(path->local_addr, local_addr, 16);
            if (remote_addr) memcpy(path->remote_addr, remote_addr, 16);
            path->local_port = local_port;
            path->remote_port = remote_port;
            path->path_id = i;
            path->next_packet_number = 0;
            path->priority = priority;
            path->weight = weight ? weight : 1;
            path->state = MP_QUIC_PATH_READY;
            path->in_use = 1;

            /* 初始化该路径的恢复模块 */
            quic_recovery_init(&path->recovery);

            conn->active_path_count++;

            vga_printf("[MP-QUIC] add path: id=%llu, priority=%u, weight=%u\n",
                       (unsigned long long)path->path_id, priority, weight);
            return i;
        }
    }
    return -1;
}

/* 移除路径 */
int mp_quic_remove_path(mp_quic_connection_t *conn, u64 path_id)
{
    if (!conn || path_id >= MP_QUIC_MAX_PATHS) return -1;
    if (!conn->paths[path_id].in_use) return -1;

    conn->paths[path_id].state = MP_QUIC_PATH_DRAINING;
    conn->paths[path_id].in_use = 0;
    if (conn->active_path_count > 0) {
        conn->active_path_count--;
    }

    vga_printf("[MP-QUIC] remove path: id=%llu\n", (unsigned long long)path_id);
    return 0;
}

/* 设置调度器 */
int mp_quic_set_scheduler(mp_quic_connection_t *conn, mp_quic_scheduler_t scheduler)
{
    if (!conn) return -1;
    conn->scheduler = scheduler;
    vga_printf("[MP-QUIC] scheduler: %s\n", mp_quic_scheduler_name(scheduler));
    return 0;
}

/* 选择发送路径 */
int mp_quic_select_path(mp_quic_connection_t *conn, u32 data_len)
{
    if (!conn || conn->active_path_count == 0) return -1;

    int best_path = -1;

    switch (conn->scheduler) {
        case MP_QUIC_SCHED_ROUND_ROBIN: {
            /* 轮询：选择下一个就绪路径 */
            static int rr_index = 0;
            for (int i = 0; i < MP_QUIC_MAX_PATHS; i++) {
                int idx = (rr_index + i) % MP_QUIC_MAX_PATHS;
                if (conn->paths[idx].in_use &&
                    conn->paths[idx].state == MP_QUIC_PATH_READY &&
                    quic_recovery_can_send(&conn->paths[idx].recovery, (uint32_t)data_len)) {
                    best_path = idx;
                    rr_index = (idx + 1) % MP_QUIC_MAX_PATHS;
                    break;
                }
            }
            break;
        }

        case MP_QUIC_SCHED_MIN_RTT: {
            /* 最低RTT */
            u32 min_rtt = UINT32_MAX;
            for (int i = 0; i < MP_QUIC_MAX_PATHS; i++) {
                if (conn->paths[i].in_use &&
                    conn->paths[i].state == MP_QUIC_PATH_READY &&
                    quic_recovery_can_send(&conn->paths[i].recovery, (uint32_t)data_len)) {
                    u32 rtt = conn->paths[i].recovery.smoothed_rtt;
                    if (rtt < min_rtt) {
                        min_rtt = rtt;
                        best_path = i;
                    }
                }
            }
            break;
        }

        case MP_QUIC_SCHED_MIN_CWND: {
            /* 最小拥塞窗口（最少使用） */
            u64 min_cwnd = UINT64_MAX;
            for (int i = 0; i < MP_QUIC_MAX_PATHS; i++) {
                if (conn->paths[i].in_use &&
                    conn->paths[i].state == MP_QUIC_PATH_READY &&
                    quic_recovery_can_send(&conn->paths[i].recovery, (uint32_t)data_len)) {
                    u64 cwnd = conn->paths[i].recovery.congestion_window;
                    if (cwnd < min_cwnd) {
                        min_cwnd = cwnd;
                        best_path = i;
                    }
                }
            }
            break;
        }

        case MP_QUIC_SCHED_WRR: {
            /* 加权轮询 */
            static int wrr_index = 0;
            static int wrr_count = 0;
            for (int i = 0; i < MP_QUIC_MAX_PATHS * 10; i++) {
                int idx = wrr_index % MP_QUIC_MAX_PATHS;
                if (conn->paths[idx].in_use &&
                    conn->paths[idx].state == MP_QUIC_PATH_READY &&
                    quic_recovery_can_send(&conn->paths[idx].recovery, (uint32_t)data_len)) {
                    if ((uint32_t)wrr_count < conn->paths[idx].weight) {
                        best_path = idx;
                        wrr_count++;
                        break;
                    } else {
                        wrr_count = 0;
                        wrr_index++;
                    }
                } else {
                    wrr_index++;
                    wrr_count = 0;
                }
            }
            break;
        }

        case MP_QUIC_SCHED_BLEST:
        default: {
            /* BLEST: 阻塞估计，简化为最低RTT */
            u32 min_rtt = UINT32_MAX;
            for (int i = 0; i < MP_QUIC_MAX_PATHS; i++) {
                if (conn->paths[i].in_use &&
                    conn->paths[i].state == MP_QUIC_PATH_READY &&
                    quic_recovery_can_send(&conn->paths[i].recovery, (uint32_t)data_len)) {
                    u32 rtt = conn->paths[i].recovery.smoothed_rtt;
                    if (rtt < min_rtt) {
                        min_rtt = rtt;
                        best_path = i;
                    }
                }
            }
            break;
        }
    }

    return best_path;
}

/* 发送数据 */
int mp_quic_send(mp_quic_connection_t *conn, const u8 *data, u32 len,
                 int fin, u64 *path_id)
{
    if (!conn || !data || len == 0) return -1;

    int path_idx = mp_quic_select_path(conn, len);
    if (path_idx < 0) return -1;

    mp_quic_path_t *path = &conn->paths[path_idx];
    path->state = MP_QUIC_PATH_SENDING;

    /* 记录发送 */
    quic_recovery_on_packet_sent(&path->recovery, path->next_packet_number++,
                                  len, data, len);
    path->bytes_sent += len;
    path->packets_sent++;
    conn->total_bytes_sent += len;
    conn->total_packets_sent++;

    if (fin) {
        path->state = MP_QUIC_PATH_READY;
    }

    if (path_id) *path_id = path->path_id;

    vga_printf("[MP-QUIC] send: path=%llu, %u bytes%s\n",
               (unsigned long long)path->path_id, len, fin ? " (FIN)" : "");
    return len;
}

/* 接收数据 */
int mp_quic_receive(mp_quic_connection_t *conn, u8 *buffer,
                    u32 buf_len, u32 *received)
{
    if (!conn || !buffer) return -1;

    /* 简化：从基础QUIC接收 */
    (void)conn;
    (void)buffer;
    (void)buf_len;
    if (received) *received = 0;
    return 0;
}

/* 处理收到的包 */
int mp_quic_handle_packet(mp_quic_connection_t *conn, u64 path_id,
                           const u8 *packet, u32 len)
{
    if (!conn || !packet || len == 0) return -1;
    if (path_id >= MP_QUIC_MAX_PATHS || !conn->paths[path_id].in_use) return -1;

    mp_quic_path_t *path = &conn->paths[path_id];
    path->bytes_received += len;
    path->packets_received++;
    conn->total_bytes_received += len;
    conn->total_packets_received++;

    /* 处理ACK（简化） */
    quic_recovery_on_ack(&path->recovery, path->next_packet_number - 1, 0, NULL, 0);

    return 0;
}

/* 获取最佳路径 */
int mp_quic_get_best_path(const mp_quic_connection_t *conn)
{
    if (!conn) return -1;

    int best = -1;
    u32 best_score = UINT32_MAX;

    for (int i = 0; i < MP_QUIC_MAX_PATHS; i++) {
        if (conn->paths[i].in_use &&
            conn->paths[i].state == MP_QUIC_PATH_READY) {
            /* 综合评分：RTT * priority */
            u32 score = conn->paths[i].recovery.smoothed_rtt *
                             conn->paths[i].priority;
            if (score < best_score) {
                best_score = score;
                best = i;
            }
        }
    }
    return best;
}

/* 获取路径统计 */
int mp_quic_get_path_stats(const mp_quic_connection_t *conn, u64 path_id,
                            u32 *rtt, u64 *cwnd, u64 *in_flight,
                            u64 *sent, u64 *received)
{
    if (!conn || path_id >= MP_QUIC_MAX_PATHS) return -1;
    if (!conn->paths[path_id].in_use) return -1;

    const mp_quic_path_t *path = &conn->paths[path_id];
    quic_recovery_get_stats(&path->recovery, rtt, NULL, cwnd, in_flight, NULL, NULL);
    if (sent) *sent = path->bytes_sent;
    if (received) *received = path->bytes_received;

    return 0;
}

/* 获取总统计 */
int mp_quic_get_stats(const mp_quic_connection_t *conn, u32 *active_paths,
                       u64 *total_sent, u64 *total_received,
                       u32 *scheduler)
{
    if (!conn) return -1;

    if (active_paths) *active_paths = conn->active_path_count;
    if (total_sent) *total_sent = conn->total_bytes_sent;
    if (total_received) *total_received = conn->total_bytes_received;
    if (scheduler) *scheduler = conn->scheduler;

    return 0;
}

/* 调度器名称 */
const char *mp_quic_scheduler_name(mp_quic_scheduler_t scheduler)
{
    switch (scheduler) {
        case MP_QUIC_SCHED_ROUND_ROBIN: return "ROUND_ROBIN";
        case MP_QUIC_SCHED_MIN_RTT: return "MIN_RTT";
        case MP_QUIC_SCHED_MIN_CWND: return "MIN_CWND";
        case MP_QUIC_SCHED_BLEST: return "BLEST";
        case MP_QUIC_SCHED_WRR: return "WEIGHTED_ROUND_ROBIN";
        default: return "UNKNOWN";
    }
}

/* 路径状态名称 */
const char *mp_quic_path_state_name(mp_quic_path_state_t state)
{
    switch (state) {
        case MP_QUIC_PATH_IDLE: return "IDLE";
        case MP_QUIC_PATH_READY: return "READY";
        case MP_QUIC_PATH_SENDING: return "SENDING";
        case MP_QUIC_PATH_DRAINING: return "DRAINING";
        case MP_QUIC_PATH_DEAD: return "DEAD";
        default: return "UNKNOWN";
    }
}
