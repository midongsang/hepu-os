/* ============================================================================
 * 赫普OS - 通信深化
 * VoLTE + eSIM + 5G切片 + 卫星通信 + NFC支付
 * ========================================================================== */

#include <stdint.h>
#include "telecom2.h"
#include "vga.h"
#include "string.h"

telecom2_t t2;

void telecom2_init(void)
{
    memset(&t2, 0, sizeof(telecom2_t));
    t2.initialized = 1;

    /* SIM1 */
    t2.sims[0].used = 1;
    t2.sims[0].id = 0;
    t2.sims[0].slot = 0;
    t2.sims[0].type = SIM_TYPE_PHYSICAL;
    t2.sims[0].state = T2_SIM_STATE_READY;
    strncpy(t2.sims[0].iccid, "89860012345678901234", 31);
    strncpy(t2.sims[0].imsi, "460001234567890", 31);
    strncpy(t2.sims[0].msisdn, "13800138000", 31);
    strncpy(t2.sims[0].operator_name, " in 国 move ", 63);
    strncpy(t2.sims[0].country, "CN", 7);
    t2.sims[0].mcc = 460;
    t2.sims[0].mnc = 0;
    t2.sims[0].active = 1;
    t2.sims[0].data_enabled = 1;
    t2.sims[0].voice_enabled = 1;
    t2.sims[0].sms_enabled = 1;
    t2.sims[0].signal_strength = 85;
    t2.sims[0].network_type = 4;  /* 5G */
    t2.sims[0].volte_enabled = 1;
    t2.sims[0].vowifi_enabled = 1;

    /* SIM2 (eSIM) */
    t2.sims[1].used = 1;
    t2.sims[1].id = 1;
    t2.sims[1].slot = 1;
    t2.sims[1].type = SIM_TYPE_ESIM;
    t2.sims[1].state = T2_SIM_STATE_READY;
    strncpy(t2.sims[1].iccid, "89860198765432109876", 31);
    strncpy(t2.sims[1].operator_name, " in 国电信 ", 63);
    t2.sims[1].mcc = 460;
    t2.sims[1].mnc = 3;
    t2.sims[1].signal_strength = 70;
    t2.sims[1].network_type = 3;  /* 5G NSA */

    t2.sim_count = 2;
    t2.active_sim = 0;
    t2.default_data_sim = 0;
    t2.default_voice_sim = 0;
    t2.default_sms_sim = 0;

    /* eSIM配置文件 */
    t2.esim_profiles[0].used = 1;
    t2.esim_profiles[0].id = 0;
    strncpy(t2.esim_profiles[0].iccid, "89860198765432109876", 31);
    strncpy(t2.esim_profiles[0].operator_name, " in 国电信 ", 63);
    t2.esim_profiles[0].active = 1;
    t2.esim_profiles[0].enabled = 1;
    strncpy(t2.esim_profiles[0].profile_name, " master 力 card ", 63);
    t2.esim_profile_count = 1;

    /* 网络状态 */
    t2.network_type = T2_NETWORK_TYPE_5G_SA;
    strncpy(t2.operator_name, " in 国 move ", 63);
    t2.signal_strength = 85;
    t2.data_enabled = 1;

    /* 5G切片 */
    t2.slices[0].used = 1;
    t2.slices[0].id = 0;
    strncpy(t2.slices[0].name, "eMBB- default ", 63);
    t2.slices[0].sst = 1;
    t2.slices[0].sd = 1;
    t2.slices[0].default_slice = 1;
    t2.slices[0].active = 1;
    t2.slices[0].downlink_mbps = 1000;
    t2.slices[0].uplink_mbps = 100;
    t2.slices[0].latency_ms = 10;
    t2.slice_count = 1;

    t2.slices[1].used = 1;
    t2.slices[1].id = 1;
    strncpy(t2.slices[1].name, "URLLC- low 延迟 ", 63);
    t2.slices[1].sst = 2;
    t2.slices[1].sd = 2;
    t2.slices[1].downlink_mbps = 100;
    t2.slices[1].uplink_mbps = 50;
    t2.slices[1].latency_ms = 1;
    t2.slices[1].reliability = 99;
    t2.slice_count = 2;

    t2.slices[2].used = 1;
    t2.slices[2].id = 2;
    strncpy(t2.slices[2].name, "mMTC- 物联网 ", 63);
    t2.slices[2].sst = 3;
    t2.slices[2].sd = 3;
    t2.slices[2].downlink_mbps = 10;
    t2.slices[2].uplink_mbps = 5;
    t2.slices[2].latency_ms = 100;
    t2.slice_count = 3;

    /* VoLTE */
    t2.volte.enabled = 1;
    t2.volte.registered = 1;
    t2.volte.hd_audio = 1;
    t2.volte.emergency_support = 1;

    /* VoWiFi */
    t2.vowifi.enabled = 1;

    /* 卫星 */
    t2.satellite.enabled = 0;

    /* NFC */
    t2.nfc_enabled = 1;
    t2.nfc_payment_enabled = 1;

    /* 支付卡 */
    t2.payment_cards[0].used = 1;
    t2.payment_cards[0].id = 0;
    strncpy(t2.payment_cards[0].card_name, " 招商银行信用 card ", 63);
    strncpy(t2.payment_cards[0].card_number_last4, "8888", 7);
    strncpy(t2.payment_cards[0].card_network, "UnionPay", 15);
    t2.payment_cards[0].card_type = 0;
    t2.payment_cards[0].default_card = 1;
    t2.payment_cards[0].active = 1;
    t2.payment_cards[0].tokenized = 1;
    t2.payment_cards[0].expiry_month = 12;
    t2.payment_cards[0].expiry_year = 28;
    t2.payment_cards[0].contactless_limit = 1000;
    t2.payment_card_count = 1;

    vga_puts("[TELECOM2] telecom deep init done \n");
    vga_printf(" SIM: %d, net: 5G SA, VoLTE: on, 5G slices: %d, NFC payment: on \n",
               t2.sim_count, t2.slice_count);
}

int telecom2_get_sim_count(void) { return t2.sim_count; }

int telecom2_get_sim_info(int sim_id, sim_info_t *info)
{
    if (sim_id < 0 || sim_id >= T2_MAX_SIMS || !t2.sims[sim_id].used || !info) return -1;
    memcpy(info, &t2.sims[sim_id], sizeof(sim_info_t));
    return 0;
}

int telecom2_list_sims(sim_info_t *sims, int max)
{
    if (!sims || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < T2_MAX_SIMS && count < max; i++) {
        if (t2.sims[i].used) memcpy(&sims[count++], &t2.sims[i], sizeof(sim_info_t));
    }
    return count;
}

int telecom2_set_active_sim(int sim_id)
{
    if (sim_id < 0 || sim_id >= T2_MAX_SIMS || !t2.sims[sim_id].used) return -1;
    for (int i = 0; i < T2_MAX_SIMS; i++) t2.sims[i].active = 0;
    t2.sims[sim_id].active = 1;
    t2.active_sim = sim_id;
    return 0;
}

int telecom2_get_active_sim(void) { return t2.active_sim; }

int telecom2_set_default_data_sim(int sim_id)
{
    if (sim_id < 0 || sim_id >= T2_MAX_SIMS || !t2.sims[sim_id].used) return -1;
    t2.default_data_sim = sim_id;
    return 0;
}

int telecom2_set_default_voice_sim(int sim_id)
{
    if (sim_id < 0 || sim_id >= T2_MAX_SIMS || !t2.sims[sim_id].used) return -1;
    t2.default_voice_sim = sim_id;
    return 0;
}

int telecom2_set_default_sms_sim(int sim_id)
{
    if (sim_id < 0 || sim_id >= T2_MAX_SIMS || !t2.sims[sim_id].used) return -1;
    t2.default_sms_sim = sim_id;
    return 0;
}

int telecom2_enable_data(int sim_id, int enable)
{
    if (sim_id < 0 || sim_id >= T2_MAX_SIMS || !t2.sims[sim_id].used) return -1;
    t2.sims[sim_id].data_enabled = enable ? 1 : 0;
    return 0;
}

int telecom2_enable_roaming(int sim_id, int enable)
{
    if (sim_id < 0 || sim_id >= T2_MAX_SIMS || !t2.sims[sim_id].used) return -1;
    t2.sims[sim_id].roaming_enabled = enable ? 1 : 0;
    return 0;
}

int telecom2_get_signal_strength(int sim_id)
{
    if (sim_id < 0 || sim_id >= T2_MAX_SIMS || !t2.sims[sim_id].used) return -1;
    return t2.sims[sim_id].signal_strength;
}


int telecom2_get_network_type(void) { return t2.network_type; }

int telecom2_set_preferred_network(int type)
{
    t2.network_type = type;
    return 0;
}

int telecom2_get_operator_name(char *name, int max)
{
    if (!name || max <= 0) return -1;
    strncpy(name, t2.operator_name, max - 1);
    return 0;
}

int telecom2_enable_airplane_mode(int enable)
{
    t2.airplane_mode = enable ? 1 : 0;
    vga_printf("[TELECOM2] airplane mode %s\n", enable ? " on " : " close ");
    return 0;
}

int telecom2_is_airplane_mode(void) { return t2.airplane_mode; }

int telecom2_get_roaming_status(void) { return t2.roaming; }

int telecom2_slice_create(const char *name, int sst, int sd, int dnn)
{
    for (int i = 0; i < T2_MAX_SLICES; i++) {
        if (!t2.slices[i].used) {
            memset(&t2.slices[i], 0, sizeof(network_slice_t));
            t2.slices[i].used = 1;
            t2.slices[i].id = i;
            if (name) strncpy(t2.slices[i].name, name, 63);
            t2.slices[i].sst = sst;
            t2.slices[i].sd = sd;
            t2.slices[i].dnn = dnn;
            t2.slice_count++;
            return i;
        }
    }
    return -1;
}

int telecom2_slice_delete(int slice_id)
{
    if (slice_id < 0 || slice_id >= T2_MAX_SLICES || !t2.slices[slice_id].used) return -1;
    t2.slices[slice_id].used = 0;
    t2.slice_count--;
    return 0;
}

int telecom2_slice_activate(int slice_id)
{
    if (slice_id < 0 || slice_id >= T2_MAX_SLICES || !t2.slices[slice_id].used) return -1;
    t2.slices[slice_id].active = 1;
    t2.slices[slice_id].connected = 1;
    return 0;
}

int telecom2_slice_deactivate(int slice_id)
{
    if (slice_id < 0 || slice_id >= T2_MAX_SLICES || !t2.slices[slice_id].used) return -1;
    t2.slices[slice_id].active = 0;
    t2.slices[slice_id].connected = 0;
    return 0;
}

int telecom2_slice_set_default(int slice_id)
{
    if (slice_id < 0 || slice_id >= T2_MAX_SLICES || !t2.slices[slice_id].used) return -1;
    for (int i = 0; i < T2_MAX_SLICES; i++) t2.slices[i].default_slice = 0;
    t2.slices[slice_id].default_slice = 1;
    return 0;
}

int telecom2_slice_list(network_slice_t *slices, int max)
{
    if (!slices || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < T2_MAX_SLICES && count < max; i++) {
        if (t2.slices[i].used) memcpy(&slices[count++], &t2.slices[i], sizeof(network_slice_t));
    }
    return count;
}

int telecom2_slice_get_stats(int slice_id, uint64_t *sent, uint64_t *received)
{
    if (slice_id < 0 || slice_id >= T2_MAX_SLICES || !t2.slices[slice_id].used) return -1;
    if (sent) *sent = t2.slices[slice_id].bytes_sent;
    if (received) *received = t2.slices[slice_id].bytes_received;
    return 0;
}

int telecom2_get_stats(uint64_t *data_sent, uint64_t *data_received, uint64_t *call_time, int *sms)
{
    if (data_sent) *data_sent = t2.total_data_sent;
    if (data_received) *data_received = t2.total_data_received;
    if (call_time) *call_time = t2.call_time_seconds;
    if (sms) *sms = t2.sms_sent + t2.sms_received;
    return 0;
}


