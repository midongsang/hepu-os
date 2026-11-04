#ifndef MP_QUIC_H
#include "types.h"
#define MP_QUIC_H

#include <stdint.h>
#include "quic.h"
#include "quic_recovery.h"

#define MP_QUIC_MAX_PATHS 4
#define MP_QUIC_MAX_SCHEDULER 16

/* 多路径调度器类型 */
typedef enum {
    MP_QUIC_SCHED_ROUND_ROBIN = 0,
    MP_QUIC_SCHED_MIN_RTT = 1,
    MP_QUIC_SCHED_MIN_CWND = 2,
    MP_QUIC_SCHED_BLEST = 3,
    MP_QUIC_SCHED_WRR = 4
} mp_quic_scheduler_t;

/* 多路径状态 */
typedef enum {
    MP_QUIC_PATH_IDLE = 0,
    MP_QUIC_PATH_READY = 1,
    MP_QUIC_PATH_SENDING = 2,
    MP_QUIC_PATH_DRAINING = 3,
    MP_QUIC_PATH_DEAD = 4
} mp_quic_path_state_t;

/* 多路径路径 */
typedef struct {
    u8  local_addr[16];
    u8  remote_addr[16];
    u16 local_port;
    u16 remote_port;
    u64 path_id;
    u64 next_packet_number;
    u32 priority;
    u32 weight;
    u64 bytes_sent;
    u64 bytes_received;
    u64 packets_sent;
    u64 packets_received;
    quic_recovery_t recovery;
    mp_quic_path_state_t state;
    int      in_use;
} mp_quic_path_t;

/* MP-QUIC连接 */
typedef struct {
    quic_connection_t base_quic;
    mp_quic_path_t paths[MP_QUIC_MAX_PATHS];
    mp_quic_scheduler_t scheduler;
    int      active_path_count;
    u64 total_bytes_sent;
    u64 total_bytes_received;
    u64 total_packets_sent;
    u64 total_packets_received;
    u32 reordering_buffer[MP_QUIC_MAX_SCHEDULER];
    u32 reordering_count;
    int      enabled;
    int      initialized;
} mp_quic_connection_t;

void mp_quic_init(void);
int mp_quic_connect(mp_quic_connection_t *conn, const char *host, u16 port);
int mp_quic_add_path(mp_quic_connection_t *conn, const u8 *local_addr,
                     u16 local_port, const u8 *remote_addr,
                     u16 remote_port, u32 priority, u32 weight);
int mp_quic_remove_path(mp_quic_connection_t *conn, u64 path_id);
int mp_quic_set_scheduler(mp_quic_connection_t *conn, mp_quic_scheduler_t scheduler);
int mp_quic_send(mp_quic_connection_t *conn, const u8 *data, u32 len,
                 int fin, u64 *path_id);
int mp_quic_receive(mp_quic_connection_t *conn, u8 *buffer,
                    u32 buf_len, u32 *received);
int mp_quic_select_path(mp_quic_connection_t *conn, u32 data_len);
int mp_quic_handle_packet(mp_quic_connection_t *conn, u64 path_id,
                           const u8 *packet, u32 len);
int mp_quic_get_best_path(const mp_quic_connection_t *conn);
int mp_quic_get_path_stats(const mp_quic_connection_t *conn, u64 path_id,
                            u32 *rtt, u64 *cwnd, u64 *in_flight,
                            u64 *sent, u64 *received);
int mp_quic_get_stats(const mp_quic_connection_t *conn, u32 *active_paths,
                       u64 *total_sent, u64 *total_received,
                       u32 *scheduler);
const char *mp_quic_scheduler_name(mp_quic_scheduler_t scheduler);
const char *mp_quic_path_state_name(mp_quic_path_state_t state);

#endif /* MP_QUIC_H */
