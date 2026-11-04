#ifndef NET2_H
#define NET2_H

#include <stdint.h>

#define NET2_MAX_SOCKETS 64
#define NET2_MAX_DNS_CACHE 64
#define NET2_MAX_FIREWALL_RULES 128
#define NET2_MAX_HTTP_CONNECTIONS 16
#define NET2_MAX_WEBSOCKETS 16
#define NET2_MAX_COOKIES 64
#define NET2_MAX_HEADERS 32

/* HTTP方法 */
typedef enum {
    HTTP_GET = 0,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_PATCH,
    HTTP_HEAD,
    HTTP_OPTIONS,
    HTTP_CONNECT,
    HTTP_TRACE,
} http_method_t;

/* HTTP状态码 */
typedef enum {
    HTTP_CONTINUE = 100,
    HTTP_OK = 200,
    HTTP_CREATED = 201,
    HTTP_NO_CONTENT = 204,
    HTTP_MOVED_PERMANENTLY = 301,
    HTTP_FOUND = 302,
    HTTP_NOT_MODIFIED = 304,
    HTTP_BAD_REQUEST = 400,
    HTTP_UNAUTHORIZED = 401,
    HTTP_FORBIDDEN = 403,
    HTTP_NOT_FOUND = 404,
    HTTP_METHOD_NOT_ALLOWED = 405,
    HTTP_INTERNAL_SERVER_ERROR = 500,
    HTTP_NOT_IMPLEMENTED = 501,
    HTTP_BAD_GATEWAY = 502,
    HTTP_SERVICE_UNAVAILABLE = 503,
} http_status_t;

/* HTTP头 */
typedef struct {
    char name[64];
    char value[256];
} http_header_t;

/* HTTP请求 */
typedef struct {
    http_method_t method;
    char url[512];
    char host[256];
    int port;
    char path[512];
    http_header_t headers[NET2_MAX_HEADERS];
    int header_count;
    uint8_t *body;
    int body_length;
    int content_length;
    char content_type[128];
    int timeout_ms;
    int follow_redirects;
    int max_redirects;
} http_request_t;

/* HTTP响应 */
typedef struct {
    int status_code;
    char status_text[64];
    http_header_t headers[NET2_MAX_HEADERS];
    int header_count;
    uint8_t *body;
    int body_length;
    int content_length;
    char content_type[128];
    uint64_t response_time_ms;
    int redirect_count;
    char final_url[512];
} http_response_t;

/* HTTP连接 */
typedef struct {
    int used;
    int id;
    int socket;
    char host[256];
    int port;
    int connected;
    int keep_alive;
    uint64_t last_used;
    int tls_enabled;
} http_connection_t;

/* WebSocket状态 */
typedef enum {
    WS_STATE_CONNECTING = 0,
    WS_STATE_OPEN,
    WS_STATE_CLOSING,
    WS_STATE_CLOSED,
} ws_state_t;

/* WebSocket消息类型 */
typedef enum {
    WS_OPCODE_CONTINUATION = 0,
    WS_OPCODE_TEXT = 1,
    WS_OPCODE_BINARY = 2,
    WS_OPCODE_CLOSE = 8,
    WS_OPCODE_PING = 9,
    WS_OPCODE_PONG = 10,
} ws_opcode_t;

/* WebSocket */
typedef struct {
    int used;
    int id;
    ws_state_t state;
    char url[512];
    char host[256];
    int port;
    int socket;
    int tls_enabled;
    char protocols[128];
    char extensions[128];
    uint64_t last_ping;
    uint64_t last_pong;
    int ping_interval;
    void (*on_message)(int ws_id, ws_opcode_t opcode, const uint8_t *data, int len);
    void (*on_open)(int ws_id);
    void (*on_close)(int ws_id, int code, const char *reason);
    void (*on_error)(int ws_id, const char *error);
} websocket_t;

/* DNS记录类型 */
typedef enum {
    DNS_TYPE_A = 1,
    DNS_TYPE_NS = 2,
    DNS_TYPE_CNAME = 5,
    DNS_TYPE_MX = 15,
    DNS_TYPE_TXT = 16,
    DNS_TYPE_AAAA = 28,
    DNS_TYPE_SRV = 33,
} dns_type_t;

/* DNS缓存条目 */
typedef struct {
    int used;
    char hostname[256];
    uint32_t ip_address;
    dns_type_t type;
    uint32_t ttl;
    uint64_t expire_time;
    int resolved;
} dns_cache_t;

/* 防火墙规则动作 */
typedef enum {
    FW_ACTION_ACCEPT = 0,
    FW_ACTION_DROP,
    FW_ACTION_REJECT,
    FW_ACTION_LOG,
} fw_action_t;

/* 防火墙规则方向 */
typedef enum {
    FW_DIR_INBOUND = 0,
    FW_DIR_OUTBOUND,
    FW_DIR_BOTH,
} fw_direction_t;

/* 防火墙规则 */
typedef struct {
    int used;
    int id;
    fw_action_t action;
    fw_direction_t direction;
    uint32_t src_ip;
    uint32_t src_mask;
    uint32_t dst_ip;
    uint32_t dst_mask;
    int src_port;
    int dst_port;
    int protocol;  /* 0=any,6=TCP,17=UDP,1=ICMP */
    int enabled;
    int priority;
    uint64_t hit_count;
    char description[128];
} firewall_rule_t;

/* Cookie */
typedef struct {
    int used;
    char name[64];
    char value[256];
    char domain[128];
    char path[128];
    uint64_t expires;
    int secure;
    int http_only;
    int same_site;  /* 0=none,1=lax,2=strict */
} cookie_t;

/* TLS状态 */
typedef struct {
    int initialized;
    int version;  /* 0=SSL3,1=TLS1.0,2=TLS1.1,3=TLS1.2,4=TLS1.3 */
    int cipher_suite;
    uint8_t session_id[32];
    int session_id_len;
    int verified;
    char peer_cn[128];
    char peer_issuer[128];
} tls_state_t;

/* 网络深化状态 */
typedef struct {
    int initialized;
    /* HTTP */
    http_connection_t http_connections[NET2_MAX_HTTP_CONNECTIONS];
    int http_connection_count;
    /* WebSocket */
    websocket_t websockets[NET2_MAX_WEBSOCKETS];
    int websocket_count;
    /* DNS */
    dns_cache_t dns_cache[NET2_MAX_DNS_CACHE];
    int dns_cache_count;
    uint32_t dns_server1;
    uint32_t dns_server2;
    /* 防火墙 */
    firewall_rule_t firewall_rules[NET2_MAX_FIREWALL_RULES];
    int firewall_rule_count;
    int firewall_enabled;
    int default_inbound;
    int default_outbound;
    /* Cookie */
    cookie_t cookies[NET2_MAX_COOKIES];
    int cookie_count;
    /* 统计 */
    uint64_t http_requests;
    uint64_t http_errors;
    uint64_t dns_queries;
    uint64_t dns_cache_hits;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t connections_accepted;
    uint64_t connections_rejected;
} net2_t;

/* 初始化网络深化 */
void net2_init(void);

/* HTTP客户端 */
int http_request(http_request_t *request, http_response_t *response);
int http_get(const char *url, http_response_t *response);
int http_post(const char *url, const char *content_type, const uint8_t *body, int body_len, http_response_t *response);
int http_add_header(http_request_t *req, const char *name, const char *value);
int http_get_header(http_response_t *resp, const char *name, char *value, int len);
int http_connection_pool_get(const char *host, int port, int tls);
int http_connection_pool_release(int conn_id);

/* HTTPS/TLS */
int tls_init(tls_state_t *tls);
int tls_connect(int socket, const char *hostname, tls_state_t *tls);
int tls_send(int socket, const uint8_t *data, int len, tls_state_t *tls);
int tls_recv(int socket, uint8_t *buf, int len, tls_state_t *tls);
int tls_close(int socket, tls_state_t *tls);
int tls_verify_certificate(tls_state_t *tls, const char *hostname);

/* WebSocket */
int websocket_connect(const char *url, const char *protocols);
int websocket_send(int ws_id, ws_opcode_t opcode, const uint8_t *data, int len);
int websocket_send_text(int ws_id, const char *text);
int websocket_send_binary(int ws_id, const uint8_t *data, int len);
int websocket_ping(int ws_id);
int websocket_close(int ws_id, int code, const char *reason);
int websocket_get_state(int ws_id, ws_state_t *state);
int websocket_poll(int ws_id, uint8_t *buf, int len, ws_opcode_t *opcode);

/* DNS */
int dns_resolve(const char *hostname, uint32_t *ip);
int dns_resolve_async(const char *hostname, void (*callback)(const char *host, uint32_t ip, void *arg), void *arg);
int dns_cache_add(const char *hostname, uint32_t ip, dns_type_t type, uint32_t ttl);
int dns_cache_lookup(const char *hostname, uint32_t *ip);
int dns_cache_clear(void);
int dns_set_server(uint32_t primary, uint32_t secondary);

/* 防火墙 */
int firewall_enable(int enable);
int firewall_add_rule(fw_action_t action, fw_direction_t direction,
                      uint32_t src_ip, uint32_t src_mask, int src_port,
                      uint32_t dst_ip, uint32_t dst_mask, int dst_port,
                      int protocol, int priority, const char *desc);
int firewall_remove_rule(int rule_id);
int firewall_enable_rule(int rule_id, int enable);
int firewall_list_rules(firewall_rule_t *rules, int max);
int firewall_check(fw_direction_t direction, uint32_t src_ip, int src_port,
                   uint32_t dst_ip, int dst_port, int protocol);
int firewall_set_default(int inbound, int outbound);

/* Cookie管理 */
int cookie_set(const char *domain, const char *path, const char *name, const char *value,
               uint64_t expires, int secure, int http_only);
int cookie_get(const char *domain, const char *path, const char *name, char *value, int len);
int cookie_delete(const char *domain, const char *name);
int cookie_list(cookie_t *cookies, int max);
int cookie_clear(void);

/* 网络共享 */
int net2_tethering_enable(int enable);
int net2_tethering_get_clients(int *clients, int max);

/* 统计 */
int net2_get_stats(uint64_t *http_req, uint64_t *dns_q, uint64_t *sent, uint64_t *recv);
int net2_get_firewall_stats(uint64_t *accepted, uint64_t *rejected, uint64_t *dropped);

#endif /* NET2_H */
