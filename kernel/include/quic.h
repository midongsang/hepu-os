#ifndef QUIC_H
#define QUIC_H

#include <stdint.h>

#define QUIC_MAX_CONNECTIONS 16
#define QUIC_MAX_STREAMS 64
#define QUIC_MAX_PACKET_SIZE 1500
#define QUIC_INITIAL_MAX_DATA 1048576
#define QUIC_INITIAL_MAX_STREAM_DATA 262144
#define QUIC_INITIAL_MAX_STREAMS_BIDI 100
#define QUIC_INITIAL_MAX_STREAMS_UNI 100
#define QUIC_IDLE_TIMEOUT 30000
#define QUIC_MAX_ACK_DELAY 25

/* QUIC包类型 */
typedef enum {
    QUIC_PACKET_INITIAL = 0,
    QUIC_PACKET_0RTT = 1,
    QUIC_PACKET_HANDSHAKE = 2,
    QUIC_PACKET_RETRY = 3,
    QUIC_PACKET_1RTT = 48  /* 短包头 */
} quic_packet_type_t;

/* QUIC帧类型 */
typedef enum {
    QUIC_FRAME_PADDING = 0,
    QUIC_FRAME_PING = 1,
    QUIC_FRAME_ACK = 2,
    QUIC_FRAME_ACK_ECN = 3,
    QUIC_FRAME_RESET_STREAM = 4,
    QUIC_FRAME_STOP_SENDING = 5,
    QUIC_FRAME_CRYPTO = 6,
    QUIC_FRAME_NEW_TOKEN = 7,
    QUIC_FRAME_STREAM = 8,
    QUIC_FRAME_MAX_DATA = 16,
    QUIC_FRAME_MAX_STREAM_DATA = 17,
    QUIC_FRAME_MAX_STREAMS_BIDI = 18,
    QUIC_FRAME_MAX_STREAMS_UNI = 19,
    QUIC_FRAME_DATA_BLOCKED = 20,
    QUIC_FRAME_STREAM_DATA_BLOCKED = 21,
    QUIC_FRAME_STREAMS_BLOCKED_BIDI = 22,
    QUIC_FRAME_STREAMS_BLOCKED_UNI = 23,
    QUIC_FRAME_NEW_CONNECTION_ID = 24,
    QUIC_FRAME_RETIRE_CONNECTION_ID = 25,
    QUIC_FRAME_PATH_CHALLENGE = 26,
    QUIC_FRAME_PATH_RESPONSE = 27,
    QUIC_FRAME_CONNECTION_CLOSE = 28,
    QUIC_FRAME_CONNECTION_CLOSE_APP = 29,
    QUIC_FRAME_HANDSHAKE_DONE = 30
} quic_frame_type_t;

/* QUIC流状态 */
typedef enum {
    QUIC_STREAM_IDLE = 0,
    QUIC_STREAM_OPEN = 1,
    QUIC_STREAM_HALF_CLOSED_LOCAL = 2,
    QUIC_STREAM_HALF_CLOSED_REMOTE = 3,
    QUIC_STREAM_CLOSED = 4
} quic_stream_state_t;

/* QUIC连接状态 */
typedef enum {
    QUIC_CONN_INITIAL = 0,
    QUIC_CONN_HANDSHAKE = 1,
    QUIC_CONN_ESTABLISHED = 2,
    QUIC_CONN_CLOSING = 3,
    QUIC_CONN_DRAINING = 4,
    QUIC_CONN_CLOSED = 5
} quic_conn_state_t;

/* QUIC流 */
typedef struct {
    uint64_t stream_id;
    quic_stream_state_t state;
    uint64_t offset;
    uint64_t max_data;
    uint64_t max_stream_data;
    uint8_t  buffer[4096];
    uint32_t buffer_len;
    int      in_use;
} quic_stream_t;

/* QUIC连接 */
typedef struct {
    uint8_t  dcid[20];  /* 目标连接ID */
    uint8_t  scid[20];  /* 源连接ID */
    uint8_t  dcid_len;
    uint8_t  scid_len;
    uint64_t packet_number;
    uint64_t largest_acked;
    uint64_t max_data;
    uint64_t max_stream_data_bidi;
    uint64_t max_stream_data_uni;
    uint64_t max_streams_bidi;
    uint64_t max_streams_uni;
    uint32_t idle_timeout;
    uint32_t max_ack_delay;
    quic_conn_state_t state;
    quic_stream_t streams[QUIC_MAX_STREAMS];
    uint64_t next_stream_id;
    uint32_t rtt;
    uint32_t rtt_var;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t packets_sent;
    uint64_t packets_received;
    int      in_use;
} quic_connection_t;

void quic_init(void);
int quic_connect(quic_connection_t *conn, const char *host, uint16_t port);
int quic_close(quic_connection_t *conn, uint64_t error_code, const char *reason);
int quic_create_stream(quic_connection_t *conn, uint64_t *stream_id);
int quic_stream_send(quic_connection_t *conn, uint64_t stream_id,
                     const uint8_t *data, uint32_t len, int fin);
int quic_stream_receive(quic_connection_t *conn, uint64_t stream_id,
                        uint8_t *buffer, uint32_t buf_len, uint32_t *received);
int quic_stream_reset(quic_connection_t *conn, uint64_t stream_id, uint64_t error_code);
int quic_send_ping(quic_connection_t *conn);
int quic_handle_packet(quic_connection_t *conn, const uint8_t *packet, uint32_t len);
int quic_build_packet(quic_connection_t *conn, uint8_t *buffer, uint32_t *buf_len);
int quic_get_stats(quic_connection_t *conn, uint64_t *sent, uint64_t *received,
                   uint32_t *rtt, uint32_t *streams);
const char *quic_state_name(quic_conn_state_t state);
const char *quic_frame_name(quic_frame_type_t type);

#endif /* QUIC_H */
