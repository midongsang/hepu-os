/* ============================================================================
 * 赫普OS 64位 - UDP协议（用户数据报协议）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

#define UDP_MAX_PORTS 16
#define UDP_RX_BUF_SIZE 1500

/* UDP头 */
typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

/* UDP接收缓冲区 */
typedef struct {
    int used;
    uint16_t port;
    uint8_t buffer[UDP_RX_BUF_SIZE];
    int length;
    uint32_t src_ip;
    uint16_t src_port;
} udp_socket_t;

static udp_socket_t udp_sockets[UDP_MAX_PORTS];
static int udp_initialized = 0;

extern void vga_puts(const char *s);
extern int ip64_send(uint32_t dst_ip, uint8_t protocol, const uint8_t *data, int data_len);
extern uint32_t eth64_get_ip(void);

static void udp_memcpy(void *dst, const void *src, int n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}

static uint16_t htons16(uint16_t v) { return (v >> 8) | (v << 8); }
static uint32_t htonl32(uint32_t v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
           ((v & 0xFF0000) >> 8) | ((v & 0xFF000000) >> 24);
}

/* UDP校验和（含伪头部） */
static uint16_t udp_checksum(uint32_t src_ip, uint32_t dst_ip,
                              const uint8_t *udp_data, int udp_len)
{
    uint32_t sum = 0;

    /* 伪头部 */
    sum += (src_ip >> 16) & 0xFFFF;
    sum += src_ip & 0xFFFF;
    sum += (dst_ip >> 16) & 0xFFFF;
    sum += dst_ip & 0xFFFF;
    sum += 17;  /* UDP协议号 */
    sum += udp_len;

    /* UDP数据 */
    for (int i = 0; i < udp_len; i += 2) {
        uint16_t word = udp_data[i] << 8;
        if (i + 1 < udp_len) word |= udp_data[i + 1];
        sum += word;
    }

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    uint16_t result = (uint16_t)(~sum);
    return result ? result : 0xFFFF;  /* 0表示无校验和 */
}

/* 初始化UDP */
void udp64_init(void)
{
    for (int i = 0; i < UDP_MAX_PORTS; i++) {
        udp_sockets[i].used = 0;
    }
    udp_initialized = 1;
    vga_puts("[UDP64] UDP protocol initialized\n");
}

/* 绑定UDP端口 */
int udp64_bind(uint16_t port)
{
    if (!udp_initialized) return -1;

    for (int i = 0; i < UDP_MAX_PORTS; i++) {
        if (udp_sockets[i].used && udp_sockets[i].port == port) return -1;
    }
    for (int i = 0; i < UDP_MAX_PORTS; i++) {
        if (!udp_sockets[i].used) {
            udp_sockets[i].used = 1;
            udp_sockets[i].port = port;
            udp_sockets[i].length = 0;
            return i;
        }
    }
    return -1;
}

/* 发送UDP数据报 */
int udp64_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
               const uint8_t *data, int data_len)
{
    if (!udp_initialized || !data || data_len <= 0) return -1;

    uint8_t packet[1500];
    udp_header_t *udp = (udp_header_t *)packet;
    int udp_len = 8 + data_len;

    if (udp_len > 1500) { vga_puts("[UDP] fail:too-long\n"); return -1; }

    udp->src_port = htons16(src_port);
    udp->dst_port = htons16(dst_port);
    udp->length = htons16(udp_len);
    udp->checksum = 0;

    udp_memcpy(packet + 8, data, data_len);

    /* 计算校验和 */
    udp->checksum = udp_checksum(htonl32(eth64_get_ip()), htonl32(dst_ip), packet, udp_len);

    return ip64_send(dst_ip, 17, packet, udp_len);
}

/* 接收UDP数据 */
int udp64_recv(int sock, uint8_t *buffer, int max_len,
               uint32_t *src_ip, uint16_t *src_port)
{
    if (!udp_initialized || sock < 0 || sock >= UDP_MAX_PORTS) return -1;
    if (!udp_sockets[sock].used) return -1;

    if (udp_sockets[sock].length == 0) return 0;

    int len = udp_sockets[sock].length;
    if (len > max_len) len = max_len;

    udp_memcpy(buffer, udp_sockets[sock].buffer, len);
    if (src_ip) *src_ip = udp_sockets[sock].src_ip;
    if (src_port) *src_port = udp_sockets[sock].src_port;

    udp_sockets[sock].length = 0;
    return len;
}

/* 处理收到的UDP包 */
void udp64_handle(const uint8_t *packet, int length, uint32_t src_ip, uint32_t dst_ip)
{
    if (length < 8) return;

    udp_header_t *udp = (udp_header_t *)packet;
    uint16_t dst_port = htons16(udp->dst_port);
    uint16_t src_port = htons16(udp->src_port);
    int data_len = htons16(udp->length) - 8;

    if (data_len < 0 || data_len > length - 8) return;

    /* 查找匹配的socket */
    for (int i = 0; i < UDP_MAX_PORTS; i++) {
        if (udp_sockets[i].used && udp_sockets[i].port == dst_port) {
            if (data_len > UDP_RX_BUF_SIZE) data_len = UDP_RX_BUF_SIZE;
            udp_memcpy(udp_sockets[i].buffer, packet + 8, data_len);
            udp_sockets[i].length = data_len;
            udp_sockets[i].src_ip = src_ip;
            udp_sockets[i].src_port = src_port;
            return;
        }
    }
}
