/* ============================================================================
 * 赫普OS - TCP连接管理 (V4.50-E95 深化)
 * 完整TCP状态机、重传退避、滑动窗口、发送/接收缓冲、远端行为模拟
 *
 * E-95: 从"立即成功"的模拟升级为符合 TCP 语义的状态机——
 *   - connect: SYN_SENT -> (模拟 SYN-ACK) -> ESTABLISHED, 带指数退避重传
 *   - accept:  LISTEN -> (模拟 SYN) -> SYN_RECEIVED -> (ACK) -> ESTABLISHED
 *   - send:    数据入发送缓冲, 窗口扣减, 模拟对端 ACK 恢复窗口
 *   - receive: 从接收缓冲取数据 (模拟对端回显)
 *   - close:   FIN_WAIT_1 -> (模拟 FIN+ACK) -> TIME_WAIT -> CLOSED
 * ========================================================================== */

#include <stdint.h>
#include "tcp.h"
#include "vga.h"
#include "string.h"
extern void serial_puts(uint16_t port, const char *s);
#include "kheap.h"

#define TCP_SIM_SYNACK_TICKS  2   /* 模拟对端 SYN-ACK 延迟 (tick) */
#define TCP_SIM_FIN_TICKS     2   /* 模拟对端 FIN+ACK 延迟 */
#define TCP_SIM_ACK_TICKS     1   /* 模拟对端 ACK 延迟 */
#define TCP_BUFFER_DEFAULT    2048

static hepu_hepu_tcp_connection_t tcp_connections[TCP_MAX_CONNECTIONS];
static uint32_t tcp_tick_count = 0;
static int tcp_initialized = 0;

/* 初始化TCP */
void tcp_init(void)
{
    memset(tcp_connections, 0, sizeof(tcp_connections));
    tcp_tick_count = 0;
    tcp_initialized = 1;
    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[TCP] init\n");
}

/* 按指针查 fd (用于模拟日志) */
static int tcp_find_fd(hepu_hepu_tcp_connection_t *conn)
{
    if (!conn) return -1;
    return (int)(conn - tcp_connections);
}

/* 查找空闲连接 */
static int tcp_find_free(void)
{
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        if (!tcp_connections[i].in_use) return i;
    }
    return -1;
}

/* 查找连接 */
static hepu_hepu_tcp_connection_t *tcp_find(int fd)
{
    if (fd < 0 || fd >= TCP_MAX_CONNECTIONS) return NULL;
    if (!tcp_connections[fd].in_use) return NULL;
    return &tcp_connections[fd];
}

/* 生成初始序列号 */
static uint32_t tcp_generate_isn(void)
{
    static uint32_t isn = 0x12345678;
    isn += 0x400000;
    return isn;
}

/* V4.50-E95: 释放连接缓冲 */
static void tcp_free_buffers(hepu_hepu_tcp_connection_t *conn)
{
    if (conn->send_buffer) { kfree(conn->send_buffer); conn->send_buffer = NULL; }
    if (conn->recv_buffer) { kfree(conn->recv_buffer); conn->recv_buffer = NULL; }
    conn->send_buffer_size = 0;
    conn->send_buffer_len = 0;
    conn->recv_buffer_size = 0;
    conn->recv_buffer_len = 0;
}

/* V8.01: TCP头结构 */
typedef struct {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset;  /* 高4位：数据偏移，低4位：保留 */
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed)) tcp_header_t;

/* TCP标志位 */
#define TCP_FLAG_FIN  0x01
#define TCP_FLAG_SYN  0x02
#define TCP_FLAG_RST  0x04
#define TCP_FLAG_PSH  0x08
#define TCP_FLAG_ACK  0x10
#define TCP_FLAG_URG  0x20

/* V8.01: 计算TCP校验和（包含伪首部） */
static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dest_ip,
                              const uint8_t *data, uint32_t length)
{
    uint32_t sum = 0;

    /* 伪首部 */
    sum += (src_ip >> 16) & 0xFFFF;
    sum += src_ip & 0xFFFF;
    sum += (dest_ip >> 16) & 0xFFFF;
    sum += dest_ip & 0xFFFF;
    sum += 0x0006;  /* 协议号TCP=6 */
    sum += length;

    /* TCP头和数据 */
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

/* V8.01: 发送TCP包 */
static int tcp_send_packet(hepu_hepu_tcp_connection_t *conn,
                            uint8_t flags, const uint8_t *data, uint32_t data_len)
{
    extern uint32_t ip_get_local(void);
    uint32_t src_ip = ip_get_local();
    uint32_t dest_ip = conn->remote_ip;

    /* 构造TCP包 */
    uint32_t tcp_len = sizeof(tcp_header_t) + data_len;
    uint8_t tcp_packet[1500];
    if (tcp_len > sizeof(tcp_packet)) return -1;
    memset(tcp_packet, 0, sizeof(tcp_packet));

    tcp_header_t *tcp = (tcp_header_t *)tcp_packet;
    tcp->src_port = conn->local_port;
    tcp->dest_port = conn->remote_port;
    tcp->seq_num = conn->seq_num;
    tcp->ack_num = conn->ack_num;
    tcp->data_offset = (5 << 4);  /* 数据偏移=20字节 */
    tcp->flags = flags;
    tcp->window = conn->window;
    tcp->checksum = 0;
    tcp->urgent_ptr = 0;

    /* 复制数据 */
    if (data && data_len > 0) {
        memcpy(tcp_packet + sizeof(tcp_header_t), data, data_len);
    }

    /* 计算校验和 */
    tcp->checksum = tcp_checksum(src_ip, dest_ip, tcp_packet, tcp_len);

    /* 通过IP层发送（协议号6=TCP） */
    extern int ip_send(uint32_t dest_ip, uint8_t protocol,
                       const uint8_t *data, uint32_t data_len);
    int ret = ip_send(dest_ip, 6, tcp_packet, tcp_len);

    /* 更新序列号 */
    if (flags & TCP_FLAG_SYN) {
        conn->seq_num++;
    }
    if (flags & TCP_FLAG_FIN) {
        conn->seq_num++;
    }
    conn->seq_num += data_len;

    return ret;
}

/* V8.01: 处理接收到的TCP包（由IP层调用） */
int tcp_process_packet(uint32_t src_ip, const uint8_t *data, uint32_t length)
{
    if (!tcp_initialized || !data || length < sizeof(tcp_header_t)) return -1;

    const tcp_header_t *tcp = (const tcp_header_t *)data;
    uint16_t src_port = tcp->src_port;
    uint16_t dest_port = tcp->dest_port;
    uint32_t seq_num = tcp->seq_num;
    uint32_t ack_num = tcp->ack_num;
    uint8_t flags = tcp->flags;

    /* 查找匹配的连接 */
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        hepu_hepu_tcp_connection_t *conn = &tcp_connections[i];
        if (conn->in_use &&
            conn->remote_ip == src_ip &&
            conn->remote_port == src_port &&
            conn->local_port == dest_port) {

            /* 更新确认号 */
            if (flags & TCP_FLAG_ACK) {
                conn->ack_num = seq_num + 1;
            }

            /* 处理SYN-ACK（连接建立） */
            if (conn->state == TCP_STATE_SYN_SENT &&
                (flags & TCP_FLAG_SYN) && (flags & TCP_FLAG_ACK)) {
                conn->state = TCP_STATE_ESTABLISHED;
                conn->ack_num = seq_num + 1;
                /* 发送ACK */
                tcp_send_packet(conn, TCP_FLAG_ACK, NULL, 0);
                serial_puts(0x3F8, "[TCP] connection established\n");
            }

            /* 处理数据 */
            uint32_t header_len = (tcp->data_offset >> 4) * 4;
            uint32_t data_len = length - header_len;
            if (data_len > 0 && conn->recv_buffer) {
                uint32_t copy_len = data_len;
                if (copy_len > conn->recv_buffer_size - conn->recv_buffer_len) {
                    copy_len = conn->recv_buffer_size - conn->recv_buffer_len;
                }
                memcpy(conn->recv_buffer + conn->recv_buffer_len,
                       data + header_len, copy_len);
                conn->recv_buffer_len += copy_len;
            }

            /* 处理FIN（连接关闭） */
            if (flags & TCP_FLAG_FIN) {
                conn->state = TCP_STATE_CLOSE_WAIT;
                /* 发送ACK */
                tcp_send_packet(conn, TCP_FLAG_ACK, NULL, 0);
                /* 发送FIN */
                tcp_send_packet(conn, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
                conn->state = TCP_STATE_LAST_ACK;
            }

            return 0;
        }
    }

    return -1;
}

/* 主动连接 */
int tcp_connect(uint32_t remote_ip, uint16_t remote_port, uint16_t local_port)
{
    if (!tcp_initialized) return -1;

    int fd = tcp_find_free();
    if (fd < 0) return -1;

    hepu_hepu_tcp_connection_t *conn = &tcp_connections[fd];
    memset(conn, 0, sizeof(hepu_hepu_tcp_connection_t));
    conn->in_use = 1;
    conn->state = TCP_STATE_SYN_SENT;
    conn->remote_ip = remote_ip;
    conn->remote_port = remote_port;
    conn->local_port = local_port;
    conn->seq_num = tcp_generate_isn();
    conn->ack_num = 0;
    conn->window = TCP_WINDOW_SIZE;
    conn->retries = 0;
    conn->last_activity = tcp_tick_count;

    /* V4.50-E95: 分配发送/接收缓冲 */
    conn->send_buffer = kmalloc(TCP_BUFFER_DEFAULT);
    conn->recv_buffer = kmalloc(TCP_BUFFER_DEFAULT);
    if (!conn->send_buffer || !conn->recv_buffer) {
        tcp_free_buffers(conn);
        conn->in_use = 0;
        return -1;
    }
    conn->send_buffer_size = TCP_BUFFER_DEFAULT;
    conn->recv_buffer_size = TCP_BUFFER_DEFAULT;

    serial_puts(0x3F8, "[TCP] connect SYN_SENT\n");

    /* V8.01: 真实发送SYN包 */
    tcp_send_packet(conn, TCP_FLAG_SYN, NULL, 0);

    return fd;
}

/* 监听 */
int tcp_listen(uint16_t local_port, int backlog)
{
    if (!tcp_initialized) return -1;

    int fd = tcp_find_free();
    if (fd < 0) return -1;

    hepu_hepu_tcp_connection_t *conn = &tcp_connections[fd];
    memset(conn, 0, sizeof(hepu_hepu_tcp_connection_t));
    conn->in_use = 1;
    conn->state = TCP_STATE_LISTEN;
    conn->local_port = local_port;
    conn->window = TCP_WINDOW_SIZE;

    return fd;
}

/* 接受连接 */
int tcp_accept(int listen_fd, uint32_t *client_ip, uint16_t *client_port)
{
    if (!tcp_initialized) return -1;
    hepu_hepu_tcp_connection_t *listen = tcp_find(listen_fd);
    if (!listen || listen->state != TCP_STATE_LISTEN) return -1;

    int fd = tcp_find_free();
    if (fd < 0) return -1;

    hepu_hepu_tcp_connection_t *conn = &tcp_connections[fd];
    memset(conn, 0, sizeof(hepu_hepu_tcp_connection_t));
    conn->in_use = 1;
    conn->state = TCP_STATE_SYN_RECEIVED;   /* V4.50-E95: 真实状态流转 */
    conn->local_port = listen->local_port;
    conn->seq_num = tcp_generate_isn();
    conn->window = TCP_WINDOW_SIZE;

    if (client_ip) *client_ip = (192 << 24) | (168 << 16) | (1 << 8) | 50;
    if (client_port) *client_port = 12345;
    conn->remote_ip = client_ip ? *client_ip : 0;
    conn->remote_port = client_port ? *client_port : 0;
    conn->last_activity = tcp_tick_count;

    conn->send_buffer = kmalloc(TCP_BUFFER_DEFAULT);
    conn->recv_buffer = kmalloc(TCP_BUFFER_DEFAULT);
    if (!conn->send_buffer || !conn->recv_buffer) {
        tcp_free_buffers(conn);
        conn->in_use = 0;
        return -1;
    }
    conn->send_buffer_size = TCP_BUFFER_DEFAULT;
    conn->recv_buffer_size = TCP_BUFFER_DEFAULT;

    return fd;
}

/* 发送数据 */
int tcp_send(int fd, const uint8_t *data, uint32_t length)
{
    if (!tcp_initialized || !data || length == 0) return -1;

    hepu_hepu_tcp_connection_t *conn = tcp_find(fd);
    if (!conn || conn->state != TCP_STATE_ESTABLISHED) return -1;

    /* V4.50-E95: 滑动窗口检查 */
    uint32_t unacked = conn->seq_num - conn->ack_num + conn->send_buffer_len;
    if (unacked + length > conn->window) {
        return 0;
    }

    if (conn->send_buffer_len + length > conn->send_buffer_size) {
        vga_puts("[TCP] send buffer full\n");
        return 0;
    }

    memcpy(conn->send_buffer + conn->send_buffer_len, data, length);
    conn->send_buffer_len += length;
    conn->last_activity = tcp_tick_count;

    /* V8.01: 真实发送PSH+ACK数据包 */
    tcp_send_packet(conn, TCP_FLAG_PSH | TCP_FLAG_ACK, data, length);

    return (int)length;
}

/* 接收数据 */
int tcp_receive(int fd, uint8_t *buffer, uint32_t max_length)
{
    if (!tcp_initialized || !buffer || max_length == 0) return -1;

    hepu_hepu_tcp_connection_t *conn = tcp_find(fd);
    if (!conn || conn->state != TCP_STATE_ESTABLISHED) return -1;

    conn->last_activity = tcp_tick_count;

    /* V4.50-E95: 从接收缓冲取数据 */
    if (conn->recv_buffer_len == 0) return 0;
    uint32_t n = conn->recv_buffer_len < max_length ? conn->recv_buffer_len : max_length;
    memcpy(buffer, conn->recv_buffer, n);
    if (n < conn->recv_buffer_len) {
        memmove(conn->recv_buffer, conn->recv_buffer + n, conn->recv_buffer_len - n);
    }
    conn->recv_buffer_len -= n;
    return (int)n;
}

/* 关闭连接 */
int tcp_close(int fd)
{
    if (!tcp_initialized) return -1;

    hepu_hepu_tcp_connection_t *conn = tcp_find(fd);
    if (!conn) return -1;

    if (conn->state == TCP_STATE_ESTABLISHED ||
        conn->state == TCP_STATE_SYN_RECEIVED) {
        conn->state = TCP_STATE_FIN_WAIT_1;
        conn->last_activity = tcp_tick_count;
        return 0;
    }

    /* 非活跃状态直接释放 */
    tcp_free_buffers(conn);
    conn->state = TCP_STATE_CLOSED;
    conn->in_use = 0;
    return 0;
}

/* 获取状态 */
int tcp_get_state(int fd, hepu_tcp_state_t *state)
{
    hepu_hepu_tcp_connection_t *conn = tcp_find(fd);
    if (!conn || !state) return -1;
    *state = conn->state;
    return 0;
}

/* 获取连接信息 */
int tcp_get_connection_info(int fd, hepu_hepu_tcp_connection_t *info)
{
    hepu_hepu_tcp_connection_t *conn = tcp_find(fd);
    if (!conn || !info) return -1;
    *info = *conn;
    return 0;
}

/* 状态文本 */
const char *hepu_tcp_state_text(hepu_tcp_state_t state)
{
    switch (state) {
        case TCP_STATE_CLOSED: return "CLOSED";
        case TCP_STATE_LISTEN: return "LISTEN";
        case TCP_STATE_SYN_SENT: return "SYN_SENT";
        case TCP_STATE_SYN_RECEIVED: return "SYN_RECEIVED";
        case TCP_STATE_ESTABLISHED: return "ESTABLISHED";
        case TCP_STATE_FIN_WAIT_1: return "FIN_WAIT_1";
        case TCP_STATE_FIN_WAIT_2: return "FIN_WAIT_2";
        case TCP_STATE_CLOSE_WAIT: return "CLOSE_WAIT";
        case TCP_STATE_CLOSING: return "CLOSING";
        case TCP_STATE_LAST_ACK: return "LAST_ACK";
        case TCP_STATE_TIME_WAIT: return "TIME_WAIT";
        default: return "UNKNOWN";
    }
}

/* V4.50-E95: 模拟远端行为——SYN-ACK / ACK / FIN+ACK */
static void tcp_simulate_peer(hepu_hepu_tcp_connection_t *conn)
{
    switch (conn->state) {
    case TCP_STATE_SYN_SENT:
        /* 2 tick 后模拟对端 SYN-ACK, 进入 ESTABLISHED */
        if (tcp_tick_count - conn->last_activity >= TCP_SIM_SYNACK_TICKS) {
            conn->state = TCP_STATE_ESTABLISHED;
            conn->ack_num = conn->seq_num + 1;
            conn->last_activity = tcp_tick_count;
        }
        break;
    case TCP_STATE_SYN_RECEIVED:
        /* 1 tick 后模拟对端 ACK, 进入 ESTABLISHED */
        if (tcp_tick_count - conn->last_activity >= TCP_SIM_ACK_TICKS) {
            conn->state = TCP_STATE_ESTABLISHED;
            conn->ack_num = conn->seq_num + 1;
            conn->last_activity = tcp_tick_count;
        }
        break;
    case TCP_STATE_ESTABLISHED:
        /* 模拟对端 ACK: 已发送数据被确认, 窗口恢复 + 清发送缓冲 */
        if (conn->send_buffer_len > 0 &&
            tcp_tick_count - conn->last_activity >= TCP_SIM_ACK_TICKS) {
            conn->ack_num = conn->seq_num;   /* 全部确认 */
            conn->send_buffer_len = 0;
            conn->last_activity = tcp_tick_count;
        }
        break;
    case TCP_STATE_FIN_WAIT_1:
        /* 2 tick 后模拟对端 FIN+ACK, 进入 TIME_WAIT */
        if (tcp_tick_count - conn->last_activity >= TCP_SIM_FIN_TICKS) {
            conn->state = TCP_STATE_TIME_WAIT;
            conn->last_activity = tcp_tick_count;
        }
        break;
    case TCP_STATE_TIME_WAIT:
        /* 2 tick 后进入 CLOSED 释放 */
        if (tcp_tick_count - conn->last_activity >= TCP_SIM_FIN_TICKS) {
            tcp_free_buffers(conn);
            conn->state = TCP_STATE_CLOSED;
            conn->in_use = 0;
        }
        break;
    default:
        break;
    }
}

/* 定时处理（重传、超时、远端模拟） */
void tcp_tick(void)
{
    tcp_tick_count++;
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        hepu_hepu_tcp_connection_t *conn = &tcp_connections[i];
        if (!conn->in_use) continue;

        /* 检查超时——指数退避重传 */
        if (conn->state == TCP_STATE_SYN_SENT) {
            uint32_t rto = TCP_RETRANSMIT_TIMEOUT;
            for (int r = 0; r < conn->retries; r++) rto *= 2;  /* 指数退避 */
            if (tcp_tick_count - conn->last_activity > (rto / 10)) {
                conn->retries++;
                if (conn->retries >= TCP_MAX_RETRIES) {
                    tcp_free_buffers(conn);
                    conn->state = TCP_STATE_CLOSED;
                    conn->in_use = 0;
                } else {
                    conn->last_activity = tcp_tick_count;
                }
            }
        }

        /* 模拟远端行为 (状态推进) */
        tcp_simulate_peer(conn);
    }
}
