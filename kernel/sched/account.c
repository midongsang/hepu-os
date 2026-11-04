/* ============================================================================
 * 赫普OS - 账户与同步
 * AccountManager + SyncAdapter + 云同步框架
 * ========================================================================== */

#include <stdint.h>
#include "account.h"
#include "vga.h"
#include "string.h"

static account_manager_t am;
static char account_types[ACCOUNT_MAX_TYPES][ACCOUNT_TYPE_LEN];
static char account_type_labels[ACCOUNT_MAX_TYPES][64];
static int account_type_total = 0;

void account_init(void)
{
    memset(&am, 0, sizeof(account_manager_t));
    am.initialized = 1;
    am.sync_state = SYNC_STATE_IDLE;
    am.current_sync_account = -1;
    account_type_total = 0;

    /* 注册默认账户类型 */
    account_register_type("com.hepu", "HEPU Account", 1);
    account_register_type("com.google", "Google", 1);
    account_register_type("com.email", "Email", 0);
    account_register_type("com.exchange", "Exchange", 0);

    /* 添加默认账户 */
    account_t acc;
    memset(&acc, 0, sizeof(acc));
    acc.used = 1;
    acc.id = 0;
    strcpy(acc.name, "user@hepu.os");
    strcpy(acc.type, "com.hepu");
    acc.type_id = ACCOUNT_TYPE_HEPU;
    acc.primary = 1;
    acc.enabled = 1;
    strcpy(acc.display_name, "HEPU User");
    strcpy(acc.email, "user@hepu.os");
    am.accounts[0] = acc;
    am.account_count = 1;

    /* 注册默认同步适配器 */
    sync_adapter_t sa;
    memset(&sa, 0, sizeof(sa));
    sa.used = 1;
    sa.id = 0;
    strcpy(sa.authority, "com.hepu.contacts");
    strcpy(sa.account_type, "com.hepu");
    sa.syncable = 1;
    sa.auto_sync = 1;
    sa.sync_interval = 86400;  /* 24小时 */
    sync_register_adapter(&sa);

    memset(&sa, 0, sizeof(sa));
    sa.used = 1;
    sa.id = 1;
    strcpy(sa.authority, "com.hepu.calendar");
    strcpy(sa.account_type, "com.hepu");
    sa.syncable = 1;
    sa.auto_sync = 1;
    sa.sync_interval = 3600;  /* 1小时 */
    sync_register_adapter(&sa);

    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[ACCOUNT] init done\n");
}

int account_register_type(const char *type, const char *label, int supports_oauth)
{
    (void)supports_oauth;
    if (!type || account_type_total >= ACCOUNT_MAX_TYPES) return -1;
    strncpy(account_types[account_type_total], type, ACCOUNT_TYPE_LEN - 1);
    if (label) strncpy(account_type_labels[account_type_total], label, 63);
    account_type_total++;
    return 0;
}

int account_add(const char *name, const char *type, const char *password)
{
    (void)password;
    if (!name || !type || !am.initialized) return -1;
    for (int i = 0; i < ACCOUNT_MAX_ACCOUNTS; i++) {
        if (!am.accounts[i].used) {
            memset(&am.accounts[i], 0, sizeof(account_t));
            am.accounts[i].used = 1;
            am.accounts[i].id = i;
            strncpy(am.accounts[i].name, name, ACCOUNT_NAME_LEN - 1);
            strncpy(am.accounts[i].type, type, ACCOUNT_TYPE_LEN - 1);
            am.accounts[i].enabled = 1;
            am.account_count++;
            vga_printf("[ACCOUNT] add account: %s(%s)\n", name, type);
            return i;
        }
    }
    return -1;
}

int account_remove(int account_id)
{
    if (account_id < 0 || account_id >= ACCOUNT_MAX_ACCOUNTS || !am.accounts[account_id].used) return -1;
    am.accounts[account_id].used = 0;
    am.account_count--;
    /* 移除相关令牌 */
    for (int i = 0; i < ACCOUNT_MAX_AUTHS; i++) {
        if (am.tokens[i].used && am.tokens[i].account_id == account_id) {
            am.tokens[i].used = 0;
            am.token_count--;
        }
    }
    return 0;
}

int account_get(int account_id, account_t *account)
{
    if (account_id < 0 || account_id >= ACCOUNT_MAX_ACCOUNTS || !am.accounts[account_id].used || !account) return -1;
    memcpy(account, &am.accounts[account_id], sizeof(account_t));
    return 0;
}

int account_get_by_name(const char *name, account_t *account)
{
    if (!name || !account) return -1;
    for (int i = 0; i < ACCOUNT_MAX_ACCOUNTS; i++) {
        if (am.accounts[i].used && strcmp(am.accounts[i].name, name) == 0) {
            memcpy(account, &am.accounts[i], sizeof(account_t));
            return i;
        }
    }
    return -1;
}

int account_list(account_t *list, int max)
{
    if (!list || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < ACCOUNT_MAX_ACCOUNTS && count < max; i++) {
        if (am.accounts[i].used) memcpy(&list[count++], &am.accounts[i], sizeof(account_t));
    }
    return count;
}

int account_list_by_type(const char *type, account_t *list, int max)
{
    if (!type || !list || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < ACCOUNT_MAX_ACCOUNTS && count < max; i++) {
        if (am.accounts[i].used && strcmp(am.accounts[i].type, type) == 0) {
            memcpy(&list[count++], &am.accounts[i], sizeof(account_t));
        }
    }
    return count;
}

int account_set_primary(int account_id)
{
    if (account_id < 0 || account_id >= ACCOUNT_MAX_ACCOUNTS || !am.accounts[account_id].used) return -1;
    for (int i = 0; i < ACCOUNT_MAX_ACCOUNTS; i++) {
        am.accounts[i].primary = (i == account_id) ? 1 : 0;
    }
    return 0;
}

int account_get_primary(account_t *account)
{
    if (!account) return -1;
    for (int i = 0; i < ACCOUNT_MAX_ACCOUNTS; i++) {
        if (am.accounts[i].used && am.accounts[i].primary) {
            memcpy(account, &am.accounts[i], sizeof(account_t));
            return i;
        }
    }
    return -1;
}

int account_set_enabled(int account_id, int enabled)
{
    if (account_id < 0 || account_id >= ACCOUNT_MAX_ACCOUNTS || !am.accounts[account_id].used) return -1;
    am.accounts[account_id].enabled = enabled ? 1 : 0;
    return 0;
}

int account_count(void) { return am.account_count; }

int account_get_auth_token(int account_id, const char *token_type, char *token, int len)
{
    if (!token || len <= 0) return -1;
    for (int i = 0; i < ACCOUNT_MAX_AUTHS; i++) {
        if (am.tokens[i].used && am.tokens[i].account_id == account_id &&
            strcmp(am.tokens[i].type, token_type) == 0) {
            /* 检查是否过期 */
            if (am.tokens[i].expires_at > 0) {
                /* 原型阶段: 假设不过期 */
            }
            strncpy(token, am.tokens[i].token, len - 1);
            token[len - 1] = '\0';
            return 0;
        }
    }
    return -1;
}

int account_invalidate_auth_token(int account_id, const char *token)
{
    (void)account_id; (void)token;
    return 0;
}

int account_set_auth_token(int account_id, const char *token_type, const char *token,
                           uint64_t expires_at)
{
    if (!token_type || !token) return -1;
    for (int i = 0; i < ACCOUNT_MAX_AUTHS; i++) {
        if (!am.tokens[i].used) {
            memset(&am.tokens[i], 0, sizeof(auth_token_t));
            am.tokens[i].used = 1;
            am.tokens[i].id = i;
            am.tokens[i].account_id = account_id;
            strncpy(am.tokens[i].type, token_type, ACCOUNT_TYPE_LEN - 1);
            strncpy(am.tokens[i].token, token, ACCOUNT_TOKEN_LEN - 1);
            am.tokens[i].expires_at = expires_at;
            am.token_count++;
            return i;
        }
    }
    return -1;
}

int account_get_password(int account_id, char *password, int len)
{
    (void)account_id;
    if (!password || len <= 0) return -1;
    /* 原型阶段: 不存储明文密码 */
    return -1;
}

int account_update_password(int account_id, const char *old_pw, const char *new_pw)
{
    (void)account_id; (void)old_pw; (void)new_pw;
    return 0;
}

int sync_register_adapter(sync_adapter_t *adapter)
{
    if (!adapter || !am.initialized) return -1;
    for (int i = 0; i < 8; i++) {
        if (!am.adapters[i].used) {
            memcpy(&am.adapters[i], adapter, sizeof(sync_adapter_t));
            am.adapters[i].used = 1;
            am.adapters[i].id = i;
            am.adapter_count++;
            return i;
        }
    }
    return -1;
}

int sync_unregister_adapter(int adapter_id)
{
    if (adapter_id < 0 || adapter_id >= 8 || !am.adapters[adapter_id].used) return -1;
    am.adapters[adapter_id].used = 0;
    am.adapter_count--;
    return 0;
}

int sync_request(int account_id, const char *authority, int manual)
{
    (void)manual;
    if (!authority) return -1;
    for (int i = 0; i < 8; i++) {
        if (am.adapters[i].used && strcmp(am.adapters[i].authority, authority) == 0) {
            am.adapters[i].syncing = 1;
            am.sync_state = SYNC_STATE_SYNCING;
            am.current_sync_account = account_id;
            am.current_sync_authority = authority;
            vga_printf("[SYNC] sync start: %s\n", authority);
            /* 原型阶段: 立即完成 */
            am.adapters[i].last_sync = 0;
            am.adapters[i].syncing = 0;
            am.adapters[i].sync_result = 0;
            am.sync_state = SYNC_STATE_DONE;
            vga_printf("[SYNC] sync done: %s\n", authority);
            return 0;
        }
    }
    return -1;
}

int sync_cancel(int account_id, const char *authority)
{
    (void)account_id; (void)authority;
    am.sync_state = SYNC_STATE_IDLE;
    return 0;
}

int sync_get_status(int account_id, const char *authority, sync_state_t *state)
{
    (void)account_id; (void)authority;
    if (!state) return -1;
    *state = am.sync_state;
    return 0;
}

int sync_set_auto_sync(int account_id, const char *authority, int enable)
{
    (void)account_id;
    if (!authority) return -1;
    for (int i = 0; i < 8; i++) {
        if (am.adapters[i].used && strcmp(am.adapters[i].authority, authority) == 0) {
            am.adapters[i].auto_sync = enable ? 1 : 0;
            return 0;
        }
    }
    return -1;
}

int sync_get_auto_sync(int account_id, const char *authority)
{
    (void)account_id;
    if (!authority) return 0;
    for (int i = 0; i < 8; i++) {
        if (am.adapters[i].used && strcmp(am.adapters[i].authority, authority) == 0) {
            return am.adapters[i].auto_sync;
        }
    }
    return 0;
}

int sync_set_interval(int account_id, const char *authority, int seconds)
{
    (void)account_id;
    if (!authority) return -1;
    for (int i = 0; i < 8; i++) {
        if (am.adapters[i].used && strcmp(am.adapters[i].authority, authority) == 0) {
            am.adapters[i].sync_interval = seconds;
            return 0;
        }
    }
    return -1;
}

int sync_list_adapters(sync_adapter_t *list, int max)
{
    if (!list || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < 8 && count < max; i++) {
        if (am.adapters[i].used) memcpy(&list[count++], &am.adapters[i], sizeof(sync_adapter_t));
    }
    return count;
}

int cloud_sync_start(int account_id)
{
    vga_printf("[CLOUD] cloud sync start: accounts #%d\n", account_id);
    am.sync_state = SYNC_STATE_SYNCING;
    /* 同步所有适配器 */
    for (int i = 0; i < 8; i++) {
        if (am.adapters[i].used && am.adapters[i].syncable) {
            sync_request(account_id, am.adapters[i].authority, 1);
        }
    }
    am.sync_state = SYNC_STATE_DONE;
    vga_puts("[CLOUD] cloud sync done \n");
    return 0;
}

int cloud_sync_stop(void)
{
    am.sync_state = SYNC_STATE_IDLE;
    return 0;
}

int cloud_sync_get_state(sync_state_t *state)
{
    if (!state) return -1;
    *state = am.sync_state;
    return 0;
}

int account_type_count(void) { return account_type_total; }

int account_sync_active_count(void)
{
    int count = 0;
    for (int i = 0; i < 8; i++) {
        if (am.adapters[i].used && am.adapters[i].syncing) count++;
    }
    return count;
}
