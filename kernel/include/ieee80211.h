/* ============================================================================
 * 赫普OS - IEEE 802.11 WiFi协议定义
 * V8.63: 初始实现
 * 802.11帧结构、管理帧、信息元素
 * ========================================================================== */

#ifndef _IEEE80211_H
#define _IEEE80211_H

#include <stdint.h>

/* 802.11帧类型 */
#define IEEE80211_FTYPE_MGMT  0x00
#define IEEE80211_FTYPE_CTRL  0x01
#define IEEE80211_FTYPE_DATA  0x02

/* 管理帧子类型 */
#define IEEE80211_STYPE_ASSOC_REQ    0x00
#define IEEE80211_STYPE_ASSOC_RESP   0x01
#define IEEE80211_STYPE_REASSOC_REQ  0x02
#define IEEE80211_STYPE_REASSOC_RESP 0x03
#define IEEE80211_STYPE_PROBE_REQ    0x04
#define IEEE80211_STYPE_PROBE_RESP   0x05
#define IEEE80211_STYPE_BEACON       0x08
#define IEEE80211_STYPE_ATIM         0x09
#define IEEE80211_STYPE_DISASSOC     0x0A
#define IEEE80211_STYPE_AUTH         0x0B
#define IEEE80211_STYPE_DEAUTH       0x0C
#define IEEE80211_STYPE_ACTION       0x0D

/* 控制帧子类型 */
#define IEEE80211_STYPE_PSPOLL       0x0A
#define IEEE80211_STYPE_RTS          0x0B
#define IEEE80211_STYPE_CTS          0x0C
#define IEEE80211_STYPE_ACK          0x0D
#define IEEE80211_STYPE_CFEND        0x0E
#define IEEE80211_STYPE_CFENDACK     0x0F

/* 数据帧子类型 */
#define IEEE80211_STYPE_DATA         0x00
#define IEEE80211_STYPE_DATA_CFACK   0x01
#define IEEE80211_STYPE_DATA_CFPOLL  0x02
#define IEEE80211_STYPE_DATA_CFACKPOLL 0x03
#define IEEE80211_STYPE_NULLFUNC     0x04
#define IEEE80211_STYPE_CFACK        0x05
#define IEEE80211_STYPE_CFPOLL       0x06
#define IEEE80211_STYPE_CFACKPOLL    0x07
#define IEEE80211_STYPE_QOS_DATA     0x08

/* 信息元素ID */
#define IEEE80211_EID_SSID              0
#define IEEE80211_EID_SUPP_RATES        1
#define IEEE80211_EID_FH_PARAMS         2
#define IEEE80211_EID_DS_PARAMS         3
#define IEEE80211_EID_CF_PARAMS         4
#define IEEE80211_EID_TIM               5
#define IEEE80211_EID_IBSS_PARAMS       6
#define IEEE80211_EID_COUNTRY           7
#define IEEE80211_EID_REQUEST           10
#define IEEE80211_EID_BSS_LOAD          11
#define IEEE80211_EID_EDCA_PARAM_SET    12
#define IEEE80211_EID_TSPEC             13
#define IEEE80211_EID_TCLAS             14
#define IEEE80211_EID_SCHEDULE          15
#define IEEE80211_EID_CHALLENGE_TEXT    16
#define IEEE80211_EID_PWR_CONSTRAINT    32
#define IEEE80211_EID_PWR_CAPABILITY    33
#define IEEE80211_EID_TPC_REQUEST       34
#define IEEE80211_EID_TPC_REPORT        35
#define IEEE80211_EID_SUPPORTED_CHANNELS 36
#define IEEE80211_EID_CHANNEL_SWITCH    37
#define IEEE80211_EID_MEASURE_REQUEST   38
#define IEEE80211_EID_MEASURE_REPORT    39
#define IEEE80211_EID_QUIET             40
#define IEEE80211_EID_IBSS_DFS          41
#define IEEE80211_EID_ERP_INFO          42
#define IEEE80211_EID_TS_DELAY          43
#define IEEE80211_EID_TCLAS_PROCESSING  44
#define IEEE80211_EID_HT_CAPABILITY     45
#define IEEE80211_EID_QOS_CAPABILITY    46
#define IEEE80211_EID_RSN               48
#define IEEE80211_EID_EXT_SUPP_RATES    50
#define IEEE80211_EID_NEIGHBOR_REPORT   52
#define IEEE80211_EID_RRM_ENABLED       70
#define IEEE80211_EID_MOBILITY_DOMAIN   54
#define IEEE80211_EID_FAST_BSS_TRANSITION 55
#define IEEE80211_EID_TIMEOUT_INTERVAL  56
#define IEEE80211_EID_RIC_DESCRIPTOR    75
#define IEEE80211_EID_MDE               77
#define IEEE80211_EID_FTE               78
#define IEEE80211_EID_VENDOR_SPECIFIC   221

/* 能力信息字段位 */
#define IEEE80211_CAP_ESS               0x0001
#define IEEE80211_CAP_IBSS              0x0002
#define IEEE80211_CAP_POLLABLE          0x0004
#define IEEE80211_CAP_POLL_REQUEST      0x0008
#define IEEE80211_CAP_PRIVACY           0x0010
#define IEEE80211_CAP_SHORT_PREAMBLE    0x0020
#define IEEE80211_CAP_PBCC              0x0040
#define IEEE80211_CAP_CHANNEL_AGILITY   0x0080
#define IEEE80211_CAP_SPECTRUM_MGMT     0x0100
#define IEEE80211_CAP_QOS               0x0200
#define IEEE80211_CAP_SHORT_SLOT_TIME   0x0400
#define IEEE80211_CAP_APSD              0x0800
#define IEEE80211_CAP_RADIO_MEASUREMENT 0x1000
#define IEEE80211_CAP_DSSS_OFDM         0x2000
#define IEEE80211_CAP_DELAYED_BLOCK_ACK 0x4000
#define IEEE80211_CAP_IMM_BLOCK_ACK     0x8000

/* 认证算法编号 */
#define IEEE80211_AUTH_ALGO_OPEN        0
#define IEEE80211_AUTH_ALGO_SHARED_KEY  1
#define IEEE80211_AUTH_ALGO_FT          2
#define IEEE80211_AUTH_ALGO_SAE         3

/* 状态码 */
#define IEEE80211_STATUS_SUCCESS        0
#define IEEE80211_STATUS_FAILURE        1
#define IEEE80211_STATUS_CAPABILITIES   10
#define IEEE80211_STATUS_REASSOC_NO_ASSOC 11
#define IEEE80211_STATUS_ASSOC_DENIED   12
#define IEEE80211_STATUS_AUTH_ALGO      13
#define IEEE80211_STATUS_AUTH_SEQ       14
#define IEEE80211_STATUS_AUTH_CHALLENGE 15
#define IEEE80211_STATUS_AUTH_TIMEOUT   16
#define IEEE80211_STATUS_ASSOC_FULL     17
#define IEEE80211_STATUS_RATES          18
#define IEEE80211_STATUS_PREAMBLE       19
#define IEEE80211_STATUS_SPECTRUM       22
#define IEEE80211_STATUS_POWER_CAP      23
#define IEEE80211_STATUS_CHANNEL        24
#define IEEE80211_STATUS_NO_SHORT_SLOT  25
#define IEEE80211_STATUS_NO_HT          27
#define IEEE80211_STATUS_ROBUST_MGMT    29

/* 原因码 */
#define IEEE80211_REASON_UNSPECIFIED    1
#define IEEE80211_REASON_AUTH_EXPIRE    2
#define IEEE80211_REASON_AUTH_LEAVE     3
#define IEEE80211_REASON_INACTIVITY     4
#define IEEE80211_REASON_NO_RESOURCES   5
#define IEEE80211_REASON_INVALID_CLASS2 6
#define IEEE80211_REASON_INVALID_CLASS3 7
#define IEEE80211_REASON_DISASSOC_LEAVE 8
#define IEEE80211_REASON_NOT_AUTHED     9
#define IEEE80211_REASON_POWER_CAP      10
#define IEEE80211_REASON_CHANNEL        11
#define IEEE80211_REASON_INVALID_IE     13
#define IEEE80211_REASON_MIC_FAILURE    14
#define IEEE80211_REASON_4WAY_TIMEOUT   15
#define IEEE80211_REASON_GROUPKEY_TIMEOUT 16
#define IEEE80211_REASON_IE_DIFFERENT   17
#define IEEE80211_REASON_INVALID_GROUP_CIPHER 18
#define IEEE80211_REASON_INVALID_PAIRWISE_CIPHER 19
#define IEEE80211_REASON_INVALID_AKMP   20
#define IEEE80211_REASON_UNSUPPORTED_RSN 21
#define IEEE80211_REASON_INVALID_RSN    22
#define IEEE80211_REASON_8021X_AUTH    23
#define IEEE80211_REASON_CIPHER_REJECT  24
#define IEEE80211_REASON_PEER_NOT_READY 25
#define IEEE80211_REASON_POLICY         39

/* 802.11 MAC地址长度 */
#define IEEE80211_ADDR_LEN 6

/* 802.11帧控制字段 */
typedef struct {
    uint8_t  protocol_version : 2;
    uint8_t  type             : 2;
    uint8_t  subtype          : 4;
    uint8_t  to_ds            : 1;
    uint8_t  from_ds          : 1;
    uint8_t  more_frag        : 1;
    uint8_t  retry            : 1;
    uint8_t  pwr_mgt          : 1;
    uint8_t  more_data        : 1;
    uint8_t  protected_frame  : 1;
    uint8_t  order            : 1;
} __attribute__((packed)) ieee80211_frame_control_t;

/* 802.11 MAC头（3地址格式） */
typedef struct {
    ieee80211_frame_control_t frame_control;
    uint16_t duration;
    uint8_t  addr1[IEEE80211_ADDR_LEN];
    uint8_t  addr2[IEEE80211_ADDR_LEN];
    uint8_t  addr3[IEEE80211_ADDR_LEN];
    uint16_t seq_ctrl;
} __attribute__((packed)) ieee80211_mac_header_t;

/* 802.11 MAC头（4地址格式，WDS） */
typedef struct {
    ieee80211_frame_control_t frame_control;
    uint16_t duration;
    uint8_t  addr1[IEEE80211_ADDR_LEN];
    uint8_t  addr2[IEEE80211_ADDR_LEN];
    uint8_t  addr3[IEEE80211_ADDR_LEN];
    uint16_t seq_ctrl;
    uint8_t  addr4[IEEE80211_ADDR_LEN];
} __attribute__((packed)) ieee80211_mac_header_4addr_t;

/* Beacon帧固定参数 */
typedef struct {
    uint8_t  timestamp[8];
    uint16_t beacon_interval;
    uint16_t capability_info;
} __attribute__((packed)) ieee80211_beacon_fixed_t;

/* 关联请求固定参数 */
typedef struct {
    uint16_t capability_info;
    uint16_t listen_interval;
} __attribute__((packed)) ieee80211_assoc_req_fixed_t;

/* 关联响应固定参数 */
typedef struct {
    uint16_t capability_info;
    uint16_t status_code;
    uint16_t association_id;
} __attribute__((packed)) ieee80211_assoc_resp_fixed_t;

/* 认证帧固定参数 */
typedef struct {
    uint16_t algorithm_number;
    uint16_t transaction_sequence;
    uint16_t status_code;
} __attribute__((packed)) ieee80211_auth_fixed_t;

/* 解除认证/解除关联固定参数 */
typedef struct {
    uint16_t reason_code;
} __attribute__((packed)) ieee80211_deauth_fixed_t;

/* 信息元素 */
typedef struct {
    uint8_t element_id;
    uint8_t length;
    uint8_t data[];
} __attribute__((packed)) ieee80211_ie_t;

/* RSN信息元素 */
typedef struct {
    uint8_t  element_id;
    uint8_t  length;
    uint16_t version;
    uint32_t group_cipher_suite;
    uint16_t pairwise_cipher_count;
    uint32_t pairwise_cipher_suites[];
} __attribute__((packed)) ieee80211_rsn_ie_t;

/* 供应商特定信息元素（WPA） */
typedef struct {
    uint8_t  element_id;
    uint8_t  length;
    uint8_t  oui[3];
    uint8_t  oui_type;
    uint16_t version;
    uint32_t group_cipher_suite;
    uint16_t pairwise_cipher_count;
    uint32_t pairwise_cipher_suites[];
} __attribute__((packed)) ieee80211_wpa_ie_t;

/* HT能力信息元素 */
typedef struct {
    uint8_t  element_id;
    uint8_t  length;
    uint16_t ht_capability_info;
    uint8_t  ampdu_params;
    uint8_t  supported_mcs_set[16];
    uint16_t ht_ext_capabilities;
    uint32_t tx_beamforming_capabilities;
    uint8_t  asel_capabilities;
} __attribute__((packed)) ieee80211_ht_cap_ie_t;

/* 802.11帧（管理帧） */
typedef struct {
    ieee80211_mac_header_t mac_header;
    union {
        ieee80211_beacon_fixed_t beacon;
        ieee80211_assoc_req_fixed_t assoc_req;
        ieee80211_assoc_resp_fixed_t assoc_resp;
        ieee80211_auth_fixed_t auth;
        ieee80211_deauth_fixed_t deauth;
    } u;
    uint8_t data[256];  /* 信息元素和数据 */
} __attribute__((packed)) ieee80211_mgmt_frame_t;

/* 802.11数据帧（LLC封装） */
typedef struct {
    uint8_t  dsap;
    uint8_t  ssap;
    uint8_t  control;
    uint8_t  org_code[3];
    uint16_t ether_type;
} __attribute__((packed)) ieee80211_llc_header_t;

/* WiFi信道频率表 */
#define IEEE80211_NUM_2GHZ_CHANNELS 14
#define IEEE80211_NUM_5GHZ_CHANNELS 25

/* 函数声明 */

/* 获取帧类型 */
int ieee80211_get_frame_type(const uint8_t *frame, int len);

/* 获取帧子类型 */
int ieee80211_get_frame_subtype(const uint8_t *frame, int len);

/* 解析Beacon帧 */
int ieee80211_parse_beacon(const uint8_t *frame, int len,
                           char *ssid, int ssid_max_len,
                           int *channel, int *signal_dbm,
                           int *security, int *wpa_version);

/* 查找信息元素 */
const ieee80211_ie_t *ieee80211_find_ie(const uint8_t *ies, int ies_len,
                                         uint8_t element_id);

/* 解析SSID信息元素 */
int ieee80211_parse_ssid_ie(const ieee80211_ie_t *ie, char *ssid, int max_len);

/* 解析DS参数信息元素（获取信道） */
int ieee80211_parse_ds_params_ie(const ieee80211_ie_t *ie, int *channel);

/* 解析RSN信息元素（WPA2/WPA3） */
int ieee80211_parse_rsn_ie(const ieee80211_ie_t *ie, int *akm, int *cipher);

/* 解析WPA信息元素（WPA1） */
int ieee80211_parse_wpa_ie(const ieee80211_ie_t *ie, int *akm, int *cipher);

/* 802.11帧转以太网帧 */
int ieee80211_to_ethernet(const uint8_t *frame, int frame_len,
                           uint8_t *eth_frame, int eth_max_len);

/* 以太网帧转802.11帧 */
int ieee80211_from_ethernet(const uint8_t *eth_frame, int eth_len,
                             uint8_t *frame, int frame_max_len,
                             const uint8_t *bssid, const uint8_t *src,
                             const uint8_t *dst);

/* 信道转频率（MHz） */
int ieee80211_channel_to_freq(int channel);

/* 频率转信道 */
int ieee80211_freq_to_channel(int freq);

/* 获取信道频段 */
int ieee80211_get_channel_band(int channel);

/* 构建Probe Request帧 */
int ieee80211_build_probe_request(const char *ssid,
                                   const uint8_t *src_addr,
                                   uint8_t *frame, int frame_max_len);

/* 构建Authentication帧（Open System） */
int ieee80211_build_auth(const uint8_t *src, const uint8_t *dst,
                          uint16_t seq, uint16_t status,
                          uint8_t *frame, int frame_max_len);

/* 构建Association Request帧 */
int ieee80211_build_assoc_request(const uint8_t *src, const uint8_t *bssid,
                                   const char *ssid, uint16_t capability,
                                   uint16_t listen_interval,
                                   uint8_t *frame, int frame_max_len);

/* 构建Deauthentication帧 */
int ieee80211_build_deauth(const uint8_t *src, const uint8_t *dst,
                            uint16_t reason,
                            uint8_t *frame, int frame_max_len);

#endif /* _IEEE80211_H */
