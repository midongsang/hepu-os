/* ============================================================================
 * 赫普OS - 简单Web服务器
 * 支持HTTP/1.0 GET请求，返回静态页面
 * V8.10: 初始实现
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* 系统调用 */
#define SYS_SOCKET   40
#define SYS_BIND     42
#define SYS_LISTEN   43
#define SYS_ACCEPT   44
#define SYS_SEND     45
#define SYS_RECV     46
#define SYS_CLOSE    11
#define SYS_SLEEP    15
#define SYS_PRINT    2

/* 套接字地址结构 */
struct sockaddr_in {
    uint16_t sin_family;
    uint16_t sin_port;
    uint32_t sin_addr;
    uint8_t  sin_zero[8];
};

/* 系统调用 */
static inline uint32_t syscall1(uint32_t num, uint32_t arg1)
{
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1)
        : "memory"
    );
    return ret;
}

static inline uint32_t syscall3(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3)
        : "memory"
    );
    return ret;
}

static inline uint32_t syscall4(uint32_t num, uint32_t arg1, uint32_t arg2,
                                 uint32_t arg3, uint32_t arg4)
{
    uint32_t ret;
    __asm__ volatile (
        "push %%esi\n"
        "mov %4, %%esi\n"
        "int $0x80\n"
        "pop %%esi"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3), "g"(arg4)
        : "memory"
    );
    return ret;
}

/* 打印字符串 */
static void print_str(const char *s)
{
    syscall1(SYS_PRINT, (uint32_t)s);
}

/* 打印整数 */
static void print_int(int val)
{
    char buf[16];
    int idx = 0;
    int neg = 0;

    if (val < 0) {
        neg = 1;
        val = -val;
    }
    if (val == 0) {
        buf[idx++] = '0';
    }
    while (val > 0) {
        buf[idx++] = '0' + (val % 10);
        val /= 10;
    }
    if (neg) buf[idx++] = '-';
    buf[idx] = '\0';

    /* 反转 */
    for (int i = 0; i < idx / 2; i++) {
        char tmp = buf[i];
        buf[i] = buf[idx - 1 - i];
        buf[idx - 1 - i] = tmp;
    }

    print_str(buf);
}

/* 主页HTML */
static const char *index_html =
"HTTP/1.0 200 OK\r\n"
"Content-Type: text/html\r\n"
"Content-Length: 512\r\n"
"Connection: close\r\n"
"\r\n"
"<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"<title>HepuOS Web Server</title>\n"
"<style>\n"
"body { font-family: Arial, sans-serif; background: #1a1a2e; color: #eee; margin: 0; padding: 40px; }\n"
".container { max-width: 800px; margin: 0 auto; }\n"
"h1 { color: #4fc3f7; border-bottom: 2px solid #4fc3f7; padding-bottom: 10px; }\n"
".info { background: #16213e; padding: 20px; border-radius: 8px; margin: 20px 0; }\n"
".info p { margin: 8px 0; }\n"
".status { color: #4caf50; font-weight: bold; }\n"
"footer { margin-top: 40px; text-align: center; color: #888; font-size: 12px; }\n"
"</style>\n"
"</head>\n"
"<body>\n"
"<div class='container'>\n"
"<h1>HepuOS Web Server</h1>\n"
"<div class='info'>\n"
"<p>Status: <span class='status'>Running</span></p>\n"
"<p>Server: HepuOS HTTP Server v1.0</p>\n"
"<p>Protocol: HTTP/1.0</p>\n"
"<p>Kernel: HepuOS Microkernel</p>\n"
"<p>Architecture: x86 (32-bit)</p>\n"
"</div>\n"
"<p>Welcome to HepuOS! This page is served by the built-in web server.</p>\n"
"<footer>Powered by HepuOS &copy; 2026</footer>\n"
"</div>\n"
"</body>\n"
"</html>\n";

/* 404页面 */
static const char *not_found_html =
"HTTP/1.0 404 Not Found\r\n"
"Content-Type: text/html\r\n"
"Content-Length: 256\r\n"
"Connection: close\r\n"
"\r\n"
"<!DOCTYPE html>\n"
"<html>\n"
"<head><title>404 Not Found</title></head>\n"
"<body style='font-family: Arial; background: #1a1a2e; color: #eee; text-align: center; padding: 100px;'>\n"
"<h1 style='color: #f44336;'>404 - Not Found</h1>\n"
"<p>The requested page was not found on this server.</p>\n"
"<p><a href='/' style='color: #4fc3f7;'>Return to Home</a></p>\n"
"</body>\n"
"</html>\n";

/* 处理客户端请求 */
static void handle_client(int client_fd)
{
    char request[1024];
    int recv_len = 0;

    /* 接收请求 */
    recv_len = syscall4(SYS_RECV, client_fd, (uint32_t)request, sizeof(request) - 1, 0);
    if (recv_len <= 0) {
        syscall1(SYS_CLOSE, client_fd);
        return;
    }
    request[recv_len] = '\0';

    print_str("Request received: ");
    /* 只打印请求行 */
    for (int i = 0; i < recv_len && request[i] != '\r'; i++) {
        char c = request[i];
        syscall1(SYS_PRINT, (uint32_t)&c);
    }
    print_str("\n");

    /* 解析请求路径 */
    const char *path = "/";
    if (recv_len > 4 && request[0] == 'G' && request[1] == 'E' && request[2] == 'T') {
        path = request + 4;
        /* 找到路径结束 */
        for (int i = 0; i < 100 && path[i] && path[i] != ' '; i++) {
            if (path[i] == ' ') {
                ((char *)path)[i] = '\0';
                break;
            }
        }
    }

    /* 根据路径返回响应 */
    const char *response;
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        response = index_html;
    } else {
        response = not_found_html;
    }

    /* 发送响应 */
    int response_len = strlen(response);
    int sent = syscall4(SYS_SEND, client_fd, (uint32_t)response, response_len, 0);

    print_str("Response sent: ");
    print_int(sent);
    print_str(" bytes\n");

    /* 关闭连接 */
    syscall1(SYS_CLOSE, client_fd);
}

/* 主函数 */
int main(int argc, char *argv[])
{
    print_str("=== HepuOS Web Server ===\n");

    uint16_t port = 8080;
    if (argc >= 2) {
        port = 0;
        const char *p = argv[1];
        while (*p >= '0' && *p <= '9') {
            port = port * 10 + (*p - '0');
            p++;
        }
    }

    print_str("Starting web server on port ");
    print_int(port);
    print_str("\n");

    /* 创建TCP套接字 */
    int server_fd = syscall3(SYS_SOCKET, 2, 1, 0);  /* AF_INET, SOCK_STREAM, 0 */
    if (server_fd < 0) {
        print_str("Error: socket() failed\n");
        return 1;
    }
    print_str("Socket created: fd=");
    print_int(server_fd);
    print_str("\n");

    /* 绑定地址 */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = 2;  /* AF_INET */
    addr.sin_port = (port >> 8) | (port << 8);  /* 网络字节序 */
    addr.sin_addr = 0;  /* INADDR_ANY */

    int ret = syscall3(SYS_BIND, server_fd, (uint32_t)&addr, sizeof(addr));
    if (ret < 0) {
        print_str("Error: bind() failed\n");
        syscall1(SYS_CLOSE, server_fd);
        return 1;
    }
    print_str("Bind successful\n");

    /* 监听 */
    ret = syscall3(SYS_LISTEN, server_fd, 5, 0);
    if (ret < 0) {
        print_str("Error: listen() failed\n");
        syscall1(SYS_CLOSE, server_fd);
        return 1;
    }
    print_str("Listening for connections...\n");
    print_str("Press Ctrl+C to stop\n\n");

    /* 接受连接循环 */
    while (1) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);

        int client_fd = syscall4(SYS_ACCEPT, server_fd,
                                  (uint32_t)&client_addr, (uint32_t)&client_len, 0);
        if (client_fd >= 0) {
            print_str("Client connected: fd=");
            print_int(client_fd);
            print_str("\n");

            /* 处理客户端请求 */
            handle_client(client_fd);

            print_str("Client disconnected\n\n");
        }

        /* 短暂休眠，避免CPU占用过高 */
        syscall1(SYS_SLEEP, 10);
    }

    /* 不会到达这里 */
    syscall1(SYS_CLOSE, server_fd);
    return 0;
}
