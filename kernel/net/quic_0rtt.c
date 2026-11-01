/* ============================================================================
 * 赫普OS - QUIC 0-RTT连接恢复
 * RFC 9000: 早期数据与会话票据
 * ========================================================================== */

#include <stdint.h>
#include "types.h"
#include "quic_0rtt.h"
#include "vga.h"
#include "string.h"

static int quic_0rtt_initialized = 0;

/* 初始化0-RTT */
void quic_0rtt_init(void)
{
    quic_0rtt_initialized = 1;
    vga_puts("[QUIC-0RTT] 0-RTT conn recovery init \n");
    vga_puts("[QUIC-0RTT] supports session tickets & early data \n");
}

/* 存储会话票据 */
int quic_0rtt_store_ticket(quic_0rtt_state_t *state, const u8 *ticket,
                           u32 len, u32 max_early_data)
{
    if (!quic_0rtt_initialized || !state || !ticket || len == 0) return -1;
    if (len > QUIC_0RTT_TICKET_SIZE) return -1;

    /* 查找空闲槽位或最旧的票据 */
    int slot = -1;
    u64 oldest_time = UINT64_MAX;

    for (int i = 0; i < QUIC_0RTT_MAX_TICKETS; i++) {
        if (!state->tickets[i].valid) {
            slot = i;
            break;
        }
        if (state->tickets[i].received_at < oldest_time) {
            oldest_time = state->tickets[i].received_at;
            slot = i;
        }
    }

    if (slot < 0) slot = 0;

    memcpy(state->tickets[slot].ticket, ticket, len);
    state->tickets[slot].ticket_len = len;
    state->tickets[slot].max_early_data = max_early_data;
    state->tickets[slot].received_at = 0;  /* 简化：不记录实际时间 */
    state->tickets[slot].used = 0;
    state->tickets[slot].valid = 1;

    state->max_early_data = max_early_data;

    vga_printf("[QUIC-0RTT] store ticket: slot=%d, len=%d, max_early_data=%d\n",
               slot, len, max_early_data);
    return 0;
}

/* 获取可用票据 */
int quic_0rtt_get_ticket(quic_0rtt_state_t *state, u8 *ticket,
                          u32 *ticket_len)
{
    if (!state || !ticket) return -1;

    for (int i = 0; i < QUIC_0RTT_MAX_TICKETS; i++) {
        if (state->tickets[i].valid && !state->tickets[i].used) {
            u32 len = state->tickets[i].ticket_len;
            memcpy(ticket, state->tickets[i].ticket, len);
            if (ticket_len) *ticket_len = len;
            state->tickets[i].used = 1;

            vga_printf("[QUIC-0RTT] use ticket: slot=%d, len=%d\n", i, len);
            return 0;
        }
    }
    return -1;
}

/* 启用/禁用0-RTT */
int quic_0rtt_enable(quic_0rtt_state_t *state, int enable)
{
    if (!state) return -1;
    state->enabled = enable ? 1 : 0;
    vga_printf("[QUIC-0RTT] %s\n", enable ? " enabled " : " disabled ");
    return 0;
}

/* 检查0-RTT是否启用 */
int quic_0rtt_is_enabled(const quic_0rtt_state_t *state)
{
    return state && state->enabled;
}

/* 检查是否有有效票据 */
int quic_0rtt_has_valid_ticket(const quic_0rtt_state_t *state)
{
    if (!state || !state->enabled) return 0;

    for (int i = 0; i < QUIC_0RTT_MAX_TICKETS; i++) {
        if (state->tickets[i].valid && !state->tickets[i].used) {
            return 1;
        }
    }
    return 0;
}

/* 排队早期数据 */
int quic_0rtt_queue_data(quic_0rtt_state_t *state, const u8 *data,
                          u32 len)
{
    if (!state || !data || len == 0) return -1;
    if (!state->enabled) return -1;
    if (state->early_data_len + len > QUIC_0RTT_MAX_EARLY_DATA) return -1;
    if (state->early_data_len + len > state->max_early_data) return -1;

    memcpy(state->early_data + state->early_data_len, data, len);
    state->early_data_len += len;

    vga_printf("[QUIC-0RTT] queue early data: %d bytes(total: %d)\n",
               len, state->early_data_len);
    return 0;
}

/* 获取早期数据 */
int quic_0rtt_get_early_data(quic_0rtt_state_t *state, u8 *data,
                              u32 *len)
{
    if (!state || !data) return -1;
    if (state->early_data_len == 0) return -1;

    u32 copy_len = state->early_data_len - state->early_data_offset;
    memcpy(data, state->early_data + state->early_data_offset, copy_len);
    if (len) *len = copy_len;

    state->early_data_offset = state->early_data_len;
    return 0;
}

/* 标记早期数据已发送 */
int quic_0rtt_mark_sent(quic_0rtt_state_t *state)
{
    if (!state) return -1;
    state->early_data_sent = 1;
    vga_puts("[QUIC-0RTT] early data sent \n");
    return 0;
}

/* 标记早期数据已确认 */
int quic_0rtt_mark_acked(quic_0rtt_state_t *state)
{
    if (!state) return -1;
    state->early_data_acked = 1;
    vga_puts("[QUIC-0RTT] early data acked \n");
    return 0;
}

/* 拒绝0-RTT（服务器拒绝早期数据） */
int quic_0rtt_reject(quic_0rtt_state_t *state)
{
    if (!state) return -1;

    /* 清除早期数据，需要在1-RTT中重传 */
    state->early_data_len = 0;
    state->early_data_offset = 0;
    state->early_data_sent = 0;
    state->early_data_acked = 0;

    /* 标记所有票据为已使用（不能再用） */
    for (int i = 0; i < QUIC_0RTT_MAX_TICKETS; i++) {
        state->tickets[i].used = 1;
    }

    vga_puts("[QUIC-0RTT] server rejects early data , needs retransmit \n");
    return 0;
}

/* 清除0-RTT状态 */
int quic_0rtt_clear(quic_0rtt_state_t *state)
{
    if (!state) return -1;

    memset(state->tickets, 0, sizeof(state->tickets));
    memset(state->early_data, 0, sizeof(state->early_data));
    state->early_data_len = 0;
    state->early_data_offset = 0;
    state->early_data_sent = 0;
    state->early_data_acked = 0;
    state->max_early_data = 0;

    vga_puts("[QUIC-0RTT] state cleared \n");
    return 0;
}

/* 获取统计信息 */
int quic_0rtt_get_stats(const quic_0rtt_state_t *state,
                         u32 *tickets_stored, u32 *early_data_bytes,
                         int *early_data_acked)
{
    if (!state) return -1;

    u32 stored = 0;
    for (int i = 0; i < QUIC_0RTT_MAX_TICKETS; i++) {
        if (state->tickets[i].valid) stored++;
    }

    if (tickets_stored) *tickets_stored = stored;
    if (early_data_bytes) *early_data_bytes = state->early_data_len;
    if (early_data_acked) *early_data_acked = state->early_data_acked;

    return 0;
}
