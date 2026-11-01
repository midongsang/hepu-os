/* ============================================================================
 * 赫普OS - POP3邮件接收客户端应用
 * 简单的POP3客户端，支持连接、登录、列表、接收邮件
 * V8.16: 初始实现
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

/* POP3客户端状态 */
typedef struct {
    int sockfd;
    char server_ip[16];
    uint16_t server_port;
    int connected;
    char username[64];
    char password[64];
    int authenticated;
    int message_count;
    int total_size;
} pop3_client_t;

static pop3_client_t pop3;

/* 发送POP3命令 */
static int pop3_send_command(const char *cmd)
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

    return syscall4(SYS_SEND, pop3.sockfd, (uint32_t)buf, len, 0);
}

/* 接收POP3响应（单行） */
static int pop3_recv_response(char *buf, int max_len)
{
    int total = 0;
    int n;

    /* 等待响应 */
    for (int i = 0; i < 50; i++) {
        n = syscall4(SYS_RECV, pop3.sockfd, (uint32_t)(buf + total),
                      max_len - total - 1, 0);
        if (n > 0) {
            total += n;
            buf[total] = '\0';
            /* 检查是否有换行 */
            for (int j = 0; j < total; j++) {
                if (buf[j] == '\n') {
                    return total;
                }
            }
        }
        syscall1(SYS_SLEEP, 50);
    }

    return total;
}

/* 接收多行响应（直到.结束） */
static int pop3_recv_multiline(char *buf, int max_len)
{
    int total = 0;
    int n;
    int done = 0;

    /* 等待多行响应 */
    for (int i = 0; i < 100 && !done; i++) {
        n = syscall4(SYS_RECV, pop3.sockfd, (uint32_t)(buf + total),
                      max_len - total - 1, 0);
        if (n > 0) {
            total += n;
            buf[total] = '\0';
            /* 检查是否收到.\r\n结束标记 */
            if (total >= 3) {
                for (int j = 0; j < total - 3; j++) {
                    if (buf[j] == '\r' && buf[j+1] == '\n' &&
                        buf[j+2] == '.' && buf[j+3] == '\r') {
                        done = 1;
                        break;
                    }
                }
            }
        }
        syscall1(SYS_SLEEP, 50);
    }

    return total;
}

/* 连接到POP3服务器 */
int pop3_connect(const char *server_ip, uint16_t port)
{
    memset(&pop3, 0, sizeof(pop3));
    strncpy(pop3.server_ip, server_ip, 15);
    pop3.server_port = port;

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
    pop3.sockfd = syscall3(SYS_SOCKET, 2, 1, 0);  /* AF_INET, SOCK_STREAM */
    if (pop3.sockfd < 0) {
        print_str("Error: socket() failed\n");
        return -1;
    }

    /* 连接服务器 */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = 2;
    addr.sin_port = (port >> 8) | (port << 8);
    addr.sin_addr = ip;

    int ret = syscall3(SYS_CONNECT, pop3.sockfd, (uint32_t)&addr, sizeof(addr));
    if (ret < 0) {
        print_str("Error: connect() failed\n");
        syscall1(SYS_CLOSE, pop3.sockfd);
        return -1;
    }

    pop3.connected = 1;
    print_str("Connected to ");
    print_str(server_ip);
    print_str(":");
    print_int(port);
    print_str("\n");

    /* 等待欢迎消息 */
    char response[512];
    int n = pop3_recv_response(response, sizeof(response));
    if (n > 0) {
        print_str(response);
    }

    return 0;
}

/* USER命令 */
int pop3_user(const char *username)
{
    char response[512];
    char cmd[256];

    strncpy(pop3.username, username, 63);

    strcpy(cmd, "USER ");
    strcat(cmd, username);
    pop3_send_command(cmd);
    pop3_recv_response(response, sizeof(response));
    print_str(response);

    return 0;
}

/* PASS命令 */
int pop3_pass(const char *password)
{
    char response[512];
    char cmd[256];

    strncpy(pop3.password, password, 63);

    strcpy(cmd, "PASS ");
    strcat(cmd, password);
    pop3_send_command(cmd);
    pop3_recv_response(response, sizeof(response));
    print_str(response);

    /* 检查是否认证成功（响应+OK） */
    if (response[0] == '+' && response[1] == 'O' && response[2] == 'K') {
        pop3.authenticated = 1;
        print_str("Authentication successful\n");
        return 0;
    }

    print_str("Authentication failed\n");
    return -1;
}

/* STAT命令（统计邮件数量和大小） */
int pop3_stat(void)
{
    char response[512];

    if (!pop3.authenticated) {
        print_str("Error: not authenticated\n");
        return -1;
    }

    pop3_send_command("STAT");
    pop3_recv_response(response, sizeof(response));
    print_str(response);

    /* 解析邮件数量和大小 */
    /* 响应格式：+OK <count> <size> */
    if (response[0] == '+' && response[1] == 'O' && response[2] == 'K') {
        int count = 0, size = 0;
        int i = 3;
        /* 跳过空格 */
        while (response[i] == ' ') i++;
        /* 解析数量 */
        while (response[i] >= '0' && response[i] <= '9') {
            count = count * 10 + (response[i] - '0');
            i++;
        }
        /* 跳过空格 */
        while (response[i] == ' ') i++;
        /* 解析大小 */
        while (response[i] >= '0' && response[i] <= '9') {
            size = size * 10 + (response[i] - '0');
            i++;
        }
        pop3.message_count = count;
        pop3.total_size = size;
        print_str("Messages: ");
        print_int(count);
        print_str(", Total size: ");
        print_int(size);
        print_str(" bytes\n");
    }

    return 0;
}

/* LIST命令（列出邮件） */
int pop3_list(void)
{
    char response[2048];

    if (!pop3.authenticated) {
        print_str("Error: not authenticated\n");
        return -1;
    }

    pop3_send_command("LIST");
    int n = pop3_recv_multiline(response, sizeof(response));
    if (n > 0) {
        print_str(response);
    }

    return 0;
}

/* RETR命令（接收邮件） */
int pop3_retr(int msg_id)
{
    char response[4096];
    char cmd[64];

    if (!pop3.authenticated) {
        print_str("Error: not authenticated\n");
        return -1;
    }

    strcpy(cmd, "RETR ");
    /* 整数转字符串 */
    char num[16];
    int idx = 0;
    if (msg_id == 0) num[idx++] = '0';
    while (msg_id > 0) {
        num[idx++] = '0' + (msg_id % 10);
        msg_id /= 10;
    }
    for (int i = 0; i < idx / 2; i++) {
        char tmp = num[i];
        num[i] = num[idx - 1 - i];
        num[idx - 1 - i] = tmp;
    }
    num[idx] = '\0';
    strcat(cmd, num);

    pop3_send_command(cmd);
    int n = pop3_recv_multiline(response, sizeof(response));
    if (n > 0) {
        print_str(response);
    }

    return 0;
}

/* DELE命令（删除邮件） */
int pop3_dele(int msg_id)
{
    char response[512];
    char cmd[64];

    if (!pop3.authenticated) {
        print_str("Error: not authenticated\n");
        return -1;
    }

    strcpy(cmd, "DELE ");
    char num[16];
    int idx = 0;
    if (msg_id == 0) num[idx++] = '0';
    while (msg_id > 0) {
        num[idx++] = '0' + (msg_id % 10);
        msg_id /= 10;
    }
    for (int i = 0; i < idx / 2; i++) {
        char tmp = num[i];
        num[i] = num[idx - 1 - i];
        num[idx - 1 - i] = tmp;
    }
    num[idx] = '\0';
    strcat(cmd, num);

    pop3_send_command(cmd);
    pop3_recv_response(response, sizeof(response));
    print_str(response);

    return 0;
}

/* QUIT命令（关闭连接） */
int pop3_quit(void)
{
    if (pop3.connected) {
        pop3_send_command("QUIT");
        char response[512];
        pop3_recv_response(response, sizeof(response));
        print_str(response);
        syscall1(SYS_CLOSE, pop3.sockfd);
        pop3.connected = 0;
        pop3.authenticated = 0;
    }
    print_str("POP3 session closed\n");
    return 0;
}

/* 主函数 */
int main(int argc, char *argv[])
{
    print_str("=== HepuOS POP3 Mail Client ===\n");

    if (argc < 2) {
        print_str("Usage: pop3 <server_ip> [port]\n");
        print_str("Example: pop3 pop.example.com 110\n");
        return 1;
    }

    uint16_t port = 110;
    if (argc >= 3) {
        port = 0;
        const char *p = argv[2];
        while (*p >= '0' && *p <= '9') {
            port = port * 10 + (*p - '0');
            p++;
        }
    }

    /* 连接服务器 */
    if (pop3_connect(argv[1], port) < 0) {
        return 1;
    }

    /* 认证（简化：使用测试账号） */
    pop3_user("user@hepu.os");
    pop3_pass("password");

    /* 统计邮件 */
    pop3_stat();

    /* 列出邮件 */
    pop3_list();

    /* 接收第一封邮件（如果有） */
    if (pop3.message_count > 0) {
        print_str("\n--- Retrieving message 1 ---\n");
        pop3_retr(1);
    }

    /* 关闭连接 */
    pop3_quit();

    return 0;
}
