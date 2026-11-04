#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 文件管理器
 * 浏览 / 操作 / 搜索 / 存储分析
 * ========================================================================== */

#define FM_MAX_ENTRIES 256
#define FM_MAX_BOOKMARKS 16
#define FM_MAX_TABS 4
#define FM_NAME_LEN 256
#define FM_PATH_LEN 512

/* 文件类型 */
typedef enum {
    FM_TYPE_FILE = 0,
    FM_TYPE_DIR,
    FM_TYPE_SYMLINK,
    FM_TYPE_IMAGE,
    FM_TYPE_VIDEO,
    FM_TYPE_AUDIO,
    FM_TYPE_DOCUMENT,
    FM_TYPE_ARCHIVE,
    FM_TYPE_APP,  /* .hug */
    FM_TYPE_UNKNOWN,
} fm_file_type_t;

/* 文件条目 */
typedef struct {
    int used;
    char name[FM_NAME_LEN];
    char path[FM_PATH_LEN];
    fm_file_type_t type;
    uint64_t size;
    uint64_t modified;
    uint32_t permissions;
    int hidden;
    int selected;
    char owner[32];
    char group[32];
} fm_entry_t;

/* 书签 */
typedef struct {
    int used;
    char name[64];
    char path[FM_PATH_LEN];
    int builtin;  /* 内置书签不可删除 */
} fm_bookmark_t;

/* 标签页 */
typedef struct {
    int used;
    char current_path[FM_PATH_LEN];
    char previous_path[FM_PATH_LEN];
    int position;  /* 滚动位置 */
    int sort_mode;
} fm_tab_t;

/* 排序模式 */
typedef enum {
    FM_SORT_NAME = 0,
    FM_SORT_SIZE,
    FM_SORT_DATE,
    FM_SORT_TYPE,
} fm_sort_mode_t;

/* 视图模式 */
typedef enum {
    FM_VIEW_LIST = 0,
    FM_VIEW_GRID,
    FM_VIEW_COMPACT,
} fm_view_mode_t;

/* 存储设备 */
typedef struct {
    int used;
    char name[64];
    char mount_point[FM_PATH_LEN];
    uint64_t total_size;
    uint64_t used_size;
    uint64_t free_size;
    char fs_type[16];
    int removable;
    int encrypted;
} fm_storage_t;

/* 文件管理器状态 */
typedef struct {
    int initialized;
    int enabled;
    /* 当前路径 */
    char current_path[FM_PATH_LEN];
    /* 条目 */
    fm_entry_t entries[FM_MAX_ENTRIES];
    int entry_count;
    /* 选中 */
    int selected_count;
    /* 书签 */
    fm_bookmark_t bookmarks[FM_MAX_BOOKMARKS];
    int bookmark_count;
    /* 标签页 */
    fm_tab_t tabs[FM_MAX_TABS];
    int tab_count;
    int active_tab;
    /* 存储 */
    fm_storage_t storages[8];
    int storage_count;
    /* 设置 */
    fm_sort_mode_t sort_mode;
    fm_view_mode_t view_mode;
    int show_hidden;
    int sort_reverse;
    int folder_first;
    /* 剪贴板 */
    char clipboard_path[FM_PATH_LEN];
    int clipboard_mode;  /* 0=无, 1=复制, 2=剪切 */
    /* 搜索 */
    char search_query[128];
    int searching;
    /* 统计 */
    uint64_t total_files_browsed;
    uint64_t total_operations;
} file_manager_t;

/* 初始化 */
void file_manager_init(void);

/* 浏览 */
int fm_navigate(const char *path);
int fm_navigate_up(void);
int fm_navigate_home(void);
int fm_refresh(void);
int fm_get_current_path(char *buf, int size);
int fm_list_entries(fm_entry_t *entries, int max);
int fm_get_entry(const char *path, fm_entry_t *entry);

/* 文件操作 */
int fm_create_file(const char *path);
int fm_create_dir(const char *path);
int fm_delete(const char *path);
int fm_rename(const char *old_path, const char *new_name);
int fm_copy(const char *src, const char *dst);
int fm_move(const char *src, const char *dst);
int fm_set_permissions(const char *path, uint32_t perms);

/* 选择操作 */
int fm_select(const char *path);
int fm_deselect(const char *path);
int fm_select_all(void);
int fm_deselect_all(void);
int fm_delete_selected(void);
int fm_copy_selected(const char *dst);
int fm_move_selected(const char *dst);

/* 书签 */
int fm_bookmark_add(const char *name, const char *path);
int fm_bookmark_remove(int index);
int fm_bookmark_list(fm_bookmark_t *bookmarks, int max);

/* 标签页 */
int fm_tab_open(const char *path);
int fm_tab_close(int tab_id);
int fm_tab_switch(int tab_id);
int fm_tab_get_current(fm_tab_t *tab);

/* 存储 */
int fm_storage_list(fm_storage_t *storages, int max);
int fm_storage_get_info(const char *mount, fm_storage_t *storage);

/* 搜索 */
int fm_search(const char *query, fm_entry_t *results, int max);

/* 设置 */
int fm_set_sort_mode(fm_sort_mode_t mode);
int fm_set_view_mode(fm_view_mode_t mode);
int fm_set_show_hidden(int show);
int fm_set_folder_first(int first);

/* 统计 */
int fm_get_stats(file_manager_t *stats);

#endif /* FILE_MANAGER_H */
