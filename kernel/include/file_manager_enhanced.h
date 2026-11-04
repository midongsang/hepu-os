/* ============================================================================
 * 赫普OS - 增强版文件管理器（V8.78新增）
 * 双面板、文件预览、搜索、排序、多选、书签
 * ========================================================================== */

#ifndef _FILE_MANAGER_ENHANCED_H
#define _FILE_MANAGER_ENHANCED_H

#include <stdint.h>

/* 文件类型 */
typedef enum {
    FME_TYPE_FOLDER = 0,
    FME_TYPE_FILE,
    FME_TYPE_IMAGE,
    FME_TYPE_AUDIO,
    FME_TYPE_VIDEO,
    FME_TYPE_DOCUMENT,
    FME_TYPE_ARCHIVE,
    FME_TYPE_EXECUTABLE,
    FME_TYPE_SYMLINK,
    FME_TYPE_UNKNOWN
} fme_file_type_t;

/* 排序模式 */
typedef enum {
    FME_SORT_NAME = 0,
    FME_SORT_SIZE,
    FME_SORT_DATE,
    FME_SORT_TYPE,
    FME_SORT_NAME_DESC,
    FME_SORT_SIZE_DESC,
    FME_SORT_DATE_DESC
} fme_sort_mode_t;

/* 视图模式 */
typedef enum {
    FME_VIEW_LIST = 0,
    FME_VIEW_ICON,
    FME_VIEW_DETAIL,
    FME_VIEW_DUAL_PANE
} fme_view_mode_t;

/* 文件项 */
typedef struct {
    char            name[256];
    char            path[512];
    uint64_t        size;
    uint32_t        modified;
    fme_file_type_t type;
    uint32_t        permissions;
    int             selected;
    int             is_hidden;
} fme_file_item_t;

/* 面板 */
typedef struct {
    char             current_path[512];
    fme_file_item_t  items[256];
    int              item_count;
    int              cursor;
    int              scroll;
    int              active;
    fme_sort_mode_t  sort_mode;
} fme_pane_t;

/* 书签 */
typedef struct {
    char name[64];
    char path[512];
} fme_bookmark_t;

/* 文件管理器状态 */
typedef struct {
    fme_pane_t      panes[2];
    int             active_pane;
    fme_view_mode_t view_mode;
    int             show_hidden;
    int             folder_first;
    int             show_preview;
    char            search_filter[128];
    fme_bookmark_t  bookmarks[16];
    int             bookmark_count;
    char            clipboard[512];
    int             clipboard_mode;  /* 0=none, 1=copy, 2=cut */
    int             initialized;
    /* 统计 */
    uint64_t        total_size;
    int             total_files;
    int             total_folders;
} file_manager_enhanced_t;

/* 初始化 */
int fme_init(void);
void fme_shutdown(void);

/* 导航 */
int fme_navigate(int pane, const char *path);
int fme_navigate_up(int pane);
int fme_navigate_home(int pane);
void fme_refresh(int pane);

/* 文件操作 */
int fme_open_item(int pane, int index);
int fme_create_folder(int pane, const char *name);
int fme_create_file(int pane, const char *name);
int fme_rename_item(int pane, int index, const char *new_name);
int fme_delete_item(int pane, int index);
int fme_copy_item(int src_pane, int src_index, const char *dest_path);
int fme_move_item(int src_pane, int src_index, const char *dest_path);

/* 选择 */
void fme_toggle_select(int pane, int index);
void fme_select_all(int pane);
void fme_clear_selection(int pane);
int fme_get_selected_count(int pane);

/* 搜索过滤 */
void fme_set_filter(const char *filter);
void fme_clear_filter(void);

/* 排序 */
void fme_set_sort(int pane, fme_sort_mode_t mode);
void fme_toggle_sort(int pane);

/* 视图 */
void fme_set_view_mode(fme_view_mode_t mode);
void fme_cycle_view_mode(void);
void fme_toggle_hidden(void);
void fme_toggle_preview(void);

/* 书签 */
int fme_add_bookmark(const char *name, const char *path);
int fme_remove_bookmark(int index);
int fme_goto_bookmark(int pane, int index);

/* 剪贴板 */
void fme_copy_to_clipboard(int pane);
void fme_cut_to_clipboard(int pane);
int fme_paste_from_clipboard(int pane);

/* 面板切换 */
void fme_switch_pane(void);
void fme_set_active_pane(int pane);

/* 绘制 */
void fme_draw(int x, int y, int width, int height);
void fme_draw_pane(fme_pane_t *pane, int x, int y, int width, int height, int active);
void fme_draw_preview(int x, int y, int width, int height);
void fme_draw_status_bar(int x, int y, int width);
void fme_draw_toolbar(int x, int y, int width);

/* 键盘处理 */
void fme_handle_key(char key);
void fme_handle_special(int key);

/* 获取状态 */
int fme_get_state(file_manager_enhanced_t *state);
const char *fme_get_current_path(int pane);

#endif /* _FILE_MANAGER_ENHANCED_H */
