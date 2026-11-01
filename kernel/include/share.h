#ifndef SHARE_H
#define SHARE_H

#include <stdint.h>

#define SHARE_MAX_TARGETS 16
#define SHARE_MAX_MIME_TYPES 16
#define SHARE_CLIPBOARD_MAX 8192
#define SHARE_FILE_MAX 256

/* 共享数据类型 */
typedef enum {
    SHARE_TYPE_TEXT = 0,
    SHARE_TYPE_IMAGE,
    SHARE_TYPE_AUDIO,
    SHARE_TYPE_VIDEO,
    SHARE_TYPE_FILE,
    SHARE_TYPE_URI,
    SHARE_TYPE_CONTACT,
    SHARE_TYPE_CALENDAR,
} share_type_t;

/* 剪贴板数据 */
typedef struct {
    int used;
    share_type_t type;
    char mime_type[64];
    char text[SHARE_CLIPBOARD_MAX];
    void *data;
    uint32_t data_size;
    uint64_t timestamp;
    char source_package[64];
} clipboard_data_t;

/* 共享目标应用 */
typedef struct {
    int used;
    int id;
    char package_name[64];
    char label[64];
    int icon;
    char supported_mimes[SHARE_MAX_MIME_TYPES][64];
    int mime_count;
    int priority;
} share_target_t;

/* 共享意图 */
typedef struct {
    share_type_t type;
    char action[32];
    char mime_type[64];
    char text[SHARE_CLIPBOARD_MAX];
    char subject[256];
    char *uris[16];
    int uri_count;
    void *extra_data;
    uint32_t extra_size;
    char package_filter[64];  /* 限定特定应用 */
} share_intent_t;

/* 拖拽状态 */
typedef enum {
    DRAG_STATE_IDLE = 0,
    DRAG_STATE_STARTED,
    DRAG_STATE_DRAGGING,
    DRAG_STATE_DROPPED,
    DRAG_STATE_CANCELED,
} drag_state_t;

/* 拖拽会话 */
typedef struct {
    int active;
    int source_window;
    int target_window;
    drag_state_t state;
    int start_x, start_y;
    int current_x, current_y;
    share_type_t data_type;
    void *data;
    uint32_t data_size;
    void (*on_drop)(int target_x, int target_y, void *data, uint32_t size);
} drag_session_t;

/* 文件选择器模式 */
typedef enum {
    FILE_PICKER_OPEN = 0,
    FILE_PICKER_SAVE,
    FILE_PICKER_DIRECTORY,
    FILE_PICKER_MULTIPLE,
} file_picker_mode_t;

/* 文件选择器结果 */
typedef struct {
    char paths[SHARE_FILE_MAX][256];
    int count;
    int canceled;
} file_picker_result_t;

/* 初始化共享子系统 */
void share_init(void);

/* 剪贴板 */
int clipboard_set_text(const char *text, const char *source);
int clipboard_get_text(char *buf, int len);
int clipboard_set_data(share_type_t type, const char *mime, void *data, uint32_t size, const char *source);
int clipboard_get_data(clipboard_data_t *data);
int clipboard_has_data(void);
int clipboard_clear(void);
int clipboard_get_primary(clipboard_data_t *data);

/* 共享目标注册 */
int share_register_target(share_target_t *target);
int share_unregister_target(int target_id);
int share_list_targets(const char *mime_type, share_target_t *list, int max);
int share_get_target(int id, share_target_t *target);

/* 执行共享 */
int share_send(share_intent_t *intent);
int share_send_text(const char *text, const char *mime);
int share_send_file(const char *path, const char *mime);
int share_send_uri(const char *uri, const char *mime);

/* 解析共享目标 */
int share_resolve(share_intent_t *intent, share_target_t *targets, int max);

/* 拖拽 */
int drag_start(int source_win, int x, int y, share_type_t type, void *data, uint32_t size,
               void (*on_drop)(int, int, void *, uint32_t));
int drag_update(int x, int y);
int drag_drop(int x, int y);
int drag_cancel(void);
int drag_get_state(drag_session_t *session);

/* 文件选择器 */
int file_picker_open(file_picker_mode_t mode, const char *initial_dir,
                     const char *filter_mime, const char *title);
int file_picker_get_result(file_picker_result_t *result);
int file_picker_is_active(void);
int file_picker_cancel(void);

/* 共享统计 */
int share_target_count(void);
int share_sent_count(void);

#endif /* SHARE_H */
