/* ============================================================================
 * 赫普OS - HTTP客户端应用
 * 简单的HTTP/1.0客户端，支持GET请求
 * V8.07: 初始实现
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* 系统调用 */
#define SYS_SOCKET   40
#define SYS_CONNECT  41
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

/* 简单的字符串转IP */
static uint32_t parse_ip(const char *s)
{
    uint32_t ip = 0;
    int shift = 24;
    int val = 0;

    while (*s && shift >= 0) {
        if (*s >= '0' && *s <= '9') {
            val = val * 10 + (*s - '0');
        } else if (*s == '.') {
            ip |= (val & 0xFF) << shift;
            shift -= 8;
            val = 0;
        }
        s++;
    }
    ip |= (val & 0xFF) << shift;

    return ip;
}

/* 主函数 */
int main(int argc, char *argv[])
{
    print_str("=== HepuOS HTTP Client ===\n");

    if (argc < 2) {
        print_str("Usage: http_client <ip> [port] [path]\n");
        print_str("Example: http_client 142.250.200.189 80 /\n");
        return 1;
    }

    /* 解析参数 */
    uint32_t server_ip = parse_ip(argv[1]);
    uint16_t port = 80;
    const char *path = "/";

    if (argc >= 3) {
        port = 0;
        const char *p = argv[2];
        while (*p >= '0' && *p <= '9') {
            port = port * 10 + (*p - '0');
            p++;
        }
    }
    if (argc >= 4) {
        path = argv[3];
    }

    print_str("Connecting to ");
    print_str(argv[1]);
    print_str(":");
    print_int(port);
    print_str("\n");

    /* 创建TCP套接字 */
    int sockfd = syscall3(SYS_SOCKET, 2, 1, 0);  /* AF_INET, SOCK_STREAM, 0 */
    if (sockfd < 0) {
        print_str("Error: socket() failed\n");
        return 1;
    }
    print_str("Socket created: fd=");
    print_int(sockfd);
    print_str("\n");

    /* 连接服务器 */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = 2;  /* AF_INET */
    addr.sin_port = (port >> 8) | (port << 8);  /* 网络字节序 */
    addr.sin_addr = server_ip;

    int ret = syscall3(SYS_CONNECT, sockfd, (uint32_t)&addr, sizeof(addr));
    if (ret < 0) {
        print_str("Error: connect() failed\n");
        syscall1(SYS_CLOSE, sockfd);
        return 1;
    }
    print_str("Connected!\n");

    /* 等待连接建立 */
    syscall1(SYS_SLEEP, 500);

    /* 构造HTTP GET请求 */
    char request[512];
    int req_len = 0;

    request[req_len++] = 'G';
    request[req_len++] = 'E';
    request[req_len++] = 'T';
    request[req_len++] = ' ';
    while (*path) {
        request[req_len++] = *path++;
    }
    request[req_len++] = ' ';
    request[req_len++] = 'H';
    request[req_len++] = 'T';
    request[req_len++] = 'T';
    request[req_len++] = 'P';
    request[req_len++] = '/';
    request[req_len++] = '1';
    request[req_len++] = '.';
    request[req_len++] = '0';
    request[req_len++] = '\r';
    request[req_len++] = '\n';

    /* Host头 */
    const char *host = "Host: ";
    while (*host) request[req_len++] = *host++;
    const char *ip_str = argv[1];
    while (*ip_str) request[req_len++] = *ip_str++;
    request[req_len++] = '\r';
    request[req_len++] = '\n';

    /* User-Agent */
    const char *ua = "User-Agent: HepuOS-HTTP-Client/1.0\r\n";
    while (*ua) request[req_len++] = *ua++;

    /* Connection: close */
    const char *conn = "Connection: close\r\n";
    while (*conn) request[req_len++] = *conn++;

    /* 空行 */
    request[req_len++] = '\r';
    request[req_len++] = '\n';
    request[req_len] = '\0';

    print_str("Sending request:\n");
    print_str(request);
    print_str("\n");

    /* 发送请求 */
    int sent = syscall4(SYS_SEND, sockfd, (uint32_t)request, req_len, 0);
    print_str("Sent ");
    print_int(sent);
    print_str(" bytes\n");

    /* 等待响应 */
    syscall1(SYS_SLEEP, 1000);

    /* 接收响应 */
    char response[2048];
    int total_recv = 0;

    print_str("\n=== Response ===\n");

    for (int i = 0; i < 10; i++) {
        int n = syscall4(SYS_RECV, sockfd, (uint32_t)response, sizeof(response) - 1, 0);
        if (n > 0) {
            response[n] = '\0';
            print_str(response);
            total_recv += n;
        } else if (n == 0) {
            break;
        }
        syscall1(SYS_SLEEP, 200);
    }

    print_str("\n=== End of Response ===\n");
    print_str("Total received: ");
    print_int(total_recv);
    print_str(" bytes\n");

    /* 关闭套接字 */
    syscall1(SYS_CLOSE, sockfd);
    print_str("Connection closed.\n");

    return 0;
}
