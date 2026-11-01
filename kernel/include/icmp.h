#ifndef ICMP_H
#define ICMP_H

#include <stdint.h>

/* ICMP类型 */
#define ICMP_TYPE_ECHO_REPLY    0
#define ICMP_TYPE_DEST_UNREACH  3
#define ICMP_TYPE_REDIRECT      5
#define ICMP_TYPE_ECHO_REQUEST  8
#define ICMP_TYPE_TIME_EXCEEDED 11
#define ICMP_TYPE_PARAM_PROBLEM 12
#define ICMP_TYPE_TIMESTAMP     13
#define ICMP_TYPE_TIMESTAMP_REPLY 14
#define ICMP_TYPE_INFO_REQUEST  15
#define ICMP_TYPE_INFO_REPLY    16

/* ICMP代码 */
#define ICMP_CODE_NET_UNREACH   0
#define ICMP_CODE_HOST_UNREACH  1
#define ICMP_CODE_PROTO_UNREACH 2
#define ICMP_CODE_PORT_UNREACH  3
#define ICMP_CODE_FRAG_NEEDED   4

/* ICMP头部 */
typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    union {
        struct {
            uint16_t id;
            uint16_t seq;
        } echo;
        uint32_t gateway;
        struct {
            uint16_t unused;
            uint16_t mtu;
        } frag;
    } data;
} __attribute__((packed)) hepu_icmp_header_t;

/* Ping统计 */
typedef struct {
    uint32_t transmitted;
    uint32_t received;
    uint32_t errors;
    uint32_t min_rtt_ms;
    uint32_t max_rtt_ms;
    uint32_t avg_rtt_ms;
    uint32_t total_rtt_ms;
} hepu_ping_stats_t;

/* Ping结果 */
typedef struct {
    uint32_t ip_addr;
    uint16_t seq;
    uint32_t ttl;
    uint32_t rtt_ms;
    int      success;
} hepu_ping_result_t;

void icmp_init(void);
int icmp_send_echo_request(uint32_t dest_ip, uint16_t id, uint16_t seq,
                           const uint8_t *data, uint32_t data_len);
int icmp_process_packet(const uint8_t *data, uint32_t length);
int ping(const char *hostname, int count, int interval_ms, hepu_ping_stats_t *stats);
int ping_once(uint32_t dest_ip, hepu_ping_result_t *result);
uint16_t icmp_checksum(const uint8_t *data, uint32_t length);

#endif /* ICMP_H */
