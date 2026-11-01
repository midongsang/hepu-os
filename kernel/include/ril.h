#ifndef RIL_H
#define RIL_H

#include <stdint.h>

#define RIL_MAX_SMS 256
#define RIL_MAX_CONTACTS 1024
#define RIL_MAX_CALLS 8
#define RIL_PHONE_NUMBER_LEN 32
#define RIL_IMEI_LEN 16
#define RIL_IMSI_LEN 16

/* 网络类型 */
typedef enum {
    NETWORK_TYPE_UNKNOWN = 0,
    NETWORK_TYPE_GPRS,
    NETWORK_TYPE_EDGE,
    NETWORK_TYPE_UMTS,
    NETWORK_TYPE_HSDPA,
    NETWORK_TYPE_HSUPA,
    NETWORK_TYPE_HSPA,
    NETWORK_TYPE_LTE,
    NETWORK_TYPE_LTE_ADVANCED,
    NETWORK_TYPE_NR,  /* 5G */
} network_type_t;

/* SIM状态 */
typedef enum {
    SIM_STATE_UNKNOWN = 0,
    SIM_STATE_ABSENT,
    SIM_STATE_PIN_REQUIRED,
    SIM_STATE_PUK_REQUIRED,
    SIM_STATE_NETWORK_LOCKED,
    SIM_STATE_READY,
} sim_state_t;

/* 电话状态 */
typedef enum {
    CALL_STATE_IDLE = 0,
    CALL_STATE_RINGING,
    CALL_STATE_OFFHOOK,
    CALL_STATE_ACTIVE,
    CALL_STATE_HOLDING,
    CALL_STATE_DIALING,
    CALL_STATE_ALERTING,
    CALL_STATE_INCOMING,
    CALL_STATE_DISCONNECTED,
} call_state_t;

/* 数据连接状态 */
typedef enum {
    DATA_DISCONNECTED = 0,
    DATA_CONNECTING,
    DATA_CONNECTED,
    DATA_SUSPENDED,
} data_state_t;

/* 信号强度 */
typedef struct {
    int gsm_signal;       /* 0-31, 99=unknown */
    int gsm_bit_error;
    int cdma_dbm;
    int cdma_ecio;
    int evdo_dbm;
    int evdo_ecio;
    int lte_signal;
    int lte_rsrp;
    int lte_rsrq;
    int lte_rssnr;
    int nr_ssRsrp;
    int nr_ssRsrq;
    int nr_ssSinr;
    int level;           /* 0-4 */
} signal_strength_t;

/* 电话 */
typedef struct {
    int used;
    int id;
    call_state_t state;
    char number[RIL_PHONE_NUMBER_LEN];
    uint64_t start_time;
    uint64_t duration;
    int is_incoming;
    int is_muted;
    int is_speaker;
} ril_call_t;

/* 短信 */
typedef struct {
    int used;
    int id;
    char address[RIL_PHONE_NUMBER_LEN];
    char body[1024];
    uint64_t timestamp;
    int read;
    int is_incoming;
    int status;  /* 0=received, 1=sent, 2=failed */
} ril_sms_t;

/* 数据配置 */
typedef struct {
    char apn[64];
    char username[64];
    char password[64];
    int auth_type;
    int protocol;  /* 0=IP, 1=IPV6, 2=IPV4V6 */
    int roaming;
} ril_apn_t;

/* RIL设备 */
typedef struct {
    int initialized;
    int radio_on;
    sim_state_t sim_state;
    network_type_t network_type;
    data_state_t data_state;
    signal_strength_t signal;
    char imei[RIL_IMEI_LEN];
    char imsi[RIL_IMSI_LEN];
    char operator_name[64];
    char operator_numeric[8];
    int roaming;
    /* 电话 */
    ril_call_t calls[RIL_MAX_CALLS];
    int call_count;
    /* 短信 */
    ril_sms_t sms[RIL_MAX_SMS];
    int sms_count;
    /* 数据 */
    ril_apn_t apn;
    int data_enabled;
    /* 统计 */
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t total_call_time;
    int sms_sent;
    int sms_received;
} ril_device_t;

/* 初始化RIL */
void ril_init(void);

/* 无线电控制 */
int ril_set_radio_power(int on);
int ril_get_radio_power(void);

/* SIM */
sim_state_t ril_get_sim_state(void);
int ril_enter_sim_pin(const char *pin);
int ril_change_sim_pin(const char *old_pin, const char *new_pin);

/* 网络信息 */
network_type_t ril_get_network_type(void);
int ril_get_signal_strength(signal_strength_t *signal);
int ril_get_operator_name(char *buf, int len);
int ril_is_roaming(void);
int ril_get_imei(char *buf, int len);
int ril_get_imsi(char *buf, int len);

/* 电话 */
int ril_dial(const char *number);
int ril_answer(int call_id);
int ril_hangup(int call_id);
int ril_hangup_all(void);
int ril_set_mute(int mute);
int ril_get_mute(void);
int ril_set_speaker(int speaker);
int ril_get_calls(ril_call_t *calls, int max);
int ril_get_active_call(void);

/* 短信 */
int ril_send_sms(const char *address, const char *text);
int ril_delete_sms(int sms_id);
int ril_get_sms(ril_sms_t *sms_list, int max);
int ril_get_sms_count(void);
int ril_mark_sms_read(int sms_id);

/* 数据连接 */
int ril_set_data_enabled(int enable);
int ril_get_data_enabled(void);
data_state_t ril_get_data_state(void);
int ril_setup_data_call(const ril_apn_t *apn);
int ril_deactivate_data_call(void);
int ril_get_data_usage(uint64_t *sent, uint64_t *received);

/* APN管理 */
int ril_set_apn(const ril_apn_t *apn);
int ril_get_apn(ril_apn_t *apn);

/* 网络扫描 */
int ril_start_network_scan(void);
int ril_get_available_networks(char *buf, int len);

/* 获取RIL状态 */
int ril_get_status(ril_device_t *status);

/* RIL事件回调 */
typedef void (*ril_event_cb_t)(int event, void *data);
int ril_set_event_callback(ril_event_cb_t cb);

#endif /* RIL_H */
