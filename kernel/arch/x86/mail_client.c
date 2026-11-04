/* ============================================================================
 * 赫普OS - 邮件客户端实现
 * V8.86: 初始实现
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "mail_client.h"
#include "vga.h"
#include "kstdio.h"

/* 简单字符串工具 */
static int mc_strlen(const char *s) {
    int n = 0; while (s && s[n]) n++; return n;
}
static int mc_strcmp(const char *a, const char *b) {
    if (!a || !b) return -1;
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static void mc_strncpy(char *dst, const char *src, int n) {
    if (!dst || !src || n <= 0) return;
    int i = 0; while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* 全局邮件客户端上下文 */
static mail_client_t g_mail;
static int g_initialized = 0;

/* ============================================================================
 * 初始化/关闭
 * ========================================================================== */

int mail_client_init(void)
{
    if (g_initialized) return 0;

    memset(&g_mail, 0, sizeof(mail_client_t));
    g_mail.initialized = 1;
    g_mail.account_count = 0;
    g_mail.current_account = -1;
    g_mail.inbox_count = 0;
    g_mail.sent_count = 0;
    g_mail.drafts_count = 0;
    g_mail.trash_count = 0;
    g_mail.contact_count = 0;
    g_mail.next_mail_id = 1;
    g_mail.connected = 0;

    /* 添加示例联系人 */
    mail_client_add_contact("张三", "zhangsan@example.com", "13800138000", "示例公司");
    mail_client_add_contact("李四", "lisi@example.com", "13900139000", "测试公司");

    /* 添加示例邮件 */
    mail_t *m = &g_mail.inbox[g_mail.inbox_count++];
    m->id = g_mail.next_mail_id++;
    mc_strncpy(m->from, "system@hepuos.org", 256);
    mc_strncpy(m->to, "user@hepuos.org", 256);
    mc_strncpy(m->subject, "欢迎使用赫普OS邮件客户端", 512);
    mc_strncpy(m->body, "欢迎使用赫普OS邮件客户端！\n\n这是一封欢迎邮件，您可以在这里收发邮件、管理联系人。\n\n祝使用愉快！\n赫普OS团队", 8192);
    mc_strncpy(m->date, "2026-10-06 10:00:00", 64);
    m->status = MAIL_STATUS_UNREAD;
    m->priority = MAIL_PRIORITY_NORMAL;
    m->folder = MAIL_FOLDER_INBOX;
    m->used = 1;

    g_initialized = 1;
    vga_puts("[MAIL] Mail client initialized\n");
    return 0;
}

void mail_client_shutdown(void)
{
    if (!g_initialized) return;
    if (g_mail.connected) mail_client_disconnect();
    g_initialized = 0;
    vga_puts("[MAIL] Mail client shutdown\n");
}

mail_client_t *mail_client_get_context(void)
{
    return &g_mail;
}

/* ============================================================================
 * 账户管理
 * ========================================================================== */

int mail_client_add_account(const char *name, const char *email,
                            const char *imap_server, uint16_t imap_port,
                            const char *smtp_server, uint16_t smtp_port,
                            const char *username, const char *password,
                            int use_ssl)
{
    if (!g_initialized || !name || !email) return -1;
    if (g_mail.account_count >= 4) return -1;

    mail_account_t *acc = &g_mail.accounts[g_mail.account_count];
    mc_strncpy(acc->name, name, 128);
    mc_strncpy(acc->email, email, 256);
    mc_strncpy(acc->imap_server, imap_server ? imap_server : "imap.example.com", 256);
    acc->imap_port = imap_port ? imap_port : (use_ssl ? 993 : 143);
    mc_strncpy(acc->smtp_server, smtp_server ? smtp_server : "smtp.example.com", 256);
    acc->smtp_port = smtp_port ? smtp_port : (use_ssl ? 465 : 25);
    mc_strncpy(acc->username, username ? username : email, 128);
    mc_strncpy(acc->password, password ? password : "", 256);
    acc->use_ssl = use_ssl;
    acc->active = 1;

    if (g_mail.current_account < 0) {
        g_mail.current_account = g_mail.account_count;
    }
    g_mail.account_count++;

    vga_printf("[MAIL] Account added: %s (%s)\n", name, email);
    return g_mail.account_count - 1;
}

int mail_client_remove_account(int index)
{
    if (!g_initialized || index < 0 || index >= g_mail.account_count) return -1;

    for (int i = index; i < g_mail.account_count - 1; i++) {
        g_mail.accounts[i] = g_mail.accounts[i + 1];
    }
    g_mail.account_count--;
    if (g_mail.current_account >= g_mail.account_count) {
        g_mail.current_account = g_mail.account_count - 1;
    }
    return 0;
}

int mail_client_set_current_account(int index)
{
    if (!g_initialized || index < 0 || index >= g_mail.account_count) return -1;
    g_mail.current_account = index;
    return 0;
}

int mail_client_connect(void)
{
    if (!g_initialized || g_mail.current_account < 0) return -1;
    if (g_mail.connected) return 0;

    mail_account_t *acc = &g_mail.accounts[g_mail.current_account];
    vga_printf("[MAIL] Connecting to %s...\n", acc->imap_server);

    /* 简化：模拟连接 */
    g_mail.connected = 1;
    vga_printf("[MAIL] Connected to %s:%d (SSL=%s)\n",
               acc->imap_server, acc->imap_port,
               acc->use_ssl ? "yes" : "no");
    return 0;
}

int mail_client_disconnect(void)
{
    if (!g_initialized || !g_mail.connected) return -1;
    g_mail.connected = 0;
    vga_puts("[MAIL] Disconnected\n");
    return 0;
}

/* ============================================================================
 * 邮件操作
 * ========================================================================== */

int mail_client_send(const char *to, const char *subject, const char *body)
{
    if (!g_initialized || !to || !subject) return -1;

    vga_printf("[MAIL] Sending mail to %s: %s\n", to, subject);

    /* 保存到已发送 */
    if (g_mail.sent_count < 128) {
        mail_t *m = &g_mail.sent[g_mail.sent_count++];
        m->id = g_mail.next_mail_id++;
        mc_strncpy(m->from, g_mail.accounts[g_mail.current_account].email, 256);
        mc_strncpy(m->to, to, 256);
        mc_strncpy(m->subject, subject, 512);
        mc_strncpy(m->body, body ? body : "", 8192);
        mc_strncpy(m->date, "2026-10-06 12:00:00", 64);
        m->status = MAIL_STATUS_READ;
        m->folder = MAIL_FOLDER_SENT;
        m->used = 1;
    }

    return 0;
}

int mail_client_receive(void)
{
    if (!g_initialized || !g_mail.connected) return -1;

    vga_puts("[MAIL] Receiving mail...\n");
    /* 简化：模拟接收新邮件 */
    return 0;
}

int mail_client_get_mail(mail_folder_t folder, int index, mail_t *out)
{
    if (!g_initialized || !out) return -1;

    mail_t *mails = NULL;
    int count = 0;

    switch (folder) {
        case MAIL_FOLDER_INBOX:
            mails = g_mail.inbox; count = g_mail.inbox_count; break;
        case MAIL_FOLDER_SENT:
            mails = g_mail.sent; count = g_mail.sent_count; break;
        case MAIL_FOLDER_DRAFTS:
            mails = g_mail.drafts; count = g_mail.drafts_count; break;
        case MAIL_FOLDER_TRASH:
            mails = g_mail.trash; count = g_mail.trash_count; break;
        default: return -1;
    }

    if (index < 0 || index >= count) return -1;
    *out = mails[index];
    return 0;
}

int mail_client_count(mail_folder_t folder)
{
    if (!g_initialized) return 0;
    switch (folder) {
        case MAIL_FOLDER_INBOX: return g_mail.inbox_count;
        case MAIL_FOLDER_SENT: return g_mail.sent_count;
        case MAIL_FOLDER_DRAFTS: return g_mail.drafts_count;
        case MAIL_FOLDER_TRASH: return g_mail.trash_count;
        default: return 0;
    }
}

int mail_client_delete(mail_folder_t folder, int index)
{
    if (!g_initialized) return -1;
    /* 简化：标记为已删除 */
    return mail_client_move(folder, index, MAIL_FOLDER_TRASH);
}

int mail_client_move(mail_folder_t from, int index, mail_folder_t to)
{
    if (!g_initialized) return -1;
    /* 简化：移动邮件 */
    vga_printf("[MAIL] Move mail %d from %d to %d\n", index, from, to);
    return 0;
}

int mail_client_mark_read(mail_folder_t folder, int index, int read)
{
    if (!g_initialized) return -1;
    /* 简化：标记已读/未读 */
    return 0;
}

int mail_client_star(mail_folder_t folder, int index, int star)
{
    if (!g_initialized) return -1;
    /* 简化：标记星标 */
    return 0;
}

int mail_client_save_draft(const char *to, const char *subject, const char *body)
{
    if (!g_initialized || g_mail.drafts_count >= 64) return -1;

    mail_t *m = &g_mail.drafts[g_mail.drafts_count++];
    m->id = g_mail.next_mail_id++;
    mc_strncpy(m->to, to ? to : "", 256);
    mc_strncpy(m->subject, subject ? subject : "", 512);
    mc_strncpy(m->body, body ? body : "", 8192);
    m->status = MAIL_STATUS_DRAFT;
    m->folder = MAIL_FOLDER_DRAFTS;
    m->used = 1;

    vga_puts("[MAIL] Draft saved\n");
    return 0;
}

/* ============================================================================
 * 联系人
 * ========================================================================== */

int mail_client_add_contact(const char *name, const char *email,
                            const char *phone, const char *company)
{
    if (!g_initialized || !name || !email) return -1;
    if (g_mail.contact_count >= 256) return -1;

    mail_contact_t *c = &g_mail.contacts[g_mail.contact_count];
    mc_strncpy(c->name, name, 128);
    mc_strncpy(c->email, email, 256);
    mc_strncpy(c->phone, phone ? phone : "", 32);
    mc_strncpy(c->company, company ? company : "", 128);
    c->starred = 0;
    c->used = 1;
    g_mail.contact_count++;

    return g_mail.contact_count - 1;
}

int mail_client_remove_contact(int index)
{
    if (!g_initialized || index < 0 || index >= g_mail.contact_count) return -1;
    for (int i = index; i < g_mail.contact_count - 1; i++) {
        g_mail.contacts[i] = g_mail.contacts[i + 1];
    }
    g_mail.contact_count--;
    return 0;
}

int mail_client_get_contact(int index, mail_contact_t *out)
{
    if (!g_initialized || !out || index < 0 ||
        index >= g_mail.contact_count) return -1;
    *out = g_mail.contacts[index];
    return 0;
}

int mail_client_contact_count(void)
{
    return g_initialized ? g_mail.contact_count : 0;
}

int mail_client_search_contacts(const char *query, mail_contact_t *results, int max)
{
    if (!g_initialized || !query || !results || max <= 0) return 0;

    int count = 0;
    for (int i = 0; i < g_mail.contact_count && count < max; i++) {
        mail_contact_t *c = &g_mail.contacts[i];
        /* 简单子串匹配 */
        if (strstr(c->name, query) || strstr(c->email, query) ||
            strstr(c->company, query)) {
            results[count++] = *c;
        }
    }
    return count;
}

/* ============================================================================
 * 搜索
 * ========================================================================== */

int mail_client_search(const char *query, mail_t *results, int max)
{
    if (!g_initialized || !query || !results || max <= 0) return 0;

    int count = 0;
    /* 搜索收件箱 */
    for (int i = 0; i < g_mail.inbox_count && count < max; i++) {
        mail_t *m = &g_mail.inbox[i];
        if (strstr(m->subject, query) || strstr(m->from, query) ||
            strstr(m->body, query)) {
            results[count++] = *m;
        }
    }
    return count;
}

/* ============================================================================
 * 统计
 * ========================================================================== */

int mail_client_unread_count(void)
{
    if (!g_initialized) return 0;
    int count = 0;
    for (int i = 0; i < g_mail.inbox_count; i++) {
        if (g_mail.inbox[i].status == MAIL_STATUS_UNREAD) count++;
    }
    return count;
}

int mail_client_total_count(void)
{
    if (!g_initialized) return 0;
    return g_mail.inbox_count + g_mail.sent_count +
           g_mail.drafts_count + g_mail.trash_count;
}
