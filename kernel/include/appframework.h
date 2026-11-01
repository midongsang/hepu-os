#ifndef APPFRAMEWORK_H
#define APPFRAMEWORK_H

#include <stdint.h>

#define AF_MAX_ACTIVITIES 32
#define AF_MAX_SERVICES 16
#define AF_MAX_BROADCAST_RECEIVERS 32
#define AF_MAX_PROVIDERS 16
#define AF_MAX_INTENTS 64
#define AF_MAX_TASKS 16
#define AF_COMPONENT_NAME_LEN 64
#define AF_ACTION_LEN 64
#define AF_EXTRA_KEY_LEN 32
#define AF_EXTRA_VALUE_LEN 256

/* 组件类型 */
typedef enum {
    COMPONENT_ACTIVITY = 0,
    COMPONENT_SERVICE,
    COMPONENT_BROADCAST_RECEIVER,
    COMPONENT_CONTENT_PROVIDER,
} component_type_t;

/* Activity生命周期状态 */
typedef enum {
    ACTIVITY_CREATED = 0,
    ACTIVITY_STARTED,
    ACTIVITY_RESUMED,
    ACTIVITY_PAUSED,
    ACTIVITY_STOPPED,
    ACTIVITY_DESTROYED,
} activity_state_t;

/* Service状态 */
typedef enum {
    SERVICE_CREATED = 0,
    SERVICE_STARTED,
    SERVICE_BOUND,
    SERVICE_DESTROYED,
} af_service_state_t;

/* Intent */
typedef struct {
    int used;
    char action[AF_ACTION_LEN];
    char component[AF_COMPONENT_NAME_LEN];
    char category[32];
    char data_type[32];
    int flags;
    /* extras (简化: 固定数量键值对) */
    char extra_keys[8][AF_EXTRA_KEY_LEN];
    char extra_values[8][AF_EXTRA_VALUE_LEN];
    int extra_count;
    int request_code;
} af_intent_t;

/* Activity */
typedef struct {
    int used;
    int id;
    char name[AF_COMPONENT_NAME_LEN];
    char package[AF_COMPONENT_NAME_LEN];
    activity_state_t state;
    int task_id;
    int launch_mode;  /* 0=standard, 1=singleTop, 2=singleTask, 3=singleInstance */
    int visible;
    int focus;
    uint64_t launch_time;
    /* 生命周期回调 */
    void (*onCreate)(af_intent_t *intent);
    void (*onStart)(void);
    void (*onResume)(void);
    void (*onPause)(void);
    void (*onStop)(void);
    void (*onDestroy)(void);
    void (*onActivityResult)(int request_code, int result_code, af_intent_t *data);
} af_activity_t;

/* Service */
typedef struct {
    int used;
    int id;
    char name[AF_COMPONENT_NAME_LEN];
    char package[AF_COMPONENT_NAME_LEN];
    af_service_state_t state;
    int start_count;
    int client_count;
    uint64_t start_time;
    /* 回调 */
    void (*onCreate)(void);
    int (*onStartCommand)(af_intent_t *intent, int flags, int start_id);
    void (*onBind)(af_intent_t *intent);
    void (*onUnbind)(af_intent_t *intent);
    void (*onDestroy)(void);
} af_service_t;

/* BroadcastReceiver */
typedef struct {
    int used;
    int id;
    char name[AF_COMPONENT_NAME_LEN];
    char action_filter[AF_ACTION_LEN];
    int priority;
    int enabled;
    void (*onReceive)(af_intent_t *intent);
} af_receiver_t;

/* ContentProvider */
typedef struct {
    int used;
    int id;
    char name[AF_COMPONENT_NAME_LEN];
    char authority[AF_COMPONENT_NAME_LEN];
    int (*query)(const char *uri, char *result, int len);
    int (*insert)(const char *uri, const char *values);
    int (*update)(const char *uri, const char *values, const char *where);
    int (*delete)(const char *uri, const char *where);
    int (*getType)(const char *uri, char *type, int len);
} af_provider_t;

/* Task (Activity栈) */
typedef struct {
    int used;
    int id;
    char name[AF_COMPONENT_NAME_LEN];
    int activity_stack[AF_MAX_ACTIVITIES];
    int stack_top;
} af_task_t;

/* 初始化应用框架 */
void appframework_init(void);

/* Activity管理 */
int af_activity_register(af_activity_t *activity);
int af_activity_start(af_intent_t *intent);
int af_activity_finish(int activity_id);
int af_activity_move_to_front(int activity_id);
int af_activity_get_state(int activity_id);
int af_activity_get_current(void);
int af_activity_list(af_activity_t *list, int max);

/* Service管理 */
int af_service_register(af_service_t *service);
int af_service_start(af_intent_t *intent);
int af_service_stop(af_intent_t *intent);
int af_service_bind(af_intent_t *intent);
int af_service_unbind(int service_id);
int af_service_get_state(int service_id);
int af_service_list(af_service_t *list, int max);

/* Broadcast管理 */
int af_receiver_register(af_receiver_t *receiver);
int af_receiver_unregister(int receiver_id);
int af_send_broadcast(af_intent_t *intent);
int af_send_sticky_broadcast(af_intent_t *intent);
int af_receiver_list(af_receiver_t *list, int max);

/* ContentProvider管理 */
int af_provider_register(af_provider_t *provider);
int af_provider_query(const char *uri, char *result, int len);
int af_provider_insert(const char *uri, const char *values);
int af_provider_update(const char *uri, const char *values, const char *where);
int af_provider_delete(const char *uri, const char *where);
int af_provider_get_type(const char *uri, char *type, int len);

/* Intent操作 */
int af_intent_create(af_intent_t *intent, const char *action);
int af_intent_put_extra(af_intent_t *intent, const char *key, const char *value);
int af_intent_get_extra(af_intent_t *intent, const char *key, char *value, int len);

/* Task管理 */
int af_task_create(const char *name);
int af_task_get_top(int task_id);
int af_task_list(af_task_t *list, int max);

/* 获取组件数量 */
int af_activity_count(void);
int af_service_count(void);
int af_receiver_count(void);
int af_provider_count(void);

#endif /* APPFRAMEWORK_H */
