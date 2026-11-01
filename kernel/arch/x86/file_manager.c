/* ============================================================================
 * 赫普OS - 文件管理器
 * ========================================================================== */

#include <stdint.h>
#include "file_manager.h"
#include "vga.h"
#include "string.h"

static void fm_strcat(char *dst, const char *src, int max) {
    int dlen = 0; while (dst[dlen] && dlen < max-1) dlen++;
    int i = 0;
    while (src[i] && dlen < max-1) { dst[dlen++] = src[i++]; }
    dst[dlen] = 0;
}

static file_manager_t fm;

/* 根据扩展名判断文件类型 */
static fm_file_type_t detect_type(const char *name)
{
    if (!name) return FM_TYPE_UNKNOWN;
    int len = strlen(name);
    if (len < 4) return FM_TYPE_FILE;
    const char *ext = name + len - 4;
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".png") == 0 ||
        strcmp(ext, ".gif") == 0 || strcmp(ext, ".bmp") == 0 ||
        strcmp(ext, "jpeg") == 0) return FM_TYPE_IMAGE;
    if (strcmp(ext, ".mp4") == 0 || strcmp(ext, ".mkv") == 0 ||
        strcmp(ext, ".avi") == 0 || strcmp(ext, ".mov") == 0) return FM_TYPE_VIDEO;
    if (strcmp(ext, ".mp3") == 0 || strcmp(ext, ".wav") == 0 ||
        strcmp(ext, ".ogg") == 0 || strcmp(ext, ".flac") == 0) return FM_TYPE_AUDIO;
    if (strcmp(ext, ".txt") == 0 || strcmp(ext, ".pdf") == 0 ||
        strcmp(ext, ".doc") == 0 || strcmp(ext, ".docx") == 0) return FM_TYPE_DOCUMENT;
    if (strcmp(ext, ".zip") == 0 || strcmp(ext, ".tar") == 0 ||
        strcmp(ext, ".gz") == 0 || strcmp(ext, ".rar") == 0) return FM_TYPE_ARCHIVE;
    if (strcmp(ext, ".hug") == 0) return FM_TYPE_APP;
    return FM_TYPE_FILE;
}

void file_manager_init(void)
{
    memset(&fm, 0, sizeof(file_manager_t));
    fm.initialized = 1;
    fm.enabled = 1;
    strncpy(fm.current_path, "/", 511);
    fm.sort_mode = FM_SORT_NAME;
    fm.view_mode = FM_VIEW_LIST;
    fm.show_hidden = 0;
    fm.folder_first = 1;

    /* 内置书签 */
    fm_bookmark_add(" master dir ", "/home/user");
    fm_bookmark_add(" download ", "/home/user/Downloads");
    fm_bookmark_add(" map 片 ", "/home/user/Pictures");
    fm_bookmark_add(" 音乐 ", "/home/user/Music");
    fm_bookmark_add(" video ", "/home/user/Videos");
    fm_bookmark_add(" 文档 ", "/home/user/Documents");
    fm.bookmarks[0].builtin = 1;
    fm.bookmarks[1].builtin = 1;
    fm.bookmarks[2].builtin = 1;
    fm.bookmarks[3].builtin = 1;
    fm.bookmarks[4].builtin = 1;
    fm.bookmarks[5].builtin = 1;

    /* 存储设备 */
    fm.storages[0].used = 1;
    strncpy(fm.storages[0].name, " 内部 storage ", 63);
    strncpy(fm.storages[0].mount_point, "/", 511);
    fm.storages[0].total_size = 64ULL * 1024 * 1024 * 1024;
    fm.storages[0].used_size = 20ULL * 1024 * 1024 * 1024;
    fm.storages[0].free_size = 44ULL * 1024 * 1024 * 1024;
    strncpy(fm.storages[0].fs_type, "ext4", 15);
    fm.storage_count = 1;

    /* 模拟根目录条目 */
    fm_refresh();

    vga_puts("[FM] file manager init done \n");
    vga_printf(" bookmarks: %d, storage: %d, current: %s\n",
               fm.bookmark_count, fm.storage_count, fm.current_path);
}

/* 浏览 */
int fm_navigate(const char *path)
{
    if (!path) return -1;
    strncpy(fm.current_path, path, 511);
    fm_refresh();
    return 0;
}

int fm_navigate_up(void)
{
    int len = strlen(fm.current_path);
    if (len <= 1) return -1;
    for (int i = len - 1; i >= 0; i--) {
        if (fm.current_path[i] == '/') {
            if (i == 0) fm.current_path[1] = 0;
            else fm.current_path[i] = 0;
            break;
        }
    }
    fm_refresh();
    return 0;
}

int fm_navigate_home(void)
{
    return fm_navigate("/home/user");
}

int fm_refresh(void)
{
    /* 清空条目 */
    for (int i = 0; i < FM_MAX_ENTRIES; i++) fm.entries[i].used = 0;
    fm.entry_count = 0;
    fm.selected_count = 0;

    /* 模拟目录内容 */
    if (strcmp(fm.current_path, "/") == 0) {
        const char *dirs[] = {"home", "etc", "bin", "sbin", "lib", "usr", "var", "tmp", "dev", "proc", "sys"};
        for (int i = 0; i < 11; i++) {
            strncpy(fm.entries[i].name, dirs[i], 255);
            strncpy(fm.entries[i].path, "/", 511);
            fm_strcat(fm.entries[i].path, dirs[i], 512);
            fm.entries[i].type = FM_TYPE_DIR;
            fm.entries[i].size = 4096;
            fm.entries[i].permissions = 0755;
            fm.entries[i].used = 1;
            fm.entry_count++;
        }
    } else if (strcmp(fm.current_path, "/home/user") == 0) {
        const char *dirs[] = {"Downloads", "Pictures", "Music", "Videos", "Documents", "Desktop"};
        for (int i = 0; i < 6; i++) {
            strncpy(fm.entries[i].name, dirs[i], 255);
            strncpy(fm.entries[i].path, "/home/user/", 511);
            fm_strcat(fm.entries[i].path, dirs[i], 512);
            fm.entries[i].type = FM_TYPE_DIR;
            fm.entries[i].size = 4096;
            fm.entries[i].permissions = 0755;
            fm.entries[i].used = 1;
            fm.entry_count++;
        }
        /* 一些文件 */
        const char *files[] = {"readme.txt", "photo.jpg", "song.mp3", "app.hug"};
        for (int i = 0; i < 4; i++) {
            int idx = fm.entry_count;
            strncpy(fm.entries[idx].name, files[i], 255);
            strncpy(fm.entries[idx].path, "/home/user/", 511);
            fm_strcat(fm.entries[idx].path, files[i], 512);
            fm.entries[idx].type = detect_type(files[i]);
            fm.entries[idx].size = 1024 * (i + 1) * 100;
            fm.entries[idx].permissions = 0644;
            fm.entries[idx].used = 1;
            fm.entry_count++;
        }
    }

    fm.total_files_browsed += fm.entry_count;
    return fm.entry_count;
}

int fm_get_current_path(char *buf, int size)
{
    if (!buf || size <= 0) return -1;
    strncpy(buf, fm.current_path, size - 1);
    return strlen(fm.current_path);
}

int fm_list_entries(fm_entry_t *entries, int max)
{
    if (!entries || max <= 0) return 0;
    int count = fm.entry_count < max ? fm.entry_count : max;
    for (int i = 0; i < count; i++)
        memcpy(&entries[i], &fm.entries[i], sizeof(fm_entry_t));
    return count;
}

int fm_get_entry(const char *path, fm_entry_t *entry)
{
    if (!path || !entry) return -1;
    for (int i = 0; i < fm.entry_count; i++) {
        if (strcmp(fm.entries[i].path, path) == 0) {
            memcpy(entry, &fm.entries[i], sizeof(fm_entry_t));
            return 0;
        }
    }
    return -1;
}

/* 文件操作 */
int fm_create_file(const char *path)
{
    (void)path;
    fm.total_operations++;
    return 0;
}

int fm_create_dir(const char *path)
{
    (void)path;
    fm.total_operations++;
    return 0;
}

int fm_delete(const char *path)
{
    (void)path;
    fm.total_operations++;
    return 0;
}

int fm_rename(const char *old_path, const char *new_name)
{
    (void)old_path; (void)new_name;
    fm.total_operations++;
    return 0;
}

int fm_copy(const char *src, const char *dst)
{
    (void)src; (void)dst;
    fm.total_operations++;
    return 0;
}

int fm_move(const char *src, const char *dst)
{
    (void)src; (void)dst;
    fm.total_operations++;
    return 0;
}

int fm_set_permissions(const char *path, uint32_t perms)
{
    (void)path; (void)perms;
    return 0;
}

/* 选择操作 */
int fm_select(const char *path)
{
    for (int i = 0; i < fm.entry_count; i++) {
        if (strcmp(fm.entries[i].path, path) == 0) {
            if (!fm.entries[i].selected) {
                fm.entries[i].selected = 1;
                fm.selected_count++;
            }
            return 0;
        }
    }
    return -1;
}

int fm_deselect(const char *path)
{
    for (int i = 0; i < fm.entry_count; i++) {
        if (strcmp(fm.entries[i].path, path) == 0) {
            if (fm.entries[i].selected) {
                fm.entries[i].selected = 0;
                fm.selected_count--;
            }
            return 0;
        }
    }
    return -1;
}

int fm_select_all(void)
{
    for (int i = 0; i < fm.entry_count; i++) {
        if (!fm.entries[i].selected) {
            fm.entries[i].selected = 1;
            fm.selected_count++;
        }
    }
    return fm.selected_count;
}

int fm_deselect_all(void)
{
    for (int i = 0; i < fm.entry_count; i++)
        fm.entries[i].selected = 0;
    fm.selected_count = 0;
    return 0;
}

int fm_delete_selected(void)
{
    int count = fm.selected_count;
    fm_deselect_all();
    fm.total_operations += count;
    return count;
}

int fm_copy_selected(const char *dst)
{
    (void)dst;
    int count = fm.selected_count;
    fm.total_operations += count;
    return count;
}

int fm_move_selected(const char *dst)
{
    (void)dst;
    int count = fm.selected_count;
    fm.total_operations += count;
    return count;
}

/* 书签 */
int fm_bookmark_add(const char *name, const char *path)
{
    for (int i = 0; i < FM_MAX_BOOKMARKS; i++) {
        if (!fm.bookmarks[i].used) {
            memset(&fm.bookmarks[i], 0, sizeof(fm_bookmark_t));
            fm.bookmarks[i].used = 1;
            if (name) strncpy(fm.bookmarks[i].name, name, 63);
            if (path) strncpy(fm.bookmarks[i].path, path, 511);
            fm.bookmark_count++;
            return i;
        }
    }
    return -1;
}

int fm_bookmark_remove(int index)
{
    if (index < 0 || index >= FM_MAX_BOOKMARKS || !fm.bookmarks[index].used) return -1;
    if (fm.bookmarks[index].builtin) return -1;
    fm.bookmarks[index].used = 0;
    fm.bookmark_count--;
    return 0;
}

int fm_bookmark_list(fm_bookmark_t *bookmarks, int max)
{
    if (!bookmarks || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < FM_MAX_BOOKMARKS && count < max; i++) {
        if (fm.bookmarks[i].used) memcpy(&bookmarks[count++], &fm.bookmarks[i], sizeof(fm_bookmark_t));
    }
    return count;
}

/* 标签页 */
int fm_tab_open(const char *path)
{
    for (int i = 0; i < FM_MAX_TABS; i++) {
        if (!fm.tabs[i].used) {
            memset(&fm.tabs[i], 0, sizeof(fm_tab_t));
            fm.tabs[i].used = 1;
            if (path) strncpy(fm.tabs[i].current_path, path, 511);
            fm.tab_count++;
            fm.active_tab = i;
            return i;
        }
    }
    return -1;
}

int fm_tab_close(int tab_id)
{
    if (tab_id < 0 || tab_id >= FM_MAX_TABS || !fm.tabs[tab_id].used) return -1;
    fm.tabs[tab_id].used = 0;
    fm.tab_count--;
    if (fm.active_tab == tab_id) fm.active_tab = 0;
    return 0;
}

int fm_tab_switch(int tab_id)
{
    if (tab_id < 0 || tab_id >= FM_MAX_TABS || !fm.tabs[tab_id].used) return -1;
    fm.active_tab = tab_id;
    strncpy(fm.current_path, fm.tabs[tab_id].current_path, 511);
    fm_refresh();
    return 0;
}

int fm_tab_get_current(fm_tab_t *tab)
{
    if (!tab || fm.active_tab < 0) return -1;
    memcpy(tab, &fm.tabs[fm.active_tab], sizeof(fm_tab_t));
    return 0;
}

/* 存储 */
int fm_storage_list(fm_storage_t *storages, int max)
{
    if (!storages || max <= 0) return 0;
    int count = fm.storage_count < max ? fm.storage_count : max;
    for (int i = 0; i < count; i++)
        memcpy(&storages[i], &fm.storages[i], sizeof(fm_storage_t));
    return count;
}

int fm_storage_get_info(const char *mount, fm_storage_t *storage)
{
    if (!mount || !storage) return -1;
    for (int i = 0; i < 8; i++) {
        if (fm.storages[i].used && strcmp(fm.storages[i].mount_point, mount) == 0) {
            memcpy(storage, &fm.storages[i], sizeof(fm_storage_t));
            return 0;
        }
    }
    return -1;
}

/* 搜索 */
int fm_search(const char *query, fm_entry_t *results, int max)
{
    if (!query || !results || max <= 0) return 0;
    strncpy(fm.search_query, query, 127);
    fm.searching = 1;
    int count = 0;
    for (int i = 0; i < fm.entry_count && count < max; i++) {
        if (strstr(fm.entries[i].name, query))
            memcpy(&results[count++], &fm.entries[i], sizeof(fm_entry_t));
    }
    return count;
}

/* 设置 */
int fm_set_sort_mode(fm_sort_mode_t mode)
{
    fm.sort_mode = mode;
    return 0;
}

int fm_set_view_mode(fm_view_mode_t mode)
{
    fm.view_mode = mode;
    return 0;
}

int fm_set_show_hidden(int show)
{
    fm.show_hidden = show ? 1 : 0;
    return 0;
}

int fm_set_folder_first(int first)
{
    fm.folder_first = first ? 1 : 0;
    return 0;
}

/* 统计 */
int fm_get_stats(file_manager_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &fm, sizeof(file_manager_t));
    return 0;
}
