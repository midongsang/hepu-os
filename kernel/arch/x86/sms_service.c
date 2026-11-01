/* ============================================================================
 * 赫普OS - 短信服务
 * ========================================================================== */

#include <stdint.h>
#include "sms_service.h"
#include "vga.h"
#include "string.h"



static sms_service_t ss;

void sms_service_init(void)
{
    memset(&ss, 0, sizeof(sms_service_t));
    ss.initialized = 1;
    ss.enabled = 1;
    ss.delivery_reports = 1;
    ss.read_reports = 0;
    ss.auto_delete_old = 0;
    ss.max_messages_per_conv = 500;
    ss.signature_enabled = 0;
    strncpy(ss.service_center, "+8613800138000", 31);
    ss.center_number_set = 1;

    /* 测试消息 */
    sms_receive("10086", " 您 话费余额 50.00 元 ", SMS_TYPE_TEXT);
    sms_receive("13800138000", " 晚 一起吃饭吗 ?", SMS_TYPE_TEXT);

    vga_puts("[SMS] SMS service init done \n");
    vga_printf(" msg: %d, session: %d, unread: %d\n",
               ss.message_count, ss.conversation_count, sms_get_unread_count());
}

/* 发送/接收 */
int sms_send(const char *address, const char *body, sms_type_t type)
{
    if (!address || !body) return -1;
    for (int i = 0; i < SMS_MAX_MESSAGES; i++) {
        if (!ss.messages[i].used) {
            memset(&ss.messages[i], 0, sizeof(sms_message_t));
            ss.messages[i].used = 1;
            ss.messages[i].id = i;
            strncpy(ss.messages[i].address, address, 31);
            strncpy(ss.messages[i].body, body, SMS_MAX_TEXT_LEN);
            ss.messages[i].type = type;
            ss.messages[i].state = SMS_STATE_SENT;
            ss.messages[i].outgoing = 1;
            ss.messages[i].timestamp = 1;
            ss.messages[i].read = 1;
            ss.messages[i].sim_slot = 0;
            ss.messages[i].encoding = 2;  /* UTF8 */
            ss.message_count++;
            ss.total_sent++;
            /* 关联会话 */
            int conv_id = sms_get_or_create_conversation(address);
            ss.messages[i].conversation_id = conv_id;
            return i;
        }
    }
    return -1;
}

int sms_receive(const char *address, const char *body, sms_type_t type)
{
    if (!address || !body) return -1;
    for (int i = 0; i < SMS_MAX_MESSAGES; i++) {
        if (!ss.messages[i].used) {
            memset(&ss.messages[i], 0, sizeof(sms_message_t));
            ss.messages[i].used = 1;
            ss.messages[i].id = i;
            strncpy(ss.messages[i].address, address, 31);
            strncpy(ss.messages[i].body, body, SMS_MAX_TEXT_LEN);
            ss.messages[i].type = type;
            ss.messages[i].state = SMS_STATE_RECEIVED;
            ss.messages[i].outgoing = 0;
            ss.messages[i].timestamp = 1;
            ss.messages[i].read = 0;
            ss.messages[i].sim_slot = 0;
            ss.message_count++;
            ss.total_received++;
            /* 关联会话 */
            int conv_id = sms_get_or_create_conversation(address);
            ss.messages[i].conversation_id = conv_id;
            return i;
        }
    }
    return -1;
}

int sms_resend(int message_id)
{
    if (message_id < 0 || message_id >= SMS_MAX_MESSAGES || !ss.messages[message_id].used) return -1;
    ss.messages[message_id].state = SMS_STATE_SENDING;
    ss.messages[message_id].state = SMS_STATE_SENT;
    return 0;
}

int sms_cancel(int message_id)
{
    if (message_id < 0 || message_id >= SMS_MAX_MESSAGES || !ss.messages[message_id].used) return -1;
    if (ss.messages[message_id].state == SMS_STATE_QUEUED || ss.messages[message_id].state == SMS_STATE_SENDING) {
        ss.messages[message_id].state = SMS_STATE_FAILED;
        return 0;
    }
    return -1;
}

/* 消息管理 */
int sms_get_message(int message_id, sms_message_t *msg)
{
    if (message_id < 0 || message_id >= SMS_MAX_MESSAGES || !ss.messages[message_id].used || !msg) return -1;
    memcpy(msg, &ss.messages[message_id], sizeof(sms_message_t));
    return 0;
}

int sms_list_messages(sms_message_t *messages, int max)
{
    if (!messages || max <= 0) return 0;
    int count = 0;
    for (int i = SMS_MAX_MESSAGES - 1; i >= 0 && count < max; i--) {
        if (ss.messages[i].used) memcpy(&messages[count++], &ss.messages[i], sizeof(sms_message_t));
    }
    return count;
}

int sms_list_by_conversation(int conv_id, sms_message_t *messages, int max)
{
    if (!messages || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < SMS_MAX_MESSAGES && count < max; i++) {
        if (ss.messages[i].used && ss.messages[i].conversation_id == conv_id)
            memcpy(&messages[count++], &ss.messages[i], sizeof(sms_message_t));
    }
    return count;
}

int sms_delete_message(int message_id)
{
    if (message_id < 0 || message_id >= SMS_MAX_MESSAGES || !ss.messages[message_id].used) return -1;
    if (ss.messages[message_id].locked) return -1;
    int conv_id = ss.messages[message_id].conversation_id;
    ss.messages[message_id].used = 0;
    ss.message_count--;
    /* 更新会话计数 */
    if (conv_id >= 0 && conv_id < SMS_MAX_CONVERSATIONS && ss.conversations[conv_id].used) {
        if (ss.conversations[conv_id].message_count > 0)
            ss.conversations[conv_id].message_count--;
    }
    return 0;
}

int sms_delete_conversation(int conv_id)
{
    if (conv_id < 0 || conv_id >= SMS_MAX_CONVERSATIONS || !ss.conversations[conv_id].used) return -1;
    for (int i = 0; i < SMS_MAX_MESSAGES; i++) {
        if (ss.messages[i].used && ss.messages[i].conversation_id == conv_id && !ss.messages[i].locked) {
            ss.messages[i].used = 0;
            ss.message_count--;
        }
    }
    ss.conversations[conv_id].used = 0;
    ss.conversation_count--;
    return 0;
}

int sms_mark_read(int message_id)
{
    if (message_id < 0 || message_id >= SMS_MAX_MESSAGES || !ss.messages[message_id].used) return -1;
    if (!ss.messages[message_id].read) {
        ss.messages[message_id].read = 1;
        ss.messages[message_id].state = SMS_STATE_READ;
        int conv_id = ss.messages[message_id].conversation_id;
        if (conv_id >= 0 && conv_id < SMS_MAX_CONVERSATIONS && ss.conversations[conv_id].unread_count > 0)
            ss.conversations[conv_id].unread_count--;
    }
    return 0;
}

int sms_mark_unread(int message_id)
{
    if (message_id < 0 || message_id >= SMS_MAX_MESSAGES || !ss.messages[message_id].used) return -1;
    ss.messages[message_id].read = 0;
    return 0;
}

int sms_lock_message(int message_id, int lock)
{
    if (message_id < 0 || message_id >= SMS_MAX_MESSAGES || !ss.messages[message_id].used) return -1;
    ss.messages[message_id].locked = lock ? 1 : 0;
    return 0;
}

int sms_search(const char *keyword, sms_message_t *results, int max)
{
    if (!keyword || !results || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < SMS_MAX_MESSAGES && count < max; i++) {
        if (ss.messages[i].used && strstr(ss.messages[i].body, keyword))
            memcpy(&results[count++], &ss.messages[i], sizeof(sms_message_t));
    }
    return count;
}

/* 会话管理 */
int sms_get_conversation(int conv_id, sms_conversation_t *conv)
{
    if (conv_id < 0 || conv_id >= SMS_MAX_CONVERSATIONS || !ss.conversations[conv_id].used || !conv) return -1;
    memcpy(conv, &ss.conversations[conv_id], sizeof(sms_conversation_t));
    return 0;
}

int sms_list_conversations(sms_conversation_t *convs, int max)
{
    if (!convs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < SMS_MAX_CONVERSATIONS && count < max; i++) {
        if (ss.conversations[i].used) memcpy(&convs[count++], &ss.conversations[i], sizeof(sms_conversation_t));
    }
    return count;
}

int sms_get_or_create_conversation(const char *address)
{
    if (!address) return -1;
    for (int i = 0; i < SMS_MAX_CONVERSATIONS; i++) {
        if (ss.conversations[i].used && strcmp(ss.conversations[i].address, address) == 0) {
            ss.conversations[i].message_count++;
            ss.conversations[i].last_message_time = 1;
            return i;
        }
    }
    for (int i = 0; i < SMS_MAX_CONVERSATIONS; i++) {
        if (!ss.conversations[i].used) {
            memset(&ss.conversations[i], 0, sizeof(sms_conversation_t));
            ss.conversations[i].used = 1;
            ss.conversations[i].id = i;
            strncpy(ss.conversations[i].address, address, 31);
            ss.conversations[i].message_count = 1;
            ss.conversations[i].unread_count = 1;
            ss.conversations[i].last_message_time = 1;
            ss.conversations[i].notification_enabled = 1;
            ss.conversation_count++;
            return i;
        }
    }
    return -1;
}

int sms_get_unread_count(void)
{
    int count = 0;
    for (int i = 0; i < SMS_MAX_MESSAGES; i++) {
        if (ss.messages[i].used && !ss.messages[i].read && !ss.messages[i].outgoing) count++;
    }
    return count;
}

int sms_mark_conversation_read(int conv_id)
{
    if (conv_id < 0 || conv_id >= SMS_MAX_CONVERSATIONS) return -1;
    for (int i = 0; i < SMS_MAX_MESSAGES; i++) {
        if (ss.messages[i].used && ss.messages[i].conversation_id == conv_id && !ss.messages[i].read)
            sms_mark_read(i);
    }
    if (ss.conversations[conv_id].used) ss.conversations[conv_id].unread_count = 0;
    return 0;
}

int sms_archive_conversation(int conv_id, int archive)
{
    if (conv_id < 0 || conv_id >= SMS_MAX_CONVERSATIONS || !ss.conversations[conv_id].used) return -1;
    ss.conversations[conv_id].archived = archive ? 1 : 0;
    return 0;
}

int sms_block_conversation(int conv_id, int block)
{
    if (conv_id < 0 || conv_id >= SMS_MAX_CONVERSATIONS || !ss.conversations[conv_id].used) return -1;
    ss.conversations[conv_id].blocked = block ? 1 : 0;
    return 0;
}

/* 草稿 */
int sms_draft_save(const char *address, const char *body)
{
    if (!address || !body) return -1;
    for (int i = 0; i < 32; i++) {
        if (!ss.drafts[i].used) {
            memset(&ss.drafts[i], 0, sizeof(sms_draft_t));
            ss.drafts[i].used = 1;
            ss.drafts[i].id = i;
            strncpy(ss.drafts[i].address, address, 31);
            strncpy(ss.drafts[i].body, body, SMS_MAX_TEXT_LEN);
            ss.drafts[i].saved_time = 1;
            ss.draft_count++;
            return i;
        }
    }
    return -1;
}

int sms_draft_get(int draft_id, sms_draft_t *draft)
{
    if (draft_id < 0 || draft_id >= 32 || !ss.drafts[draft_id].used || !draft) return -1;
    memcpy(draft, &ss.drafts[draft_id], sizeof(sms_draft_t));
    return 0;
}

int sms_draft_list(sms_draft_t *drafts, int max)
{
    if (!drafts || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < 32 && count < max; i++) {
        if (ss.drafts[i].used) memcpy(&drafts[count++], &ss.drafts[i], sizeof(sms_draft_t));
    }
    return count;
}

int sms_draft_delete(int draft_id)
{
    if (draft_id < 0 || draft_id >= 32 || !ss.drafts[draft_id].used) return -1;
    ss.drafts[draft_id].used = 0;
    ss.draft_count--;
    return 0;
}

/* MMS */
int sms_send_mms(const char *address, const char *text, const char *attachment, uint32_t size)
{
    (void)attachment; (void)size;
    int id = sms_send(address, text ? text : "", SMS_TYPE_MMS);
    if (id >= 0) {
        ss.messages[id].has_attachment = 1;
        ss.total_mms++;
    }
    return id;
}

int sms_download_mms(int message_id)
{
    if (message_id < 0 || message_id >= SMS_MAX_MESSAGES || !ss.messages[message_id].used) return -1;
    return 0;
}

/* 设置 */
int sms_set_delivery_reports(int enable)
{
    ss.delivery_reports = enable ? 1 : 0;
    return 0;
}

int sms_set_signature(const char *sig)
{
    if (!sig) return -1;
    strncpy(ss.signature, sig, 63);
    ss.signature_enabled = 1;
    return 0;
}

int sms_set_service_center(const char *number)
{
    if (!number) return -1;
    strncpy(ss.service_center, number, 31);
    ss.center_number_set = 1;
    return 0;
}

int sms_set_auto_delete(int enable, int max_per_conv)
{
    ss.auto_delete_old = enable ? 1 : 0;
    ss.max_messages_per_conv = max_per_conv;
    return 0;
}

/* 统计 */
int sms_get_stats(sms_service_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &ss, sizeof(sms_service_t));
    return 0;
}
