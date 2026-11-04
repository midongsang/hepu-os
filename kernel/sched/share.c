/* ============================================================================
 * 赫普OS - 内容共享子系统
 * 剪贴板 + ShareAction + 拖拽 + 文件选择器
 * ========================================================================== */

#include <stdint.h>
#include "share.h"
#include "vga.h"
#include "string.h"
static clipboard_data_t clipboard;
static share_target_t targets[SHARE_MAX_TARGETS];
static drag_session_t drag;
static file_picker_result_t picker_result;
static int picker_active = 0;
static int target_count = 0;
static int share_sent = 0;
static int share_initialized = 0;

void share_init(void)
{
    memset(&clipboard, 0, sizeof(clipboard_data_t));
    for (int i = 0; i < SHARE_MAX_TARGETS; i++) targets[i].used = 0;
    memset(&drag, 0, sizeof(drag_session_t));
    memset(&picker_result, 0, sizeof(file_picker_result_t));
    target_count = 0;
    share_sent = 0;
    picker_active = 0;
    share_initialized = 1;

    /* 注册默认共享目标 */
    share_target_t t;
    memset(&t, 0, sizeof(t));
    t.used = 1;
    strcpy(t.package_name, "com.hepu.messaging");
    strcpy(t.label, "Messages");
    t.mime_count = 2;
    strcpy(t.supported_mimes[0], "text/plain");
    strcpy(t.supported_mimes[1], "image/*");
    t.priority = 0;
    share_register_target(&t);

    memset(&t, 0, sizeof(t));
    t.used = 1;
    strcpy(t.package_name, "com.hepu.email");
    strcpy(t.label, "Email");
    t.mime_count = 3;
    strcpy(t.supported_mimes[0], "text/plain");
    strcpy(t.supported_mimes[1], "image/*");
    strcpy(t.supported_mimes[2], "file/*");
    t.priority = 1;
    share_register_target(&t);

    memset(&t, 0, sizeof(t));
    t.used = 1;
    strcpy(t.package_name, "com.hepu.gallery");
    strcpy(t.label, "Gallery");
    t.mime_count = 2;
    strcpy(t.supported_mimes[0], "image/*");
    strcpy(t.supported_mimes[1], "video/*");
    t.priority = 2;
    share_register_target(&t);

    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[SHARE] init done\n");
}

int clipboard_set_text(const char *text, const char *source)
{
    if (!text || !share_initialized) return -1;
    memset(&clipboard, 0, sizeof(clipboard_data_t));
    clipboard.used = 1;
    clipboard.type = SHARE_TYPE_TEXT;
    strcpy(clipboard.mime_type, "text/plain");
    strncpy(clipboard.text, text, SHARE_CLIPBOARD_MAX - 1);
    clipboard.timestamp = 0;
    if (source) strncpy(clipboard.source_package, source, 63);
    return 0;
}

int clipboard_get_text(char *buf, int len)
{
    if (!buf || len <= 0 || !clipboard.used) return -1;
    strncpy(buf, clipboard.text, len - 1);
    buf[len - 1] = '\0';
    return strlen(buf);
}

int clipboard_set_data(share_type_t type, const char *mime, void *data, uint32_t size, const char *source)
{
    if (!share_initialized) return -1;
    memset(&clipboard, 0, sizeof(clipboard_data_t));
    clipboard.used = 1;
    clipboard.type = type;
    if (mime) strncpy(clipboard.mime_type, mime, 63);
    clipboard.data = data;
    clipboard.data_size = size;
    clipboard.timestamp = 0;
    if (source) strncpy(clipboard.source_package, source, 63);
    return 0;
}

int clipboard_get_data(clipboard_data_t *data)
{
    if (!data || !clipboard.used) return -1;
    memcpy(data, &clipboard, sizeof(clipboard_data_t));
    return 0;
}

int clipboard_has_data(void) { return clipboard.used ? 1 : 0; }

int clipboard_clear(void)
{
    memset(&clipboard, 0, sizeof(clipboard_data_t));
    return 0;
}

int clipboard_get_primary(clipboard_data_t *data)
{
    return clipboard_get_data(data);
}

int share_register_target(share_target_t *target)
{
    if (!target || !share_initialized) return -1;
    for (int i = 0; i < SHARE_MAX_TARGETS; i++) {
        if (!targets[i].used) {
            memcpy(&targets[i], target, sizeof(share_target_t));
            targets[i].used = 1;
            targets[i].id = i;
            target_count++;
            return i;
        }
    }
    return -1;
}

int share_unregister_target(int target_id)
{
    if (target_id < 0 || target_id >= SHARE_MAX_TARGETS || !targets[target_id].used) return -1;
    targets[target_id].used = 0;
    target_count--;
    return 0;
}

static int mime_matches(const char *supported, const char *requested)
{
    if (!supported || !requested) return 0;
    if (strcmp(supported, "*/*") == 0) return 1;
    if (strcmp(supported, requested) == 0) return 1;
    /* 通配符匹配 image star vs image png */
    const char *star = strchr(supported, '*');
    if (star) {
        int prefix_len = star - supported;
        return strncmp(supported, requested, prefix_len) == 0;
    }
    return 0;
}

int share_list_targets(const char *mime_type, share_target_t *list, int max)
{
    if (!list || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < SHARE_MAX_TARGETS && count < max; i++) {
        if (!targets[i].used) continue;
        if (!mime_type) {
            memcpy(&list[count++], &targets[i], sizeof(share_target_t));
        } else {
            for (int m = 0; m < targets[i].mime_count; m++) {
                if (mime_matches(targets[i].supported_mimes[m], mime_type)) {
                    memcpy(&list[count++], &targets[i], sizeof(share_target_t));
                    break;
                }
            }
        }
    }
    return count;
}

int share_get_target(int id, share_target_t *target)
{
    if (id < 0 || id >= SHARE_MAX_TARGETS || !targets[id].used || !target) return -1;
    memcpy(target, &targets[id], sizeof(share_target_t));
    return 0;
}

int share_send(share_intent_t *intent)
{
    if (!intent || !share_initialized) return -1;
    share_target_t matched[SHARE_MAX_TARGETS];
    int count = share_resolve(intent, matched, SHARE_MAX_TARGETS);
    share_sent++;
    vga_printf("[SHARE] share %s: found %d targets \n", intent->mime_type, count);
    return count;
}

int share_send_text(const char *text, const char *mime)
{
    share_intent_t intent;
    memset(&intent, 0, sizeof(intent));
    intent.type = SHARE_TYPE_TEXT;
    strcpy(intent.action, "ACTION_SEND");
    strncpy(intent.mime_type, mime ? mime : "text/plain", 63);
    strncpy(intent.text, text, SHARE_CLIPBOARD_MAX - 1);
    return share_send(&intent);
}

int share_send_file(const char *path, const char *mime)
{
    share_intent_t intent;
    memset(&intent, 0, sizeof(intent));
    intent.type = SHARE_TYPE_FILE;
    strcpy(intent.action, "ACTION_SEND");
    strncpy(intent.mime_type, mime ? mime : "file/*", 63);
    static char uri_buf[256];
    strcpy(uri_buf, "file://");
    strncat(uri_buf, path, 200);
    intent.uris[0] = uri_buf;
    intent.uri_count = 1;
    return share_send(&intent);
}

int share_send_uri(const char *uri, const char *mime)
{
    share_intent_t intent;
    memset(&intent, 0, sizeof(intent));
    intent.type = SHARE_TYPE_URI;
    strcpy(intent.action, "ACTION_VIEW");
    strncpy(intent.mime_type, mime ? mime : "*/*", 63);
    static char uri_buf[256];
    strncpy(uri_buf, uri, 255);
    intent.uris[0] = uri_buf;
    intent.uri_count = 1;
    return share_send(&intent);
}

int share_resolve(share_intent_t *intent, share_target_t *targets_out, int max)
{
    if (!intent || !targets_out || max <= 0) return 0;
    return share_list_targets(intent->mime_type, targets_out, max);
}

int drag_start(int source_win, int x, int y, share_type_t type, void *data, uint32_t size,
               void (*on_drop)(int, int, void *, uint32_t))
{
    if (drag.active) return -1;
    drag.active = 1;
    drag.source_window = source_win;
    drag.state = DRAG_STATE_STARTED;
    drag.start_x = x;
    drag.start_y = y;
    drag.current_x = x;
    drag.current_y = y;
    drag.data_type = type;
    drag.data = data;
    drag.data_size = size;
    drag.on_drop = on_drop;
    vga_printf("[SHARE] drag start:(%d,%d)\n", x, y);
    return 0;
}

int drag_update(int x, int y)
{
    if (!drag.active) return -1;
    drag.current_x = x;
    drag.current_y = y;
    drag.state = DRAG_STATE_DRAGGING;
    return 0;
}

int drag_drop(int x, int y)
{
    if (!drag.active) return -1;
    drag.current_x = x;
    drag.current_y = y;
    drag.state = DRAG_STATE_DROPPED;
    if (drag.on_drop) drag.on_drop(x, y, drag.data, drag.data_size);
    vga_printf("[SHARE] drag release:(%d,%d)\n", x, y);
    drag.active = 0;
    return 0;
}

int drag_cancel(void)
{
    if (!drag.active) return -1;
    drag.state = DRAG_STATE_CANCELED;
    drag.active = 0;
    return 0;
}

int drag_get_state(drag_session_t *session)
{
    if (!session) return -1;
    memcpy(session, &drag, sizeof(drag_session_t));
    return 0;
}

int file_picker_open(file_picker_mode_t mode, const char *initial_dir,
                     const char *filter_mime, const char *title)
{
    (void)mode; (void)initial_dir; (void)filter_mime; (void)title;
    picker_active = 1;
    memset(&picker_result, 0, sizeof(file_picker_result_t));
    vga_puts("[SHARE] file picker open \n");
    return 0;
}

int file_picker_get_result(file_picker_result_t *result)
{
    if (!result) return -1;
    memcpy(result, &picker_result, sizeof(file_picker_result_t));
    picker_active = 0;
    return picker_result.count;
}

int file_picker_is_active(void) { return picker_active; }

int file_picker_cancel(void)
{
    picker_result.canceled = 1;
    picker_active = 0;
    return 0;
}

int share_target_count(void) { return target_count; }
int share_sent_count(void) { return share_sent; }
