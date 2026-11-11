/* ============================================================================
 * 赫普OS 64位 - TCP协议（传输控制协议）
 * 简化版：支持三次握手、数据传输、四次挥手
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

#define TCP_MAX_CONNECTIONS 8
#define TCP_RX_BUF_SIZE 4096
#define TCP_TX_BUF_SIZE 4096
#define TCP_WINDOW_SIZE 4096
#define TCP_RTO_MS 1000       /* 初始重传超时 1s */
#define TCP_MAX_RETRIES 5     /* 最大重传次数 */
#define TCP_CWND_INIT 1       /* 初始拥塞窗口 1 MSS */
#define TCP_SSTHRESH_INIT 65535  /* 初始慢启动阈值 */

/* TCP状态 */
#define TCP_CLOSED      0
#define TCP_LISTEN      1
#define TCP_SYN_SENT    2
#define TCP_SYN_RCVD    3
#define TCP_ESTABLISHED 4
#define TCP_FIN_WAIT1   5
#define TCP_FIN_WAIT2   6
#define TCP_CLOSE_WAIT  7
#define TCP_LAST_ACK    8
#define TCP_TIME_WAIT   9

/* TCP标志 */
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10
#define TCP_URG 0x20

/* TCP头 */
typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_offset;  /* 高4位=数据偏移, 低4位=保留 */
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed)) tcp_header_t;

/* TCP连接 */
typedef struct {
    int used;
    int state;
    uint32_t remote_ip;
    uint16_t remote_port;
    uint16_t local_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t rx_buf[TCP_RX_BUF_SIZE];
    int rx_len;
    int rx_read;
    /* 重传 & 拥塞控制 */
    uint32_t unacked_seq;      /* 未确认的最早序列号 */
    int retries;               /* 当前重传次数 */
    uint64_t last_send_ms;     /* 最后发送时间(ms) */
    int cwnd;                  /* 拥塞窗口(MSS数) */
    int ssthresh;              /* 慢启动阈值 */
    int rtt_est;               /* RTT估计(ms) */
} tcp_connection_t;

static tcp_connection_t tcp_conns[TCP_MAX_CONNECTIONS];
static int tcp_initialized = 0;
static uint32_t tcp_isn = 0x12345678;  /* 初始序列号 */

extern void vga_puts(const char *s);
extern int ip64_send(uint32_t dst_ip, uint8_t protocol, const uint8_t *data, int data_len);
extern uint32_t eth64_get_ip(void);

static void tcp_memcpy(void *dst, const void *src, int n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}

static uint16_t htons16(uint16_t v) { return (v >> 8) | (v << 8); }
static uint32_t htonl32(uint32_t v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
           ((v & 0xFF0000) >> 8) | ((v & 0xFF000000) >> 24);
}
static uint32_t ntohl32(uint32_t v) { return htonl32(v); }

/* TCP校验和（含伪头部） */
static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                              const uint8_t *tcp_data, int tcp_len)
{
    uint32_t sum = 0;
    sum += (src_ip >> 16) & 0xFFFF;
    sum += src_ip & 0xFFFF;
    sum += (dst_ip >> 16) & 0xFFFF;
    sum += dst_ip & 0xFFFF;
    sum += 6;  /* TCP协议号 */
    sum += tcp_len;
    for (int i = 0; i < tcp_len; i += 2) {
        uint16_t word = tcp_data[i] << 8;
        if (i + 1 < tcp_len) word |= tcp_data[i + 1];
        sum += word;
    }
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

/* 发送TCP包 */
static int tcp_send_packet(tcp_connection_t *conn, uint8_t flags,
                           const uint8_t *data, int data_len)
{
    uint8_t packet[1500];
    tcp_header_t *tcp = (tcp_header_t *)packet;
    int tcp_len = 20 + data_len;

    if (tcp_len > 1500) return -1;

    tcp->src_port = htons16(conn->local_port);
    tcp->dst_port = htons16(conn->remote_port);
    tcp->seq = htonl32(conn->seq_num);
    tcp->ack = htonl32(conn->ack_num);
    tcp->data_offset = 0x50;  /* 20字节头 */
    tcp->flags = flags;
    tcp->window = htons16(TCP_WINDOW_SIZE);
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;

    if (data && data_len > 0) tcp_memcpy(packet + 20, data, data_len);

    /* V0.57.0: 校验和字段必须按网络字节序写入, 否则 TCP 握手永远得不到回应 */
    /* V0.57.0: 伪头部 IP 用主机序 (同 UDP, 传字节交换过的值会算出错校验和) */
    tcp->checksum = htons16(tcp_checksum(eth64_get_ip(), conn->remote_ip, packet, tcp_len));

    int sret = ip64_send(conn->remote_ip, 6, packet, tcp_len);

    /* 记录发送时间用于RTT估计 */
    if (flags & TCP_PSH) {
        extern uint64_t pit64_get_ms(void);
        conn->last_send_ms = pit64_get_ms();
        conn->unacked_seq = conn->seq_num;
    }

    /* V0.13.65: SYN 被 ARP 未解析阻塞(-2)时, 泵网络收 ARP 应答后重发至多 40 次 */
    if (sret == -2 && (flags & TCP_SYN)) {
        extern void net64_poll(void);
        for (int r = 0; r < 40 && sret == -2; r++) {
            for (int pp = 0; pp < 50; pp++) net64_poll();
            sret = ip64_send(conn->remote_ip, 6, packet, tcp_len);
        }
    }

    /* V0.13.9: 发送成功后才推进序列号 (原先加后发, ARP 未解析时序列错位)。
       V0.54.12(R4 NET-01): ip64_send 成功返回正字节数（eth64_send 透传），
       旧判据 sret==0 恒假，导致序列号永不推进、后续数据全乱序；改为 >0。 */
    if (sret > 0) {
        if (flags & (TCP_SYN | TCP_FIN)) conn->seq_num++;
        conn->seq_num += data_len;
    }
    return sret;
}

/* 初始化TCP */
void tcp64_init(void)
{
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        tcp_conns[i].used = 0;
        tcp_conns[i].state = TCP_CLOSED;
    }
    tcp_initialized = 1;
    vga_puts("[TCP64] TCP protocol initialized\n");
}

/* 主动连接 */
int tcp64_connect(uint32_t remote_ip, uint16_t remote_port, uint16_t local_port)
{
    if (!tcp_initialized) return -1;

    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        if (!tcp_conns[i].used) {
            tcp_conns[i].used = 1;
            tcp_conns[i].state = TCP_SYN_SENT;
            tcp_conns[i].remote_ip = remote_ip;
            tcp_conns[i].remote_port = remote_port;
            tcp_conns[i].local_port = local_port;
            tcp_conns[i].seq_num = tcp_isn++;
            tcp_conns[i].ack_num = 0;
            tcp_conns[i].rx_len = 0;
            tcp_conns[i].rx_read = 0;

            tcp_send_packet(&tcp_conns[i], TCP_SYN, NULL, 0);
            return i;
        }
    }
    return -1;
}

/* 监听端口 */
int tcp64_listen(uint16_t port)
{
    if (!tcp_initialized) return -1;
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        if (!tcp_conns[i].used) {
            tcp_conns[i].used = 1;
            tcp_conns[i].state = TCP_LISTEN;
            tcp_conns[i].local_port = port;
            tcp_conns[i].remote_ip = 0;
            tcp_conns[i].remote_port = 0;
            return i;
        }
    }
    return -1;
}

/* 发送数据 */
int tcp64_send(int sock, const uint8_t *data, int length)
{
    if (!tcp_initialized || sock < 0 || sock >= TCP_MAX_CONNECTIONS) return -1;
    if (!tcp_conns[sock].used || tcp_conns[sock].state != TCP_ESTABLISHED) return -1;

    return tcp_send_packet(&tcp_conns[sock], TCP_PSH | TCP_ACK, data, length);
}

/* 接收数据 */
int tcp64_recv(int sock, uint8_t *buffer, int max_len)
{
    if (!tcp_initialized || sock < 0 || sock >= TCP_MAX_CONNECTIONS) return -1;
    if (!tcp_conns[sock].used) return -1;

    tcp_connection_t *conn = &tcp_conns[sock];
    if (conn->rx_read >= conn->rx_len) return 0;

    int avail = conn->rx_len - conn->rx_read;
    if (avail > max_len) avail = max_len;
    tcp_memcpy(buffer, conn->rx_buf + conn->rx_read, avail);
    conn->rx_read += avail;
    /* V0.54.12(R4 NET-05): 旧实现只推进 rx_read，rx_len 永不回落，缓冲
       填满一次后新数据在接收分支静默丢弃。取尽则复位，部分取走则把剩余
       未读数据压缩到缓冲头部，腾出接收空间。 */
    if (conn->rx_read >= conn->rx_len) {
        conn->rx_len = 0;
        conn->rx_read = 0;
    } else {
        int remain = conn->rx_len - conn->rx_read;
        tcp_memcpy(conn->rx_buf, conn->rx_buf + conn->rx_read, remain);
        conn->rx_len = remain;
        conn->rx_read = 0;
    }
    return avail;
}

/* 关闭连接 */
int tcp64_close(int sock)
{
    if (!tcp_initialized || sock < 0 || sock >= TCP_MAX_CONNECTIONS) return -1;
    if (!tcp_conns[sock].used) return -1;

    tcp_connection_t *conn = &tcp_conns[sock];
    if (conn->state == TCP_ESTABLISHED) {
        conn->state = TCP_FIN_WAIT1;
        tcp_send_packet(conn, TCP_FIN | TCP_ACK, NULL, 0);
    } else {
        conn->used = 0;
        conn->state = TCP_CLOSED;
    }
    return 0;
}

/* V0.54.12(R4 NET-02): 处理对我方已发数据的确认（RTT/拥塞窗口/重传记账）。
 * 旧代码把这段逻辑嵌在 data_len>0 分支里，纯 ACK（无数据）在 ESTABLISHED
 * 下不匹配任何分支，导致 unacked_seq 不清除、RTT 不更新、拥塞窗口不增长。 */
static void tcp_process_ack(tcp_connection_t *conn, uint32_t ack)
{
    if (conn->unacked_seq && ack >= conn->unacked_seq) {
        extern uint64_t pit64_get_ms(void);
        uint64_t now = pit64_get_ms();
        int rtt = (int)(now - conn->last_send_ms);
        if (conn->rtt_est == 0) conn->rtt_est = rtt;
        else conn->rtt_est = (conn->rtt_est * 7 + rtt) / 8;
        conn->unacked_seq = 0;
        conn->retries = 0;
        if (conn->cwnd < conn->ssthresh) conn->cwnd++;
    }
}

/* 处理收到的TCP包 */
void tcp64_handle(const uint8_t *packet, int length, uint32_t src_ip, uint32_t dst_ip)
{
    if (length < 20) return;

    tcp_header_t *tcp = (tcp_header_t *)packet;
    uint16_t src_port = htons16(tcp->src_port);
    uint16_t dst_port = htons16(tcp->dst_port);
    uint32_t seq = ntohl32(tcp->seq);
    uint32_t ack = ntohl32(tcp->ack);
    int data_offset = (tcp->data_offset >> 4) * 4;
    /* 安全：数据偏移必须是合法 TCP 头长度（最小 20，最大 60）且不超过
     * 报文长度。旧实现不校验，畸形包会得到负 data_len 或把头部当数据，
     * 并据此错误确认序列号。非法报文直接丢弃。 */
    if (data_offset < 20 || data_offset > length) return;
    int data_len = length - data_offset;
    const uint8_t *data = packet + data_offset;

    /* 查找匹配的连接 */
    tcp_connection_t *conn = NULL;
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        if (tcp_conns[i].used &&
            tcp_conns[i].local_port == dst_port &&
            (tcp_conns[i].state == TCP_LISTEN ||
             (tcp_conns[i].remote_ip == src_ip &&
              tcp_conns[i].remote_port == src_port))) {
            conn = &tcp_conns[i];
            break;
        }
    }

    if (!conn) return;

    switch (conn->state) {
        case TCP_LISTEN:
            if (tcp->flags & TCP_SYN) {
                /* 收到SYN，发送SYN+ACK */
                conn->state = TCP_SYN_RCVD;
                conn->remote_ip = src_ip;
                conn->remote_port = src_port;
                conn->seq_num = tcp_isn++;
                conn->ack_num = seq + 1;
                tcp_send_packet(conn, TCP_SYN | TCP_ACK, NULL, 0);
            }
            break;

        case TCP_SYN_SENT:
            if ((tcp->flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
                /* 收到SYN+ACK，发送ACK */
                conn->state = TCP_ESTABLISHED;
                conn->ack_num = seq + 1;
                conn->seq_num = ack;
                tcp_send_packet(conn, TCP_ACK, NULL, 0);
                vga_puts("[TCP64] Connection established\n");
            }
            break;

        case TCP_SYN_RCVD:
            if (tcp->flags & TCP_ACK) {
                conn->state = TCP_ESTABLISHED;
                vga_puts("[TCP64] Connection accepted\n");
            }
            break;

        case TCP_ESTABLISHED:
            if (tcp->flags & TCP_FIN) {
                /* 收到FIN，发送ACK */
                conn->state = TCP_CLOSE_WAIT;
                conn->ack_num = seq + 1;
                tcp_send_packet(conn, TCP_ACK, NULL, 0);
                /* 发送FIN */
                conn->state = TCP_LAST_ACK;
                tcp_send_packet(conn, TCP_FIN | TCP_ACK, NULL, 0);
            } else if (data_len > 0) {
                /* 收到数据 */
                if (conn->rx_len + data_len <= TCP_RX_BUF_SIZE) {
                    tcp_memcpy(conn->rx_buf + conn->rx_len, data, data_len);
                    conn->rx_len += data_len;
                }
                conn->ack_num = seq + data_len;
                tcp_send_packet(conn, TCP_ACK, NULL, 0);
                tcp_process_ack(conn, ack);
            } else if (tcp->flags & TCP_ACK) {
                /* V0.54.12(R4 NET-02): 纯 ACK 分支——只确认我方数据，
                   不存数据、不回 ACK；旧实现此处静默丢弃。 */
                tcp_process_ack(conn, ack);
            }
            break;

        case TCP_FIN_WAIT1:
            if (tcp->flags & TCP_ACK) {
                conn->state = TCP_FIN_WAIT2;
            }
            break;

        case TCP_FIN_WAIT2:
            if (tcp->flags & TCP_FIN) {
                conn->state = TCP_TIME_WAIT;
                conn->ack_num = seq + 1;
                tcp_send_packet(conn, TCP_ACK, NULL, 0);
                conn->used = 0;
                conn->state = TCP_CLOSED;
            }
            break;

        case TCP_LAST_ACK:
            if (tcp->flags & TCP_ACK) {
                conn->used = 0;
                conn->state = TCP_CLOSED;
            }
            break;
    }
}

/* 获取连接状态 */
int tcp64_get_state(int sock)
{
    if (!tcp_initialized || sock < 0 || sock >= TCP_MAX_CONNECTIONS) return -1;
    if (!tcp_conns[sock].used) return TCP_CLOSED;
    return tcp_conns[sock].state;
}

/* TCP定时器：每ms调用一次，检查重传 */
void net64_tcp_timer(void) {
    if (!tcp_initialized) return;
    extern uint64_t pit64_get_ms(void);
    uint64_t now = pit64_get_ms();
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        if (!tcp_conns[i].used) continue;
        if (tcp_conns[i].state != TCP_ESTABLISHED) continue;
        /* 检查是否超时未确认 */
        if (tcp_conns[i].unacked_seq && (now - tcp_conns[i].last_send_ms) > TCP_RTO_MS) {
            tcp_conns[i].retries++;
            if (tcp_conns[i].retries > TCP_MAX_RETRIES) {
                /* 重传次数过多，关闭连接 */
                tcp_conns[i].state = TCP_CLOSED;
                tcp_conns[i].used = 0;
                continue;
            }
            /* 超时 → 拥塞窗口减半，进入拥塞避免 */
            tcp_conns[i].ssthresh = tcp_conns[i].cwnd / 2;
            if (tcp_conns[i].ssthresh < 2) tcp_conns[i].ssthresh = 2;
            tcp_conns[i].cwnd = 1;
        }
    }
}
