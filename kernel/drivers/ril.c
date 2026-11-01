/* ============================================================================
 * 赫普OS - 通信子系统 (RIL 无线接口层)
 * 电话/短信/移动数据(2G/3G/4G/5G) + SIM管理 + 信号强度
 * ========================================================================== */

#include <stdint.h>
#include "ril.h"
#include "vga.h"
#include "string.h"

static ril_device_t ril;
static ril_event_cb_t event_cb = NULL;

void ril_init(void)
{
    memset(&ril, 0, sizeof(ril_device_t));
    ril.initialized = 1;
    ril.radio_on = 1;
    ril.sim_state = SIM_STATE_READY;
    ril.network_type = NETWORK_TYPE_LTE;
    ril.data_state = DATA_DISCONNECTED;
    ril.roaming = 0;
    ril.data_enabled = 0;

    /* 模拟设备信息 */
    strcpy(ril.imei, "860123456789012");
    strcpy(ril.imsi, "460011234567890");
    strcpy(ril.operator_name, "China Mobile");
    strcpy(ril.operator_numeric, "46001");

    /* 模拟信号强度 */
    ril.signal.lte_signal = 18;
    ril.signal.lte_rsrp = -85;
    ril.signal.lte_rsrq = -10;
    ril.signal.lte_rssnr = 15;
    ril.signal.level = 3;

    /* 默认APN */
    strcpy(ril.apn.apn, "cmnet");
    ril.apn.protocol = 0;

    vga_puts("[RIL] RIL init done \n");
    vga_printf(" net: %s, signal: %d bars, SIM: %s\n",
               "LTE", ril.signal.level, " ready ");
    vga_printf(" carrier: %s, IMEI: %s\n", ril.operator_name, ril.imei);
}

int ril_set_radio_power(int on)
{
    if (!ril.initialized) return -1;
    ril.radio_on = on ? 1 : 0;
    if (!on) {
        ril.data_state = DATA_DISCONNECTED;
        ril.network_type = NETWORK_TYPE_UNKNOWN;
    } else {
        ril.network_type = NETWORK_TYPE_LTE;
    }
    vga_printf("[RIL] radio %s\n", on ? " on " : " close ");
    return 0;
}

int ril_get_radio_power(void) { return ril.radio_on; }

sim_state_t ril_get_sim_state(void) { return ril.sim_state; }

int ril_enter_sim_pin(const char *pin)
{
    if (!ril.initialized || !pin) return -1;
    if (ril.sim_state != SIM_STATE_PIN_REQUIRED) return -1;
    /* 原型阶段: 任何PIN都通过 */
    ril.sim_state = SIM_STATE_READY;
    vga_puts("[RIL] SIM PIN verify ok \n");
    return 0;
}

int ril_change_sim_pin(const char *old_pin, const char *new_pin)
{
    (void)old_pin; (void)new_pin;
    return 0;
}

network_type_t ril_get_network_type(void) { return ril.network_type; }

int ril_get_signal_strength(signal_strength_t *signal)
{
    if (!signal) return -1;
    memcpy(signal, &ril.signal, sizeof(signal_strength_t));
    return 0;
}

int ril_get_operator_name(char *buf, int len)
{
    if (!buf || len <= 0) return -1;
    strncpy(buf, ril.operator_name, len - 1);
    buf[len - 1] = '\0';
    return 0;
}

int ril_is_roaming(void) { return ril.roaming; }

int ril_get_imei(char *buf, int len)
{
    if (!buf || len <= 0) return -1;
    strncpy(buf, ril.imei, len - 1);
    buf[len - 1] = '\0';
    return 0;
}

int ril_get_imsi(char *buf, int len)
{
    if (!buf || len <= 0) return -1;
    strncpy(buf, ril.imsi, len - 1);
    buf[len - 1] = '\0';
    return 0;
}

int ril_dial(const char *number)
{
    if (!ril.initialized || !number) return -1;
    if (!ril.radio_on) return -1;

    for (int i = 0; i < RIL_MAX_CALLS; i++) {
        if (!ril.calls[i].used) {
            memset(&ril.calls[i], 0, sizeof(ril_call_t));
            ril.calls[i].used = 1;
            ril.calls[i].id = i;
            ril.calls[i].state = CALL_STATE_DIALING;
            strncpy(ril.calls[i].number, number, RIL_PHONE_NUMBER_LEN - 1);
            ril.calls[i].is_incoming = 0;
            ril.call_count++;
            vga_printf("[RIL] dial: %s\n", number);
            /* 模拟接通 */
            ril.calls[i].state = CALL_STATE_ACTIVE;
            ril.calls[i].start_time = 0;
            return i;
        }
    }
    return -1;
}

int ril_answer(int call_id)
{
    if (call_id < 0 || call_id >= RIL_MAX_CALLS || !ril.calls[call_id].used) return -1;
    if (ril.calls[call_id].state != CALL_STATE_INCOMING &&
        ril.calls[call_id].state != CALL_STATE_RINGING) return -1;
    ril.calls[call_id].state = CALL_STATE_ACTIVE;
    ril.calls[call_id].start_time = 0;
    vga_printf("[RIL] answer call: %s\n", ril.calls[call_id].number);
    return 0;
}

int ril_hangup(int call_id)
{
    if (call_id < 0 || call_id >= RIL_MAX_CALLS || !ril.calls[call_id].used) return -1;
    ril.calls[call_id].state = CALL_STATE_DISCONNECTED;
    ril.calls[call_id].used = 0;
    ril.call_count--;
    vga_puts("[RIL] hangup \n");
    return 0;
}

int ril_hangup_all(void)
{
    for (int i = 0; i < RIL_MAX_CALLS; i++) {
        if (ril.calls[i].used) {
            ril.calls[i].used = 0;
            ril.call_count--;
        }
    }
    return 0;
}

int ril_set_mute(int mute)
{
    for (int i = 0; i < RIL_MAX_CALLS; i++) {
        if (ril.calls[i].used && ril.calls[i].state == CALL_STATE_ACTIVE) {
            ril.calls[i].is_muted = mute ? 1 : 0;
        }
    }
    return 0;
}

int ril_get_mute(void)
{
    for (int i = 0; i < RIL_MAX_CALLS; i++) {
        if (ril.calls[i].used && ril.calls[i].state == CALL_STATE_ACTIVE) {
            return ril.calls[i].is_muted;
        }
    }
    return 0;
}

int ril_set_speaker(int speaker)
{
    for (int i = 0; i < RIL_MAX_CALLS; i++) {
        if (ril.calls[i].used && ril.calls[i].state == CALL_STATE_ACTIVE) {
            ril.calls[i].is_speaker = speaker ? 1 : 0;
        }
    }
    return 0;
}

int ril_get_calls(ril_call_t *calls, int max)
{
    if (!calls || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < RIL_MAX_CALLS && count < max; i++) {
        if (ril.calls[i].used) {
            memcpy(&calls[count++], &ril.calls[i], sizeof(ril_call_t));
        }
    }
    return count;
}

int ril_get_active_call(void)
{
    for (int i = 0; i < RIL_MAX_CALLS; i++) {
        if (ril.calls[i].used && ril.calls[i].state == CALL_STATE_ACTIVE) return i;
    }
    return -1;
}

int ril_send_sms(const char *address, const char *text)
{
    if (!ril.initialized || !address || !text) return -1;
    if (!ril.radio_on) return -1;
    if (ril.sms_count >= RIL_MAX_SMS) return -1;

    ril_sms_t *s = &ril.sms[ril.sms_count];
    memset(s, 0, sizeof(ril_sms_t));
    s->used = 1;
    s->id = ril.sms_count;
    strncpy(s->address, address, RIL_PHONE_NUMBER_LEN - 1);
    strncpy(s->body, text, 1023);
    s->timestamp = 0;
    s->read = 1;
    s->is_incoming = 0;
    s->status = 1;  /* sent */
    ril.sms_count++;
    ril.sms_sent++;

    vga_printf("[RIL] SMS to %s: %s\n", address, text);
    return s->id;
}

int ril_delete_sms(int sms_id)
{
    if (sms_id < 0 || sms_id >= RIL_MAX_SMS || !ril.sms[sms_id].used) return -1;
    ril.sms[sms_id].used = 0;
    return 0;
}

int ril_get_sms(ril_sms_t *sms_list, int max)
{
    if (!sms_list || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < RIL_MAX_SMS && count < max; i++) {
        if (ril.sms[i].used) {
            memcpy(&sms_list[count++], &ril.sms[i], sizeof(ril_sms_t));
        }
    }
    return count;
}

int ril_get_sms_count(void) { return ril.sms_count; }

int ril_mark_sms_read(int sms_id)
{
    if (sms_id < 0 || sms_id >= RIL_MAX_SMS || !ril.sms[sms_id].used) return -1;
    ril.sms[sms_id].read = 1;
    return 0;
}

int ril_set_data_enabled(int enable)
{
    if (!ril.initialized) return -1;
    ril.data_enabled = enable ? 1 : 0;
    if (enable && ril.data_state == DATA_DISCONNECTED) {
        ril.data_state = DATA_CONNECTING;
        /* 模拟连接成功 */
        ril.data_state = DATA_CONNECTED;
        vga_puts("[RIL] data connected \n");
    } else if (!enable && ril.data_state == DATA_CONNECTED) {
        ril.data_state = DATA_DISCONNECTED;
        vga_puts("[RIL] data disconnected \n");
    }
    return 0;
}

int ril_get_data_enabled(void) { return ril.data_enabled; }

data_state_t ril_get_data_state(void) { return ril.data_state; }

int ril_setup_data_call(const ril_apn_t *apn)
{
    if (!apn) return -1;
    memcpy(&ril.apn, apn, sizeof(ril_apn_t));
    ril.data_state = DATA_CONNECTING;
    ril.data_state = DATA_CONNECTED;
    vga_printf("[RIL] data call up: APN=%s\n", apn->apn);
    return 0;
}

int ril_deactivate_data_call(void)
{
    ril.data_state = DATA_DISCONNECTED;
    return 0;
}

int ril_get_data_usage(uint64_t *sent, uint64_t *received)
{
    if (sent) *sent = ril.bytes_sent;
    if (received) *received = ril.bytes_received;
    return 0;
}

int ril_set_apn(const ril_apn_t *apn)
{
    if (!apn) return -1;
    memcpy(&ril.apn, apn, sizeof(ril_apn_t));
    return 0;
}

int ril_get_apn(ril_apn_t *apn)
{
    if (!apn) return -1;
    memcpy(apn, &ril.apn, sizeof(ril_apn_t));
    return 0;
}

int ril_start_network_scan(void)
{
    vga_puts("[RIL] start net scan ...\n");
    return 0;
}

int ril_get_available_networks(char *buf, int len)
{
    if (!buf || len <= 0) return -1;
    strncpy(buf, "China Mobile(46001),China Unicom(46001),China Telecom(46003)", len - 1);
    buf[len - 1] = '\0';
    return 3;
}

int ril_get_status(ril_device_t *status)
{
    if (!status) return -1;
    memcpy(status, &ril, sizeof(ril_device_t));
    return 0;
}

int ril_set_event_callback(ril_event_cb_t cb)
{
    event_cb = cb;
    return 0;
}
