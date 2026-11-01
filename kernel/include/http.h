#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>

#define HTTP_PORT 80
#define HTTPS_PORT 443
#define HTTP_MAX_HEADER_SIZE 8192
#define HTTP_MAX_BODY_SIZE 65536
#define HTTP_MAX_URL_LEN 256
#define HTTP_MAX_REDIRECTS 5

/* HTTP方法 */
typedef enum {
    HTTP_METHOD_GET = 0,
    HTTP_METHOD_POST = 1,
    HTTP_METHOD_PUT = 2,
    HTTP_METHOD_DELETE = 3,
    HTTP_METHOD_HEAD = 4,
    HTTP_METHOD_OPTIONS = 5,
    HTTP_METHOD_PATCH = 6
} hepu_http_method_t;

/* HTTP状态码 */
#define HTTP_STATUS_CONTINUE          100
#define HTTP_STATUS_OK                200
#define HTTP_STATUS_CREATED           201
#define HTTP_STATUS_NO_CONTENT        204
#define HTTP_STATUS_MOVED_PERM        301
#define HTTP_STATUS_FOUND             302
#define HTTP_STATUS_NOT_MODIFIED      304
#define HTTP_STATUS_TEMP_REDIRECT     307
#define HTTP_STATUS_BAD_REQUEST       400
#define HTTP_STATUS_UNAUTHORIZED      401
#define HTTP_STATUS_FORBIDDEN         403
#define HTTP_STATUS_NOT_FOUND         404
#define HTTP_STATUS_METHOD_NOT_ALLOWED 405
#define HTTP_STATUS_INTERNAL_ERROR    500
#define HTTP_STATUS_NOT_IMPLEMENTED   501
#define HTTP_STATUS_BAD_GATEWAY       502
#define HTTP_STATUS_SERVICE_UNAVAIL   503

/* HTTP请求 */
typedef struct {
    hepu_http_method_t method;
    char url[HTTP_MAX_URL_LEN];
    char host[128];
    uint16_t port;
    char path[128];
    char user_agent[64];
    char content_type[64];
    const uint8_t *body;
    uint32_t body_length;
    int follow_redirects;
    int timeout_ms;
} hepu_http_request_t;

/* HTTP响应 */
typedef struct {
    uint16_t status_code;
    char status_text[64];
    char content_type[64];
    uint32_t content_length;
    char location[HTTP_MAX_URL_LEN];
    uint8_t *body;
    uint32_t body_length;
    int headers_count;
    struct {
        char name[64];
        char value[256];
    } headers[32];
} hepu_http_response_t;

void hepu_http_init(void);
int hepu_http_request(const hepu_http_request_t *request, hepu_http_response_t *response);
int hepu_http_get(const char *url, hepu_http_response_t *response);
int hepu_http_post(const char *url, const char *content_type,
              const uint8_t *body, uint32_t body_length, hepu_http_response_t *response);
int hepu_http_download(const char *url, const char *save_path);
const char *hepu_http_status_text(uint16_t status_code);
void hepu_http_free_response(hepu_http_response_t *response);

#endif /* HTTP_H */
