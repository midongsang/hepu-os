/* ============================================================================
 * 赫普OS - IEEE 802.11 WiFi协议实现
 * V8.63: 初始实现
 * 帧解析、Beacon解析、信息元素处理、帧转换
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../include/ieee80211.h"
#include "../include/vga.h"

/* 获取帧类型 */
int ieee80211_get_frame_type(const uint8_t *frame, int len)
{
    if (!frame || len < 2) return -1;
    return (frame[0] >> 2) & 0x03;
}

/* 获取帧子类型 */
int ieee80211_get_frame_subtype(const uint8_t *frame, int len)
{
    if (!frame || len < 2) return -1;
    return (frame[0] >> 4) & 0x0F;
}

/* 查找信息元素 */
const ieee80211_ie_t *ieee80211_find_ie(const uint8_t *ies, int ies_len,
                                         uint8_t element_id)
{
    if (!ies || ies_len < 2) return NULL;

    int pos = 0;
    while (pos + 2 <= ies_len) {
        const ieee80211_ie_t *ie = (const ieee80211_ie_t *)&ies[pos];
        if (ie->length == 0 && ie->element_id == 0) break;
        if (ie->element_id == element_id) return ie;
        pos += 2 + ie->length;
        if (pos > ies_len) break;
    }
    return NULL;
}

/* 解析SSID信息元素 */
int ieee80211_parse_ssid_ie(const ieee80211_ie_t *ie, char *ssid, int max_len)
{
    if (!ie || !ssid || max_len <= 0) return -1;
    if (ie->element_id != IEEE80211_EID_SSID) return -1;

    int copy_len = ie->length;
    if (copy_len > max_len - 1) copy_len = max_len - 1;
    if (copy_len > 32) copy_len = 32;

    memcpy(ssid, ie->data, copy_len);
    ssid[copy_len] = '\0';
    return copy_len;
}

/* 解析DS参数信息元素 */
int ieee80211_parse_ds_params_ie(const ieee80211_ie_t *ie, int *channel)
{
    if (!ie || !channel) return -1;
    if (ie->element_id != IEEE80211_EID_DS_PARAMS) return -1;
    if (ie->length < 1) return -1;

    *channel = ie->data[0];
    return 0;
}

/* 解析RSN信息元素 */
int ieee80211_parse_rsn_ie(const ieee80211_ie_t *ie, int *akm, int *cipher)
{
    if (!ie) return -1;
    if (ie->element_id != IEEE80211_EID_RSN) return -1;
    if (ie->length < 6) return -1;

    const ieee80211_rsn_ie_t *rsn = (const ieee80211_rsn_ie_t *)ie;

    /* 检查版本 */
    if (rsn->version != 1) return -1;

    /* 解析AKM（认证和密钥管理） */
    if (akm) {
        /* AKM在pairwise_cipher_suites之后 */
        int offset = 6 + rsn->pairwise_cipher_count * 4;
        if (offset + 6 <= ie->length) {
            uint16_t akm_count = *(uint16_t *)&ie->data[offset];
            if (akm_count > 0) {
                uint32_t akm_suite = *(uint32_t *)&ie->data[offset + 2];
                /* OUI类型: 1=802.1X, 2=PSK */
                uint8_t akm_type = (akm_suite >> 24) & 0xFF;
                *akm = akm_type;
            }
        }
    }

    /* 解析组播密码套件 */
    if (cipher) {
        /* OUI类型: 2=TKIP, 4=CCMP(AES), 8=GCMP */
        uint8_t cipher_type = (rsn->group_cipher_suite >> 24) & 0xFF;
        *cipher = cipher_type;
    }

    return 0;
}

/* 解析WPA信息元素 */
int ieee80211_parse_wpa_ie(const ieee80211_ie_t *ie, int *akm, int *cipher)
{
    if (!ie) return -1;
    if (ie->element_id != IEEE80211_EID_VENDOR_SPECIFIC) return -1;
    if (ie->length < 8) return -1;

    /* 检查WPA OUI (00:50:F2:01) */
    if (ie->data[0] != 0x00 || ie->data[1] != 0x50 ||
        ie->data[2] != 0xF2 || ie->data[3] != 0x01) {
        return -1;
    }

    const ieee80211_wpa_ie_t *wpa = (const ieee80211_wpa_ie_t *)ie;

    if (akm) {
        int offset = 8 + wpa->pairwise_cipher_count * 4;
        if (offset + 6 <= ie->length) {
            uint16_t akm_count = *(uint16_t *)&ie->data[offset];
            if (akm_count > 0) {
                uint32_t akm_suite = *(uint32_t *)&ie->data[offset + 2];
                uint8_t akm_type = (akm_suite >> 24) & 0xFF;
                *akm = akm_type;
            }
        }
    }

    if (cipher) {
        uint8_t cipher_type = (wpa->group_cipher_suite >> 24) & 0xFF;
        *cipher = cipher_type;
    }

    return 0;
}

/* 解析Beacon帧 */
int ieee80211_parse_beacon(const uint8_t *frame, int len,
                           char *ssid, int ssid_max_len,
                           int *channel, int *signal_dbm,
                           int *security, int *wpa_version)
{
    if (!frame || len < 36) return -1;

    /* 检查是否是Beacon帧 */
    int ftype = ieee80211_get_frame_type(frame, len);
    int fsubtype = ieee80211_get_frame_subtype(frame, len);
    if (ftype != IEEE80211_FTYPE_MGMT || fsubtype != IEEE80211_STYPE_BEACON) {
        return -1;
    }

    /* MAC头24字节 + 固定参数12字节 = 36字节 */
    const ieee80211_mgmt_frame_t *mgmt = (const ieee80211_mgmt_frame_t *)frame;

    /* 初始化输出 */
    if (ssid && ssid_max_len > 0) ssid[0] = '\0';
    if (channel) *channel = 0;
    if (security) *security = 0;  /* 0=Open */
    if (wpa_version) *wpa_version = 0;

    /* 检查能力信息中的Privacy位 */
    int has_privacy = (mgmt->u.beacon.capability_info & IEEE80211_CAP_PRIVACY) ? 1 : 0;

    /* 信息元素从36字节开始 */
    int ies_offset = 36;
    int ies_len = len - ies_offset;
    if (ies_len <= 0) return 0;

    const uint8_t *ies = &frame[ies_offset];

    /* 解析SSID */
    const ieee80211_ie_t *ssid_ie = ieee80211_find_ie(ies, ies_len, IEEE80211_EID_SSID);
    if (ssid_ie && ssid && ssid_max_len > 0) {
        ieee80211_parse_ssid_ie(ssid_ie, ssid, ssid_max_len);
    }

    /* 解析信道（DS参数） */
    const ieee80211_ie_t *ds_ie = ieee80211_find_ie(ies, ies_len, IEEE80211_EID_DS_PARAMS);
    if (ds_ie && channel) {
        ieee80211_parse_ds_params_ie(ds_ie, channel);
    }

    /* 解析安全类型 */
    int akm = 0, cipher = 0;

    /* 检查RSN（WPA2/WPA3） */
    const ieee80211_ie_t *rsn_ie = ieee80211_find_ie(ies, ies_len, IEEE80211_EID_RSN);
    if (rsn_ie) {
        ieee80211_parse_rsn_ie(rsn_ie, &akm, &cipher);
        if (security) {
            if (akm == 2) *security = 3;  /* WPA2-PSK */
            else if (akm == 1) *security = 5;  /* WPA2-Enterprise */
            else *security = 3;  /* 默认WPA2 */
        }
        if (wpa_version) *wpa_version = 2;
    }

    /* 检查WPA（WPA1） */
    const ieee80211_ie_t *wpa_ie = ieee80211_find_ie(ies, ies_len, IEEE80211_EID_VENDOR_SPECIFIC);
    if (wpa_ie && !rsn_ie) {
        if (ieee80211_parse_wpa_ie(wpa_ie, &akm, &cipher) == 0) {
            if (security) {
                if (akm == 2) *security = 2;  /* WPA-PSK */
                else *security = 2;
            }
            if (wpa_version) *wpa_version = 1;
        }
    }

    /* 如果有Privacy位但没有WPA/RSN，可能是WEP */
    if (has_privacy && security && *security == 0) {
        *security = 1;  /* WEP */
    }

    return 0;
}

/* 802.11帧转以太网帧 */
int ieee80211_to_ethernet(const uint8_t *frame, int frame_len,
                           uint8_t *eth_frame, int eth_max_len)
{
    if (!frame || !eth_frame || frame_len < 30 || eth_max_len < 14) return -1;

    int ftype = ieee80211_get_frame_type(frame, frame_len);
    if (ftype != IEEE80211_FTYPE_DATA) return -1;

    const ieee80211_mac_header_t *hdr = (const ieee80211_mac_header_t *)frame;

    /* 确定地址顺序（ToDS/FromDS） */
    const uint8_t *dst, *src;
    if (hdr->frame_control.to_ds && !hdr->frame_control.from_ds) {
        dst = hdr->addr3;
        src = hdr->addr2;
    } else if (!hdr->frame_control.to_ds && hdr->frame_control.from_ds) {
        dst = hdr->addr1;
        src = hdr->addr3;
    } else if (hdr->frame_control.to_ds && hdr->frame_control.from_ds) {
        /* WDS 4地址格式 */
        if (frame_len < 36) return -1;
        const ieee80211_mac_header_4addr_t *hdr4 =
            (const ieee80211_mac_header_4addr_t *)frame;
        dst = hdr4->addr3;
        src = hdr4->addr4;
    } else {
        dst = hdr->addr1;
        src = hdr->addr2;
    }

    /* 数据从MAC头之后开始 */
    int data_offset = 24;
    if (hdr->frame_control.to_ds && hdr->frame_control.from_ds) {
        data_offset = 30;  /* 4地址头 */
    }

    /* 检查LLC头 */
    if (frame_len < data_offset + 8) return -1;
    const ieee80211_llc_header_t *llc =
        (const ieee80211_llc_header_t *)&frame[data_offset];

    /* 检查是否是SNAP封装 (AA:AA:03) */
    if (llc->dsap != 0xAA || llc->ssap != 0xAA || llc->control != 0x03) {
        return -1;
    }

    /* 构建以太网帧 */
    memcpy(eth_frame, dst, 6);
    memcpy(eth_frame + 6, src, 6);
    memcpy(eth_frame + 12, &llc->ether_type, 2);

    /* 复制数据 */
    int payload_offset = data_offset + 8;
    int payload_len = frame_len - payload_offset;
    if (payload_len > eth_max_len - 14) payload_len = eth_max_len - 14;
    if (payload_len > 0) {
        memcpy(eth_frame + 14, &frame[payload_offset], payload_len);
    }

    return 14 + payload_len;
}

/* 以太网帧转802.11帧 */
int ieee80211_from_ethernet(const uint8_t *eth_frame, int eth_len,
                             uint8_t *frame, int frame_max_len,
                             const uint8_t *bssid, const uint8_t *src,
                             const uint8_t *dst)
{
    if (!eth_frame || !frame || eth_len < 14 || frame_max_len < 32) return -1;
    if (!bssid || !src || !dst) return -1;

    ieee80211_mac_header_t *hdr = (ieee80211_mac_header_t *)frame;
    memset(hdr, 0, sizeof(ieee80211_mac_header_t));

    /* 设置帧控制：数据帧，FromDS=1 */
    hdr->frame_control.protocol_version = 0;
    hdr->frame_control.type = IEEE80211_FTYPE_DATA;
    hdr->frame_control.subtype = IEEE80211_STYPE_DATA;
    hdr->frame_control.from_ds = 1;

    /* 地址：addr1=dst, addr2=bssid, addr3=src */
    memcpy(hdr->addr1, dst, 6);
    memcpy(hdr->addr2, bssid, 6);
    memcpy(hdr->addr3, src, 6);

    hdr->duration = 0;
    hdr->seq_ctrl = 0;

    /* LLC头（SNAP封装） */
    ieee80211_llc_header_t *llc = (ieee80211_llc_header_t *)&frame[24];
    llc->dsap = 0xAA;
    llc->ssap = 0xAA;
    llc->control = 0x03;
    llc->org_code[0] = 0x00;
    llc->org_code[1] = 0x00;
    llc->org_code[2] = 0x00;
    memcpy(&llc->ether_type, &eth_frame[12], 2);

    /* 复制数据 */
    int payload_len = eth_len - 14;
    int frame_len = 32 + payload_len;
    if (frame_len > frame_max_len) {
        payload_len = frame_max_len - 32;
        frame_len = frame_max_len;
    }
    if (payload_len > 0) {
        memcpy(&frame[32], &eth_frame[14], payload_len);
    }

    return frame_len;
}

/* 信道转频率 */
int ieee80211_channel_to_freq(int channel)
{
    if (channel >= 1 && channel <= 13) {
        return 2407 + channel * 5;  /* 2.4GHz */
    }
    if (channel == 14) {
        return 2484;  /* 日本专用 */
    }
    if (channel >= 36 && channel <= 165) {
        return 5000 + channel * 5;  /* 5GHz */
    }
    return 0;
}

/* 频率转信道 */
int ieee80211_freq_to_channel(int freq)
{
    if (freq >= 2412 && freq <= 2472) {
        return (freq - 2407) / 5;
    }
    if (freq == 2484) return 14;
    if (freq >= 5180 && freq <= 5825) {
        return (freq - 5000) / 5;
    }
    return 0;
}

/* 获取信道频段 */
int ieee80211_get_channel_band(int channel)
{
    if (channel >= 1 && channel <= 14) return 0;  /* 2.4GHz */
    if (channel >= 36 && channel <= 165) return 1;  /* 5GHz */
    return -1;
}

/* 构建Probe Request帧 */
int ieee80211_build_probe_request(const char *ssid,
                                   const uint8_t *src_addr,
                                   uint8_t *frame, int frame_max_len)
{
    if (!frame || frame_max_len < 24 || !src_addr) return -1;

    ieee80211_mac_header_t *hdr = (ieee80211_mac_header_t *)frame;
    memset(hdr, 0, sizeof(ieee80211_mac_header_t));

    hdr->frame_control.protocol_version = 0;
    hdr->frame_control.type = IEEE80211_FTYPE_MGMT;
    hdr->frame_control.subtype = IEEE80211_STYPE_PROBE_REQ;

    /* 广播地址 */
    memset(hdr->addr1, 0xFF, 6);
    memcpy(hdr->addr2, src_addr, 6);
    memset(hdr->addr3, 0xFF, 6);

    hdr->duration = 0;
    hdr->seq_ctrl = 0;

    int pos = 24;

    /* SSID信息元素 */
    if (ssid && ssid[0]) {
        int ssid_len = 0;
        while (ssid[ssid_len] && ssid_len < 32) ssid_len++;
        if (pos + 2 + ssid_len <= frame_max_len) {
            frame[pos++] = IEEE80211_EID_SSID;
            frame[pos++] = ssid_len;
            memcpy(&frame[pos], ssid, ssid_len);
            pos += ssid_len;
        }
    } else {
        /* 空SSID（广播探测） */
        if (pos + 2 <= frame_max_len) {
            frame[pos++] = IEEE80211_EID_SSID;
            frame[pos++] = 0;
        }
    }

    /* 支持速率信息元素（简化：1,2,5.5,11 Mbps） */
    if (pos + 6 <= frame_max_len) {
        frame[pos++] = IEEE80211_EID_SUPP_RATES;
        frame[pos++] = 4;
        frame[pos++] = 0x82;  /* 1 Mbps */
        frame[pos++] = 0x84;  /* 2 Mbps */
        frame[pos++] = 0x8B;  /* 5.5 Mbps */
        frame[pos++] = 0x96;  /* 11 Mbps */
    }

    return pos;
}

/* 构建Authentication帧 */
int ieee80211_build_auth(const uint8_t *src, const uint8_t *dst,
                          uint16_t seq, uint16_t status,
                          uint8_t *frame, int frame_max_len)
{
    if (!frame || frame_max_len < 30 || !src || !dst) return -1;

    ieee80211_mgmt_frame_t *mgmt = (ieee80211_mgmt_frame_t *)frame;
    memset(mgmt, 0, sizeof(ieee80211_mac_header_t) + sizeof(ieee80211_auth_fixed_t));

    mgmt->mac_header.frame_control.protocol_version = 0;
    mgmt->mac_header.frame_control.type = IEEE80211_FTYPE_MGMT;
    mgmt->mac_header.frame_control.subtype = IEEE80211_STYPE_AUTH;

    memcpy(mgmt->mac_header.addr1, dst, 6);
    memcpy(mgmt->mac_header.addr2, src, 6);
    memcpy(mgmt->mac_header.addr3, dst, 6);

    mgmt->u.auth.algorithm_number = 0;  /* Open System */
    mgmt->u.auth.transaction_sequence = seq;
    mgmt->u.auth.status_code = status;

    return 30;
}

/* 构建Association Request帧 */
int ieee80211_build_assoc_request(const uint8_t *src, const uint8_t *bssid,
                                   const char *ssid, uint16_t capability,
                                   uint16_t listen_interval,
                                   uint8_t *frame, int frame_max_len)
{
    if (!frame || frame_max_len < 28 || !src || !bssid) return -1;

    ieee80211_mgmt_frame_t *mgmt = (ieee80211_mgmt_frame_t *)frame;
    memset(mgmt, 0, sizeof(ieee80211_mac_header_t) + sizeof(ieee80211_assoc_req_fixed_t));

    mgmt->mac_header.frame_control.protocol_version = 0;
    mgmt->mac_header.frame_control.type = IEEE80211_FTYPE_MGMT;
    mgmt->mac_header.frame_control.subtype = IEEE80211_STYPE_ASSOC_REQ;

    memcpy(mgmt->mac_header.addr1, bssid, 6);
    memcpy(mgmt->mac_header.addr2, src, 6);
    memcpy(mgmt->mac_header.addr3, bssid, 6);

    mgmt->u.assoc_req.capability_info = capability;
    mgmt->u.assoc_req.listen_interval = listen_interval;

    int pos = 28;

    /* SSID信息元素 */
    if (ssid && ssid[0]) {
        int ssid_len = 0;
        while (ssid[ssid_len] && ssid_len < 32) ssid_len++;
        if (pos + 2 + ssid_len <= frame_max_len) {
            frame[pos++] = IEEE80211_EID_SSID;
            frame[pos++] = ssid_len;
            memcpy(&frame[pos], ssid, ssid_len);
            pos += ssid_len;
        }
    }

    /* 支持速率 */
    if (pos + 6 <= frame_max_len) {
        frame[pos++] = IEEE80211_EID_SUPP_RATES;
        frame[pos++] = 4;
        frame[pos++] = 0x82;
        frame[pos++] = 0x84;
        frame[pos++] = 0x8B;
        frame[pos++] = 0x96;
    }

    return pos;
}

/* 构建Deauthentication帧 */
int ieee80211_build_deauth(const uint8_t *src, const uint8_t *dst,
                            uint16_t reason,
                            uint8_t *frame, int frame_max_len)
{
    if (!frame || frame_max_len < 26 || !src || !dst) return -1;

    ieee80211_mgmt_frame_t *mgmt = (ieee80211_mgmt_frame_t *)frame;
    memset(mgmt, 0, sizeof(ieee80211_mac_header_t) + sizeof(ieee80211_deauth_fixed_t));

    mgmt->mac_header.frame_control.protocol_version = 0;
    mgmt->mac_header.frame_control.type = IEEE80211_FTYPE_MGMT;
    mgmt->mac_header.frame_control.subtype = IEEE80211_STYPE_DEAUTH;

    memcpy(mgmt->mac_header.addr1, dst, 6);
    memcpy(mgmt->mac_header.addr2, src, 6);
    memcpy(mgmt->mac_header.addr3, dst, 6);

    mgmt->u.deauth.reason_code = reason;

    return 26;
}
