/* ============================================================================
 * 赫普OS - 网络深化
 * HTTP/HTTPS客户端 + WebSocket + DNS + 防火墙 + Cookie + 网络共享
 * ========================================================================== */

#include <stdint.h>
#include "net2.h"
#include "vga.h"
#include "string.h"

net2_t net2;

void net2_init(void)
{
    memset(&net2, 0, sizeof(net2_t));
    net2.initialized = 1;
    net2.dns_server1 = 0x08080808;  /* 8.8.8.8 */
    net2.dns_server2 = 0x08080404;  /* 8.8.4.4 */
    net2.firewall_enabled = 0;
    net2.default_inbound = FW_ACTION_DROP;
    net2.default_outbound = FW_ACTION_ACCEPT;

    /* 默认防火墙规则: 允许已建立的连接 */
    firewall_add_rule(FW_ACTION_ACCEPT, FW_DIR_INBOUND, 0, 0, 0, 0, 0, 0, 0, 100, "allow established");
    firewall_add_rule(FW_ACTION_ACCEPT, FW_DIR_OUTBOUND, 0, 0, 0, 0, 0, 0, 0, 100, "allow all outbound");

    vga_puts("[NET2] net deep init done \n");
    vga_printf(" DNS: %d.%d.%d.%d, firewall rules: %d\n",
               8, 8, 8, 8, net2.firewall_rule_count);
}

int http_request(http_request_t *request, http_response_t *response)
{
    if (!request || !response) return -1;
    memset(response, 0, sizeof(http_response_t));
    net2.http_requests++;

    /* 原型阶段: 模拟HTTP请求 */
    response->status_code = HTTP_OK;
    strncpy(response->status_text, "OK", 63);
    response->content_length = 0;
    strncpy(response->content_type, "text/html", 127);
    response->response_time_ms = 50;
    strncpy(response->final_url, request->url, 511);

    vga_printf("[NET2] HTTP %s %s -> %d\n",
               request->method == HTTP_GET ? "GET" : request->method == HTTP_POST ? "POST" : "OTHER",
               request->url, response->status_code);
    return 0;
}

int http_get(const char *url, http_response_t *response)
{
    if (!url || !response) return -1;
    http_request_t req;
    memset(&req, 0, sizeof(http_request_t));
    req.method = HTTP_GET;
    strncpy(req.url, url, 511);
    req.timeout_ms = 30000;
    req.follow_redirects = 1;
    req.max_redirects = 5;
    return http_request(&req, response);
}

int http_post(const char *url, const char *content_type, const uint8_t *body, int body_len, http_response_t *response)
{
    (void)body;
    if (!url || !response) return -1;
    http_request_t req;
    memset(&req, 0, sizeof(http_request_t));
    req.method = HTTP_POST;
    strncpy(req.url, url, 511);
    if (content_type) strncpy(req.content_type, content_type, 127);
    req.body = (uint8_t *)body;
    req.body_length = body_len;
    req.content_length = body_len;
    req.timeout_ms = 30000;
    return http_request(&req, response);
}

int http_add_header(http_request_t *req, const char *name, const char *value)
{
    if (!req || !name || !value) return -1;
    if (req->header_count >= NET2_MAX_HEADERS) return -1;
    strncpy(req->headers[req->header_count].name, name, 63);
    strncpy(req->headers[req->header_count].value, value, 255);
    req->header_count++;
    return 0;
}

int http_get_header(http_response_t *resp, const char *name, char *value, int len)
{
    if (!resp || !name || !value || len <= 0) return -1;
    for (int i = 0; i < resp->header_count; i++) {
        if (strcmp(resp->headers[i].name, name) == 0) {
            strncpy(value, resp->headers[i].value, len - 1);
            value[len - 1] = '\0';
            return 0;
        }
    }
    return -1;
}

int http_connection_pool_get(const char *host, int port, int tls)
{
    (void)host; (void)port; (void)tls;
    /* 原型阶段: 返回模拟连接ID */
    return 0;
}

int http_connection_pool_release(int conn_id)
{
    (void)conn_id;
    return 0;
}

int tls_init(tls_state_t *tls)
{
    if (!tls) return -1;
    memset(tls, 0, sizeof(tls_state_t));
    tls->initialized = 1;
    tls->version = 4;  /* TLS 1.3 */
    tls->cipher_suite = 0x1301;  /* TLS_AES_128_GCM_SHA256 */
    return 0;
}

int tls_connect(int socket, const char *hostname, tls_state_t *tls)
{
    (void)socket; (void)hostname;
    if (!tls) return -1;
    tls->verified = 1;
    strncpy(tls->peer_cn, hostname ? hostname : "localhost", 127);
    strncpy(tls->peer_issuer, "Hepu CA", 127);
    return 0;
}

int tls_send(int socket, const uint8_t *data, int len, tls_state_t *tls)
{
    (void)socket; (void)data; (void)tls;
    return len;  /* 原型阶段: 假设全部发送 */
}

int tls_recv(int socket, uint8_t *buf, int len, tls_state_t *tls)
{
    (void)socket; (void)buf; (void)tls;
    return len;  /* 原型阶段 */
}

int tls_close(int socket, tls_state_t *tls)
{
    (void)socket;
    if (tls) tls->initialized = 0;
    return 0;
}

int tls_verify_certificate(tls_state_t *tls, const char *hostname)
{
    (void)hostname;
    if (!tls) return -1;
    return tls->verified ? 0 : -1;
}

int websocket_connect(const char *url, const char *protocols)
{
    if (!url) return -1;
    for (int i = 0; i < NET2_MAX_WEBSOCKETS; i++) {
        if (!net2.websockets[i].used) {
            memset(&net2.websockets[i], 0, sizeof(websocket_t));
            net2.websockets[i].used = 1;
            net2.websockets[i].id = i;
            net2.websockets[i].state = WS_STATE_OPEN;
            strncpy(net2.websockets[i].url, url, 511);
            if (protocols) strncpy(net2.websockets[i].protocols, protocols, 127);
            net2.websockets[i].ping_interval = 30000;
            net2.websocket_count++;
            vga_printf("[NET2] WebSocket connect: %s\n", url);
            return i;
        }
    }
    return -1;
}

int websocket_send(int ws_id, ws_opcode_t opcode, const uint8_t *data, int len)
{
    if (ws_id < 0 || ws_id >= NET2_MAX_WEBSOCKETS || !net2.websockets[ws_id].used) return -1;
    if (net2.websockets[ws_id].state != WS_STATE_OPEN) return -1;
    (void)opcode; (void)data;
    return len;
}

int websocket_send_text(int ws_id, const char *text)
{
    if (!text) return -1;
    return websocket_send(ws_id, WS_OPCODE_TEXT, (const uint8_t *)text, strlen(text));
}

int websocket_send_binary(int ws_id, const uint8_t *data, int len)
{
    return websocket_send(ws_id, WS_OPCODE_BINARY, data, len);
}

int websocket_ping(int ws_id)
{
    if (ws_id < 0 || ws_id >= NET2_MAX_WEBSOCKETS || !net2.websockets[ws_id].used) return -1;
    net2.websockets[ws_id].last_ping = 0;
    return websocket_send(ws_id, WS_OPCODE_PING, NULL, 0);
}

int websocket_close(int ws_id, int code, const char *reason)
{
    if (ws_id < 0 || ws_id >= NET2_MAX_WEBSOCKETS || !net2.websockets[ws_id].used) return -1;
    net2.websockets[ws_id].state = WS_STATE_CLOSED;
    if (net2.websockets[ws_id].on_close) {
        net2.websockets[ws_id].on_close(ws_id, code, reason ? reason : "");
    }
    net2.websockets[ws_id].used = 0;
    net2.websocket_count--;
    return 0;
}

int websocket_get_state(int ws_id, ws_state_t *state)
{
    if (ws_id < 0 || ws_id >= NET2_MAX_WEBSOCKETS || !net2.websockets[ws_id].used || !state) return -1;
    *state = net2.websockets[ws_id].state;
    return 0;
}

int websocket_poll(int ws_id, uint8_t *buf, int len, ws_opcode_t *opcode)
{
    (void)ws_id; (void)buf; (void)len;
    if (opcode) *opcode = WS_OPCODE_TEXT;
    return 0;
}

int dns_resolve(const char *hostname, uint32_t *ip)
{
    if (!hostname || !ip) return -1;
    net2.dns_queries++;
    /* 先查缓存 */
    if (dns_cache_lookup(hostname, ip) == 0) {
        net2.dns_cache_hits++;
        return 0;
    }
    /* 原型阶段: 返回模拟IP */
    *ip = 0x08080808;  /* 8.8.8.8 */
    dns_cache_add(hostname, *ip, DNS_TYPE_A, 300);
    return 0;
}

int dns_resolve_async(const char *hostname, void (*callback)(const char *host, uint32_t ip, void *arg), void *arg)
{
    if (!hostname || !callback) return -1;
    uint32_t ip;
    dns_resolve(hostname, &ip);
    callback(hostname, ip, arg);
    return 0;
}

int dns_cache_add(const char *hostname, uint32_t ip, dns_type_t type, uint32_t ttl)
{
    if (!hostname) return -1;
    for (int i = 0; i < NET2_MAX_DNS_CACHE; i++) {
        if (!net2.dns_cache[i].used) {
            memset(&net2.dns_cache[i], 0, sizeof(dns_cache_t));
            net2.dns_cache[i].used = 1;
            strncpy(net2.dns_cache[i].hostname, hostname, 255);
            net2.dns_cache[i].ip_address = ip;
            net2.dns_cache[i].type = type;
            net2.dns_cache[i].ttl = ttl;
            net2.dns_cache[i].resolved = 1;
            net2.dns_cache_count++;
            return 0;
        }
    }
    return -1;
}

int dns_cache_lookup(const char *hostname, uint32_t *ip)
{
    if (!hostname || !ip) return -1;
    for (int i = 0; i < NET2_MAX_DNS_CACHE; i++) {
        if (net2.dns_cache[i].used && strcmp(net2.dns_cache[i].hostname, hostname) == 0) {
            *ip = net2.dns_cache[i].ip_address;
            return 0;
        }
    }
    return -1;
}

int dns_cache_clear(void)
{
    for (int i = 0; i < NET2_MAX_DNS_CACHE; i++) net2.dns_cache[i].used = 0;
    net2.dns_cache_count = 0;
    return 0;
}

int dns_set_server(uint32_t primary, uint32_t secondary)
{
    net2.dns_server1 = primary;
    net2.dns_server2 = secondary;
    return 0;
}

int net2_get_stats(uint64_t *http_req, uint64_t *dns_q, uint64_t *sent, uint64_t *recv)
{
    if (http_req) *http_req = net2.http_requests;
    if (dns_q) *dns_q = net2.dns_queries;
    if (sent) *sent = net2.bytes_sent;
    if (recv) *recv = net2.bytes_received;
    return 0;
}

int net2_get_firewall_stats(uint64_t *accepted, uint64_t *rejected, uint64_t *dropped)
{
    (void)accepted; (void)rejected; (void)dropped;
    return 0;
}

