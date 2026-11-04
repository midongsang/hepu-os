/* ============================================================================
 * 赫普OS - 应用框架
 * Activity/Service/BroadcastReceiver/ContentProvider 四大组件
 * Intent + Task回退栈
 * ========================================================================== */

#include <stdint.h>
#include "appframework.h"
#include "vga.h"
#include "string.h"


static af_activity_t activities[AF_MAX_ACTIVITIES];
static af_service_t services[AF_MAX_SERVICES];
static af_receiver_t receivers[AF_MAX_BROADCAST_RECEIVERS];
static af_provider_t providers[AF_MAX_PROVIDERS];
static af_task_t tasks[AF_MAX_TASKS];
static int activity_count = 0;
static int service_count = 0;
static int receiver_count = 0;
static int provider_count = 0;
static int task_count = 0;
static int af_initialized = 0;

void appframework_init(void)
{
    for (int i = 0; i < AF_MAX_ACTIVITIES; i++) activities[i].used = 0;
    for (int i = 0; i < AF_MAX_SERVICES; i++) services[i].used = 0;
    for (int i = 0; i < AF_MAX_BROADCAST_RECEIVERS; i++) receivers[i].used = 0;
    for (int i = 0; i < AF_MAX_PROVIDERS; i++) providers[i].used = 0;
    for (int i = 0; i < AF_MAX_TASKS; i++) tasks[i].used = 0;
    activity_count = service_count = receiver_count = provider_count = task_count = 0;
    af_initialized = 1;

    /* 创建默认Task */
    af_task_create("Main Task");

    vga_puts("[APPFW] app framework init done \n");
    vga_puts(" 4 components: Activity/Service/Broadcast/Provider\n");
}

int af_activity_register(af_activity_t *activity)
{
    if (!activity || !af_initialized) return -1;
    for (int i = 0; i < AF_MAX_ACTIVITIES; i++) {
        if (!activities[i].used) {
            memcpy(&activities[i], activity, sizeof(af_activity_t));
            activities[i].used = 1;
            activities[i].id = i;
            activities[i].state = ACTIVITY_CREATED;
            activity_count++;
            return i;
        }
    }
    return -1;
}

int af_activity_start(af_intent_t *intent)
{
    if (!intent || !af_initialized) return -1;

    /* 查找匹配的Activity */
    for (int i = 0; i < AF_MAX_ACTIVITIES; i++) {
        if (activities[i].used &&
            strcmp(activities[i].name, intent->component) == 0) {

            /* 生命周期: onCreate -> onStart -> onResume */
            if (activities[i].state == ACTIVITY_CREATED ||
                activities[i].state == ACTIVITY_DESTROYED) {
                if (activities[i].onCreate) activities[i].onCreate(intent);
                activities[i].state = ACTIVITY_CREATED;
            }
            if (activities[i].onStart) activities[i].onStart();
            activities[i].state = ACTIVITY_STARTED;
            if (activities[i].onResume) activities[i].onResume();
            activities[i].state = ACTIVITY_RESUMED;
            activities[i].visible = 1;
            activities[i].focus = 1;

            /* 加入Task栈 */
            int task_id = activities[i].task_id;
            if (task_id < 0) task_id = 0;
            if (tasks[task_id].used) {
                tasks[task_id].activity_stack[tasks[task_id].stack_top++] = i;
            }

            vga_printf("[APPFW] start Activity: %s\n", activities[i].name);
            return i;
        }
    }
    vga_printf("[APPFW] not found Activity: %s\n", intent->component);
    return -1;
}

int af_activity_finish(int activity_id)
{
    if (activity_id < 0 || activity_id >= AF_MAX_ACTIVITIES || !activities[activity_id].used) return -1;

    /* 生命周期: onPause -> onStop -> onDestroy */
    if (activities[activity_id].onPause) activities[activity_id].onPause();
    activities[activity_id].state = ACTIVITY_PAUSED;
    if (activities[activity_id].onStop) activities[activity_id].onStop();
    activities[activity_id].state = ACTIVITY_STOPPED;
    if (activities[activity_id].onDestroy) activities[activity_id].onDestroy();
    activities[activity_id].state = ACTIVITY_DESTROYED;
    activities[activity_id].visible = 0;
    activities[activity_id].focus = 0;

    vga_printf("[APPFW] finish Activity: %s\n", activities[activity_id].name);
    return 0;
}

int af_activity_move_to_front(int activity_id)
{
    if (activity_id < 0 || activity_id >= AF_MAX_ACTIVITIES || !activities[activity_id].used) return -1;
    if (activities[activity_id].state == ACTIVITY_STOPPED) {
        if (activities[activity_id].onStart) activities[activity_id].onStart();
        activities[activity_id].state = ACTIVITY_STARTED;
    }
    if (activities[activity_id].state == ACTIVITY_PAUSED) {
        if (activities[activity_id].onResume) activities[activity_id].onResume();
        activities[activity_id].state = ACTIVITY_RESUMED;
    }
    activities[activity_id].visible = 1;
    activities[activity_id].focus = 1;
    return 0;
}

int af_activity_get_state(int activity_id)
{
    if (activity_id < 0 || activity_id >= AF_MAX_ACTIVITIES || !activities[activity_id].used) return -1;
    return activities[activity_id].state;
}

int af_activity_get_current(void)
{
    for (int i = 0; i < AF_MAX_ACTIVITIES; i++) {
        if (activities[i].used && activities[i].state == ACTIVITY_RESUMED) return i;
    }
    return -1;
}

int af_activity_list(af_activity_t *list, int max)
{
    if (!list || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < AF_MAX_ACTIVITIES && count < max; i++) {
        if (activities[i].used) memcpy(&list[count++], &activities[i], sizeof(af_activity_t));
    }
    return count;
}

int af_service_register(af_service_t *service)
{
    if (!service || !af_initialized) return -1;
    for (int i = 0; i < AF_MAX_SERVICES; i++) {
        if (!services[i].used) {
            memcpy(&services[i], service, sizeof(af_service_t));
            services[i].used = 1;
            services[i].id = i;
            services[i].state = SERVICE_CREATED;
            service_count++;
            return i;
        }
    }
    return -1;
}

int af_service_start(af_intent_t *intent)
{
    if (!intent || !af_initialized) return -1;
    for (int i = 0; i < AF_MAX_SERVICES; i++) {
        if (services[i].used && strcmp(services[i].name, intent->component) == 0) {
            if (services[i].state == SERVICE_CREATED || services[i].state == SERVICE_DESTROYED) {
                if (services[i].onCreate) services[i].onCreate();
            }
            if (services[i].onStartCommand) services[i].onStartCommand(intent, 0, services[i].start_count);
            services[i].state = SERVICE_STARTED;
            services[i].start_count++;
            vga_printf("[APPFW] start Service: %s\n", services[i].name);
            return i;
        }
    }
    return -1;
}

int af_service_stop(af_intent_t *intent)
{
    if (!intent || !af_initialized) return -1;
    for (int i = 0; i < AF_MAX_SERVICES; i++) {
        if (services[i].used && strcmp(services[i].name, intent->component) == 0) {
            if (services[i].onDestroy) services[i].onDestroy();
            services[i].state = SERVICE_DESTROYED;
            vga_printf("[APPFW] stop Service: %s\n", services[i].name);
            return 0;
        }
    }
    return -1;
}

int af_service_bind(af_intent_t *intent)
{
    if (!intent || !af_initialized) return -1;
    for (int i = 0; i < AF_MAX_SERVICES; i++) {
        if (services[i].used && strcmp(services[i].name, intent->component) == 0) {
            if (services[i].state == SERVICE_CREATED) {
                if (services[i].onCreate) services[i].onCreate();
            }
            if (services[i].onBind) services[i].onBind(intent);
            services[i].state = SERVICE_BOUND;
            services[i].client_count++;
            return i;
        }
    }
    return -1;
}

int af_service_unbind(int service_id)
{
    if (service_id < 0 || service_id >= AF_MAX_SERVICES || !services[service_id].used) return -1;
    services[service_id].client_count--;
    if (services[service_id].client_count <= 0) {
        if (services[service_id].onUnbind) services[service_id].onUnbind(NULL);
        services[service_id].state = SERVICE_STARTED;
    }
    return 0;
}

int af_service_get_state(int service_id)
{
    if (service_id < 0 || service_id >= AF_MAX_SERVICES || !services[service_id].used) return -1;
    return services[service_id].state;
}

int af_service_list(af_service_t *list, int max)
{
    if (!list || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < AF_MAX_SERVICES && count < max; i++) {
        if (services[i].used) memcpy(&list[count++], &services[i], sizeof(af_service_t));
    }
    return count;
}

int af_receiver_register(af_receiver_t *receiver)
{
    if (!receiver || !af_initialized) return -1;
    for (int i = 0; i < AF_MAX_BROADCAST_RECEIVERS; i++) {
        if (!receivers[i].used) {
            memcpy(&receivers[i], receiver, sizeof(af_receiver_t));
            receivers[i].used = 1;
            receivers[i].id = i;
            receivers[i].enabled = 1;
            receiver_count++;
            return i;
        }
    }
    return -1;
}

int af_receiver_unregister(int receiver_id)
{
    if (receiver_id < 0 || receiver_id >= AF_MAX_BROADCAST_RECEIVERS || !receivers[receiver_id].used) return -1;
    receivers[receiver_id].used = 0;
    receiver_count--;
    return 0;
}

int af_send_broadcast(af_intent_t *intent)
{
    if (!intent || !af_initialized) return -1;
    int delivered = 0;
    for (int i = 0; i < AF_MAX_BROADCAST_RECEIVERS; i++) {
        if (receivers[i].used && receivers[i].enabled &&
            strcmp(receivers[i].action_filter, intent->action) == 0) {
            if (receivers[i].onReceive) receivers[i].onReceive(intent);
            delivered++;
        }
    }
    vga_printf("[APPFW] broadcast %s: deliver to %d receivers \n", intent->action, delivered);
    return delivered;
}

int af_send_sticky_broadcast(af_intent_t *intent)
{
    /* 原型阶段: 同普通广播 */
    return af_send_broadcast(intent);
}

int af_receiver_list(af_receiver_t *list, int max)
{
    if (!list || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < AF_MAX_BROADCAST_RECEIVERS && count < max; i++) {
        if (receivers[i].used) memcpy(&list[count++], &receivers[i], sizeof(af_receiver_t));
    }
    return count;
}

int af_provider_register(af_provider_t *provider)
{
    if (!provider || !af_initialized) return -1;
    for (int i = 0; i < AF_MAX_PROVIDERS; i++) {
        if (!providers[i].used) {
            memcpy(&providers[i], provider, sizeof(af_provider_t));
            providers[i].used = 1;
            providers[i].id = i;
            provider_count++;
            return i;
        }
    }
    return -1;
}

static af_provider_t *find_provider(const char *uri)
{
    if (!uri) return NULL;
    /* 解析 authority: content://authority/path */
    const char *auth_start = strstr(uri, "://");
    if (!auth_start) return NULL;
    auth_start += 3;
    const char *auth_end = strchr(auth_start, '/');
    int auth_len = auth_end ? (int)(auth_end - auth_start) : (int)strlen(auth_start);

    for (int i = 0; i < AF_MAX_PROVIDERS; i++) {
        if (providers[i].used &&
            strncmp(providers[i].authority, auth_start, auth_len) == 0 &&
            (int)(strlen(providers[i].authority)) == auth_len) {
            return &providers[i];
        }
    }
    return NULL;
}

int af_provider_query(const char *uri, char *result, int len)
{
    af_provider_t *p = find_provider(uri);
    if (!p || !p->query) return -1;
    return p->query(uri, result, len);
}

int af_provider_insert(const char *uri, const char *values)
{
    af_provider_t *p = find_provider(uri);
    if (!p || !p->insert) return -1;
    return p->insert(uri, values);
}

int af_provider_update(const char *uri, const char *values, const char *where)
{
    af_provider_t *p = find_provider(uri);
    if (!p || !p->update) return -1;
    return p->update(uri, values, where);
}

int af_provider_delete(const char *uri, const char *where)
{
    af_provider_t *p = find_provider(uri);
    if (!p || !p->delete) return -1;
    return p->delete(uri, where);
}

int af_provider_get_type(const char *uri, char *type, int len)
{
    af_provider_t *p = find_provider(uri);
    if (!p || !p->getType) return -1;
    return p->getType(uri, type, len);
}

int af_intent_create(af_intent_t *intent, const char *action)
{
    if (!intent) return -1;
    memset(intent, 0, sizeof(af_intent_t));
    intent->used = 1;
    if (action) strncpy(intent->action, action, AF_ACTION_LEN - 1);
    return 0;
}

int af_intent_put_extra(af_intent_t *intent, const char *key, const char *value)
{
    if (!intent || !key || !value) return -1;
    if (intent->extra_count >= 8) return -1;
    strncpy(intent->extra_keys[intent->extra_count], key, AF_EXTRA_KEY_LEN - 1);
    strncpy(intent->extra_values[intent->extra_count], value, AF_EXTRA_VALUE_LEN - 1);
    intent->extra_count++;
    return 0;
}

int af_intent_get_extra(af_intent_t *intent, const char *key, char *value, int len)
{
    if (!intent || !key || !value) return -1;
    for (int i = 0; i < intent->extra_count; i++) {
        if (strcmp(intent->extra_keys[i], key) == 0) {
            strncpy(value, intent->extra_values[i], len - 1);
            value[len - 1] = '\0';
            return 0;
        }
    }
    return -1;
}

int af_task_create(const char *name)
{
    if (!af_initialized) return -1;
    for (int i = 0; i < AF_MAX_TASKS; i++) {
        if (!tasks[i].used) {
            memset(&tasks[i], 0, sizeof(af_task_t));
            tasks[i].used = 1;
            tasks[i].id = i;
            if (name) strncpy(tasks[i].name, name, AF_COMPONENT_NAME_LEN - 1);
            tasks[i].stack_top = 0;
            task_count++;
            return i;
        }
    }
    return -1;
}

int af_task_get_top(int task_id)
{
    if (task_id < 0 || task_id >= AF_MAX_TASKS || !tasks[task_id].used) return -1;
    if (tasks[task_id].stack_top <= 0) return -1;
    return tasks[task_id].activity_stack[tasks[task_id].stack_top - 1];
}

int af_task_list(af_task_t *list, int max)
{
    if (!list || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < AF_MAX_TASKS && count < max; i++) {
        if (tasks[i].used) memcpy(&list[count++], &tasks[i], sizeof(af_task_t));
    }
    return count;
}

int af_activity_count(void) { return activity_count; }
int af_service_count(void) { return service_count; }
int af_receiver_count(void) { return receiver_count; }
int af_provider_count(void) { return provider_count; }
