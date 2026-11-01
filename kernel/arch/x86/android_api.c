/* ============================================================================
 * 赫普OS - Android API 兼容层
 * 为APK兼容层提供常见Android系统API的stub实现
 * ========================================================================== */

#include <stdint.h>
#include "android_api.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"

#define MAX_ACTIVITIES 16
#define MAX_INTENTS 32
#define MAX_PREFS 64

typedef struct {
    int used;
    char package[64];
    char name[64];
    aa_activity_state_t state;
} android_activity_t;

typedef struct {
    int used;
    char action[64];
    char data_uri[128];
} android_intent_t;

typedef struct {
    char key[32];
    char value[128];
    int is_int;
    int int_value;
} android_pref_t;

static android_activity_t activities[MAX_ACTIVITIES];
static android_intent_t intents[MAX_INTENTS];
static android_pref_t prefs[MAX_PREFS];
static int api_initialized = 0;

void android_api_init(void)
{
    memset(activities, 0, sizeof(activities));
    memset(intents, 0, sizeof(intents));
    memset(prefs, 0, sizeof(prefs));
    api_initialized = 1;
    vga_puts("[ANDROID-API] Android API compat layer init(API 34)\n");
}

/* === Activity 管理 === */
int android_activity_create(const char *package, const char *activity)
{
    for (int i = 0; i < MAX_ACTIVITIES; i++) {
        if (!activities[i].used) {
            activities[i].used = 1;
            strncpy(activities[i].package, package, 63);
            strncpy(activities[i].name, activity, 63);
            activities[i].state = AA_ACTIVITY_CREATED;
            return i;
        }
    }
    return ANDROID_ERROR;
}

int android_activity_start(int id)
{
    if (id < 0 || id >= MAX_ACTIVITIES || !activities[id].used) return ANDROID_ERROR;
    activities[id].state = AA_ACTIVITY_STARTED;
    return ANDROID_OK;
}

int android_activity_pause(int id)
{
    if (id < 0 || id >= MAX_ACTIVITIES || !activities[id].used) return ANDROID_ERROR;
    activities[id].state = AA_ACTIVITY_PAUSED;
    return ANDROID_OK;
}

int android_activity_resume(int id)
{
    if (id < 0 || id >= MAX_ACTIVITIES || !activities[id].used) return ANDROID_ERROR;
    activities[id].state = AA_ACTIVITY_RESUMED;
    return ANDROID_OK;
}

int android_activity_stop(int id)
{
    if (id < 0 || id >= MAX_ACTIVITIES || !activities[id].used) return ANDROID_ERROR;
    activities[id].state = AA_ACTIVITY_STOPPED;
    return ANDROID_OK;
}

int android_activity_destroy(int id)
{
    if (id < 0 || id >= MAX_ACTIVITIES || !activities[id].used) return ANDROID_ERROR;
    activities[id].state = AA_ACTIVITY_DESTROYED;
    activities[id].used = 0;
    return ANDROID_OK;
}

int android_activity_get_state(int id)
{
    if (id < 0 || id >= MAX_ACTIVITIES || !activities[id].used) return -1;
    return activities[id].state;
}

/* === Intent 系统 === */
int android_intent_create(const char *action, const char *data_uri)
{
    for (int i = 0; i < MAX_INTENTS; i++) {
        if (!intents[i].used) {
            intents[i].used = 1;
            strncpy(intents[i].action, action, 63);
            if (data_uri) strncpy(intents[i].data_uri, data_uri, 127);
            return i;
        }
    }
    return ANDROID_ERROR;
}

int android_intent_set_extra(int intent_id, const char *key, const char *value)
{
    (void)intent_id; (void)key; (void)value;
    return ANDROID_OK;  /* stub */
}

int android_intent_start_activity(int intent_id)
{
    if (intent_id < 0 || intent_id >= MAX_INTENTS || !intents[intent_id].used)
        return ANDROID_ERROR;
    return ANDROID_OK;  /* stub: 由兼容层调度 */
}

int android_intent_send_broadcast(int intent_id)
{
    if (intent_id < 0 || intent_id >= MAX_INTENTS || !intents[intent_id].used)
        return ANDROID_ERROR;
    return ANDROID_OK;  /* stub */
}

/* === Content Provider === */
int android_content_query(const char *uri, const char *projection,
                          const char *selection, const char *sort_order)
{
    (void)uri; (void)projection; (void)selection; (void)sort_order;
    return 0;  /* stub: 返回0行 */
}

int android_content_insert(const char *uri, const char *values)
{ (void)uri; (void)values; return ANDROID_OK; }

int android_content_update(const char *uri, const char *values, const char *where)
{ (void)uri; (void)values; (void)where; return 0; }

int android_content_delete(const char *uri, const char *where)
{ (void)uri; (void)where; return 0; }

/* === 权限检查 === */
int android_check_permission(const char *permission)
{
    (void)permission;
    return 0;  /* stub: 默认授予（实际由权限管理模块决定） */
}

int android_request_permission(const char *permission)
{
    (void)permission;
    return ANDROID_OK;  /* stub */
}

/* === 资源访问 === */
int android_get_string(int res_id, char *buf, int size)
{
    (void)res_id;
    if (buf && size > 0) { buf[0] = '\0'; return 0; }
    return ANDROID_ERROR;
}

int android_get_drawable(int res_id) { (void)res_id; return 0; }
int android_get_color(int res_id) { (void)res_id; return 0xFF000000; }

/* === 系统服务 === */
int android_get_system_service(const char *name)
{
    (void)name;
    return 1;  /* stub: 返回非空服务句柄 */
}

int android_notification_post(int id, const char *title, const char *text)
{
    (void)id;
    vga_printf("[NOTIFY] %s: %s\n", title, text);
    return ANDROID_OK;
}

int android_vibrate(int milliseconds)
{ (void)milliseconds; return ANDROID_OK; }

int android_toast_show(const char *text, int duration)
{
    (void)duration;
    vga_printf("[TOAST] %s\n", text);
    return ANDROID_OK;
}

/* === SharedPreferences === */
static android_pref_t *find_pref(const char *key)
{
    for (int i = 0; i < MAX_PREFS; i++) {
        if (prefs[i].key[0] && strcmp(prefs[i].key, key) == 0) return &prefs[i];
    }
    return NULL;
}

int android_prefs_get_string(const char *key, char *value, int size)
{
    android_pref_t *p = find_pref(key);
    if (!p || !value || size <= 0) return ANDROID_ERROR;
    strncpy(value, p->value, size - 1);
    value[size - 1] = '\0';
    return ANDROID_OK;
}

int android_prefs_put_string(const char *key, const char *value)
{
    android_pref_t *p = find_pref(key);
    if (!p) {
        for (int i = 0; i < MAX_PREFS; i++) {
            if (!prefs[i].key[0]) { p = &prefs[i]; break; }
        }
    }
    if (!p) return ANDROID_ERROR;
    strncpy(p->key, key, 31);
    strncpy(p->value, value, 127);
    p->is_int = 0;
    return ANDROID_OK;
}

int android_prefs_get_int(const char *key, int def_value)
{
    android_pref_t *p = find_pref(key);
    if (!p || !p->is_int) return def_value;
    return p->int_value;
}

int android_prefs_put_int(const char *key, int value)
{
    android_pref_t *p = find_pref(key);
    if (!p) {
        for (int i = 0; i < MAX_PREFS; i++) {
            if (!prefs[i].key[0]) { p = &prefs[i]; break; }
        }
    }
    if (!p) return ANDROID_ERROR;
    strncpy(p->key, key, 31);
    p->int_value = value;
    p->is_int = 1;
    return ANDROID_OK;
}
