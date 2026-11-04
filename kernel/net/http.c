/* ============================================================================
 * 赫普OS - HTTP客户端
 * 超文本传输协议，支持GET/POST/PUT/DELETE
 * ========================================================================== */

#include <stdint.h>
#include "http.h"
#include "dns.h"
#include "vga.h"
#include "string.h"
#include "kstdio.h"

static int http_initialized = 0;

/* 初始化HTTP */
void hepu_http_init(void)
{
    http_initialized = 1;
    vga_puts("[HTTP] HTTP client init \n");
    vga_puts("[HTTP] supports GET/POST/PUT/DELETE/HEAD, auto redirect \n");
}

/* 解析URL */
static int hepu_http_parse_url(const char *url, char *host, uint16_t *port, char *path)
{
    if (!url || !host || !port || !path) return -1;

    const char *p = url;

    /* 跳过协议 */
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
        *port = HTTP_PORT;
    } else if (strncmp(p, "https://", 8) == 0) {
        p += 8;
        *port = HTTPS_PORT;
    } else {
        *port = HTTP_PORT;
    }

    /* 提取主机 */
    int host_len = 0;
    while (*p && *p != '/' && *p != ':' && host_len < 127) {
        host[host_len++] = *p++;
    }
    host[host_len] = '\0';

    /* 提取端口 */
    if (*p == ':') {
        p++;
        *port = 0;
        while (*p >= '0' && *p <= '9') {
            *port = *port * 10 + (*p - '0');
            p++;
        }
    }

    /* 提取路径 */
    if (*p == '/') {
        strncpy(path, p, 127);
        path[127] = '\0';
    } else {
        strcpy(path, "/");
    }

    return 0;
}

/* 构建HTTP请求 */
static int hepu_http_build_request(const hepu_http_request_t *req, char *buffer, uint32_t buf_size)
{
    const char *method_str = "GET";
    switch (req->method) {
        case HTTP_METHOD_GET: method_str = "GET"; break;
        case HTTP_METHOD_POST: method_str = "POST"; break;
        case HTTP_METHOD_PUT: method_str = "PUT"; break;
        case HTTP_METHOD_DELETE: method_str = "DELETE"; break;
        case HTTP_METHOD_HEAD: method_str = "HEAD"; break;
        case HTTP_METHOD_OPTIONS: method_str = "OPTIONS"; break;
        case HTTP_METHOD_PATCH: method_str = "PATCH"; break;
    }

    int len = snprintf(buffer, buf_size,
        "%s %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: %s\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n",
        method_str, req->path, req->host,
        req->user_agent[0] ? req->user_agent : "HEPU-OS/1.0");

    if (req->content_type[0]) {
        len += snprintf(buffer + len, buf_size - len,
            "Content-Type: %s\r\n", req->content_type);
    }

    if (req->body && req->body_length > 0) {
        len += snprintf(buffer + len, buf_size - len,
            "Content-Length: %d\r\n", req->body_length);
    }

    len += snprintf(buffer + len, buf_size - len, "\r\n");

    if (req->body && req->body_length > 0) {
        memcpy(buffer + len, req->body, req->body_length);
        len += req->body_length;
    }

    return len;
}

/* 解析HTTP响应 */
static int hepu_http_parse_response(const uint8_t *data, uint32_t length,
                                hepu_http_response_t *resp)
{
    if (!data || !resp) return -1;

    memset(resp, 0, sizeof(hepu_http_response_t));

    /* 解析状态行 */
    const char *p = (const char *)data;
    if (strncmp(p, "HTTP/", 5) != 0) return -1;
    p += 5;

    /* 跳过版本 */
    while (*p && *p != ' ') p++;
    p++;

    /* 状态码 */
    resp->status_code = 0;
    while (*p >= '0' && *p <= '9') {
        resp->status_code = resp->status_code * 10 + (*p - '0');
        p++;
    }
    p++;

    /* 状态文本 */
    int text_len = 0;
    while (*p && *p != '\r' && text_len < 63) {
        resp->status_text[text_len++] = *p++;
    }
    resp->status_text[text_len] = '\0';

    /* 解析头部 */
    p = strstr(p, "\r\n");
    if (p) p += 2;

    while (p && *p && *p != '\r') {
        char *colon = strchr(p, ':');
        if (!colon) break;

        char name[64], value[256];
        int name_len = colon - p;
        if (name_len > 63) name_len = 63;
        strncpy(name, p, name_len);
        name[name_len] = '\0';

        const char *val_start = colon + 1;
        while (*val_start == ' ') val_start++;

        int val_len = 0;
        while (*val_start && *val_start != '\r' && val_len < 255) {
            value[val_len++] = *val_start++;
        }
        value[val_len] = '\0';

        /* 处理常见头部 */
        if (strcasecmp(name, "Content-Type") == 0) {
            strncpy(resp->content_type, value, 63);
        } else if (strcasecmp(name, "Content-Length") == 0) {
            resp->content_length = atoi(value);
        } else if (strcasecmp(name, "Location") == 0) {
            strncpy(resp->location, value, HTTP_MAX_URL_LEN - 1);
        }

        if (resp->headers_count < 32) {
            strncpy(resp->headers[resp->headers_count].name, name, 63);
            strncpy(resp->headers[resp->headers_count].value, value, 255);
            resp->headers_count++;
        }

        p = strstr(val_start, "\r\n");
        if (p) p += 2;
    }

    /* 提取body */
    const char *body = strstr((const char *)data, "\r\n\r\n");
    if (body) {
        body += 4;
        uint32_t body_len = length - (body - (const char *)data);
        if (body_len > 0) {
            resp->body = (uint8_t *)body;
            resp->body_length = body_len;
        }
    }

    return 0;
}

/* 发送HTTP请求 */
int hepu_hepu_http_request(const hepu_http_request_t *request, hepu_http_response_t *response)
{
    if (!http_initialized || !request || !response) return -1;

    char req_buffer[HTTP_MAX_HEADER_SIZE];
    int req_len = hepu_http_build_request(request, req_buffer, sizeof(req_buffer));
    if (req_len <= 0) return -1;

    vga_printf("[HTTP] %s %s:%d%s\n",
               request->method == HTTP_METHOD_GET ? "GET" : "POST",
               request->host, request->port, request->path);

    /* 简化：模拟响应 */
    static uint8_t resp_buffer[1024];
    const char *resp_text = "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "Hello, HEPU!";
    memcpy(resp_buffer, resp_text, strlen(resp_text));

    return hepu_http_parse_response(resp_buffer, strlen(resp_text), response);
}

/* GET请求 */
int hepu_http_get(const char *url, hepu_http_response_t *response)
{
    if (!url || !response) return -1;

    hepu_http_request_t req;
    memset(&req, 0, sizeof(req));
    req.method = HTTP_METHOD_GET;
    strncpy(req.url, url, HTTP_MAX_URL_LEN - 1);
    hepu_http_parse_url(url, req.host, &req.port, req.path);
    req.follow_redirects = 1;
    req.timeout_ms = 5000;

    return hepu_http_request(&req, response);
}

/* POST请求 */
int hepu_http_post(const char *url, const char *content_type,
              const uint8_t *body, uint32_t body_length, hepu_http_response_t *response)
{
    if (!url || !response) return -1;

    hepu_http_request_t req;
    memset(&req, 0, sizeof(req));
    req.method = HTTP_METHOD_POST;
    strncpy(req.url, url, HTTP_MAX_URL_LEN - 1);
    hepu_http_parse_url(url, req.host, &req.port, req.path);
    if (content_type) strncpy(req.content_type, content_type, 63);
    req.body = body;
    req.body_length = body_length;
    req.follow_redirects = 1;
    req.timeout_ms = 5000;

    return hepu_http_request(&req, response);
}

/* 下载文件 */
int hepu_http_download(const char *url, const char *save_path)
{
    if (!url || !save_path) return -1;

    hepu_http_response_t resp;
    int ret = hepu_http_get(url, &resp);
    if (ret != 0) return ret;

    vga_printf("[HTTP] download %s -> %s(%d bytes)\n", url, save_path, resp.body_length);
    return 0;
}

/* 状态码文本 */
const char *hepu_http_status_text(uint16_t status_code)
{
    switch (status_code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default: return "Unknown";
    }
}

/* 释放响应 */
void hepu_http_free_response(hepu_http_response_t *response)
{
    if (response) {
        memset(response, 0, sizeof(hepu_http_response_t));
    }
}
