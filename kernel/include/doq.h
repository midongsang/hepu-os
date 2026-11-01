#ifndef DOQ_H
#include "types.h"
#define DOQ_H

#include <stdint.h>
#include "quic.h"

#define DOQ_DEFAULT_PORT 853
#define DOQ_MAX_QUERIES 16
#define DOQ_MAX_DNS_MESSAGE 4096
#define DOQ_QUERY_TIMEOUT_MS 5000  /* 5秒 */

/* DoQ查询状态 */
typedef enum {
    DOQ_QUERY_IDLE = 0,
    DOQ_QUERY_SENT = 1,
    DOQ_QUERY_RECEIVED = 2,
    DOQ_QUERY_TIMEOUT = 3,
    DOQ_QUERY_ERROR = 4
} doq_query_state_t;

/* DoQ查询 */
typedef struct {
    u16 query_id;
    u64 stream_id;
    char     hostname[256];
    u16 query_type;
    u16 query_class;
    u8  response[DOQ_MAX_DNS_MESSAGE];
    u32 response_len;
    doq_query_state_t state;
    u32 timestamp;
    int      in_use;
} doq_query_t;

/* DoQ连接 */
typedef struct {
    quic_connection_t quic;
    doq_query_t queries[DOQ_MAX_QUERIES];
    u16 next_query_id;
    u64 next_stream_id;
    int      connected;
    char     server_host[256];
    u16 server_port;
} doq_connection_t;

void doq_init(void);
int doq_connect(doq_connection_t *conn, const char *server, u16 port);
int doq_close(doq_connection_t *conn);
int doq_resolve(doq_connection_t *conn, const char *hostname,
                u16 qtype, u32 *ip);
int doq_resolve_async(doq_connection_t *conn, const char *hostname,
                      u16 qtype, u16 *query_id);
int doq_get_response(doq_connection_t *conn, u16 query_id,
                     u8 *response, u32 *resp_len);
int doq_parse_a_record(const u8 *response, u32 len, u32 *ip);
int doq_parse_aaaa_record(const u8 *response, u32 len, u8 *ip);
int doq_get_stats(doq_connection_t *conn, u32 *active_queries,
                  u32 *total_queries, u32 *success_count);
const char *doq_query_state_name(doq_query_state_t state);

#endif /* DOQ_H */
