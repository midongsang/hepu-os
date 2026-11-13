/* ============================================================================
 * 赫普OS 64位 - UDP协议（用户数据报协议）
 * ========================================================================== */

#include <stdint.h>
#include "spinlock.h"
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

/* SB-13: UDP 端口表锁（表级，irqsave）。
 * 旧 bind 是「先查端口占用、再查空槽」两趟循环，两核同时 bind 同一个端口
 * 都能过第一趟，随后各占一个槽 —— 端口重复绑定；收包路径还会往刚被
 * close 的槽写数据。判断与置位必须在同一临界区。 */
static spinlock_t g_udp_lock;

extern void vga_puts(const char *s);
extern int ip64_send(uint32_t dst_ip, uint8_t protocol, const uint8_t *data, int data_len);
extern uint32_t eth64_get_ip(void);

static void udp_memcpy(void *dst, const void *src, int n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}

static uint16_t htons16(uint16_t v) { return (v >> 8) | (v << 8); }

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
    spinlock_init(&g_udp_lock);
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

    uint64_t f = spinlock_lock_irqsave(&g_udp_lock);
    int out = -1;
    /* 端口判重与占槽在同一个临界区：分两趟查的旧写法下，两核 bind 同一
     * 端口都能过判重，随后各占一个槽，端口就重复绑定了。 */
    int dup = 0;
    for (int i = 0; i < UDP_MAX_PORTS; i++) {
        if (udp_sockets[i].used && udp_sockets[i].port == port) { dup = 1; break; }
    }
    if (!dup) {
        for (int i = 0; i < UDP_MAX_PORTS; i++) {
            if (!udp_sockets[i].used) {
                udp_sockets[i].used = 1;
                udp_sockets[i].port = port;
                udp_sockets[i].length = 0;
                out = i;
                break;
            }
        }
    }
    spinlock_unlock_irqrestore(&g_udp_lock, f);
    return out;
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
    /* V0.57.0: 同上, 校验和字段必须按网络字节序写入, 否则对端直接丢包 */
    /* V0.57.0: 伪头部的 IP 必须以"主机序"传入 —— udp_checksum 内部按
     * (ip>>16)&0xFFFF / ip&0xFFFF 拆成两个 16 位字, 这里先 htonl32 过一遍
     * 等于把两个字的字节再交换一次, 算出来的校验和是错的(实测相差 0x5FA 量级),
     * 对端校验失败后整个数据报被静默丢弃。 */
    udp->checksum = htons16(udp_checksum(eth64_get_ip(), dst_ip, packet, udp_len));

    return ip64_send(dst_ip, 17, packet, udp_len);
}

/* 接收UDP数据 */
static int udp64_recv_inner(int sock, uint8_t *buffer, int max_len,
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
static void udp64_handle_inner(const uint8_t *packet, int length, uint32_t src_ip, uint32_t dst_ip)
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

/* SB-13: 对外入口持表锁。recv 会清 length，与收包路径写缓冲互斥，
 * 否则读方可能读到被新包覆盖一半的缓冲（撕裂）。 */
int udp64_recv(int sock, uint8_t *buffer, int max_len,
               uint32_t *src_ip, uint16_t *src_port)
{
    uint64_t f = spinlock_lock_irqsave(&g_udp_lock);
    int r = udp64_recv_inner(sock, buffer, max_len, src_ip, src_port);
    spinlock_unlock_irqrestore(&g_udp_lock, f);
    return r;
}

void udp64_handle(const uint8_t *packet, int length, uint32_t src_ip, uint32_t dst_ip)
{
    spinlock_lock(&g_udp_lock);        /* 中断上下文：plain lock */
    udp64_handle_inner(packet, length, src_ip, dst_ip);
    spinlock_unlock(&g_udp_lock);
}
