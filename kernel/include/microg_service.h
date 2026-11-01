#ifndef MICROG_SERVICE_H
#define MICROG_SERVICE_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS microG 服务集成
 * GMS替代 / 位置服务 / 推送 / 地图 / 账号
 * ========================================================================== */

#define MG_MAX_SERVICES 16
#define MG_MAX_ACCOUNTS 32
#define MG_MAX_LOCATIONS 64

/* 服务类型 */
typedef enum {
    MG_SERVICE_LOCATION = 0,     /* 位置服务 */
    MG_SERVICE_PUSH,             /* 推送通知(FCM替代) */
    MG_SERVICE_MAPS,             /* 地图服务 */
    MG_SERVICE_AUTH,             /* 账号认证 */
    MG_SERVICE_CLOUD_MESSAGING,  /* 云消息 */
    MG_SERVICE_SAFETYNET,        /* 设备认证 */
    MG_SERVICE_ADVERTISING,      /* 广告ID */
    MG_SERVICE_FITNESS,          /* 健康数据 */
    MG_SERVICE_PAYMENTS,         /* 支付 */
    MG_SERVICE_NEARBY,           /* 附近设备 */
    MG_SERVICE_CAST,             /* 投屏 */
    MG_SERVICE_DRIVE,            /* 云存储 */
} mg_service_type_t;

/* 服务状态 */
typedef enum {
    MG_STATE_STOPPED = 0,
    MG_STATE_STARTING,
    MG_STATE_RUNNING,
    MG_STATE_ERROR,
} mg_service_state_t;

/* 服务信息 */
typedef struct {
    int used;
    mg_service_type_t type;
    mg_service_state_t state;
    char name[64];
    char description[256];
    int enabled;
    int priority;
    uint64_t start_time;
    uint64_t total_requests;
    uint64_t total_errors;
} mg_service_t;

/* 位置信息 */
typedef struct {
    double latitude;
    double longitude;
    double altitude;
    float accuracy;
    float speed;
    float bearing;
    uint64_t timestamp;
    int provider;  /* 0=gps,1=network,2=passive,3=fused */
    int satellites;
} mg_location_t;

/* 账号 */
typedef struct {
    int used;
    int id;
    char email[128];
    char display_name[64];
    char type[32];  /* google/hepu/local */
    int authenticated;
    uint64_t login_time;
    char token[256];
} mg_account_t;

/* 推送消息 */
typedef struct {
    int id;
    char from[128];
    char to[128];
    char title[128];
    char body[512];
    char data[1024];
    int priority;  /* 0=normal,1=high */
    uint64_t sent_time;
    int delivered;
} mg_push_message_t;

/* 设备认证结果 */
typedef enum {
    MG_ATTEST_PASS = 0,
    MG_ATTEST_BASIC,
    MG_ATTEST_FAIL,
} mg_attest_result_t;

/* microG状态 */
typedef struct {
    int initialized;
    int enabled;
    mg_service_t services[MG_MAX_SERVICES];
    int service_count;
    mg_account_t accounts[MG_MAX_ACCOUNTS];
    int account_count;
    mg_location_t last_location;
    int has_location;
    /* 配置 */
    int location_mode;  /* 0=off,1=device_only,2=battery_saving,3=high_accuracy */
    int push_enabled;
    int maps_enabled;
    int safety_net_enabled;
    int ad_tracking_enabled;
    /* 统计 */
    uint64_t total_location_requests;
    uint64_t total_push_messages;
    uint64_t total_api_calls;
} microg_service_t;

/* 初始化 */
void microg_service_init(void);

/* 服务管理 */
int mg_service_start(mg_service_type_t type);
int mg_service_stop(mg_service_type_t type);
int mg_service_get(mg_service_type_t type, mg_service_t *svc);
int mg_service_list(mg_service_t *svcs, int max);
int mg_service_enable(mg_service_type_t type, int enable);

/* 位置服务 */
int mg_location_get_last(mg_location_t *loc);
int mg_location_request_updates(int min_time, float min_distance);
int mg_location_remove_updates(void);
int mg_location_set_mode(int mode);
int mg_location_get_mode(int *mode);

/* 推送服务 */
int mg_push_send(const mg_push_message_t *msg);
int mg_push_receive(mg_push_message_t *msg);
int mg_push_register(const char *sender_id, char *token, int max);
int mg_push_unregister(const char *token);

/* 账号服务 */
int mg_account_add(const char *email, const char *type, const char *password);
int mg_account_remove(int id);
int mg_account_get(int id, mg_account_t *acc);
int mg_account_list(mg_account_t *accounts, int max);
int mg_account_signin(int id);
int mg_account_signout(int id);
int mg_account_get_token(int id, char *token, int max);

/* 设备认证 */
int mg_safetynet_attest(mg_attest_result_t *result);
int mg_safetynet_verify(const char *nonce, char *result, int max);

/* 广告ID */
int mg_advertising_get_id(char *id, int max);
int mg_advertising_set_tracking(int enable);

/* 配置 */
int microg_set_enabled(int enable);
int microg_get_enabled(int *enabled);
int microg_get_stats(microg_service_t *stats);

#endif /* MICROG_SERVICE_H */
