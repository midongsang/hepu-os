/* ============================================================================
 * 赫普OS - SMTP邮件客户端应用
 * 简单的SMTP客户端，支持连接、登录、发送邮件
 * V8.15: 初始实现
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

/* SMTP客户端状态 */
typedef struct {
    int sockfd;
    char server_ip[16];
    uint16_t server_port;
    int connected;
    char username[64];
    char password[64];
    int authenticated;
} smtp_client_t;

static smtp_client_t smtp;

/* 发送SMTP命令 */
static int smtp_send_command(const char *cmd)
{
    char buf[512];
    int len = 0;

    while (cmd[len]) {
        buf[len] = cmd[len];
        len++;
    }
    buf[len++] = '\r';
    buf[len++] = '\n';

    print_str("> ");
    print_str(cmd);
    print_str("\n");

    return syscall4(SYS_SEND, smtp.sockfd, (uint32_t)buf, len, 0);
}

/* 接收SMTP响应 */
static int smtp_recv_response(char *buf, int max_len)
{
    int total = 0;
    int n;

    /* 等待响应 */
    for (int i = 0; i < 50; i++) {
        n = syscall4(SYS_RECV, smtp.sockfd, (uint32_t)(buf + total),
                      max_len - total - 1, 0);
        if (n > 0) {
            total += n;
            buf[total] = '\0';
            /* 检查是否收到完整响应（以数字开头） */
            if (total >= 3 && buf[0] >= '1' && buf[0] <= '5') {
                /* 检查是否有换行 */
                for (int j = 0; j < total; j++) {
                    if (buf[j] == '\n') {
                        return total;
                    }
                }
            }
        }
        syscall1(SYS_SLEEP, 50);
    }

    return total;
}

/* 简单的Base64编码 */
static void base64_encode(const char *input, char *output)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int len = 0;
    while (input[len]) len++;

    int i = 0, j = 0;
    while (i < len) {
        uint32_t octet_a = i < len ? (uint8_t)input[i++] : 0;
        uint32_t octet_b = i < len ? (uint8_t)input[i++] : 0;
        uint32_t octet_c = i < len ? (uint8_t)input[i++] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        output[j++] = table[(triple >> 18) & 0x3F];
        output[j++] = table[(triple >> 12) & 0x3F];
        output[j++] = (i > len + 1) ? '=' : table[(triple >> 6) & 0x3F];
        output[j++] = (i > len) ? '=' : table[triple & 0x3F];
    }
    output[j] = '\0';
}

/* 连接到SMTP服务器 */
int smtp_connect(const char *server_ip, uint16_t port)
{
    memset(&smtp, 0, sizeof(smtp));
    strncpy(smtp.server_ip, server_ip, 15);
    smtp.server_port = port;

    /* 解析IP */
    uint32_t ip = 0;
    int shift = 24;
    int val = 0;
    const char *p = server_ip;

    while (*p && shift >= 0) {
        if (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
        } else if (*p == '.') {
            ip |= (val & 0xFF) << shift;
            shift -= 8;
            val = 0;
        }
        p++;
    }
    ip |= (val & 0xFF) << shift;

    /* 创建套接字 */
    smtp.sockfd = syscall3(SYS_SOCKET, 2, 1, 0);  /* AF_INET, SOCK_STREAM */
    if (smtp.sockfd < 0) {
        print_str("Error: socket() failed\n");
        return -1;
    }

    /* 连接服务器 */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = 2;
    addr.sin_port = (port >> 8) | (port << 8);
    addr.sin_addr = ip;

    int ret = syscall3(SYS_CONNECT, smtp.sockfd, (uint32_t)&addr, sizeof(addr));
    if (ret < 0) {
        print_str("Error: connect() failed\n");
        syscall1(SYS_CLOSE, smtp.sockfd);
        return -1;
    }

    smtp.connected = 1;
    print_str("Connected to ");
    print_str(server_ip);
    print_str(":");
    print_int(port);
    print_str("\n");

    /* 等待欢迎消息 */
    char response[512];
    int n = smtp_recv_response(response, sizeof(response));
    if (n > 0) {
        print_str(response);
    }

    return 0;
}

/* EHLO命令 */
int smtp_ehlo(const char *domain)
{
    char response[512];
    char cmd[256];

    strcpy(cmd, "EHLO ");
    strcat(cmd, domain);
    smtp_send_command(cmd);
    smtp_recv_response(response, sizeof(response));
    print_str(response);

    return 0;
}

/* AUTH LOGIN认证 */
int smtp_auth_login(const char *username, const char *password)
{
    char response[512];
    char encoded[256];

    strncpy(smtp.username, username, 63);
    strncpy(smtp.password, password, 63);

    /* 发送AUTH LOGIN */
    smtp_send_command("AUTH LOGIN");
    smtp_recv_response(response, sizeof(response));
    print_str(response);

    /* 发送用户名（Base64编码） */
    base64_encode(username, encoded);
    smtp_send_command(encoded);
    smtp_recv_response(response, sizeof(response));
    print_str(response);

    /* 发送密码（Base64编码） */
    base64_encode(password, encoded);
    smtp_send_command(encoded);
    smtp_recv_response(response, sizeof(response));
    print_str(response);

    /* 检查是否认证成功（响应码235） */
    if (response[0] == '2' && response[1] == '3' && response[2] == '5') {
        smtp.authenticated = 1;
        print_str("Authentication successful\n");
        return 0;
    }

    print_str("Authentication failed\n");
    return -1;
}

/* 发送邮件 */
int smtp_send_mail(const char *from, const char *to, const char *subject, const char *body)
{
    char response[512];
    char cmd[512];

    if (!smtp.authenticated) {
        print_str("Error: not authenticated\n");
        return -1;
    }

    /* MAIL FROM */
    strcpy(cmd, "MAIL FROM:<");
    strcat(cmd, from);
    strcat(cmd, ">");
    smtp_send_command(cmd);
    smtp_recv_response(response, sizeof(response));
    print_str(response);

    /* RCPT TO */
    strcpy(cmd, "RCPT TO:<");
    strcat(cmd, to);
    strcat(cmd, ">");
    smtp_send_command(cmd);
    smtp_recv_response(response, sizeof(response));
    print_str(response);

    /* DATA */
    smtp_send_command("DATA");
    smtp_recv_response(response, sizeof(response));
    print_str(response);

    /* 发送邮件内容 */
    char data[1024];
    int data_len = 0;

    /* From */
    strcpy(data, "From: ");
    strcat(data, from);
    strcat(data, "\r\n");
    /* To */
    strcat(data, "To: ");
    strcat(data, to);
    strcat(data, "\r\n");
    /* Subject */
    strcat(data, "Subject: ");
    strcat(data, subject);
    strcat(data, "\r\n");
    /* 空行 */
    strcat(data, "\r\n");
    /* Body */
    strcat(data, body);
    strcat(data, "\r\n.\r\n");

    data_len = strlen(data);
    syscall4(SYS_SEND, smtp.sockfd, (uint32_t)data, data_len, 0);
    print_str("Message sent\n");

    /* 等待响应 */
    smtp_recv_response(response, sizeof(response));
    print_str(response);

    return 0;
}

/* 关闭连接 */
int smtp_quit(void)
{
    if (smtp.connected) {
        smtp_send_command("QUIT");
        char response[512];
        smtp_recv_response(response, sizeof(response));
        print_str(response);
        syscall1(SYS_CLOSE, smtp.sockfd);
        smtp.connected = 0;
        smtp.authenticated = 0;
    }
    print_str("SMTP session closed\n");
    return 0;
}

/* 主函数 */
int main(int argc, char *argv[])
{
    print_str("=== HepuOS SMTP Mail Client ===\n");

    if (argc < 2) {
        print_str("Usage: smtp <server_ip> [port]\n");
        print_str("Example: smtp smtp.example.com 587\n");
        return 1;
    }

    uint16_t port = 25;
    if (argc >= 3) {
        port = 0;
        const char *p = argv[2];
        while (*p >= '0' && *p <= '9') {
            port = port * 10 + (*p - '0');
            p++;
        }
    }

    /* 连接服务器 */
    if (smtp_connect(argv[1], port) < 0) {
        return 1;
    }

    /* EHLO */
    smtp_ehlo("hepu.os");

    /* 认证（简化：使用测试账号） */
    smtp_auth_login("user@hepu.os", "password");

    /* 发送测试邮件 */
    smtp_send_mail("user@hepu.os", "recipient@example.com",
                    "Test from HepuOS", "Hello from HepuOS SMTP client!");

    /* 关闭连接 */
    smtp_quit();

    return 0;
}
