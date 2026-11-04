/* ============================================================================
 * 赫普OS - FTP客户端应用
 * 简单的FTP客户端，支持连接、登录、列表、下载
 * V8.13: 初始实现
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

/* FTP客户端状态 */
typedef struct {
    int control_fd;
    int data_fd;
    char server_ip[16];
    uint16_t server_port;
    int connected;
    int logged_in;
    char current_dir[256];
} ftp_client_t;

static ftp_client_t ftp;

/* 发送FTP命令 */
static int ftp_send_command(const char *cmd)
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

    return syscall4(SYS_SEND, ftp.control_fd, (uint32_t)buf, len, 0);
}

/* 接收FTP响应 */
static int ftp_recv_response(char *buf, int max_len)
{
    int total = 0;
    int n;

    /* 等待响应 */
    for (int i = 0; i < 50; i++) {
        n = syscall4(SYS_RECV, ftp.control_fd, (uint32_t)(buf + total),
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

/* 连接到FTP服务器 */
int ftp_connect(const char *server_ip, uint16_t port)
{
    memset(&ftp, 0, sizeof(ftp));
    strncpy(ftp.server_ip, server_ip, 15);
    ftp.server_port = port;

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

    /* 创建控制连接 */
    ftp.control_fd = syscall3(SYS_SOCKET, 2, 1, 0);  /* AF_INET, SOCK_STREAM */
    if (ftp.control_fd < 0) {
        print_str("Error: socket() failed\n");
        return -1;
    }

    /* 连接服务器 */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = 2;
    addr.sin_port = (port >> 8) | (port << 8);
    addr.sin_addr = ip;

    int ret = syscall3(SYS_CONNECT, ftp.control_fd, (uint32_t)&addr, sizeof(addr));
    if (ret < 0) {
        print_str("Error: connect() failed\n");
        syscall1(SYS_CLOSE, ftp.control_fd);
        return -1;
    }

    ftp.connected = 1;
    print_str("Connected to ");
    print_str(server_ip);
    print_str(":");
    print_int(port);
    print_str("\n");

    /* 等待欢迎消息 */
    char response[512];
    int n = ftp_recv_response(response, sizeof(response));
    if (n > 0) {
        print_str(response);
    }

    return 0;
}

/* 登录FTP服务器 */
int ftp_login(const char *username, const char *password)
{
    char response[512];
    char cmd[256];

    /* 发送用户名 */
    strcpy(cmd, "USER ");
    strcat(cmd, username);
    ftp_send_command(cmd);
    ftp_recv_response(response, sizeof(response));
    print_str(response);

    /* 发送密码 */
    strcpy(cmd, "PASS ");
    strcat(cmd, password);
    ftp_send_command(cmd);
    ftp_recv_response(response, sizeof(response));
    print_str(response);

    /* 检查是否登录成功（响应码230） */
    if (response[0] == '2' && response[1] == '3' && response[2] == '0') {
        ftp.logged_in = 1;
        print_str("Login successful\n");
        return 0;
    }

    print_str("Login failed\n");
    return -1;
}

/* 列出目录内容 */
int ftp_list(const char *path)
{
    char response[512];
    char cmd[256];

    if (!ftp.logged_in) {
        print_str("Error: not logged in\n");
        return -1;
    }

    /* 发送PASV命令（被动模式） */
    ftp_send_command("PASV");
    ftp_recv_response(response, sizeof(response));
    print_str(response);

    /* 解析PASV响应中的IP和端口 */
    /* 简化：使用主动模式 */

    /* 发送LIST命令 */
    if (path && path[0]) {
        strcpy(cmd, "LIST ");
        strcat(cmd, path);
    } else {
        strcpy(cmd, "LIST");
    }
    ftp_send_command(cmd);

    /* 接收响应 */
    ftp_recv_response(response, sizeof(response));
    print_str(response);

    return 0;
}

/* 下载文件 */
int ftp_get(const char *remote_file, const char *local_file)
{
    char response[512];
    char cmd[256];

    if (!ftp.logged_in) {
        print_str("Error: not logged in\n");
        return -1;
    }

    (void)local_file;  /* 简化：不保存到本地文件 */

    /* 设置二进制模式 */
    ftp_send_command("TYPE I");
    ftp_recv_response(response, sizeof(response));

    /* 发送PASV命令 */
    ftp_send_command("PASV");
    ftp_recv_response(response, sizeof(response));

    /* 发送RETR命令 */
    strcpy(cmd, "RETR ");
    strcat(cmd, remote_file);
    ftp_send_command(cmd);

    /* 接收响应 */
    ftp_recv_response(response, sizeof(response));
    print_str(response);

    print_str("File download started (simplified)\n");
    return 0;
}

/* 关闭连接 */
int ftp_quit(void)
{
    if (ftp.connected) {
        ftp_send_command("QUIT");
        char response[512];
        ftp_recv_response(response, sizeof(response));
        print_str(response);
        syscall1(SYS_CLOSE, ftp.control_fd);
        ftp.connected = 0;
        ftp.logged_in = 0;
    }
    print_str("FTP session closed\n");
    return 0;
}

/* 主函数 */
int main(int argc, char *argv[])
{
    print_str("=== HepuOS FTP Client ===\n");

    if (argc < 2) {
        print_str("Usage: ftp <server_ip> [port]\n");
        print_str("Example: ftp 192.168.1.1 21\n");
        return 1;
    }

    uint16_t port = 21;
    if (argc >= 3) {
        port = 0;
        const char *p = argv[2];
        while (*p >= '0' && *p <= '9') {
            port = port * 10 + (*p - '0');
            p++;
        }
    }

    /* 连接服务器 */
    if (ftp_connect(argv[1], port) < 0) {
        return 1;
    }

    /* 登录（简化：使用匿名登录） */
    ftp_login("anonymous", "user@hepu.os");

    /* 列出根目录 */
    ftp_list("/");

    /* 关闭连接 */
    ftp_quit();

    return 0;
}
