/* ============================================================================
 * 赫普OS - UDP协议层
 * 用户数据报协议，支持DHCP/DNS等应用
 * V7.99: 初始实现
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "eth.h"
#include "vga.h"
#include "serial.h"

/* UDP头结构 */
typedef struct {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

/* UDP套接字 */
#define UDP_MAX_SOCKETS 8
typedef struct {
    int used;
    uint16_t local_port;
    uint8_t recv_buffer[1500];
    uint32_t recv_len;
} udp_socket_t;

static udp_socket_t udp_sockets[UDP_MAX_SOCKETS];
static int udp_initialized = 0;

/* 计算UDP校验和（包含伪首部） */
static uint16_t udp_checksum(uint32_t src_ip, uint32_t dest_ip,
                              const uint8_t *data, uint32_t length)
{
    uint32_t sum = 0;

    /* 伪首部 */
    sum += (src_ip >> 16) & 0xFFFF;
    sum += src_ip & 0xFFFF;
    sum += (dest_ip >> 16) & 0xFFFF;
    sum += dest_ip & 0xFFFF;
    sum += 0x0011;  /* 协议号UDP=17 */
    sum += length;

    /* UDP头和数据 */
    const uint16_t *ptr = (const uint16_t *)data;
    while (length > 1) {
        sum += *ptr++;
        length -= 2;
    }
    if (length == 1) {
        sum += *(const uint8_t *)ptr;
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

/* 初始化UDP层 */
void udp_init(void)
{
    memset(udp_sockets, 0, sizeof(udp_sockets));
    udp_initialized = 1;
    serial_puts(0x3F8, "[UDP] initialized\n");
}

/* 创建UDP套接字 */
int udp_socket(uint16_t local_port)
{
    if (!udp_initialized) return -1;

    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (!udp_sockets[i].used) {
            udp_sockets[i].used = 1;
            udp_sockets[i].local_port = local_port;
            udp_sockets[i].recv_len = 0;
            return i;
        }
    }
    return -1;
}

/* 关闭UDP套接字 */
int udp_close(int sockfd)
{
    if (!udp_initialized || sockfd < 0 || sockfd >= UDP_MAX_SOCKETS) return -1;
    udp_sockets[sockfd].used = 0;
    return 0;
}

/* 发送UDP数据报 */
int udp_sendto(int sockfd, uint32_t dest_ip, uint16_t dest_port,
               const uint8_t *data, uint32_t data_len)
{
    if (!udp_initialized || sockfd < 0 || sockfd >= UDP_MAX_SOCKETS) return -1;
    if (!udp_sockets[sockfd].used || !data || data_len == 0) return -1;

    /* 获取本机IP */
    extern uint32_t ip_get_local(void);
    uint32_t src_ip = ip_get_local();
    uint16_t src_port = udp_sockets[sockfd].local_port;

    /* 构造UDP包 */
    uint32_t udp_len = sizeof(udp_header_t) + data_len;
    uint8_t udp_packet[1500];
    if (udp_len > sizeof(udp_packet)) return -1;
    memset(udp_packet, 0, sizeof(udp_packet));

    udp_header_t *udp = (udp_header_t *)udp_packet;
    udp->src_port = src_port;
    udp->dest_port = dest_port;
    udp->length = udp_len;
    udp->checksum = 0;
    memcpy(udp_packet + sizeof(udp_header_t), data, data_len);

    /* 计算校验和（可选，0表示不校验） */
    udp->checksum = 0;  /* 简化：不校验UDP校验和 */

    /* 通过IP层发送（协议号17=UDP） */
    extern int ip_send(uint32_t dest_ip, uint8_t protocol,
                       const uint8_t *data, uint32_t data_len);
    int ret = ip_send(dest_ip, 17, udp_packet, udp_len);
    if (ret > 0) {
        serial_puts(0x3F8, "[UDP] packet sent\n");
    }

    return ret;
}

/* 接收UDP数据报 */
int udp_recvfrom(int sockfd, uint8_t *buf, uint32_t max_len,
                 uint32_t *src_ip, uint16_t *src_port)
{
    if (!udp_initialized || sockfd < 0 || sockfd >= UDP_MAX_SOCKETS) return -1;
    if (!udp_sockets[sockfd].used || !buf) return -1;

    if (udp_sockets[sockfd].recv_len == 0) {
        return 0;  /* 无数据 */
    }

    uint32_t len = udp_sockets[sockfd].recv_len;
    if (len > max_len) len = max_len;
    memcpy(buf, udp_sockets[sockfd].recv_buffer, len);
    udp_sockets[sockfd].recv_len = 0;

    return len;
}

/* 处理接收到的UDP包（由IP层调用） */
int udp_process_packet(uint32_t src_ip, const uint8_t *data, uint32_t length)
{
    if (!udp_initialized || !data || length < sizeof(udp_header_t)) return -1;

    const udp_header_t *udp = (const udp_header_t *)data;
    uint16_t dest_port = udp->dest_port;
    uint16_t src_port = udp->src_port;
    uint32_t data_len = length - sizeof(udp_header_t);
    const uint8_t *payload = data + sizeof(udp_header_t);

    serial_puts(0x3F8, "[UDP] packet received\n");

    /* 查找匹配的套接字 */
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (udp_sockets[i].used && udp_sockets[i].local_port == dest_port) {
            /* 保存到接收缓冲区 */
            if (data_len <= sizeof(udp_sockets[i].recv_buffer)) {
                memcpy(udp_sockets[i].recv_buffer, payload, data_len);
                udp_sockets[i].recv_len = data_len;
            }
            return 0;
        }
    }

    serial_puts(0x3F8, "[UDP] no socket for port\n");
    return -1;
}
