/* ============================================================================
 * 赫普OS - ICMP协议与Ping
 * 互联网控制消息协议，网络诊断工具
 * ========================================================================== */

#include <stdint.h>
#include "icmp.h"
#include "dns.h"
#include "vga.h"
#include "string.h"
extern void serial_puts(uint16_t port, const char *s);

static int icmp_initialized = 0;
static uint16_t icmp_next_id = 0x1234;

/* V7.98: 最后收到的Echo Reply状态 */
static volatile int icmp_reply_received = 0;
static volatile uint16_t icmp_reply_id = 0;
static volatile uint16_t icmp_reply_seq = 0;
static volatile uint32_t icmp_reply_time = 0;

/* 初始化ICMP */
void icmp_init(void)
{
    icmp_initialized = 1;
    icmp_next_id = 0x1234;
    vga_puts("[ICMP] ICMP proto init \n");
    vga_puts("[ICMP] supports Echo Request/Reply, Destination Unreachable\n");
}

/* 计算校验和 */
uint16_t icmp_checksum(const uint8_t *data, uint32_t length)
{
    uint32_t sum = 0;
    const uint16_t *ptr = (const uint16_t *)data;

    while (length > 1) {
        sum += *ptr++;
        length -= 2;
    }
    if (length == 1) {
        sum += *(const uint8_t *)ptr;
    }

    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return (uint16_t)(~sum);
}

/* 发送Echo Request */
int icmp_send_echo_request(uint32_t dest_ip, uint16_t id, uint16_t seq,
                           const uint8_t *data, uint32_t data_len)
{
    if (!icmp_initialized) return -1;

    uint8_t packet[64];
    memset(packet, 0, sizeof(packet));

    hepu_icmp_header_t *icmp = (hepu_icmp_header_t *)packet;
    icmp->type = ICMP_TYPE_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->data.echo.id = id;
    icmp->data.echo.seq = seq;

    /* 复制数据 */
    if (data && data_len > 0 && data_len < sizeof(packet) - sizeof(hepu_icmp_header_t)) {
        memcpy(packet + sizeof(hepu_icmp_header_t), data, data_len);
    }

    /* 计算校验和 */
    uint32_t total_len = sizeof(hepu_icmp_header_t) + data_len;
    icmp->checksum = icmp_checksum(packet, total_len);

    vga_printf("[ICMP] send Echo Request to %d.%d.%d.%d(id=%d, seq=%d)\n",
               (dest_ip >> 24) & 0xFF, (dest_ip >> 16) & 0xFF,
               (dest_ip >> 8) & 0xFF, dest_ip & 0xFF, id, seq);

    return total_len;
}

/* 处理ICMP包 */
int icmp_process_packet(const uint8_t *data, uint32_t length)
{
    if (!icmp_initialized || !data || length < sizeof(hepu_icmp_header_t)) return -1;

    const hepu_icmp_header_t *icmp = (const hepu_icmp_header_t *)data;

    switch (icmp->type) {
        case ICMP_TYPE_ECHO_REPLY:
            /* V7.98: 保存Echo Reply状态供ping使用 */
            icmp_reply_received = 1;
            icmp_reply_id = icmp->data.echo.id;
            icmp_reply_seq = icmp->data.echo.seq;
            vga_printf("[ICMP] recv Echo Reply(id=%d, seq=%d)\n",
                       icmp->data.echo.id, icmp->data.echo.seq);
            break;
        case ICMP_TYPE_DEST_UNREACH:
            vga_printf("[ICMP] unreachable(code=%d)\n", icmp->code);
            break;
        case ICMP_TYPE_TIME_EXCEEDED:
            vga_puts("[ICMP] timeout \n");
            break;
        default:
            vga_printf("[ICMP] recv type =%d, code=%d\n", icmp->type, icmp->code);
            break;
    }
    return 0;
}

/* 单次Ping */
int ping_once(uint32_t dest_ip, hepu_ping_result_t *result)
{
    if (!icmp_initialized || !result) return -1;

    uint16_t id = icmp_next_id++;
    uint16_t seq = 1;
    uint8_t data[32];
    memset(data, 0xAB, sizeof(data));

    /* 构造ICMP包 */
    uint8_t icmp_packet[64];
    memset(icmp_packet, 0, sizeof(icmp_packet));
    hepu_icmp_header_t *icmp = (hepu_icmp_header_t *)icmp_packet;
    icmp->type = ICMP_TYPE_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->data.echo.id = id;
    icmp->data.echo.seq = seq;
    memcpy(icmp_packet + sizeof(hepu_icmp_header_t), data, sizeof(data));
    uint32_t icmp_len = sizeof(hepu_icmp_header_t) + sizeof(data);
    icmp->checksum = icmp_checksum(icmp_packet, icmp_len);

    /* V7.98: 使用IP层发送 */
    extern int ip_send(uint32_t dest_ip, uint8_t protocol, const uint8_t *data, uint32_t data_len);
    int ret = ip_send(dest_ip, 1, icmp_packet, icmp_len);
    if (ret < 0) {
        result->success = 0;
        return -1;
    }

    /* 等待回复（最多2秒） */
    icmp_reply_received = 0;
    uint32_t start_time = 0;
    extern uint32_t pit_get_ticks(void);
    start_time = pit_get_ticks();

    while (!icmp_reply_received) {
        /* 网络轮询接收数据包 */
        extern void net_poll(void);
        net_poll();

        uint32_t elapsed = pit_get_ticks() - start_time;
        if (elapsed > 2000) {  /* 2秒超时 */
            break;
        }
    }

    result->ip_addr = dest_ip;
    result->seq = seq;
    result->ttl = 64;

    if (icmp_reply_received && icmp_reply_id == id) {
        result->rtt_ms = pit_get_ticks() - start_time;
        result->success = 1;
        vga_printf("64 bytes from %d.%d.%d.%d: icmp_seq=%d ttl=%d time=%dms\n",
                   (dest_ip >> 24) & 0xFF, (dest_ip >> 16) & 0xFF,
                   (dest_ip >> 8) & 0xFF, dest_ip & 0xFF,
                   seq, result->ttl, result->rtt_ms);
    } else {
        result->rtt_ms = 0;
        result->success = 0;
        vga_printf("Request timeout for icmp_seq %d\n", seq);
    }

    return 0;
}

/* Ping命令 */
int ping(const char *hostname, int count, int interval_ms, hepu_ping_stats_t *stats)
{
    (void)interval_ms;
    if (!icmp_initialized || !hostname || !stats) return -1;

    /* 解析域名 */
    uint32_t ip_addr;
    if (hepu_dns_resolve(hostname, &ip_addr) != 0) {
        vga_printf("ping: cannot resolve %s\n", hostname);
        return -1;
    }

    vga_printf("PING %s (%d.%d.%d.%d): 32 data bytes\n",
               hostname,
               (ip_addr >> 24) & 0xFF, (ip_addr >> 16) & 0xFF,
               (ip_addr >> 8) & 0xFF, ip_addr & 0xFF);

    memset(stats, 0, sizeof(hepu_ping_stats_t));
    stats->min_rtt_ms = 0xFFFFFFFF;

    for (int i = 0; i < count; i++) {
        hepu_ping_result_t result;
        if (ping_once(ip_addr, &result) == 0 && result.success) {
            stats->received++;
            stats->total_rtt_ms += result.rtt_ms;
            if (result.rtt_ms < stats->min_rtt_ms) stats->min_rtt_ms = result.rtt_ms;
            if (result.rtt_ms > stats->max_rtt_ms) stats->max_rtt_ms = result.rtt_ms;
        } else {
            stats->errors++;
        }
        stats->transmitted++;
    }

    /* 计算平均RTT */
    if (stats->received > 0) {
        stats->avg_rtt_ms = stats->total_rtt_ms / stats->received;
    }

    /* 输出统计 */
    uint32_t loss = 0;
    if (stats->transmitted > 0) {
        loss = ((stats->transmitted - stats->received) * 100) / stats->transmitted;
    }
    vga_printf("\n--- %s ping stats ---\n", hostname);
    vga_printf("%d packets transmitted, %d received, %d%% packet loss\n",
               stats->transmitted, stats->received, loss);
    if (stats->received > 0) {
        vga_printf("rtt min/avg/max = %d/%d/%d ms\n",
                   stats->min_rtt_ms, stats->avg_rtt_ms, stats->max_rtt_ms);
    }

    return 0;
}
