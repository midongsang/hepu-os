/* ============================================================================
 * 赫普OS - 电话服务
 * ========================================================================== */

#include <stdint.h>
#include "phone_service.h"
#include "vga.h"
#include "string.h"

static ps_phone_service_t ps;

void phone_service_init(void)
{
    memset(&ps, 0, sizeof(ps_phone_service_t));
    ps.initialized = 1;
    ps.enabled = 1;
    ps.airplane_mode = 0;
    ps.default_sim = 0;
    ps.active_call = -1;

    /* 模拟SIM卡 */
    ps.sims[0].present = 1;
    ps.sims[0].slot = 0;
    strncpy(ps.sims[0].imsi, "460011234567890", 15);
    strncpy(ps.sims[0].imei, "861234567890123", 15);
    strncpy(ps.sims[0].operator_name, " in 国 move ", 63);
    strncpy(ps.sims[0].country, "CN", 7);
    ps.sims[0].signal_strength = 75;
    ps.sims[0].network_type = 3;  /* 5G */
    ps.sim_count = 1;

    /* 默认设置 */
    ps.voicemail_enabled = 1;
    strncpy(ps.voicemail_number, "10086", 31);
    ps.call_waiting = 1;
    ps.caller_id = 1;
    ps.auto_record = 0;

    /* 添加测试联系人 */
    phone_contact_add(" 三 ", "13800138000");
    phone_contact_add(" 李四 ", "13900139000");
    phone_contact_add(" 客服 ", "10086");

    vga_puts("[PHONE] phone service init done \n");
    vga_printf(" SIM: %d, contacts: %d, call log: %d\n",
               ps.sim_count, ps.contact_count, ps.call_log_count);
}

/* 通话管理 */
int phone_dial(const char *number, ps_ps_call_type_t type)
{
    if (!number || ps.airplane_mode) return -1;
    for (int i = 0; i < PS_MAX_CALLS; i++) {
        if (!ps.calls[i].used) {
            memset(&ps.calls[i], 0, sizeof(ps_call_t));
            ps.calls[i].used = 1;
            ps.calls[i].id = i;
            strncpy(ps.calls[i].number, number, 31);
            ps.calls[i].type = type;
            ps.calls[i].state = PS_CALL_STATE_DIALING;
            ps.calls[i].outgoing = 1;
            ps.calls[i].start_time = 1;
            ps.call_count++;
            ps.total_calls++;
            ps.total_outgoing++;
            ps.active_call = i;
            /* 查找联系人 */
            ps_contact_t c;
            if (phone_contact_find_by_number(number, &c) == 0)
                strncpy(ps.calls[i].contact_name, c.name, 63);
            return i;
        }
    }
    return -1;
}

int phone_answer(int call_id)
{
    if (call_id < 0 || call_id >= PS_MAX_CALLS || !ps.calls[call_id].used) return -1;
    if (ps.calls[call_id].state != PS_CALL_STATE_RINGING) return -1;
    ps.calls[call_id].state = PS_CALL_STATE_ACTIVE;
    ps.calls[call_id].connect_time = 1;
    ps.active_call = call_id;
    return 0;
}

int phone_hangup(int call_id)
{
    if (call_id < 0 || call_id >= PS_MAX_CALLS || !ps.calls[call_id].used) return -1;
    ps.calls[call_id].state = PS_CALL_STATE_ENDED;
    ps.calls[call_id].end_time = 1;
    /* 添加通话记录 */
    phone_call_log_add(ps.calls[call_id].number, ps.calls[call_id].contact_name,
                       ps.calls[call_id].type, ps.calls[call_id].outgoing, 0, 0);
    ps.calls[call_id].used = 0;
    ps.call_count--;
    if (ps.active_call == call_id) ps.active_call = -1;
    return 0;
}

int phone_reject(int call_id)
{
    if (call_id < 0 || call_id >= PS_MAX_CALLS || !ps.calls[call_id].used) return -1;
    ps.calls[call_id].state = PS_CALL_STATE_BUSY;
    ps.calls[call_id].used = 0;
    ps.call_count--;
    return 0;
}

int phone_hold(int call_id)
{
    if (call_id < 0 || call_id >= PS_MAX_CALLS || !ps.calls[call_id].used) return -1;
    if (ps.calls[call_id].state != PS_CALL_STATE_ACTIVE) return -1;
    ps.calls[call_id].state = PS_CALL_STATE_ON_HOLD;
    return 0;
}

int phone_resume(int call_id)
{
    if (call_id < 0 || call_id >= PS_MAX_CALLS || !ps.calls[call_id].used) return -1;
    if (ps.calls[call_id].state != PS_CALL_STATE_ON_HOLD) return -1;
    ps.calls[call_id].state = PS_CALL_STATE_ACTIVE;
    ps.active_call = call_id;
    return 0;
}

int phone_swap_calls(void)
{
    for (int i = 0; i < PS_MAX_CALLS; i++) {
        if (ps.calls[i].used && ps.calls[i].state == PS_CALL_STATE_ON_HOLD) {
            if (ps.active_call >= 0) ps.calls[ps.active_call].state = PS_CALL_STATE_ON_HOLD;
            ps.calls[i].state = PS_CALL_STATE_ACTIVE;
            ps.active_call = i;
            return 0;
        }
    }
    return -1;
}

int phone_merge_calls(void)
{
    return 0;  /* 简化: 会议电话 */
}

int phone_get_call(int call_id, ps_call_t *call)
{
    if (call_id < 0 || call_id >= PS_MAX_CALLS || !ps.calls[call_id].used || !call) return -1;
    memcpy(call, &ps.calls[call_id], sizeof(ps_call_t));
    return 0;
}

int phone_list_calls(ps_call_t *calls, int max)
{
    if (!calls || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < PS_MAX_CALLS && count < max; i++) {
        if (ps.calls[i].used) memcpy(&calls[count++], &ps.calls[i], sizeof(ps_call_t));
    }
    return count;
}

int phone_get_active_call(ps_call_t *call)
{
    if (ps.active_call < 0 || !call) return -1;
    return phone_get_call(ps.active_call, call);
}

/* 通话控制 */
int phone_mute(int call_id, int mute)
{
    if (call_id < 0 || call_id >= PS_MAX_CALLS || !ps.calls[call_id].used) return -1;
    ps.calls[call_id].muted = mute ? 1 : 0;
    return 0;
}

int phone_speaker(int call_id, int on)
{
    if (call_id < 0 || call_id >= PS_MAX_CALLS || !ps.calls[call_id].used) return -1;
    ps.calls[call_id].speaker = on ? 1 : 0;
    return 0;
}

int phone_bluetooth(int call_id, int on)
{
    if (call_id < 0 || call_id >= PS_MAX_CALLS || !ps.calls[call_id].used) return -1;
    ps.calls[call_id].bluetooth = on ? 1 : 0;
    return 0;
}

int phone_record(int call_id, int start)
{
    if (call_id < 0 || call_id >= PS_MAX_CALLS || !ps.calls[call_id].used) return -1;
    ps.calls[call_id].recording = start ? 1 : 0;
    return 0;
}

int phone_send_dtmf(int call_id, char digit)
{
    (void)call_id; (void)digit;
    return 0;
}

/* 通讯录 */
int phone_contact_add(const char *name, const char *number)
{
    if (!name || !number) return -1;
    for (int i = 0; i < PS_MAX_CONTACTS; i++) {
        if (!ps.contacts[i].used) {
            memset(&ps.contacts[i], 0, sizeof(ps_contact_t));
            ps.contacts[i].used = 1;
            ps.contacts[i].id = i;
            strncpy(ps.contacts[i].name, name, 63);
            strncpy(ps.contacts[i].number, number, 31);
            ps.contacts[i].created = 1;
            ps.contact_count++;
            return i;
        }
    }
    return -1;
}

int phone_contact_remove(int contact_id)
{
    if (contact_id < 0 || contact_id >= PS_MAX_CONTACTS || !ps.contacts[contact_id].used) return -1;
    ps.contacts[contact_id].used = 0;
    ps.contact_count--;
    return 0;
}

int phone_contact_update(int contact_id, const ps_contact_t *contact)
{
    if (contact_id < 0 || contact_id >= PS_MAX_CONTACTS || !ps.contacts[contact_id].used || !contact) return -1;
    memcpy(&ps.contacts[contact_id], contact, sizeof(ps_contact_t));
    ps.contacts[contact_id].id = contact_id;
    ps.contacts[contact_id].used = 1;
    return 0;
}

int phone_contact_get(int contact_id, ps_contact_t *contact)
{
    if (contact_id < 0 || contact_id >= PS_MAX_CONTACTS || !ps.contacts[contact_id].used || !contact) return -1;
    memcpy(contact, &ps.contacts[contact_id], sizeof(ps_contact_t));
    return 0;
}

int phone_contact_find_by_number(const char *number, ps_contact_t *contact)
{
    if (!number || !contact) return -1;
    for (int i = 0; i < PS_MAX_CONTACTS; i++) {
        if (ps.contacts[i].used && strcmp(ps.contacts[i].number, number) == 0) {
            memcpy(contact, &ps.contacts[i], sizeof(ps_contact_t));
            return 0;
        }
    }
    return -1;
}

int phone_contact_list(ps_contact_t *contacts, int max)
{
    if (!contacts || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < PS_MAX_CONTACTS && count < max; i++) {
        if (ps.contacts[i].used) memcpy(&contacts[count++], &ps.contacts[i], sizeof(ps_contact_t));
    }
    return count;
}

int phone_contact_set_favorite(int contact_id, int favorite)
{
    if (contact_id < 0 || contact_id >= PS_MAX_CONTACTS || !ps.contacts[contact_id].used) return -1;
    ps.contacts[contact_id].favorite = favorite ? 1 : 0;
    return 0;
}

int phone_contact_set_blocked(int contact_id, int blocked)
{
    if (contact_id < 0 || contact_id >= PS_MAX_CONTACTS || !ps.contacts[contact_id].used) return -1;
    ps.contacts[contact_id].blocked = blocked ? 1 : 0;
    return 0;
}

int phone_contact_count(void)
{
    return ps.contact_count;
}

/* 通话记录 */
int phone_call_log_add(const char *number, const char *name, ps_ps_call_type_t type,
                       int outgoing, int missed, uint64_t duration)
{
    for (int i = 0; i < PS_MAX_CALL_LOG; i++) {
        if (!ps.call_log[i].used) {
            memset(&ps.call_log[i], 0, sizeof(ps_call_log_entry_t));
            ps.call_log[i].used = 1;
            ps.call_log[i].id = i;
            if (number) strncpy(ps.call_log[i].number, number, 31);
            if (name) strncpy(ps.call_log[i].contact_name, name, 63);
            ps.call_log[i].type = type;
            ps.call_log[i].outgoing = outgoing;
            ps.call_log[i].missed = missed;
            ps.call_log[i].duration = duration;
            ps.call_log[i].start_time = 1;
            ps.call_log_count++;
            if (missed) ps.total_missed++;
            ps.total_duration += duration;
            return i;
        }
    }
    return -1;
}

int phone_call_log_get(int index, ps_call_log_entry_t *entry)
{
    if (index < 0 || index >= PS_MAX_CALL_LOG || !ps.call_log[index].used || !entry) return -1;
    memcpy(entry, &ps.call_log[index], sizeof(ps_call_log_entry_t));
    return 0;
}

int phone_call_log_list(ps_call_log_entry_t *entries, int max)
{
    if (!entries || max <= 0) return 0;
    int count = 0;
    for (int i = PS_MAX_CALL_LOG - 1; i >= 0 && count < max; i--) {
        if (ps.call_log[i].used) memcpy(&entries[count++], &ps.call_log[i], sizeof(ps_call_log_entry_t));
    }
    return count;
}

int phone_call_log_clear(void)
{
    for (int i = 0; i < PS_MAX_CALL_LOG; i++) ps.call_log[i].used = 0;
    ps.call_log_count = 0;
    return 0;
}

int phone_call_log_count(void)
{
    return ps.call_log_count;
}

/* SIM卡 */
int phone_sim_get_info(int slot, ps_sim_card_t *sim)
{
    if (slot < 0 || slot >= 2 || !sim) return -1;
    memcpy(sim, &ps.sims[slot], sizeof(ps_sim_card_t));
    return 0;
}

int phone_sim_set_default(int slot)
{
    if (slot < 0 || slot >= 2 || !ps.sims[slot].present) return -1;
    ps.default_sim = slot;
    return 0;
}

int phone_sim_get_default(void)
{
    return ps.default_sim;
}

int phone_get_signal_strength(int slot)
{
    if (slot < 0 || slot >= 2 || !ps.sims[slot].present) return -1;
    return ps.sims[slot].signal_strength;
}

int phone_get_network_type(int slot)
{
    if (slot < 0 || slot >= 2 || !ps.sims[slot].present) return -1;
    return ps.sims[slot].network_type;
}

/* 设置 */
int phone_set_airplane_mode(int on)
{
    ps.airplane_mode = on ? 1 : 0;
    return 0;
}

int phone_get_airplane_mode(void)
{
    return ps.airplane_mode;
}

int phone_set_voicemail(const char *number)
{
    if (!number) return -1;
    strncpy(ps.voicemail_number, number, 31);
    return 0;
}

int phone_set_call_waiting(int enable)
{
    ps.call_waiting = enable ? 1 : 0;
    return 0;
}

int phone_set_caller_id(int enable)
{
    ps.caller_id = enable ? 1 : 0;
    return 0;
}

int phone_set_auto_record(int enable)
{
    ps.auto_record = enable ? 1 : 0;
    return 0;
}

/* 统计 */
int phone_get_stats(ps_phone_service_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &ps, sizeof(ps_phone_service_t));
    return 0;
}
