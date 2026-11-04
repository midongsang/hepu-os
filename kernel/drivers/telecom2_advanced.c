/* ============================================================================
 * 赫普OS - 电信高级功能（从telecom2.c拆分）
 * VoLTE/VoWiFi/卫星/NFC/支付
 * ========================================================================== */

#include <stdint.h>
#include "telecom2.h"
#include "vga.h"
#include "string.h"

extern telecom2_t t2;

int telecom2_volte_enable(int enable)
{
    t2.volte.enabled = enable ? 1 : 0;
    vga_printf("[TELECOM2] VoLTE %s\n", enable ? " enabled " : " disabled ");
    return 0;
}

int telecom2_volte_is_enabled(void) { return t2.volte.enabled; }

int telecom2_volte_get_state(volte_state_t *state)
{
    if (!state) return -1;
    memcpy(state, &t2.volte, sizeof(volte_state_t));
    return 0;
}

int telecom2_volte_start_call(const char *number)
{
    (void)number;
    t2.volte.call_state = 1;
    return 0;
}

int telecom2_volte_end_call(void)
{
    t2.volte.call_state = 0;
    return 0;
}

int telecom2_volte_answer_call(void)
{
    t2.volte.call_state = 3;
    return 0;
}

int telecom2_vowifi_enable(int enable)
{
    t2.vowifi.enabled = enable ? 1 : 0;
    return 0;
}

int telecom2_vowifi_is_enabled(void) { return t2.vowifi.enabled; }

int telecom2_vowifi_get_state(vowifi_state_t *state)
{
    if (!state) return -1;
    memcpy(state, &t2.vowifi, sizeof(vowifi_state_t));
    return 0;
}

int telecom2_satellite_enable(int enable)
{
    t2.satellite.enabled = enable ? 1 : 0;
    vga_printf("[TELECOM2] satellite comm %s\n", enable ? " enabled " : " disabled ");
    return 0;
}

int telecom2_satellite_is_enabled(void) { return t2.satellite.enabled; }

int telecom2_satellite_connect(void)
{
    t2.satellite.connected = 1;
    t2.satellite.signal_strength = 60;
    t2.satellite.data_rate_kbps = 256;
    t2.satellite.latency_ms = 600;
    vga_puts("[TELECOM2] satellite connected \n");
    return 0;
}

int telecom2_satellite_disconnect(void)
{
    t2.satellite.connected = 0;
    return 0;
}

int telecom2_satellite_get_state(satellite_state_t *state)
{
    if (!state) return -1;
    memcpy(state, &t2.satellite, sizeof(satellite_state_t));
    return 0;
}

int telecom2_satellite_send_message(const char *message)
{
    (void)message;
    if (!t2.satellite.connected) return -1;
    t2.satellite.message_count++;
    return 0;
}

int telecom2_satellite_emergency_alert(double latitude, double longitude)
{
    (void)latitude; (void)longitude;
    t2.satellite.emergency_mode = 1;
    vga_puts("[TELECOM2] satellite SOS send \n");
    return 0;
}

int telecom2_nfc_enable(int enable)
{
    t2.nfc_enabled = enable ? 1 : 0;
    vga_printf("[TELECOM2] NFC %s\n", enable ? " enabled " : " disabled ");
    return 0;
}

int telecom2_nfc_is_enabled(void) { return t2.nfc_enabled; }

int telecom2_nfc_start_discovery(void) { return 0; }
int telecom2_nfc_stop_discovery(void) { return 0; }

int telecom2_nfc_list_devices(nfc_device_t *devices, int max)
{
    if (!devices || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < T2_MAX_NFC_DEVICES && count < max; i++) {
        if (t2.nfc_devices[i].used) memcpy(&devices[count++], &t2.nfc_devices[i], sizeof(nfc_device_t));
    }
    return count;
}

int telecom2_nfc_connect(int device_id)
{
    if (device_id < 0 || device_id >= T2_MAX_NFC_DEVICES || !t2.nfc_devices[device_id].used) return -1;
    t2.nfc_devices[device_id].connected = 1;
    return 0;
}

int telecom2_nfc_disconnect(int device_id)
{
    if (device_id < 0 || device_id >= T2_MAX_NFC_DEVICES || !t2.nfc_devices[device_id].used) return -1;
    t2.nfc_devices[device_id].connected = 0;
    return 0;
}

int telecom2_nfc_transceive(int device_id, const uint8_t *send, int send_len, uint8_t *recv, int *recv_len)
{
    (void)send; (void)send_len; (void)recv;
    if (device_id < 0 || device_id >= T2_MAX_NFC_DEVICES || !t2.nfc_devices[device_id].used) return -1;
    if (recv_len) *recv_len = 0;
    return 0;
}

int telecom2_payment_add_card(payment_card_t *card)
{
    if (!card) return -1;
    for (int i = 0; i < T2_MAX_PAYMENT_CARDS; i++) {
        if (!t2.payment_cards[i].used) {
            memcpy(&t2.payment_cards[i], card, sizeof(payment_card_t));
            t2.payment_cards[i].used = 1;
            t2.payment_cards[i].id = i;
            t2.payment_card_count++;
            return i;
        }
    }
    return -1;
}

int telecom2_payment_remove_card(int card_id)
{
    if (card_id < 0 || card_id >= T2_MAX_PAYMENT_CARDS || !t2.payment_cards[card_id].used) return -1;
    t2.payment_cards[card_id].used = 0;
    t2.payment_card_count--;
    return 0;
}

int telecom2_payment_set_default(int card_id)
{
    if (card_id < 0 || card_id >= T2_MAX_PAYMENT_CARDS || !t2.payment_cards[card_id].used) return -1;
    for (int i = 0; i < T2_MAX_PAYMENT_CARDS; i++) t2.payment_cards[i].default_card = 0;
    t2.payment_cards[card_id].default_card = 1;
    return 0;
}

int telecom2_payment_list_cards(payment_card_t *cards, int max)
{
    if (!cards || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < T2_MAX_PAYMENT_CARDS && count < max; i++) {
        if (t2.payment_cards[i].used) memcpy(&cards[count++], &t2.payment_cards[i], sizeof(payment_card_t));
    }
    return count;
}

int telecom2_payment_enable(int enable)
{
    t2.nfc_payment_enabled = enable ? 1 : 0;
    return 0;
}

int telecom2_payment_is_enabled(void) { return t2.nfc_payment_enabled; }

int telecom2_payment_start_transaction(int card_id, int amount, const char *currency)
{
    (void)amount; (void)currency;
    if (card_id < 0 || card_id >= T2_MAX_PAYMENT_CARDS || !t2.payment_cards[card_id].used) return -1;
    vga_printf("[TELECOM2] payment: %s, amount: %d\n", t2.payment_cards[card_id].card_name, amount);
    return 0;
}

