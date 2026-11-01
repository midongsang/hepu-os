/* ============================================================================
 * 赫普OS - 蓝牙深化
 * A2DP + AVRCP + HFP + GATT + BLE Mesh + 设备管理
 * ========================================================================== */

#include <stdint.h>
#include "bt2.h"
#include "vga.h"
#include "string.h"

bt2_t bt2;

static const char *codec_names[] = {
    "SBC", "AAC", "aptX", "aptX HD", "aptX Adaptive", "LDAC", "LC3", "Opus"
};

void bt2_init(void)
{
    memset(&bt2, 0, sizeof(bt2_t));
    bt2.initialized = 1;
    bt2.enabled = 0;
    bt2.discoverable = 0;
    bt2.scanning = 0;
    strncpy(bt2.local_name, "Hepu OS", 63);
    bt2.local_address[0] = 0x52;
    bt2.local_address[1] = 0x54;
    bt2.local_address[2] = 0x00;
    bt2.local_address[3] = 0x12;
    bt2.local_address[4] = 0x34;
    bt2.local_address[5] = 0x56;

    /* AVRCP默认状态 */
    bt2.avrcp.volume = 50;
    bt2.avrcp.max_volume = 127;
    bt2.avrcp.play_state = 0;

    /* HFP默认状态 */
    bt2.hfp.signal_strength = 80;
    bt2.hfp.battery_level = 100;
    bt2.hfp.speaker_volume = 50;
    bt2.hfp.microphone_volume = 50;

    vga_puts("[BT2] BT deep init done \n");
    vga_printf(" device name: %s, A2DP/AVRCP/HFP/GATT/Mesh ready \n", bt2.local_name);
}

int bt2_enable(int enable)
{
    bt2.enabled = enable ? 1 : 0;
    vga_printf("[BT2] BT %s\n", enable ? " enabled " : " disabled ");
    return 0;
}

int bt2_is_enabled(void) { return bt2.enabled; }

int bt2_set_discoverable(int enable, int timeout)
{
    bt2.discoverable = enable ? 1 : 0;
    bt2.discoverable_timeout = timeout;
    return 0;
}

int bt2_start_scan(void)
{
    bt2.scanning = 1;
    vga_puts("[BT2] start scan \n");
    return 0;
}

int bt2_stop_scan(void)
{
    bt2.scanning = 0;
    vga_puts("[BT2] stop scan \n");
    return 0;
}

int bt2_is_scanning(void) { return bt2.scanning; }

int bt2_get_device(uint8_t *address, bt2_device_t *device)
{
    if (!address || !device) return -1;
    for (int i = 0; i < BT2_MAX_DEVICES; i++) {
        if (bt2.devices[i].used && memcmp(bt2.devices[i].address, address, 6) == 0) {
            memcpy(device, &bt2.devices[i], sizeof(bt2_device_t));
            return 0;
        }
    }
    return -1;
}

int bt2_list_devices(bt2_device_t *devices, int max)
{
    if (!devices || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < BT2_MAX_DEVICES && count < max; i++) {
        if (bt2.devices[i].used) memcpy(&devices[count++], &bt2.devices[i], sizeof(bt2_device_t));
    }
    return count;
}

int bt2_pair(uint8_t *address)
{
    if (!address) return -1;
    bt2.pairing_attempts++;
    /* 查找或创建设备 */
    for (int i = 0; i < BT2_MAX_DEVICES; i++) {
        if (bt2.devices[i].used && memcmp(bt2.devices[i].address, address, 6) == 0) {
            bt2.devices[i].paired = 1;
            bt2.pairing_success++;
            vga_printf("[BT2] paired: %02x:%02x:%02x:%02x:%02x:%02x\n",
                       address[0], address[1], address[2], address[3], address[4], address[5]);
            return 0;
        }
    }
    /* 创建设备 */
    for (int i = 0; i < BT2_MAX_DEVICES; i++) {
        if (!bt2.devices[i].used) {
            memset(&bt2.devices[i], 0, sizeof(bt2_device_t));
            bt2.devices[i].used = 1;
            bt2.devices[i].id = i;
            memcpy(bt2.devices[i].address, address, 6);
            strncpy(bt2.devices[i].name, "Unknown Device", 127);
            bt2.devices[i].type = BT_DEVICE_UNKNOWN;
            bt2.devices[i].paired = 1;
            bt2.devices[i].rssi = -50;
            bt2.device_count++;
            bt2.pairing_success++;
            return 0;
        }
    }
    return -1;
}

int bt2_unpair(uint8_t *address)
{
    if (!address) return -1;
    for (int i = 0; i < BT2_MAX_DEVICES; i++) {
        if (bt2.devices[i].used && memcmp(bt2.devices[i].address, address, 6) == 0) {
            bt2.devices[i].paired = 0;
            bt2.devices[i].connected = 0;
            return 0;
        }
    }
    return -1;
}

int bt2_connect(uint8_t *address)
{
    if (!address) return -1;
    for (int i = 0; i < BT2_MAX_DEVICES; i++) {
        if (bt2.devices[i].used && memcmp(bt2.devices[i].address, address, 6) == 0) {
            bt2.devices[i].connected = 1;
            bt2.connections_total++;
            vga_printf("[BT2] connected: %s\n", bt2.devices[i].name);
            return 0;
        }
    }
    return -1;
}

int bt2_disconnect(uint8_t *address)
{
    if (!address) return -1;
    for (int i = 0; i < BT2_MAX_DEVICES; i++) {
        if (bt2.devices[i].used && memcmp(bt2.devices[i].address, address, 6) == 0) {
            bt2.devices[i].connected = 0;
            bt2.disconnections_total++;
            return 0;
        }
    }
    return -1;
}

int bt2_set_trusted(uint8_t *address, int trusted)
{
    if (!address) return -1;
    for (int i = 0; i < BT2_MAX_DEVICES; i++) {
        if (bt2.devices[i].used && memcmp(bt2.devices[i].address, address, 6) == 0) {
            bt2.devices[i].trusted = trusted ? 1 : 0;
            return 0;
        }
    }
    return -1;
}

int bt2_set_blocked(uint8_t *address, int blocked)
{
    if (!address) return -1;
    for (int i = 0; i < BT2_MAX_DEVICES; i++) {
        if (bt2.devices[i].used && memcmp(bt2.devices[i].address, address, 6) == 0) {
            bt2.devices[i].blocked = blocked ? 1 : 0;
            return 0;
        }
    }
    return -1;
}

int bt2_get_rssi(uint8_t *address)
{
    if (!address) return 0;
    for (int i = 0; i < BT2_MAX_DEVICES; i++) {
        if (bt2.devices[i].used && memcmp(bt2.devices[i].address, address, 6) == 0) {
            return bt2.devices[i].rssi;
        }
    }
    return 0;
}

int a2dp_connect(uint8_t *address)
{
    (void)address;
    bt2.a2dp.streaming = 0;
    bt2.a2dp.codec = BT_CODEC_SBC;
    bt2.a2dp.sample_rate = 44100;
    bt2.a2dp.channels = 2;
    vga_puts("[BT2] A2DP connected \n");
    return 0;
}

int a2dp_disconnect(uint8_t *address)
{
    (void)address;
    bt2.a2dp.streaming = 0;
    return 0;
}

int a2dp_start_streaming(uint8_t *address)
{
    (void)address;
    bt2.a2dp.streaming = 1;
    vga_printf("[BT2] A2DP start streaming(%s)\n", codec_names[bt2.a2dp.codec]);
    return 0;
}

int a2dp_suspend_streaming(uint8_t *address)
{
    (void)address;
    bt2.a2dp.streaming = 0;
    bt2.a2dp.suspended = 1;
    return 0;
}

int a2dp_set_codec(uint8_t *address, bt_codec_t codec)
{
    (void)address;
    bt2.a2dp.codec = codec;
    vga_printf("[BT2] A2DP codec: %s\n", codec_names[codec]);
    return 0;
}

int a2dp_get_state(uint8_t *address, a2dp_state_t *state)
{
    (void)address;
    if (!state) return -1;
    memcpy(state, &bt2.a2dp, sizeof(a2dp_state_t));
    return 0;
}

int a2dp_get_supported_codecs(uint8_t *address, bt_codec_t *codecs, int max)
{
    (void)address;
    if (!codecs || max <= 0) return 0;
    /* 原型阶段: 返回所有编解码器 */
    int count = (BT2_MAX_CODECS < max) ? BT2_MAX_CODECS : max;
    for (int i = 0; i < count; i++) codecs[i] = (bt_codec_t)i;
    return count;
}

int avrcp_connect(uint8_t *address)
{
    (void)address;
    bt2.avrcp.connected = 1;
    vga_puts("[BT2] AVRCP connected \n");
    return 0;
}

int avrcp_disconnect(uint8_t *address)
{
    (void)address;
    bt2.avrcp.connected = 0;
    return 0;
}

int avrcp_play(uint8_t *address) { (void)address; bt2.avrcp.play_state = 1; return 0; }
int avrcp_pause(uint8_t *address) { (void)address; bt2.avrcp.play_state = 2; return 0; }
int avrcp_stop(uint8_t *address) { (void)address; bt2.avrcp.play_state = 0; return 0; }
int avrcp_next(uint8_t *address) { (void)address; bt2.avrcp.track_number++; return 0; }
int avrcp_previous(uint8_t *address) { (void)address; if (bt2.avrcp.track_number > 0) bt2.avrcp.track_number--; return 0; }

int avrcp_set_volume(uint8_t *address, int volume)
{
    (void)address;
    if (volume < 0) volume = 0;
    if (volume > bt2.avrcp.max_volume) volume = bt2.avrcp.max_volume;
    bt2.avrcp.volume = volume;
    return 0;
}

int avrcp_get_volume(uint8_t *address) { (void)address; return bt2.avrcp.volume; }

int avrcp_get_state(uint8_t *address, avrcp_state_t *state)
{
    (void)address;
    if (!state) return -1;
    memcpy(state, &bt2.avrcp, sizeof(avrcp_state_t));
    return 0;
}

int avrcp_set_track_info(uint8_t *address, const char *title, const char *artist, const char *album, uint64_t duration)
{
    (void)address;
    if (title) strncpy(bt2.avrcp.track_title, title, 127);
    if (artist) strncpy(bt2.avrcp.track_artist, artist, 127);
    if (album) strncpy(bt2.avrcp.track_album, album, 127);
    bt2.avrcp.track_duration = duration;
    return 0;
}

int hfp_connect(uint8_t *address)
{
    (void)address;
    bt2.hfp.connected = 1;
    vga_puts("[BT2] HFP connected \n");
    return 0;
}

int hfp_disconnect(uint8_t *address)
{
    (void)address;
    bt2.hfp.connected = 0;
    return 0;
}

int hfp_answer_call(uint8_t *address) { (void)address; bt2.hfp.call_active = 1; bt2.hfp.call_setup = 0; return 0; }
int hfp_reject_call(uint8_t *address) { (void)address; bt2.hfp.call_setup = 0; return 0; }
int hfp_hangup_call(uint8_t *address) { (void)address; bt2.hfp.call_active = 0; return 0; }

int hfp_dial_number(uint8_t *address, const char *number)
{
    (void)address;
    if (number) strncpy(bt2.hfp.phone_number, number, 31);
    bt2.hfp.call_setup = 2;
    return 0;
}

int hfp_voice_dial(uint8_t *address)
{
    (void)address;
    bt2.hfp.voice_recognition = 1;
    return 0;
}

int hfp_set_speaker_volume(uint8_t *address, int volume)
{
    (void)address;
    if (volume < 0) volume = 0;
    if (volume > 15) volume = 15;
    bt2.hfp.speaker_volume = volume;
    return 0;
}

int hfp_set_microphone_volume(uint8_t *address, int volume)
{
    (void)address;
    if (volume < 0) volume = 0;
    if (volume > 15) volume = 15;
    bt2.hfp.microphone_volume = volume;
    return 0;
}

int hfp_get_state(uint8_t *address, hfp_state_t *state)
{
    (void)address;
    if (!state) return -1;
    memcpy(state, &bt2.hfp, sizeof(hfp_state_t));
    return 0;
}

int bt2_get_stats(uint64_t *connections, uint64_t *bytes_sent, uint64_t *bytes_recv)
{
    if (connections) *connections = bt2.connections_total;
    if (bytes_sent) *bytes_sent = bt2.bytes_sent;
    if (bytes_recv) *bytes_recv = bt2.bytes_received;
    return 0;
}

int bt2_get_connected_count(void)
{
    int count = 0;
    for (int i = 0; i < BT2_MAX_DEVICES; i++) {
        if (bt2.devices[i].used && bt2.devices[i].connected) count++;
    }
    return count;
}

