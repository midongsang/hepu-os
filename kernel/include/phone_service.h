#ifndef PHONE_SERVICE_H
#define PHONE_SERVICE_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 电话服务
 * 拨号 / 通话管理 / 通讯录 / 通话记录
 * ========================================================================== */

#define PS_MAX_CONTACTS 256
#define PS_MAX_CALL_LOG 128
#define PS_MAX_CALLS 8
#define PS_PHONE_LEN 32
#define PS_NAME_LEN 64
#define PS_IMSI_LEN 16
#define PS_IMEI_LEN 16

/* 通话状态 */
typedef enum {
    PS_CALL_STATE_IDLE = 0,
    PS_CALL_STATE_DIALING,
    PS_CALL_STATE_RINGING,
    PS_CALL_STATE_ACTIVE,
    PS_CALL_STATE_ON_HOLD,
    PS_CALL_STATE_ENDED,
    PS_CALL_STATE_FAILED,
    PS_CALL_STATE_BUSY,
    PS_CALL_STATE_NO_ANSWER,
} ps_call_state_t;

/* 通话类型 */
typedef enum {
    PS_CALL_TYPE_VOICE = 0,
    PS_CALL_TYPE_VIDEO,
    PS_CALL_TYPE_VOLTE,
    PS_CALL_TYPE_WIFI,
    PS_CALL_TYPE_SATELLITE,
} ps_ps_call_type_t;

/* 通话记录 */
typedef struct {
    int used;
    int id;
    char number[PS_PHONE_LEN];
    char contact_name[PS_NAME_LEN];
    ps_ps_call_type_t type;
    ps_call_state_t state;
    uint64_t start_time;
    uint64_t duration;  /* 秒 */
    int outgoing;  /* 1=呼出, 0=呼入 */
    int missed;
} ps_call_log_entry_t;

/* 通讯录联系人 */
typedef struct {
    int used;
    int id;
    char name[PS_NAME_LEN];
    char number[PS_PHONE_LEN];
    char number2[PS_PHONE_LEN];
    char email[64];
    char organization[64];
    char note[128];
    int favorite;
    int blocked;
    uint64_t created;
    uint64_t last_contact;
} ps_contact_t;

/* 通话实例 */
typedef struct {
    int used;
    int id;
    char number[PS_PHONE_LEN];
    char contact_name[PS_NAME_LEN];
    ps_call_state_t state;
    ps_ps_call_type_t type;
    int outgoing;
    uint64_t start_time;
    uint64_t connect_time;
    uint64_t end_time;
    int muted;
    int speaker;
    int bluetooth;
    int recording;
} ps_call_t;

/* SIM卡信息 */
typedef struct {
    int present;
    int slot;
    char imsi[PS_IMSI_LEN];
    char imei[PS_IMEI_LEN];
    char operator_name[64];
    char country[8];
    int signal_strength;  /* 0-100 */
    int network_type;  /* 0=2G,1=3G,2=4G,3=5G */
    int roaming;
    int data_enabled;
} ps_sim_card_t;

/* 电话服务状态 */
typedef struct {
    int initialized;
    int enabled;
    int airplane_mode;
    /* SIM */
    ps_sim_card_t sims[2];
    int sim_count;
    int default_sim;
    /* 通话 */
    ps_call_t calls[PS_MAX_CALLS];
    int active_call;
    int call_count;
    /* 通讯录 */
    ps_contact_t contacts[PS_MAX_CONTACTS];
    int contact_count;
    /* 通话记录 */
    ps_call_log_entry_t call_log[PS_MAX_CALL_LOG];
    int call_log_count;
    /* 设置 */
    int voicemail_enabled;
    char voicemail_number[PS_PHONE_LEN];
    int call_waiting;
    int caller_id;
    int auto_record;
    /* 统计 */
    uint64_t total_calls;
    uint64_t total_outgoing;
    uint64_t total_incoming;
    uint64_t total_missed;
    uint64_t total_duration;
} ps_phone_service_t;

/* 初始化 */
void phone_service_init(void);

/* 通话管理 */
int phone_dial(const char *number, ps_ps_call_type_t type);
int phone_answer(int call_id);
int phone_hangup(int call_id);
int phone_reject(int call_id);
int phone_hold(int call_id);
int phone_resume(int call_id);
int phone_swap_calls(void);
int phone_merge_calls(void);
int phone_get_call(int call_id, ps_call_t *call);
int phone_list_calls(ps_call_t *calls, int max);
int phone_get_active_call(ps_call_t *call);

/* 通话控制 */
int phone_mute(int call_id, int mute);
int phone_speaker(int call_id, int on);
int phone_bluetooth(int call_id, int on);
int phone_record(int call_id, int start);
int phone_send_dtmf(int call_id, char digit);

/* 通讯录 */
int phone_contact_add(const char *name, const char *number);
int phone_contact_remove(int contact_id);
int phone_contact_update(int contact_id, const ps_contact_t *contact);
int phone_contact_get(int contact_id, ps_contact_t *contact);
int phone_contact_find_by_number(const char *number, ps_contact_t *contact);
int phone_contact_list(ps_contact_t *contacts, int max);
int phone_contact_set_favorite(int contact_id, int favorite);
int phone_contact_set_blocked(int contact_id, int blocked);
int phone_contact_count(void);

/* 通话记录 */
int phone_call_log_add(const char *number, const char *name, ps_ps_call_type_t type,
                       int outgoing, int missed, uint64_t duration);
int phone_call_log_get(int index, ps_call_log_entry_t *entry);
int phone_call_log_list(ps_call_log_entry_t *entries, int max);
int phone_call_log_clear(void);
int phone_call_log_count(void);

/* SIM卡 */
int phone_sim_get_info(int slot, ps_sim_card_t *sim);
int phone_sim_set_default(int slot);
int phone_sim_get_default(void);
int phone_get_signal_strength(int slot);
int phone_get_network_type(int slot);

/* 设置 */
int phone_set_airplane_mode(int on);
int phone_get_airplane_mode(void);
int phone_set_voicemail(const char *number);
int phone_set_call_waiting(int enable);
int phone_set_caller_id(int enable);
int phone_set_auto_record(int enable);

/* 统计 */
int phone_get_stats(ps_phone_service_t *stats);

#endif /* PHONE_SERVICE_H */
