#ifndef QUIC_0RTT_H
#include "types.h"
#define QUIC_0RTT_H

#include <stdint.h>
#include "quic.h"

#define QUIC_0RTT_MAX_TICKETS 8
#define QUIC_0RTT_TICKET_SIZE 256
#define QUIC_0RTT_MAX_EARLY_DATA 16384
#define QUIC_0RTT_MAX_AGE 604800  /* 7天 */

/* 0-RTT票据 */
typedef struct {
    u8  ticket[QUIC_0RTT_TICKET_SIZE];
    u32 ticket_len;
    u64 received_at;  /* 接收时间戳 */
    u32 max_early_data;
    u8  used;
    int      valid;
} quic_0rtt_ticket_t;

/* 0-RTT会话状态 */
typedef struct {
    quic_0rtt_ticket_t tickets[QUIC_0RTT_MAX_TICKETS];
    u8  early_data[QUIC_0RTT_MAX_EARLY_DATA];
    u32 early_data_len;
    u32 early_data_offset;
    int      early_data_sent;
    int      early_data_acked;
    int      enabled;
    u64 max_early_data;
} quic_0rtt_state_t;

void quic_0rtt_init(void);
int quic_0rtt_store_ticket(quic_0rtt_state_t *state, const u8 *ticket,
                           u32 len, u32 max_early_data);
int quic_0rtt_get_ticket(quic_0rtt_state_t *state, u8 *ticket,
                          u32 *ticket_len);
int quic_0rtt_enable(quic_0rtt_state_t *state, int enable);
int quic_0rtt_is_enabled(const quic_0rtt_state_t *state);
int quic_0rtt_has_valid_ticket(const quic_0rtt_state_t *state);
int quic_0rtt_queue_data(quic_0rtt_state_t *state, const u8 *data,
                          u32 len);
int quic_0rtt_get_early_data(quic_0rtt_state_t *state, u8 *data,
                              u32 *len);
int quic_0rtt_mark_sent(quic_0rtt_state_t *state);
int quic_0rtt_mark_acked(quic_0rtt_state_t *state);
int quic_0rtt_reject(quic_0rtt_state_t *state);
int quic_0rtt_clear(quic_0rtt_state_t *state);
int quic_0rtt_get_stats(const quic_0rtt_state_t *state,
                         u32 *tickets_stored, u32 *early_data_bytes,
                         int *early_data_acked);

#endif /* QUIC_0RTT_H */
