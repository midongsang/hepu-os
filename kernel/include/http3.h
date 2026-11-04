#ifndef HTTP3_H
#include "types.h"
#define HTTP3_H

#include <stdint.h>
#include "quic.h"

#define HTTP3_MAX_REQUESTS 32
#define HTTP3_MAX_HEADER_SIZE 8192
#define HTTP3_MAX_BODY_SIZE 65536
#define HTTP3_DEFAULT_PORT 443

/* HTTP/3帧类型 */
typedef enum {
    HTTP3_FRAME_DATA = 0x0,
    HTTP3_FRAME_HEADERS = 0x1,
    HTTP3_FRAME_CANCEL_PUSH = 0x3,
    HTTP3_FRAME_SETTINGS = 0x4,
    HTTP3_FRAME_PUSH_PROMISE = 0x5,
    HTTP3_FRAME_GOAWAY = 0x7,
    HTTP3_FRAME_MAX_PUSH_ID = 0xD
} http3_frame_type_t;

/* HTTP/3设置参数 */
typedef enum {
    HTTP3_SETTINGS_QPACK_MAX_TABLE_CAPACITY = 0x1,
    HTTP3_SETTINGS_MAX_FIELD_SECTION_SIZE = 0x6,
    HTTP3_SETTINGS_QPACK_BLOCKED_STREAMS = 0x7
} http3_settings_id_t;

/* HTTP/3错误码 */
typedef enum {
    HTTP3_NO_ERROR = 0x100,
    HTTP3_GENERAL_PROTOCOL_ERROR = 0x101,
    HTTP3_INTERNAL_ERROR = 0x102,
    HTTP3_STREAM_CREATION_ERROR = 0x103,
    HTTP3_CLOSED_CRITICAL_STREAM = 0x104,
    HTTP3_FRAME_UNEXPECTED = 0x105,
    HTTP3_FRAME_ERROR = 0x106,
    HTTP3_EXCESSIVE_LOAD = 0x107,
    HTTP3_ID_ERROR = 0x108,
    HTTP3_SETTINGS_ERROR = 0x109,
    HTTP3_MISSING_SETTINGS = 0x10A,
    HTTP3_REQUEST_REJECTED = 0x10B,
    HTTP3_REQUEST_CANCELLED = 0x10C,
    HTTP3_REQUEST_INCOMPLETE = 0x10D,
    HTTP3_MESSAGE_ERROR = 0x10E,
    HTTP3_CONNECT_ERROR = 0x10F,
    HTTP3_VERSION_FALLBACK = 0x110,
    HTTP3_QPACK_DECOMPRESSION_FAILED = 0x200,
    HTTP3_QPACK_ENCODER_STREAM_ERROR = 0x201,
    HTTP3_QPACK_DECODER_STREAM_ERROR = 0x202
} http3_error_code_t;

/* HTTP/3请求方法 */
typedef enum {
    HTTP3_METHOD_GET = 0,
    HTTP3_METHOD_POST = 1,
    HTTP3_METHOD_PUT = 2,
    HTTP3_METHOD_DELETE = 3,
    HTTP3_METHOD_PATCH = 4,
    HTTP3_METHOD_HEAD = 5,
    HTTP3_METHOD_OPTIONS = 6
} http3_method_t;

/* HTTP/3请求 */
typedef struct {
    u64 stream_id;
    http3_method_t method;
    char     path[256];
    char     host[256];
    char     scheme[16];
    u8  headers[HTTP3_MAX_HEADER_SIZE];
    u32 header_len;
    u8  body[HTTP3_MAX_BODY_SIZE];
    u32 body_len;
    int      headers_received;
    int      body_received;
    int      complete;
    int      in_use;
} http3_request_t;

/* HTTP/3连接 */
typedef struct {
    quic_connection_t quic;
    http3_request_t requests[HTTP3_MAX_REQUESTS];
    u64 next_stream_id;
    u32 max_field_section_size;
    u32 qpack_max_table_capacity;
    u32 qpack_blocked_streams;
    int      settings_received;
    int      goaway_received;
    u64 goaway_id;
    int      connected;
} http3_connection_t;

void http3_init(void);
int http3_connect(http3_connection_t *conn, const char *host, u16 port);
int http3_close(http3_connection_t *conn);
int http3_send_request(http3_connection_t *conn, http3_method_t method,
                       const char *path, const char **headers, int header_count,
                       const u8 *body, u32 body_len, u64 *stream_id);
int http3_receive_response(http3_connection_t *conn, u64 stream_id,
                           u8 *headers, u32 *header_len,
                           u8 *body, u32 *body_len);
int http3_handle_frame(http3_connection_t *conn, u64 stream_id,
                       const u8 *data, u32 len);
int http3_send_settings(http3_connection_t *conn);
int http3_send_goaway(http3_connection_t *conn, u64 id);
int http3_get_stats(http3_connection_t *conn, u32 *active_requests,
                    u64 *bytes_sent, u64 *bytes_received);
const char *http3_method_name(http3_method_t method);
const char *http3_error_name(http3_error_code_t error);

#endif /* HTTP3_H */
