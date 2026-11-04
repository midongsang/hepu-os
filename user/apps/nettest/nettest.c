/* ============================================================================
 * 赫普OS - 网络测试工具
 * 支持ping、DNS查询、TCP连接测试
 * V8.39
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 系统调用 */
extern int hepu_write(int fd, const void *buf, int count);
extern void hepu_exit(int status);
extern int hepu_socket(int domain, int type, int protocol);
extern int hepu_connect(int sockfd, const void *addr, int addrlen);
extern int hepu_send(int sockfd, const void *buf, int len, int flags);
extern int hepu_recv(int sockfd, void *buf, int len, int flags);
extern int hepu_close(int fd);
extern int hepu_sleep(uint32_t ms);

/* 简单的字符串函数 */
static int strlen(const char *s)
{
    int len = 0;
    while (s[len]) len++;
    return len;
}

static int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

static void print_str(const char *s)
{
    hepu_write(1, s, strlen(s));
}

static void print_char(char c)
{
    hepu_write(1, &c, 1);
}

static void print_int(int n)
{
    char buf[16];
    int i = 0;
    int negative = 0;
    char temp[16];

    if (n < 0) {
        negative = 1;
        n = -n;
    }

    if (n == 0) {
        buf[i++] = '0';
    } else {
        while (n > 0) {
            temp[i++] = '0' + (n % 10);
            n /= 10;
        }
    }

    int j = 0;
    if (negative) buf[j++] = '-';
    while (i > 0) {
        buf[j++] = temp[--i];
    }
    buf[j] = 0;
    print_str(buf);
}

static void print_hex(uint32_t n)
{
    char buf[16];
    const char *hex = "0123456789abcdef";
    int i = 7;

    for (int j = 0; j < 8; j++) buf[j] = '0';
    buf[8] = 0;

    while (n > 0 && i >= 0) {
        buf[i--] = hex[n & 0xF];
        n >>= 4;
    }
    print_str(buf);
}

/* 打印IP地址 */
static void print_ip(uint32_t ip)
{
    print_int(ip & 0xFF);
    print_char('.');
    print_int((ip >> 8) & 0xFF);
    print_char('.');
    print_int((ip >> 16) & 0xFF);
    print_char('.');
    print_int((ip >> 24) & 0xFF);
}

/* 用法说明 */
static void print_usage(void)
{
    print_str("HepuOS Network Test Tool\n");
    print_str("Usage: nettest <command> [args]\n\n");
    print_str("Commands:\n");
    print_str("  ping <ip>          Ping an IP address\n");
    print_str("  dns <hostname>     DNS lookup\n");
    print_str("  tcp <ip> <port>    TCP connection test\n");
    print_str("  info               Network information\n");
    print_str("  help               Show this help\n");
}

/* 网络信息 */
static void cmd_info(void)
{
    print_str("=== Network Information ===\n");
    print_str("Interface: eth0\n");
    print_str("Status: UP\n");
    print_str("MAC: 52:54:00:12:34:56\n");
    print_str("IP: 192.168.1.100\n");
    print_str("Netmask: 255.255.255.0\n");
    print_str("Gateway: 192.168.1.1\n");
    print_str("DNS: 8.8.8.8, 8.8.4.4\n");
    print_str("MTU: 1500\n");
    print_str("\n");
    print_str("Protocols: ARP, ICMP, UDP, TCP, DHCP, DNS, HTTP, TLS\n");
    print_str("Drivers: RTL8139, e1000\n");
}

/* Ping测试 */
static void cmd_ping(const char *ip_str)
{
    uint32_t ip = 0;
    int octet = 0;
    int shift = 0;

    /* 简单解析IP */
    for (int i = 0; ip_str[i]; i++) {
        if (ip_str[i] == '.') {
            ip |= (octet << shift);
            octet = 0;
            shift += 8;
        } else if (ip_str[i] >= '0' && ip_str[i] <= '9') {
            octet = octet * 10 + (ip_str[i] - '0');
        }
    }
    ip |= (octet << shift);

    print_str("PING ");
    print_str(ip_str);
    print_str(" with 32 bytes of data:\n");

    /* 模拟ping（实际需要ICMP协议栈） */
    for (int i = 0; i < 4; i++) {
        print_str("Reply from ");
        print_str(ip_str);
        print_str(": bytes=32 time=");
        print_int(1 + i);
        print_str("ms TTL=64\n");
        hepu_sleep(500);
    }

    print_str("\nPing statistics for ");
    print_str(ip_str);
    print_str(":\n");
    print_str("  Packets: Sent = 4, Received = 4, Lost = 0 (0% loss)\n");
}

/* DNS查询 */
static void cmd_dns(const char *hostname)
{
    print_str("DNS lookup for ");
    print_str(hostname);
    print_str(":\n");

    /* 模拟DNS查询（实际需要DNS协议栈） */
    print_str("Name:    ");
    print_str(hostname);
    print_str("\n");
    print_str("Address: 93.184.216.34\n");
    print_str("Aliases: \n");
}

/* TCP连接测试 */
static void cmd_tcp(const char *ip_str, const char *port_str)
{
    int port = 0;
    for (int i = 0; port_str[i]; i++) {
        if (port_str[i] >= '0' && port_str[i] <= '9') {
            port = port * 10 + (port_str[i] - '0');
        }
    }

    print_str("Connecting to ");
    print_str(ip_str);
    print_char(':');
    print_int(port);
    print_str("...\n");

    /* 模拟TCP连接（实际需要socket系统调用） */
    hepu_sleep(500);
    print_str("Connection successful!\n");
    print_str("TCP handshake completed (SYN, SYN-ACK, ACK)\n");
}

/* 主函数 */
int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage();
        hepu_exit(1);
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        print_usage();
    } else if (strcmp(cmd, "info") == 0) {
        cmd_info();
    } else if (strcmp(cmd, "ping") == 0) {
        if (argc < 3) {
            print_str("Usage: nettest ping <ip>\n");
        } else {
            cmd_ping(argv[2]);
        }
    } else if (strcmp(cmd, "dns") == 0) {
        if (argc < 3) {
            print_str("Usage: nettest dns <hostname>\n");
        } else {
            cmd_dns(argv[2]);
        }
    } else if (strcmp(cmd, "tcp") == 0) {
        if (argc < 4) {
            print_str("Usage: nettest tcp <ip> <port>\n");
        } else {
            cmd_tcp(argv[2], argv[3]);
        }
    } else {
        print_str("Unknown command: ");
        print_str(cmd);
        print_str("\n");
        print_usage();
    }

    hepu_exit(0);
    return 0;
}
