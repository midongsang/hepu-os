#ifndef TELECOM2_H
#define TELECOM2_H

#include <stdint.h>

#define T2_MAX_SIMS 4
#define T2_MAX_NETWORKS 16
#define T2_MAX_SLICES 8
#define T2_MAX_ESIM_PROFILES 8
#define T2_MAX_NFC_DEVICES 16
#define T2_MAX_PAYMENT_CARDS 8
#define T2_MAX_SATELLITE_CHANNELS 4

/* SIM卡状态 */
typedef enum {
    T2_SIM_STATE_ABSENT = 0,
    T2_SIM_STATE_PRESENT,
    T2_SIM_STATE_PIN_REQUIRED,
    T2_SIM_STATE_PUK_REQUIRED,
    T2_SIM_STATE_NETWORK_LOCKED,
    T2_SIM_STATE_READY,
    T2_SIM_STATE_IO_ERROR,
    T2_SIM_STATE_UNKNOWN,
} t2_sim_state_t;

/* SIM卡类型 */
typedef enum {
    SIM_TYPE_PHYSICAL = 0,
    SIM_TYPE_ESIM,
    SIM_TYPE_IUICC,
    SIM_TYPE_SOFT_SIM,
} sim_type_t;

/* SIM卡信息 */
typedef struct {
    int used;
    int id;
    int slot;
    sim_type_t type;
    t2_sim_state_t state;
    char iccid[32];
    char imsi[32];
    char msisdn[32];
    char operator_name[64];
    char country[8];
    int mcc, mnc;
    int active;
    int data_enabled;
    int voice_enabled;
    int sms_enabled;
    int roaming_enabled;
    int signal_strength;  /* 0-100 */
    int network_type;  /* 0=2G,1=3G,2=4G,3=5G,4=5G+ */
    int volte_enabled;
    int vowifi_enabled;
    int esim_profile_count;
} sim_info_t;

/* eSIM配置文件 */
typedef struct {
    int used;
    int id;
    char iccid[32];
    char operator_name[64];
    char country[8];
    int mcc, mnc;
    int active;
    int enabled;
    char profile_name[64];
    int profile_type;  /* 0=operational,1=testing,2=provisioning */
} esim_profile_t;

/* 网络类型 */
typedef enum {
    T2_NETWORK_TYPE_2G = 0,
    T2_NETWORK_TYPE_3G,
    T2_NETWORK_TYPE_4G,
    T2_NETWORK_TYPE_5G_NSA,
    T2_NETWORK_TYPE_5G_SA,
    T2_NETWORK_TYPE_5G_ADVANCED,
    T2_NETWORK_TYPE_6G,
} t2_network_type_t;

/* 网络频段 */
typedef struct {
    int band;
    int frequency;  /* MHz */
    int bandwidth;  /* MHz */
    int technology;  /* 0=LTE,1=NR */
    int duplex;  /* 0=FDD,1=TDD */
} network_band_t;

/* 5G网络切片 */
typedef struct {
    int used;
    int id;
    char name[64];
    int sst;  /* Slice/Service Type */
    int sd;  /* Slice Differentiator */
    int dnn;  /* Data Network Name */
    int priority;
    int default_slice;
    int active;
    int uplink_mbps;
    int downlink_mbps;
    int latency_ms;
    int reliability;  /* 0-100 */
    int connected;
    uint64_t bytes_sent;
    uint64_t bytes_received;
} network_slice_t;

/* VoLTE状态 */
typedef struct {
    int enabled;
    int registered;
    int call_state;  /* 0=idle,1=calling,2=ringing,3=active,4=held */
    int hd_audio;
    int video_call;
    int rtt;  /* 实时文本 */
    int emergency_support;
} volte_state_t;

/* VoWiFi状态 */
typedef struct {
    int enabled;
    int connected;
    int wifi_ssid[64];
    int signal_strength;
    int call_state;
} vowifi_state_t;

/* 卫星通信 */
typedef struct {
    int enabled;
    int connected;
    char provider[64];
    int satellite_id;
    int signal_strength;
    int elevation;  /* 度 */
    int azimuth;  /* 度 */
    int channels[T2_MAX_SATELLITE_CHANNELS];
    int channel_count;
    int data_rate_kbps;
    int latency_ms;
    int emergency_mode;
    int message_count;
} satellite_state_t;

/* NFC类型 */
typedef enum {
    NFC_TYPE_CARD = 0,
    NFC_TYPE_READER,
    NFC_TYPE_P2P,
    NFC_TYPE_HCE,  /* Host Card Emulation */
} nfc_type_t;

/* NFC设备 */
typedef struct {
    int used;
    int id;
    char name[64];
    nfc_type_t type;
    int connected;
    char uid[16];
    char tech[32];
    int max_transceive_length;
    int timeout;
} nfc_device_t;

/* 支付卡 */
typedef struct {
    int used;
    int id;
    char card_name[64];
    char card_number_last4[8];
    char card_network[16];  /* Visa/Mastercard/UnionPay/Amex */
    int card_type;  /* 0=credit,1=debit,2=prepaid,3=transit */
    int default_card;
    int active;
    int tokenized;
    char token[32];
    int expiry_month, expiry_year;
    int contactless_limit;
    int currency;
} payment_card_t;

/* 通信深化状态 */
typedef struct {
    int initialized;
    /* SIM */
    sim_info_t sims[T2_MAX_SIMS];
    int sim_count;
    int active_sim;
    int default_data_sim;
    int default_voice_sim;
    int default_sms_sim;
    /* eSIM */
    esim_profile_t esim_profiles[T2_MAX_ESIM_PROFILES];
    int esim_profile_count;
    /* 网络 */
    t2_network_type_t network_type;
    char operator_name[64];
    int signal_strength;
    int roaming;
    int data_enabled;
    int airplane_mode;
    /* 5G切片 */
    network_slice_t slices[T2_MAX_SLICES];
    int slice_count;
    /* VoLTE */
    volte_state_t volte;
    /* VoWiFi */
    vowifi_state_t vowifi;
    /* 卫星 */
    satellite_state_t satellite;
    /* NFC */
    int nfc_enabled;
    nfc_device_t nfc_devices[T2_MAX_NFC_DEVICES];
    int nfc_device_count;
    /* 支付 */
    payment_card_t payment_cards[T2_MAX_PAYMENT_CARDS];
    int payment_card_count;
    int nfc_payment_enabled;
    /* 统计 */
    uint64_t total_data_sent;
    uint64_t total_data_received;
    uint64_t call_time_seconds;
    int sms_sent;
    int sms_received;
} telecom2_t;

/* 初始化通信深化 */
void telecom2_init(void);

/* SIM管理 */
int telecom2_get_sim_count(void);
int telecom2_get_sim_info(int sim_id, sim_info_t *info);
int telecom2_list_sims(sim_info_t *sims, int max);
int telecom2_set_active_sim(int sim_id);
int telecom2_get_active_sim(void);
int telecom2_set_default_data_sim(int sim_id);
int telecom2_set_default_voice_sim(int sim_id);
int telecom2_set_default_sms_sim(int sim_id);
int telecom2_enable_data(int sim_id, int enable);
int telecom2_enable_roaming(int sim_id, int enable);
int telecom2_get_signal_strength(int sim_id);

/* eSIM管理 */
int telecom2_esim_download_profile(const char *activation_code, const char *confirmation_code);
int telecom2_esim_enable_profile(int profile_id);
int telecom2_esim_disable_profile(int profile_id);
int telecom2_esim_delete_profile(int profile_id);
int telecom2_esim_list_profiles(esim_profile_t *profiles, int max);
int telecom2_esim_get_active_profile(esim_profile_t *profile);

/* 网络管理 */
int telecom2_get_network_type(void);
int telecom2_set_preferred_network(int type);
int telecom2_get_operator_name(char *name, int max);
int telecom2_enable_airplane_mode(int enable);
int telecom2_is_airplane_mode(void);
int telecom2_get_roaming_status(void);

/* 5G切片 */
int telecom2_slice_create(const char *name, int sst, int sd, int dnn);
int telecom2_slice_delete(int slice_id);
int telecom2_slice_activate(int slice_id);
int telecom2_slice_deactivate(int slice_id);
int telecom2_slice_set_default(int slice_id);
int telecom2_slice_list(network_slice_t *slices, int max);
int telecom2_slice_get_stats(int slice_id, uint64_t *sent, uint64_t *received);

/* VoLTE */
int telecom2_volte_enable(int enable);
int telecom2_volte_is_enabled(void);
int telecom2_volte_get_state(volte_state_t *state);
int telecom2_volte_start_call(const char *number);
int telecom2_volte_end_call(void);
int telecom2_volte_answer_call(void);

/* VoWiFi */
int telecom2_vowifi_enable(int enable);
int telecom2_vowifi_is_enabled(void);
int telecom2_vowifi_get_state(vowifi_state_t *state);

/* 卫星通信 */
int telecom2_satellite_enable(int enable);
int telecom2_satellite_is_enabled(void);
int telecom2_satellite_connect(void);
int telecom2_satellite_disconnect(void);
int telecom2_satellite_get_state(satellite_state_t *state);
int telecom2_satellite_send_message(const char *message);
int telecom2_satellite_emergency_alert(double latitude, double longitude);

/* NFC */
int telecom2_nfc_enable(int enable);
int telecom2_nfc_is_enabled(void);
int telecom2_nfc_start_discovery(void);
int telecom2_nfc_stop_discovery(void);
int telecom2_nfc_list_devices(nfc_device_t *devices, int max);
int telecom2_nfc_connect(int device_id);
int telecom2_nfc_disconnect(int device_id);
int telecom2_nfc_transceive(int device_id, const uint8_t *send, int send_len, uint8_t *recv, int *recv_len);

/* 支付 */
int telecom2_payment_add_card(payment_card_t *card);
int telecom2_payment_remove_card(int card_id);
int telecom2_payment_set_default(int card_id);
int telecom2_payment_list_cards(payment_card_t *cards, int max);
int telecom2_payment_enable(int enable);
int telecom2_payment_is_enabled(void);
int telecom2_payment_start_transaction(int card_id, int amount, const char *currency);

/* 统计 */
int telecom2_get_stats(uint64_t *data_sent, uint64_t *data_received, uint64_t *call_time, int *sms);

#endif /* TELECOM2_H */
