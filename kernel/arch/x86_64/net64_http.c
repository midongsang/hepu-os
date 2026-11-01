/* ============================================================================
 * 赫普OS 64位 - HTTP客户端
 * 支持GET/POST请求，简单响应解析
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

#define HTTP_PORT 80
#define HTTP_BUF_SIZE 2048

extern void vga_puts(const char *s);
extern int tcp64_connect(uint32_t remote_ip, uint16_t remote_port, uint16_t local_port);
extern int tcp64_send(int sock, const uint8_t *data, int length);
extern int tcp64_recv(int sock, uint8_t *buffer, int max_len);
extern int tcp64_close(int sock);
extern int tcp64_get_state(int sock);
extern uint32_t dns64_resolve(const char *hostname);

#define TCP_ESTABLISHED 4

static void http_memcpy(void *dst, const void *src, int n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}

static int http_strlen(const char *s) {
    int i = 0;
    while (s[i]) i++;
    return i;
}

static int http_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

static void http_itoa(int n, char *s) {
    int i = 0, sign = n;
    char tmp[16];
    if (n < 0) n = -n;
    do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    if (sign < 0) tmp[i++] = '-';
    int j = 0;
    while (i > 0) s[j++] = tmp[--i];
    s[j] = 0;
}

/* 初始化HTTP客户端 */
void http64_init(void)
{
    vga_puts("[HTTP64] HTTP client initialized\n");
}

/* 解析URL，提取主机名和路径 */
static int http_parse_url(const char *url, char *host, int host_max,
                           char *path, int path_max)
{
    /* 跳过 http:// */
    const char *p = url;
    if (p[0] == 'h' && p[1] == 't' && p[2] == 't' && p[3] == 'p' &&
        p[4] == ':' && p[5] == '/' && p[6] == '/') {
        p += 7;
    }

    /* 提取主机名 */
    int i = 0;
    while (*p && *p != '/' && *p != ':' && i < host_max - 1) {
        host[i++] = *p++;
    }
    host[i] = 0;

    /* 提取路径 */
    if (*p == '/') {
        i = 0;
        while (*p && i < path_max - 1) {
            path[i++] = *p++;
        }
        path[i] = 0;
    } else {
        path[0] = '/';
        path[1] = 0;
    }

    return 0;
}

/* HTTP GET请求 */
int http64_get(const char *url, uint8_t *response, int max_response)
{
    char host[128];
    char path[256];

    if (http_parse_url(url, host, sizeof(host), path, sizeof(path)) != 0) {
        return -1;
    }

    vga_puts("[HTTP64] Resolving ");
    vga_puts(host);
    vga_puts("...\n");

    uint32_t ip = dns64_resolve(host);
    if (ip == 0) {
        vga_puts("[HTTP64] DNS resolution failed\n");
        return -1;
    }

    vga_puts("[HTTP64] Connecting to ");
    char ipstr[16];
    uint8_t *ipb = (uint8_t*)&ip;
    int pos = 0;
    for (int j = 0; j < 4; j++) {
        if (ipb[j] >= 100) ipstr[pos++] = '0' + ipb[j]/100;
        if (ipb[j] >= 10) ipstr[pos++] = '0' + (ipb[j]/10)%10;
        ipstr[pos++] = '0' + ipb[j]%10;
        if (j < 3) ipstr[pos++] = '.';
    }
    ipstr[pos] = 0;
    vga_puts(ipstr);
    vga_puts(":80...\n");

    int sock = tcp64_connect(ip, HTTP_PORT, 12345);
    if (sock < 0) {
        vga_puts("[HTTP64] Connection failed\n");
        return -1;
    }

    /* 等待连接建立 */
    for (int i = 0; i < 100; i++) {
        if (tcp64_get_state(sock) == TCP_ESTABLISHED) break;
    }
    if (tcp64_get_state(sock) != TCP_ESTABLISHED) {
        vga_puts("[HTTP64] Connection timeout\n");
        tcp64_close(sock);
        return -1;
    }

    /* 构建HTTP请求 */
    char request[512];
    int req_len = 0;
    http_memcpy(request + req_len, "GET ", 4); req_len += 4;
    http_memcpy(request + req_len, path, http_strlen(path)); req_len += http_strlen(path);
    http_memcpy(request + req_len, " HTTP/1.0\r\nHost: ", 16); req_len += 16;
    http_memcpy(request + req_len, host, http_strlen(host)); req_len += http_strlen(host);
    http_memcpy(request + req_len, "\r\nUser-Agent: HepuOS/1.0\r\nConnection: close\r\n\r\n", 43); req_len += 43;

    vga_puts("[HTTP64] Sending request...\n");
    tcp64_send(sock, (uint8_t*)request, req_len);

    /* 接收响应 */
    int total = 0;
    for (int retry = 0; retry < 50; retry++) {
        int len = tcp64_recv(sock, response + total, max_response - total);
        if (len > 0) {
            total += len;
        }
        if (total >= max_response - 1) break;
    }

    response[total] = 0;
    tcp64_close(sock);

    vga_puts("[HTTP64] Received ");
    char lenstr[16];
    http_itoa(total, lenstr);
    vga_puts(lenstr);
    vga_puts(" bytes\n");

    return total;
}

/* HTTP POST请求 */
int http64_post(const char *url, const char *content_type,
                const uint8_t *body, int body_len,
                uint8_t *response, int max_response)
{
    char host[128];
    char path[256];

    if (http_parse_url(url, host, sizeof(host), path, sizeof(path)) != 0) {
        return -1;
    }

    uint32_t ip = dns64_resolve(host);
    if (ip == 0) return -1;

    int sock = tcp64_connect(ip, HTTP_PORT, 12346);
    if (sock < 0) return -1;

    for (int i = 0; i < 100; i++) {
        if (tcp64_get_state(sock) == TCP_ESTABLISHED) break;
    }
    if (tcp64_get_state(sock) != TCP_ESTABLISHED) {
        tcp64_close(sock);
        return -1;
    }

    /* 构建请求头 */
    char header[512];
    int hdr_len = 0;
    http_memcpy(header + hdr_len, "POST ", 5); hdr_len += 5;
    http_memcpy(header + hdr_len, path, http_strlen(path)); hdr_len += http_strlen(path);
    http_memcpy(header + hdr_len, " HTTP/1.0\r\nHost: ", 16); hdr_len += 16;
    http_memcpy(header + hdr_len, host, http_strlen(host)); hdr_len += http_strlen(host);
    http_memcpy(header + hdr_len, "\r\nContent-Type: ", 14); hdr_len += 14;
    http_memcpy(header + hdr_len, content_type, http_strlen(content_type)); hdr_len += http_strlen(content_type);
    http_memcpy(header + hdr_len, "\r\nContent-Length: ", 18); hdr_len += 18;
    char lenstr[16];
    http_itoa(body_len, lenstr);
    http_memcpy(header + hdr_len, lenstr, http_strlen(lenstr)); hdr_len += http_strlen(lenstr);
    http_memcpy(header + hdr_len, "\r\nConnection: close\r\n\r\n", 21); hdr_len += 21;

    tcp64_send(sock, (uint8_t*)header, hdr_len);
    if (body && body_len > 0) {
        tcp64_send(sock, body, body_len);
    }

    int total = 0;
    for (int retry = 0; retry < 50; retry++) {
        int len = tcp64_recv(sock, response + total, max_response - total);
        if (len > 0) total += len;
        if (total >= max_response - 1) break;
    }

    response[total] = 0;
    tcp64_close(sock);
    return total;
}

/* 从HTTP响应中提取状态码 */
int http64_get_status(const uint8_t *response)
{
    /* 响应格式: HTTP/1.x 200 OK\r\n */
    if (response[0] != 'H' || response[1] != 'T' || response[2] != 'T' || response[3] != 'P') {
        return -1;
    }
    /* 找到第一个空格 */
    int i = 4;
    while (response[i] && response[i] != ' ') i++;
    i++;
    /* 解析状态码 */
    int status = 0;
    while (response[i] >= '0' && response[i] <= '9') {
        status = status * 10 + (response[i] - '0');
        i++;
    }
    return status;
}

/* 从HTTP响应中提取body（跳过头部） */
int http64_get_body(const uint8_t *response, int response_len,
                     uint8_t *body, int max_body)
{
    /* 找到 \r\n\r\n */
    for (int i = 0; i < response_len - 3; i++) {
        if (response[i] == '\r' && response[i+1] == '\n' &&
            response[i+2] == '\r' && response[i+3] == '\n') {
            int body_start = i + 4;
            int body_len = response_len - body_start;
            if (body_len > max_body) body_len = max_body;
            http_memcpy(body, response + body_start, body_len);
            return body_len;
        }
    }
    return 0;
}
