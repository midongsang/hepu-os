#ifndef HTTP2_H
#define HTTP2_H

#include <stdint.h>

#define HTTP2_MAGIC "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
#define HTTP2_MAGIC_LEN 24
#define HTTP2_MAX_FRAME_SIZE 16384
#define HTTP2_DEFAULT_WINDOW 65535
#define HTTP2_MAX_STREAMS 128
#define HTTP2_MAX_HEADER_SIZE 4096

/* HTTP/2帧类型 */
typedef enum {
    HTTP2_FRAME_DATA = 0x0,
    HTTP2_FRAME_HEADERS = 0x1,
    HTTP2_FRAME_PRIORITY = 0x2,
    HTTP2_FRAME_RST_STREAM = 0x3,
    HTTP2_FRAME_SETTINGS = 0x4,
    HTTP2_FRAME_PUSH_PROMISE = 0x5,
    HTTP2_FRAME_PING = 0x6,
    HTTP2_FRAME_GOAWAY = 0x7,
    HTTP2_FRAME_WINDOW_UPDATE = 0x8,
    HTTP2_FRAME_CONTINUATION = 0x9
} http2_frame_type_t;

/* HTTP/2帧标志 */
#define HTTP2_FLAG_END_STREAM 0x1
#define HTTP2_FLAG_ACK 0x1
#define HTTP2_FLAG_END_HEADERS 0x4
#define HTTP2_FLAG_PADDED 0x8
#define HTTP2_FLAG_PRIORITY 0x20

/* HTTP/2错误码 */
typedef enum {
    HTTP2_NO_ERROR = 0x0,
    HTTP2_PROTOCOL_ERROR = 0x1,
    HTTP2_INTERNAL_ERROR = 0x2,
    HTTP2_FLOW_CONTROL_ERROR = 0x3,
    HTTP2_SETTINGS_TIMEOUT = 0x4,
    HTTP2_STREAM_CLOSED_ERROR = 0x5,
    HTTP2_FRAME_SIZE_ERROR = 0x6,
    HTTP2_REFUSED_STREAM = 0x7,
    HTTP2_CANCEL = 0x8,
    HTTP2_COMPRESSION_ERROR = 0x9,
    HTTP2_CONNECT_ERROR = 0xa,
    HTTP2_ENHANCE_YOUR_CALM = 0xb,
    HTTP2_INADEQUATE_SECURITY = 0xc,
    HTTP2_HTTP_1_1_REQUIRED = 0xd
} http2_error_code_t;

/* HTTP/2设置参数 */
typedef enum {
    HTTP2_SETTINGS_HEADER_TABLE_SIZE = 0x1,
    HTTP2_SETTINGS_ENABLE_PUSH = 0x2,
    HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS = 0x3,
    HTTP2_SETTINGS_INITIAL_WINDOW_SIZE = 0x4,
    HTTP2_SETTINGS_MAX_FRAME_SIZE = 0x5,
    HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE = 0x6
} http2_settings_id_t;

/* HTTP/2帧头 */
typedef struct {
    uint32_t length;
    uint8_t  type;
    uint8_t  flags;
    uint32_t stream_id;
} http2_frame_header_t;

/* HTTP/2流状态 */
typedef enum {
    HTTP2_STREAM_IDLE = 0,
    HTTP2_STREAM_RESERVED_LOCAL = 1,
    HTTP2_STREAM_RESERVED_REMOTE = 2,
    HTTP2_STREAM_OPEN = 3,
    HTTP2_STREAM_HALF_CLOSED_LOCAL = 4,
    HTTP2_STREAM_HALF_CLOSED_REMOTE = 5,
    HTTP2_STREAM_CLOSED = 6
} http2_stream_state_t;

/* HTTP/2流 */
typedef struct {
    uint32_t stream_id;
    http2_stream_state_t state;
    int32_t  window_size;
    uint32_t sent_bytes;
    uint32_t received_bytes;
    int      in_use;
} http2_stream_t;

/* HTTP/2连接 */
typedef struct {
    int      connected;
    int32_t  window_size;
    uint32_t max_frame_size;
    uint32_t max_concurrent_streams;
    uint32_t header_table_size;
    int      push_enabled;
    http2_stream_t streams[HTTP2_MAX_STREAMS];
    uint32_t next_stream_id;
    uint32_t last_stream_id;
    int      settings_acked;
    int      goaway_received;
    uint32_t goaway_last_stream_id;
} http2_connection_t;

void http2_init(void);
int http2_connect(http2_connection_t *conn, const char *host, uint16_t port);
int http2_send_magic(http2_connection_t *conn);
int http2_send_settings(http2_connection_t *conn);
int http2_send_headers(http2_connection_t *conn, uint32_t stream_id,
                       const char *method, const char *path,
                       const char *host, const char **headers, int header_count);
int http2_send_data(http2_connection_t *conn, uint32_t stream_id,
                    const uint8_t *data, uint32_t len, int end_stream);
int http2_send_window_update(http2_connection_t *conn, uint32_t stream_id,
                             uint32_t increment);
int http2_send_ping(http2_connection_t *conn, const uint8_t *data);
int http2_send_goaway(http2_connection_t *conn, uint32_t last_stream_id,
                      http2_error_code_t error_code);
int http2_read_frame(http2_connection_t *conn, http2_frame_header_t *header,
                     uint8_t *payload, uint32_t *payload_len);
int http2_handle_frame(http2_connection_t *conn, const http2_frame_header_t *header,
                       const uint8_t *payload);
uint32_t http2_create_stream(http2_connection_t *conn);
int http2_close_stream(http2_connection_t *conn, uint32_t stream_id,
                       http2_error_code_t error_code);
const char *http2_frame_type_name(http2_frame_type_t type);
const char *http2_error_name(http2_error_code_t error);

#endif /* HTTP2_H */
