#ifndef ACCOUNT_H
#define ACCOUNT_H

#include <stdint.h>

#define ACCOUNT_MAX_ACCOUNTS 16
#define ACCOUNT_MAX_TYPES 8
#define ACCOUNT_MAX_AUTHS 16
#define ACCOUNT_NAME_LEN 64
#define ACCOUNT_TYPE_LEN 64
#define ACCOUNT_TOKEN_LEN 256

/* 账户类型 */
typedef enum {
    ACCOUNT_TYPE_GOOGLE = 0,
    ACCOUNT_TYPE_HEPU,
    ACCOUNT_TYPE_EMAIL,
    ACCOUNT_TYPE_EXCHANGE,
    ACCOUNT_TYPE_FACEBOOK,
    ACCOUNT_TYPE_TWITTER,
    ACCOUNT_TYPE_WECHAT,
    ACCOUNT_TYPE_CUSTOM,
} account_type_id_t;

/* 认证令牌类型 */
typedef enum {
    TOKEN_TYPE_OAUTH2 = 0,
    TOKEN_TYPE_OAUTH1,
    TOKEN_TYPE_PASSWORD,
    TOKEN_TYPE_API_KEY,
    TOKEN_TYPE_CUSTOM,
} token_type_t;

/* 账户 */
typedef struct {
    int used;
    int id;
    char name[ACCOUNT_NAME_LEN];
    char type[ACCOUNT_TYPE_LEN];
    account_type_id_t type_id;
    int primary;        /* 主账户 */
    int enabled;
    uint64_t creation_time;
    uint64_t last_sync_time;
    /* 用户数据 */
    char display_name[64];
    char email[128];
    char photo_uri[256];
} account_t;

/* 认证令牌 */
typedef struct {
    int used;
    int id;
    int account_id;
    char type[ACCOUNT_TYPE_LEN];
    token_type_t token_type;
    char token[ACCOUNT_TOKEN_LEN];
    char refresh_token[ACCOUNT_TOKEN_LEN];
    uint64_t expires_at;
    int scope_count;
    char scopes[8][64];
} auth_token_t;

/* 同步适配器 */
typedef struct {
    int used;
    int id;
    char authority[64];
    char account_type[ACCOUNT_TYPE_LEN];
    int syncable;
    int auto_sync;
    int sync_interval;  /* 秒 */
    uint64_t last_sync;
    uint64_t next_sync;
    int syncing;
    int sync_result;   /* 0=ok, 1=error, 2=canceled */
    void (*on_sync)(int account_id, const char *authority);
} sync_adapter_t;

/* 同步状态 */
typedef enum {
    SYNC_STATE_IDLE = 0,
    SYNC_STATE_INITIALIZATION,
    SYNC_STATE_CONNECTING,
    SYNC_STATE_SYNCING,
    SYNC_STATE_FINALIZATION,
    SYNC_STATE_DONE,
    SYNC_STATE_ERROR,
} sync_state_t;

/* 账户管理器状态 */
typedef struct {
    int initialized;
    account_t accounts[ACCOUNT_MAX_ACCOUNTS];
    auth_token_t tokens[ACCOUNT_MAX_AUTHS];
    sync_adapter_t adapters[8];
    int account_count;
    int token_count;
    int adapter_count;
    sync_state_t sync_state;
    int current_sync_account;
    const char *current_sync_authority;
} account_manager_t;

/* 初始化账户管理器 */
void account_init(void);

/* 账户管理 */
int account_add(const char *name, const char *type, const char *password);
int account_remove(int account_id);
int account_get(int account_id, account_t *account);
int account_get_by_name(const char *name, account_t *account);
int account_list(account_t *list, int max);
int account_list_by_type(const char *type, account_t *list, int max);
int account_set_primary(int account_id);
int account_get_primary(account_t *account);
int account_set_enabled(int account_id, int enabled);
int account_count(void);

/* 认证 */
int account_get_auth_token(int account_id, const char *token_type, char *token, int len);
int account_invalidate_auth_token(int account_id, const char *token);
int account_set_auth_token(int account_id, const char *token_type, const char *token,
                           uint64_t expires_at);
int account_get_password(int account_id, char *password, int len);
int account_update_password(int account_id, const char *old_pw, const char *new_pw);

/* 同步管理 */
int sync_register_adapter(sync_adapter_t *adapter);
int sync_unregister_adapter(int adapter_id);
int sync_request(int account_id, const char *authority, int manual);
int sync_cancel(int account_id, const char *authority);
int sync_get_status(int account_id, const char *authority, sync_state_t *state);
int sync_set_auto_sync(int account_id, const char *authority, int enable);
int sync_get_auto_sync(int account_id, const char *authority);
int sync_set_interval(int account_id, const char *authority, int seconds);
int sync_list_adapters(sync_adapter_t *list, int max);

/* 云同步框架 */
int cloud_sync_start(int account_id);
int cloud_sync_stop(void);
int cloud_sync_get_state(sync_state_t *state);

/* 账户类型注册 */
int account_register_type(const char *type, const char *label, int supports_oauth);

/* 统计 */
int account_type_count(void);
int account_sync_active_count(void);

#endif /* ACCOUNT_H */
