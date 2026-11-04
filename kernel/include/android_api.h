#ifndef ANDROID_API_H
#define ANDROID_API_H

#include <stdint.h>

/* Android API 兼容层
 * 为APK兼容层提供常见Android系统API的stub实现
 */

#define ANDROID_API_VERSION 34  /* Android 14 */

/* Activity 生命周期状态 */
typedef enum {
    AA_ACTIVITY_CREATED = 0,
    AA_ACTIVITY_STARTED,
    AA_ACTIVITY_RESUMED,
    AA_ACTIVITY_PAUSED,
    AA_ACTIVITY_STOPPED,
    AA_ACTIVITY_DESTROYED
} aa_activity_state_t;

/* Intent 动作 */
#define INTENT_ACTION_MAIN      "android.intent.action.MAIN"
#define INTENT_ACTION_VIEW      "android.intent.action.VIEW"
#define INTENT_ACTION_SEND      "android.intent.action.SEND"
#define INTENT_ACTION_CALL      "android.intent.action.CALL"

/* 常见Android API返回码 */
#define ANDROID_OK              0
#define ANDROID_ERROR          -1
#define ANDROID_PERMISSION_DENIED -2
#define ANDROID_NOT_FOUND      -3

/* Activity 管理 */
int android_activity_create(const char *package, const char *activity);
int android_activity_start(int activity_id);
int android_activity_pause(int activity_id);
int android_activity_resume(int activity_id);
int android_activity_stop(int activity_id);
int android_activity_destroy(int activity_id);
int android_activity_get_state(int activity_id);

/* Intent 系统 */
int android_intent_create(const char *action, const char *data_uri);
int android_intent_set_extra(int intent_id, const char *key, const char *value);
int android_intent_start_activity(int intent_id);
int android_intent_send_broadcast(int intent_id);

/* Content Provider */
int android_content_query(const char *uri, const char *projection,
                          const char *selection, const char *sort_order);
int android_content_insert(const char *uri, const char *values);
int android_content_update(const char *uri, const char *values,
                           const char *where);
int android_content_delete(const char *uri, const char *where);

/* 权限检查 */
int android_check_permission(const char *permission);
int android_request_permission(const char *permission);

/* 资源访问 */
int android_get_string(int res_id, char *buf, int size);
int android_get_drawable(int res_id);
int android_get_color(int res_id);

/* 系统服务 */
int android_get_system_service(const char *name);
int android_notification_post(int id, const char *title, const char *text);
int android_vibrate(int milliseconds);
int android_toast_show(const char *text, int duration);

/* SharedPreferences */
int android_prefs_get_string(const char *key, char *value, int size);
int android_prefs_put_string(const char *key, const char *value);
int android_prefs_get_int(const char *key, int def_value);
int android_prefs_put_int(const char *key, int value);

/* 初始化 */
void android_api_init(void);

#endif /* ANDROID_API_H */
