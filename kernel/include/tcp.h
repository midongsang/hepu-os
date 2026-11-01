#ifndef TCP_H
#define TCP_H

#include <stdint.h>

#define TCP_HEADER_LEN 20
#define TCP_MAX_CONNECTIONS 128  /* V6.77: 从32扩展到128 */
#define TCP_WINDOW_SIZE 65535
#define TCP_MAX_RETRIES 5
#define TCP_RETRANSMIT_TIMEOUT 3000  /* 毫秒 */

/* TCP状态 */
typedef enum {
    TCP_STATE_CLOSED = 0,
    TCP_STATE_LISTEN = 1,
    TCP_STATE_SYN_SENT = 2,
    TCP_STATE_SYN_RECEIVED = 3,
    TCP_STATE_ESTABLISHED = 4,
    TCP_STATE_FIN_WAIT_1 = 5,
    TCP_STATE_FIN_WAIT_2 = 6,
    TCP_STATE_CLOSE_WAIT = 7,
    TCP_STATE_CLOSING = 8,
    TCP_STATE_LAST_ACK = 9,
    TCP_STATE_TIME_WAIT = 10
} hepu_tcp_state_t;

/* TCP标志 */
#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20

/* TCP头部 */
typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset;  /* 高4位 */
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed)) hepu_tcp_header_t;

/* TCP连接 */
typedef struct {
    hepu_tcp_state_t state;
    uint32_t local_ip;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t window;
    int      retries;
    uint32_t last_activity;
    uint8_t  *send_buffer;
    uint32_t send_buffer_size;
    uint32_t send_buffer_len;
    uint8_t  *recv_buffer;
    uint32_t recv_buffer_size;
    uint32_t recv_buffer_len;
    int      in_use;
} hepu_hepu_tcp_connection_t;

void tcp_init(void);
int tcp_connect(uint32_t remote_ip, uint16_t remote_port, uint16_t local_port);
int tcp_listen(uint16_t local_port, int backlog);
int tcp_accept(int listen_fd, uint32_t *client_ip, uint16_t *client_port);
int tcp_send(int fd, const uint8_t *data, uint32_t length);
int tcp_receive(int fd, uint8_t *buffer, uint32_t max_length);
int tcp_close(int fd);
int tcp_get_state(int fd, hepu_tcp_state_t *state);
int tcp_get_connection_info(int fd, hepu_hepu_tcp_connection_t *info);
const char *hepu_tcp_state_text(hepu_tcp_state_t state);
void tcp_tick(void);

#endif /* TCP_H */
