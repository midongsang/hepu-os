#ifndef SMS_SERVICE_H
#define SMS_SERVICE_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 短信服务
 * 收发 / 存储 / 会话 / 彩信
 * ========================================================================== */

#define SMS_MAX_MESSAGES 512
#define SMS_MAX_CONVERSATIONS 128
#define SMS_MAX_TEXT_LEN 160
#define SMS_MAX_MMS_SIZE 1024 * 1024
#define SMS_PHONE_LEN 32
#define SMS_NAME_LEN 64

/* 短信状态 */
typedef enum {
    SMS_STATE_DRAFT = 0,
    SMS_STATE_QUEUED,
    SMS_STATE_SENDING,
    SMS_STATE_SENT,
    SMS_STATE_DELIVERED,
    SMS_STATE_FAILED,
    SMS_STATE_RECEIVED,
    SMS_STATE_READ,
} sms_state_t;

/* 短信类型 */
typedef enum {
    SMS_TYPE_TEXT = 0,
    SMS_TYPE_MMS,
    SMS_TYPE_CLASS_0,  /* 闪信 */
    SMS_TYPE_WAP_PUSH,
    SMS_TYPE_CONFIG,  /* 配置短信 */
} sms_type_t;

/* 短信消息 */
typedef struct {
    int used;
    int id;
    int conversation_id;
    char address[SMS_PHONE_LEN];
    char contact_name[SMS_NAME_LEN];
    char body[SMS_MAX_TEXT_LEN + 1];
    sms_type_t type;
    sms_state_t state;
    int outgoing;  /* 1=发送, 0=接收 */
    uint64_t timestamp;
    uint64_t delivery_time;
    int read;
    int locked;
    int sim_slot;
    /* MMS */
    int has_attachment;
    char attachment_name[64];
    uint32_t attachment_size;
    uint8_t attachment_type;  /* 0=图片,1=视频,2=音频,3=文件 */
    /* 编码 */
    int encoding;  /* 0=GSM7,1=UCS2,2=UTF8 */
    int parts;  /* 长短信分段数 */
} sms_message_t;

/* 会话 */
typedef struct {
    int used;
    int id;
    char address[SMS_PHONE_LEN];
    char contact_name[SMS_NAME_LEN];
    int message_count;
    int unread_count;
    uint64_t last_message_time;
    char last_message_preview[80];
    int last_message_outgoing;
    int archived;
    int blocked;
    int notification_enabled;
} sms_conversation_t;

/* 草稿 */
typedef struct {
    int used;
    int id;
    char address[SMS_PHONE_LEN];
    char body[SMS_MAX_TEXT_LEN + 1];
    uint64_t saved_time;
} sms_draft_t;

/* 短信服务状态 */
typedef struct {
    int initialized;
    int enabled;
    /* 消息 */
    sms_message_t messages[SMS_MAX_MESSAGES];
    int message_count;
    /* 会话 */
    sms_conversation_t conversations[SMS_MAX_CONVERSATIONS];
    int conversation_count;
    /* 草稿 */
    sms_draft_t drafts[32];
    int draft_count;
    /* 设置 */
    int delivery_reports;
    int read_reports;
    int auto_delete_old;
    int max_messages_per_conv;
    int signature_enabled;
    char signature[64];
    int center_number_set;
    char service_center[SMS_PHONE_LEN];
    /* 统计 */
    uint64_t total_sent;
    uint64_t total_received;
    uint64_t total_failed;
    uint64_t total_mms;
} sms_service_t;

/* 初始化 */
void sms_service_init(void);

/* 发送/接收 */
int sms_send(const char *address, const char *body, sms_type_t type);
int sms_receive(const char *address, const char *body, sms_type_t type);
int sms_resend(int message_id);
int sms_cancel(int message_id);

/* 消息管理 */
int sms_get_message(int message_id, sms_message_t *msg);
int sms_list_messages(sms_message_t *messages, int max);
int sms_list_by_conversation(int conv_id, sms_message_t *messages, int max);
int sms_delete_message(int message_id);
int sms_delete_conversation(int conv_id);
int sms_mark_read(int message_id);
int sms_mark_unread(int message_id);
int sms_lock_message(int message_id, int lock);
int sms_search(const char *keyword, sms_message_t *results, int max);

/* 会话管理 */
int sms_get_conversation(int conv_id, sms_conversation_t *conv);
int sms_list_conversations(sms_conversation_t *convs, int max);
int sms_get_or_create_conversation(const char *address);
int sms_get_unread_count(void);
int sms_mark_conversation_read(int conv_id);
int sms_archive_conversation(int conv_id, int archive);
int sms_block_conversation(int conv_id, int block);

/* 草稿 */
int sms_draft_save(const char *address, const char *body);
int sms_draft_get(int draft_id, sms_draft_t *draft);
int sms_draft_list(sms_draft_t *drafts, int max);
int sms_draft_delete(int draft_id);

/* MMS */
int sms_send_mms(const char *address, const char *text, const char *attachment, uint32_t size);
int sms_download_mms(int message_id);

/* 设置 */
int sms_set_delivery_reports(int enable);
int sms_set_signature(const char *sig);
int sms_set_service_center(const char *number);
int sms_set_auto_delete(int enable, int max_per_conv);

/* 统计 */
int sms_get_stats(sms_service_t *stats);

#endif /* SMS_SERVICE_H */
