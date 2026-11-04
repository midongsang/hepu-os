/* ============================================================================
 * 赫普OS - 邮件客户端
 * V8.86: 初始实现
 * 支持：IMAP/POP3/SMTP、邮件管理、联系人
 * ========================================================================== */

#ifndef _MAIL_CLIENT_H
#define _MAIL_CLIENT_H

#include <stdint.h>

/* 邮件状态 */
typedef enum {
    MAIL_STATUS_UNREAD = 0,
    MAIL_STATUS_READ,
    MAIL_STATUS_REPLIED,
    MAIL_STATUS_FORWARDED,
    MAIL_STATUS_STARRED,
    MAIL_STATUS_TRASH,
    MAIL_STATUS_SPAM,
    MAIL_STATUS_DRAFT
} mail_status_t;

/* 邮件优先级 */
typedef enum {
    MAIL_PRIORITY_LOW = 0,
    MAIL_PRIORITY_NORMAL,
    MAIL_PRIORITY_HIGH
} mail_priority_t;

/* 邮件文件夹 */
typedef enum {
    MAIL_FOLDER_INBOX = 0,
    MAIL_FOLDER_SENT,
    MAIL_FOLDER_DRAFTS,
    MAIL_FOLDER_TRASH,
    MAIL_FOLDER_SPAM,
    MAIL_FOLDER_ARCHIVE,
    MAIL_FOLDER_CUSTOM
} mail_folder_t;

/* 邮件附件 */
typedef struct {
    char     filename[256];
    char     mime_type[64];
    uint32_t size;
    uint8_t *data;
    int      used;
} mail_attachment_t;

/* 邮件 */
typedef struct {
    uint32_t       id;
    char           from[256];
    char           to[256];
    char           cc[256];
    char           bcc[256];
    char           subject[512];
    char           body[8192];
    char           date[64];
    mail_status_t  status;
    mail_priority_t priority;
    mail_folder_t  folder;
    mail_attachment_t attachments[8];
    int            attachment_count;
    int            used;
} mail_t;

/* 联系人 */
typedef struct {
    char     name[128];
    char     email[256];
    char     phone[32];
    char     company[128];
    char     notes[512];
    int      starred;
    int      used;
} mail_contact_t;

/* 邮件账户 */
typedef struct {
    char     name[128];
    char     email[256];
    char     imap_server[256];
    uint16_t imap_port;
    char     smtp_server[256];
    uint16_t smtp_port;
    char     username[128];
    char     password[256];  /* 应该加密存储 */
    int      use_ssl;
    int      active;
} mail_account_t;

/* 邮件客户端上下文 */
typedef struct {
    int            initialized;
    mail_account_t accounts[4];
    int            account_count;
    int            current_account;

    mail_t         inbox[256];
    int            inbox_count;
    mail_t         sent[128];
    int            sent_count;
    mail_t         drafts[64];
    int            drafts_count;
    mail_t         trash[64];
    int            trash_count;

    mail_contact_t contacts[256];
    int            contact_count;

    uint32_t       next_mail_id;
    int            connected;
} mail_client_t;

/* 函数声明 */

/* 初始化/关闭 */
int mail_client_init(void);
void mail_client_shutdown(void);
mail_client_t *mail_client_get_context(void);

/* 账户管理 */
int mail_client_add_account(const char *name, const char *email,
                            const char *imap_server, uint16_t imap_port,
                            const char *smtp_server, uint16_t smtp_port,
                            const char *username, const char *password,
                            int use_ssl);
int mail_client_remove_account(int index);
int mail_client_set_current_account(int index);
int mail_client_connect(void);
int mail_client_disconnect(void);

/* 邮件操作 */
int mail_client_send(const char *to, const char *subject, const char *body);
int mail_client_receive(void);
int mail_client_get_mail(mail_folder_t folder, int index, mail_t *out);
int mail_client_count(mail_folder_t folder);
int mail_client_delete(mail_folder_t folder, int index);
int mail_client_move(mail_folder_t from, int index, mail_folder_t to);
int mail_client_mark_read(mail_folder_t folder, int index, int read);
int mail_client_star(mail_folder_t folder, int index, int star);
int mail_client_save_draft(const char *to, const char *subject, const char *body);

/* 联系人 */
int mail_client_add_contact(const char *name, const char *email,
                            const char *phone, const char *company);
int mail_client_remove_contact(int index);
int mail_client_get_contact(int index, mail_contact_t *out);
int mail_client_contact_count(void);
int mail_client_search_contacts(const char *query, mail_contact_t *results, int max);

/* 搜索 */
int mail_client_search(const char *query, mail_t *results, int max);

/* 统计 */
int mail_client_unread_count(void);
int mail_client_total_count(void);

#endif /* _MAIL_CLIENT_H */
