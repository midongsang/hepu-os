#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <stdint.h>

#define WS_MAGIC_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_MAX_FRAME_SIZE 65536
#define WS_MAX_MESSAGE_SIZE 1048576
#define WS_MAX_CONNECTIONS 16

/* WebSocket操作码 */
#define WS_OP_CONTINUATION 0x0
#define WS_OP_TEXT         0x1
#define WS_OP_BINARY       0x2
#define WS_OP_CLOSE        0x8
#define WS_OP_PING         0x9
#define WS_OP_PONG         0xA

/* WebSocket状态 */
typedef enum {
    HEPU_WS_STATE_INIT = 0,
    HEPU_WS_STATE_CONNECTING = 1,
    HEPU_WS_STATE_OPEN = 2,
    HEPU_WS_STATE_CLOSING = 3,
    HEPU_WS_STATE_CLOSED = 4
} hepu_ws_state_t;

/* WebSocket帧头 */
typedef struct {
    uint8_t  fin:1;
    uint8_t  rsv1:1;
    uint8_t  rsv2:1;
    uint8_t  rsv3:1;
    uint8_t  opcode:4;
    uint8_t  mask:1;
    uint8_t  payload_len:7;
    uint16_t extended_len;  /* 当payload_len=126时 */
    uint64_t extended_len64; /* 当payload_len=127时 */
    uint8_t  masking_key[4];
} hepu_ws_frame_header_t;

/* WebSocket连接 */
typedef struct {
    hepu_ws_state_t state;
    char url[256];
    char host[128];
    uint16_t port;
    char path[128];
    char protocol[64];
    char sec_key[24];
    char sec_accept[28];
    int      in_use;
    uint64_t sent_bytes;
    uint64_t received_bytes;
    uint32_t sent_frames;
    uint32_t received_frames;
} hepu_ws_connection_t;

void ws_init(void);
int ws_connect(const char *url, const char *protocol);
int ws_close(int fd);
int ws_send_text(int fd, const char *text);
int ws_send_binary(int fd, const uint8_t *data, uint32_t length);
int ws_receive(int fd, uint8_t *buffer, uint32_t max_length, uint8_t *opcode);
int ws_ping(int fd, const uint8_t *data, uint32_t length);
int ws_pong(int fd, const uint8_t *data, uint32_t length);
int ws_get_state(int fd, hepu_ws_state_t *state);
int ws_get_stats(int fd, uint64_t *sent_bytes, uint64_t *received_bytes,
                 uint32_t *sent_frames, uint32_t *received_frames);
int ws_generate_accept_key(const char *sec_key, char *accept_key);

#endif /* WEBSOCKET_H */
