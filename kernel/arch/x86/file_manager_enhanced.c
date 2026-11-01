/* ============================================================================
 * 赫普OS - 增强版文件管理器实现（V8.78新增）
 * 双面板、文件预览、搜索、排序、多选、书签
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../include/file_manager_enhanced.h"
#include "../include/vga.h"
#include "../include/kheap.h"
#include "../include/graphics.h"

/* 全局状态 */
static file_manager_enhanced_t fme;

/* 模拟文件系统数据 */
static const char *sample_paths[] = {
    "/", "/home", "/home/user", "/home/user/Desktop",
    "/home/user/Documents", "/home/user/Downloads",
    "/home/user/Pictures", "/home/user/Music",
    "/home/user/Videos", "/etc", "/usr", "/usr/bin",
    "/tmp", "/var", NULL
};

/* 模拟文件列表 */
typedef struct {
    const char *path;
    const char *name;
    uint64_t size;
    fme_file_type_t type;
} sample_file_t;

static const sample_file_t sample_files[] = {
    {"/home/user", "Desktop", 0, FME_TYPE_FOLDER},
    {"/home/user", "Documents", 0, FME_TYPE_FOLDER},
    {"/home/user", "Downloads", 0, FME_TYPE_FOLDER},
    {"/home/user", "Pictures", 0, FME_TYPE_FOLDER},
    {"/home/user", "Music", 0, FME_TYPE_FOLDER},
    {"/home/user", "Videos", 0, FME_TYPE_FOLDER},
    {"/home/user", ".bashrc", 342, FME_TYPE_FILE},
    {"/home/user", ".profile", 807, FME_TYPE_FILE},
    {"/home/user", "README.txt", 2048, FME_TYPE_DOCUMENT},
    {"/home/user", "hepu-os.zip", 104857600, FME_TYPE_ARCHIVE},
    {"/home/user/Desktop", "项目计划.doc", 45056, FME_TYPE_DOCUMENT},
    {"/home/user/Desktop", "截图.png", 1048576, FME_TYPE_IMAGE},
    {"/home/user/Desktop", "演示.pptx", 2097152, FME_TYPE_DOCUMENT},
    {"/home/user/Documents", "报告.pdf", 524288, FME_TYPE_DOCUMENT},
    {"/home/user/Documents", "代码.c", 12288, FME_TYPE_FILE},
    {"/home/user/Documents", "头文件.h", 4096, FME_TYPE_FILE},
    {"/home/user/Downloads", "安装包.deb", 52428800, FME_TYPE_ARCHIVE},
    {"/home/user/Downloads", "电影.mp4", 734003200, FME_TYPE_VIDEO},
    {"/home/user/Downloads", "音乐.mp3", 5242880, FME_TYPE_AUDIO},
    {"/home/user/Pictures", "风景.jpg", 3145728, FME_TYPE_IMAGE},
    {"/home/user/Pictures", "人物.png", 2097152, FME_TYPE_IMAGE},
    {"/home/user/Pictures", "壁纸.bmp", 8388608, FME_TYPE_IMAGE},
    {"/home/user/Music", "歌曲1.mp3", 4194304, FME_TYPE_AUDIO},
    {"/home/user/Music", "歌曲2.flac", 25165824, FME_TYPE_AUDIO},
    {"/home/user/Music", "播放列表.m3u", 1024, FME_TYPE_FILE},
    {"/home/user/Videos", "教程.mp4", 524288000, FME_TYPE_VIDEO},
    {"/home/user/Videos", "记录.mkv", 1073741824, FME_TYPE_VIDEO},
    {"/", "bin", 0, FME_TYPE_FOLDER},
    {"/", "boot", 0, FME_TYPE_FOLDER},
    {"/", "dev", 0, FME_TYPE_FOLDER},
    {"/", "etc", 0, FME_TYPE_FOLDER},
    {"/", "home", 0, FME_TYPE_FOLDER},
    {"/", "lib", 0, FME_TYPE_FOLDER},
    {"/", "mnt", 0, FME_TYPE_FOLDER},
    {"/", "opt", 0, FME_TYPE_FOLDER},
    {"/", "proc", 0, FME_TYPE_FOLDER},
    {"/", "root", 0, FME_TYPE_FOLDER},
    {"/", "sbin", 0, FME_TYPE_FOLDER},
    {"/", "tmp", 0, FME_TYPE_FOLDER},
    {"/", "usr", 0, FME_TYPE_FOLDER},
    {"/", "var", 0, FME_TYPE_FOLDER},
    {"/", "vmlinuz", 8388608, FME_TYPE_EXECUTABLE},
    {NULL, NULL, 0, FME_TYPE_UNKNOWN}
};

/* 初始化 */
int fme_init(void)
{
    if (fme.initialized) return 0;
    memset(&fme, 0, sizeof(file_manager_enhanced_t));
    fme.initialized = 1;
    fme.view_mode = FME_VIEW_DUAL_PANE;
    fme.active_pane = 0;
    fme.show_hidden = 0;
    fme.folder_first = 1;
    fme.show_preview = 1;

    /* 初始化两个面板 */
    strcpy(fme.panes[0].current_path, "/home/user");
    strcpy(fme.panes[1].current_path, "/");
    fme.panes[0].active = 1;
    fme.panes[1].active = 0;
    fme.panes[0].sort_mode = FME_SORT_NAME;
    fme.panes[1].sort_mode = FME_SORT_NAME;

    /* 默认书签 */
    fme_add_bookmark("主目录", "/home/user");
    fme_add_bookmark("桌面", "/home/user/Desktop");
    fme_add_bookmark("文档", "/home/user/Documents");
    fme_add_bookmark("下载", "/home/user/Downloads");
    fme_add_bookmark("图片", "/home/user/Pictures");
    fme_add_bookmark("根目录", "/");

    fme_refresh(0);
    fme_refresh(1);
    vga_puts("[FME] Enhanced file manager initialized (dual pane)\n");
    return 0;
}

void fme_shutdown(void)
{
    fme.initialized = 0;
}

/* 刷新文件列表 */
void fme_refresh(int pane)
{
    fme_pane_t *p = &fme.panes[pane];
    p->item_count = 0;
    fme.total_size = 0;
    fme.total_files = 0;
    fme.total_folders = 0;

    /* 添加".." */
    if (strcmp(p->current_path, "/") != 0) {
        strcpy(p->items[p->item_count].name, "..");
        strcpy(p->items[p->item_count].path, p->current_path);
        p->items[p->item_count].type = FME_TYPE_FOLDER;
        p->items[p->item_count].size = 0;
        p->item_count++;
    }

    /* 从模拟数据加载 */
    for (int i = 0; sample_files[i].path; i++) {
        if (strcmp(sample_files[i].path, p->current_path) != 0)
            continue;
        if (p->item_count >= 255) break;

        /* 隐藏文件过滤 */
        if (!fme.show_hidden && sample_files[i].name[0] == '.')
            continue;

        /* 搜索过滤 */
        if (fme.search_filter[0] &&
            strstr(sample_files[i].name, fme.search_filter) == NULL)
            continue;

        fme_file_item_t *item = &p->items[p->item_count];
        strncpy(item->name, sample_files[i].name, 255);
        snprintf(item->path, 511, "%s/%s", p->current_path,
                 sample_files[i].name);
        item->size = sample_files[i].size;
        item->type = sample_files[i].type;
        item->selected = 0;
        item->is_hidden = (sample_files[i].name[0] == '.');
        item->permissions = 0755;
        item->modified = 1700000000 + i * 3600;
        p->item_count++;

        if (item->type == FME_TYPE_FOLDER)
            fme.total_folders++;
        else {
            fme.total_files++;
            fme.total_size += item->size;
        }
    }

    /* 排序 */
    fme_set_sort(pane, p->sort_mode);
    p->cursor = 0;
    p->scroll = 0;
}

/* 排序比较 */
static int compare_items(const void *a, const void *b, fme_sort_mode_t mode)
{
    const fme_file_item_t *fa = (const fme_file_item_t *)a;
    const fme_file_item_t *fb = (const fme_file_item_t *)b;

    /* 文件夹优先 */
    if (fme.folder_first) {
        if (fa->type == FME_TYPE_FOLDER && fb->type != FME_TYPE_FOLDER)
            return -1;
        if (fa->type != FME_TYPE_FOLDER && fb->type == FME_TYPE_FOLDER)
            return 1;
    }

    /* ".."始终在最前 */
    if (strcmp(fa->name, "..") == 0) return -1;
    if (strcmp(fb->name, "..") == 0) return 1;

    switch (mode) {
        case FME_SORT_NAME:
            return strcmp(fa->name, fb->name);
        case FME_SORT_NAME_DESC:
            return -strcmp(fa->name, fb->name);
        case FME_SORT_SIZE:
            return (fa->size > fb->size) ? 1 :
                   (fa->size < fb->size) ? -1 : 0;
        case FME_SORT_SIZE_DESC:
            return (fa->size < fb->size) ? 1 :
                   (fa->size > fb->size) ? -1 : 0;
        case FME_SORT_DATE:
            return (fa->modified > fb->modified) ? 1 : -1;
        case FME_SORT_DATE_DESC:
            return (fa->modified < fb->modified) ? 1 : -1;
        case FME_SORT_TYPE:
            return fa->type - fb->type;
        default:
            return strcmp(fa->name, fb->name);
    }
}

void fme_set_sort(int pane, fme_sort_mode_t mode)
{
    fme_pane_t *p = &fme.panes[pane];
    p->sort_mode = mode;
    /* 简单冒泡排序 */
    for (int i = 0; i < p->item_count - 1; i++) {
        for (int j = 0; j < p->item_count - 1 - i; j++) {
            if (compare_items(&p->items[j], &p->items[j+1], mode) > 0) {
                fme_file_item_t tmp = p->items[j];
                p->items[j] = p->items[j+1];
                p->items[j+1] = tmp;
            }
        }
    }
}

void fme_toggle_sort(int pane)
{
    fme_pane_t *p = &fme.panes[pane];
    p->sort_mode = (p->sort_mode + 1) % 7;
    fme_set_sort(pane, p->sort_mode);
}

/* 导航 */
int fme_navigate(int pane, const char *path)
{
    if (!path) return -1;
    strncpy(fme.panes[pane].current_path, path, 511);
    fme_refresh(pane);
    return 0;
}

int fme_navigate_up(int pane)
{
    fme_pane_t *p = &fme.panes[pane];
    if (strcmp(p->current_path, "/") == 0) return -1;
    char *last = strrchr(p->current_path, '/');
    if (last == p->current_path) {
        strcpy(p->current_path, "/");
    } else {
        *last = 0;
    }
    fme_refresh(pane);
    return 0;
}

int fme_navigate_home(int pane)
{
    return fme_navigate(pane, "/home/user");
}

/* 打开项目 */
int fme_open_item(int pane, int index)
{
    fme_pane_t *p = &fme.panes[pane];
    if (index < 0 || index >= p->item_count) return -1;
    fme_file_item_t *item = &p->items[index];

    if (strcmp(item->name, "..") == 0) {
        return fme_navigate_up(pane);
    }
    if (item->type == FME_TYPE_FOLDER) {
        return fme_navigate(pane, item->path);
    }
    /* 文件：模拟打开 */
    vga_printf("[FME] Opening: %s\n", item->path);
    return 0;
}

/* 文件操作 */
int fme_create_folder(int pane, const char *name)
{
    if (!name || !*name) return -1;
    vga_printf("[FME] Create folder: %s/%s\n",
               fme.panes[pane].current_path, name);
    fme_refresh(pane);
    return 0;
}

int fme_create_file(int pane, const char *name)
{
    if (!name || !*name) return -1;
    vga_printf("[FME] Create file: %s/%s\n",
               fme.panes[pane].current_path, name);
    fme_refresh(pane);
    return 0;
}

int fme_rename_item(int pane, int index, const char *new_name)
{
    fme_pane_t *p = &fme.panes[pane];
    if (index < 0 || index >= p->item_count || !new_name) return -1;
    vga_printf("[FME] Rename: %s -> %s\n",
               p->items[index].name, new_name);
    fme_refresh(pane);
    return 0;
}

int fme_delete_item(int pane, int index)
{
    fme_pane_t *p = &fme.panes[pane];
    if (index < 0 || index >= p->item_count) return -1;
    vga_printf("[FME] Delete: %s\n", p->items[index].path);
    fme_refresh(pane);
    return 0;
}

int fme_copy_item(int src_pane, int src_index, const char *dest_path)
{
    fme_pane_t *sp = &fme.panes[src_pane];
    if (src_index < 0 || src_index >= sp->item_count) return -1;
    vga_printf("[FME] Copy: %s -> %s\n",
               sp->items[src_index].path, dest_path);
    return 0;
}

int fme_move_item(int src_pane, int src_index, const char *dest_path)
{
    fme_pane_t *sp = &fme.panes[src_pane];
    if (src_index < 0 || src_index >= sp->item_count) return -1;
    vga_printf("[FME] Move: %s -> %s\n",
               sp->items[src_index].path, dest_path);
    return 0;
}

/* 选择 */
void fme_toggle_select(int pane, int index)
{
    fme_pane_t *p = &fme.panes[pane];
    if (index >= 0 && index < p->item_count)
        p->items[index].selected = !p->items[index].selected;
}

void fme_select_all(int pane)
{
    fme_pane_t *p = &fme.panes[pane];
    for (int i = 0; i < p->item_count; i++)
        if (strcmp(p->items[i].name, "..") != 0)
            p->items[i].selected = 1;
}

void fme_clear_selection(int pane)
{
    fme_pane_t *p = &fme.panes[pane];
    for (int i = 0; i < p->item_count; i++)
        p->items[i].selected = 0;
}

int fme_get_selected_count(int pane)
{
    fme_pane_t *p = &fme.panes[pane];
    int count = 0;
    for (int i = 0; i < p->item_count; i++)
        if (p->items[i].selected) count++;
    return count;
}

/* 搜索 */
void fme_set_filter(const char *filter)
{
    if (filter) strncpy(fme.search_filter, filter, 127);
    else fme.search_filter[0] = 0;
    fme_refresh(0);
    fme_refresh(1);
}

void fme_clear_filter(void)
{
    fme.search_filter[0] = 0;
    fme_refresh(0);
    fme_refresh(1);
}

/* 视图 */
void fme_set_view_mode(fme_view_mode_t mode)
{
    fme.view_mode = mode;
}

void fme_cycle_view_mode(void)
{
    fme.view_mode = (fme.view_mode + 1) % 4;
}

void fme_toggle_hidden(void)
{
    fme.show_hidden = !fme.show_hidden;
    fme_refresh(0);
    fme_refresh(1);
}

void fme_toggle_preview(void)
{
    fme.show_preview = !fme.show_preview;
}

/* 书签 */
int fme_add_bookmark(const char *name, const char *path)
{
    if (fme.bookmark_count >= 16 || !name || !path) return -1;
    strncpy(fme.bookmarks[fme.bookmark_count].name, name, 63);
    strncpy(fme.bookmarks[fme.bookmark_count].path, path, 511);
    fme.bookmark_count++;
    return 0;
}

int fme_remove_bookmark(int index)
{
    if (index < 0 || index >= fme.bookmark_count) return -1;
    for (int i = index; i < fme.bookmark_count - 1; i++)
        fme.bookmarks[i] = fme.bookmarks[i + 1];
    fme.bookmark_count--;
    return 0;
}

int fme_goto_bookmark(int pane, int index)
{
    if (index < 0 || index >= fme.bookmark_count) return -1;
    return fme_navigate(pane, fme.bookmarks[index].path);
}

/* 剪贴板 */
void fme_copy_to_clipboard(int pane)
{
    fme_pane_t *p = &fme.panes[pane];
    if (p->cursor >= 0 && p->cursor < p->item_count) {
        strncpy(fme.clipboard, p->items[p->cursor].path, 511);
        fme.clipboard_mode = 1;
    }
}

void fme_cut_to_clipboard(int pane)
{
    fme_pane_t *p = &fme.panes[pane];
    if (p->cursor >= 0 && p->cursor < p->item_count) {
        strncpy(fme.clipboard, p->items[p->cursor].path, 511);
        fme.clipboard_mode = 2;
    }
}

int fme_paste_from_clipboard(int pane)
{
    if (fme.clipboard_mode == 0) return -1;
    const char *op = (fme.clipboard_mode == 1) ? "Copy" : "Move";
    vga_printf("[FME] %s: %s -> %s\n", op, fme.clipboard,
               fme.panes[pane].current_path);
    if (fme.clipboard_mode == 2) fme.clipboard_mode = 0;
    fme_refresh(pane);
    return 0;
}

/* 面板切换 */
void fme_switch_pane(void)
{
    fme.active_pane = 1 - fme.active_pane;
    fme.panes[0].active = (fme.active_pane == 0);
    fme.panes[1].active = (fme.active_pane == 1);
}

void fme_set_active_pane(int pane)
{
    fme.active_pane = pane;
    fme.panes[0].active = (pane == 0);
    fme.panes[1].active = (pane == 1);
}

/* 格式化大小 */
static void format_size(uint64_t size, char *buf, int buf_len)
{
    if (size == 0) {
        strncpy(buf, "-", buf_len - 1);
    } else if (size < 1024) {
        snprintf(buf, buf_len, "%llu B", (unsigned long long)size);
    } else if (size < 1048576) {
        snprintf(buf, buf_len, "%.1f KB", size / 1024.0);
    } else if (size < 1073741824) {
        snprintf(buf, buf_len, "%.1f MB", size / 1048576.0);
    } else {
        snprintf(buf, buf_len, "%.2f GB", size / 1073741824.0);
    }
}

/* 文件类型颜色 */
static gfx_color_t type_color(fme_file_type_t type)
{
    switch (type) {
        case FME_TYPE_FOLDER:    return (gfx_color_t){100, 180, 255, 255};
        case FME_TYPE_EXECUTABLE:return (gfx_color_t){100, 255, 100, 255};
        case FME_TYPE_IMAGE:     return (gfx_color_t){255, 180, 100, 255};
        case FME_TYPE_AUDIO:     return (gfx_color_t){200, 100, 255, 255};
        case FME_TYPE_VIDEO:     return (gfx_color_t){255, 100, 100, 255};
        case FME_TYPE_DOCUMENT:  return (gfx_color_t){255, 255, 150, 255};
        case FME_TYPE_ARCHIVE:   return (gfx_color_t){255, 150, 50, 255};
        default:                 return (gfx_color_t){220, 220, 220, 255};
    }
}

/* 绘制单个面板 */
void fme_draw_pane(fme_pane_t *pane, int x, int y, int width, int height, int active)
{
    gfx_color_t bg = active ? (gfx_color_t){30, 35, 45, 255} :
                              (gfx_color_t){25, 28, 35, 255};
    gfx_color_t border = active ? (gfx_color_t){100, 180, 255, 255} :
                                  (gfx_color_t){60, 60, 70, 255};
    gfx_color_t fg = {220, 220, 220, 255};
    gfx_color_t cursor_bg = {50, 70, 100, 255};
    gfx_color_t selected_bg = {60, 50, 30, 255};

    gfx_fill_rect(x, y, width, height, bg);
    gfx_draw_rect(x, y, width, height, border);

    /* 路径栏 */
    gfx_fill_rect(x + 1, y + 1, width - 2, 22,
                  (gfx_color_t){40, 45, 55, 255});
    gfx_draw_string_font(x + 5, y + 5, pane->current_path, fg, 11);

    /* 表头 */
    int header_y = y + 25;
    gfx_fill_rect(x + 1, header_y, width - 2, 18,
                  (gfx_color_t){35, 40, 50, 255});
    gfx_draw_string_font(x + 5, header_y + 3, "名称",
                         (gfx_color_t){150, 150, 160, 255}, 10);
    gfx_draw_string_font(x + width - 80, header_y + 3, "大小",
                         (gfx_color_t){150, 150, 160, 255}, 10);
    gfx_draw_string_font(x + width - 35, header_y + 3, "类型",
                         (gfx_color_t){150, 150, 160, 255}, 10);

    /* 文件列表 */
    int list_y = header_y + 20;
    int row_height = 18;
    int visible = (height - list_y - y - 25) / row_height;

    for (int i = 0; i < visible && pane->scroll + i < pane->item_count; i++) {
        int idx = pane->scroll + i;
        int ry = list_y + i * row_height;
        fme_file_item_t *item = &pane->items[idx];

        /* 背景 */
        if (idx == pane->cursor) {
            gfx_fill_rect(x + 1, ry, width - 2, row_height - 1, cursor_bg);
        } else if (item->selected) {
            gfx_fill_rect(x + 1, ry, width - 2, row_height - 1, selected_bg);
        }

        /* 名称 */
        gfx_color_t nc = type_color(item->type);
        char name_buf[64];
        snprintf(name_buf, sizeof(name_buf), "%s%s",
                 item->type == FME_TYPE_FOLDER ? "📁 " : "📄 ",
                 item->name);
        if (strlen(name_buf) > 35) {
            name_buf[32] = '.'; name_buf[33] = '.'; name_buf[34] = '.';
            name_buf[35] = 0;
        }
        gfx_draw_string_font(x + 5, ry + 3, name_buf, nc, 11);

        /* 大小 */
        char size_buf[32];
        format_size(item->size, size_buf, sizeof(size_buf));
        gfx_draw_string_font(x + width - 80, ry + 3, size_buf,
                             (gfx_color_t){180, 180, 180, 255}, 10);

        /* 类型 */
        const char *type_names[] = {"目录","文件","图片","音频","视频",
                                    "文档","压缩","可执行","链接","其他"};
        gfx_draw_string_font(x + width - 35, ry + 3,
                             type_names[item->type],
                             (gfx_color_t){160, 160, 160, 255}, 10);
    }
}

/* 绘制预览 */
void fme_draw_preview(int x, int y, int width, int height)
{
    gfx_color_t bg = {20, 22, 30, 255};
    gfx_color_t fg = {200, 200, 200, 255};
    gfx_color_t accent = {100, 180, 255, 255};

    gfx_fill_rect(x, y, width, height, bg);
    gfx_draw_rect(x, y, width, height, (gfx_color_t){60, 60, 70, 255});

    gfx_draw_string_font(x + 10, y + 8, "预览", accent, 13);

    fme_pane_t *p = &fme.panes[fme.active_pane];
    if (p->cursor >= 0 && p->cursor < p->item_count) {
        fme_file_item_t *item = &p->items[p->cursor];
        int py = y + 35;

        gfx_draw_string_font(x + 10, py, "名称:", accent, 11);
        gfx_draw_string_font(x + 60, py, item->name, fg, 11);
        py += 20;

        gfx_draw_string_font(x + 10, py, "路径:", accent, 11);
        char path_buf[80];
        snprintf(path_buf, sizeof(path_buf), "%s", item->path);
        if (strlen(path_buf) > 40) {
            path_buf[37] = '.'; path_buf[38] = '.'; path_buf[39] = '.';
            path_buf[40] = 0;
        }
        gfx_draw_string_font(x + 60, py, path_buf, fg, 11);
        py += 20;

        char size_buf[32];
        format_size(item->size, size_buf, sizeof(size_buf));
        gfx_draw_string_font(x + 10, py, "大小:", accent, 11);
        gfx_draw_string_font(x + 60, py, size_buf, fg, 11);
        py += 20;

        const char *type_names[] = {"目录","文件","图片","音频","视频",
                                    "文档","压缩","可执行","链接","其他"};
        gfx_draw_string_font(x + 10, py, "类型:", accent, 11);
        gfx_draw_string_font(x + 60, py, type_names[item->type], fg, 11);
        py += 20;

        gfx_draw_string_font(x + 10, py, "权限:", accent, 11);
        gfx_draw_string_font(x + 60, py, "rwxr-xr-x", fg, 11);
        py += 30;

        /* 预览区域 */
        gfx_draw_rect(x + 10, py, width - 20, height - py - 10,
                      (gfx_color_t){50, 50, 60, 255});
        if (item->type == FME_TYPE_IMAGE) {
            gfx_draw_string_font(x + width/2 - 30, py + 40,
                                 "[图片预览]", accent, 12);
        } else if (item->type == FME_TYPE_FOLDER) {
            gfx_draw_string_font(x + width/2 - 30, py + 40,
                                 "[文件夹]", accent, 12);
        } else {
            gfx_draw_string_font(x + width/2 - 30, py + 40,
                                 "[文本预览]", accent, 12);
        }
    } else {
        gfx_draw_string_font(x + 10, y + 40, "未选择文件",
                             (gfx_color_t){120, 120, 120, 255}, 12);
    }
}

/* 绘制工具栏 */
void fme_draw_toolbar(int x, int y, int width)
{
    gfx_fill_rect(x, y, width, 28, (gfx_color_t){45, 50, 60, 255});
    gfx_color_t btn = {70, 80, 100, 255};
    gfx_color_t fg = {220, 220, 220, 255};

    const char *buttons[] = {"上一级", "刷新", "新建", "复制", "移动",
                             "删除", "重命名", "搜索", "视图", NULL};
    int bx = x + 5;
    for (int i = 0; buttons[i]; i++) {
        gfx_fill_rect(bx, y + 3, 55, 22, btn);
        gfx_draw_string_font(bx + 5, y + 7, buttons[i], fg, 10);
        bx += 60;
    }
}

/* 绘制状态栏 */
void fme_draw_status_bar(int x, int y, int width)
{
    gfx_fill_rect(x, y, width, 22, (gfx_color_t){35, 38, 45, 255});
    gfx_color_t fg = {180, 180, 180, 255};

    fme_pane_t *p = &fme.panes[fme.active_pane];
    char status[200];
    char total_buf[32];
    format_size(fme.total_size, total_buf, sizeof(total_buf));
    snprintf(status, sizeof(status),
             " %d个项目 (%d文件夹 %d文件)  总计: %s  选中: %d  |  "
             "F1帮助 F2重命名 F3刷新 F5复制 F6移动 F8删除 Tab切换面板",
             p->item_count, fme.total_folders, fme.total_files,
             total_buf, fme_get_selected_count(fme.active_pane));
    gfx_draw_string_font(x + 5, y + 5, status, fg, 10);
}

/* 主绘制 */
void fme_draw(int x, int y, int width, int height)
{
    /* 工具栏 */
    fme_draw_toolbar(x, y, width);

    int content_y = y + 30;
    int content_h = height - 55;

    if (fme.view_mode == FME_VIEW_DUAL_PANE) {
        int pane_w = fme.show_preview ? (width - 180) / 2 : (width - 5) / 2;
        fme_draw_pane(&fme.panes[0], x + 2, content_y, pane_w,
                      content_h, fme.active_pane == 0);
        fme_draw_pane(&fme.panes[1], x + pane_w + 5, content_y, pane_w,
                      content_h, fme.active_pane == 1);
        if (fme.show_preview) {
            fme_draw_preview(x + 2 * pane_w + 8, content_y,
                             170, content_h);
        }
    } else {
        fme_draw_pane(&fme.panes[fme.active_pane], x + 2, content_y,
                      width - 4, content_h, 1);
    }

    /* 状态栏 */
    fme_draw_status_bar(x, y + height - 22, width);
}

/* 键盘处理 */
void fme_handle_key(char key)
{
    fme_pane_t *p = &fme.panes[fme.active_pane];

    if (key == '\t') {
        fme_switch_pane();
    } else if (key == '\r' || key == '\n') {
        fme_open_item(fme.active_pane, p->cursor);
    } else if (key == 0x08) {
        fme_navigate_up(fme.active_pane);
    } else if (key == ' ') {
        fme_toggle_select(fme.active_pane, p->cursor);
        if (p->cursor < p->item_count - 1) p->cursor++;
    } else if (key == 'a' || key == 'A') {
        fme_select_all(fme.active_pane);
    } else if (key == 'h' || key == 'H') {
        fme_toggle_hidden();
    } else if (key == 'p' || key == 'P') {
        fme_toggle_preview();
    } else if (key == 'v' || key == 'V') {
        fme_cycle_view_mode();
    } else if (key == 'c' || key == 'C') {
        fme_copy_to_clipboard(fme.active_pane);
    } else if (key == 'x' || key == 'X') {
        fme_cut_to_clipboard(fme.active_pane);
    } else if (key == 'v' || key == 'V') {
        fme_paste_from_clipboard(fme.active_pane);
    }
}

void fme_handle_special(int key)
{
    fme_pane_t *p = &fme.panes[fme.active_pane];
    int visible = 20;

    if (key == 0x101) {  /* 上 */
        if (p->cursor > 0) {
            p->cursor--;
            if (p->cursor < p->scroll) p->scroll = p->cursor;
        }
    } else if (key == 0x102) {  /* 下 */
        if (p->cursor < p->item_count - 1) {
            p->cursor++;
            if (p->cursor >= p->scroll + visible)
                p->scroll = p->cursor - visible + 1;
        }
    } else if (key == 0x103) {  /* PageUp */
        p->cursor -= visible;
        if (p->cursor < 0) p->cursor = 0;
        p->scroll = p->cursor;
    } else if (key == 0x104) {  /* PageDown */
        p->cursor += visible;
        if (p->cursor >= p->item_count) p->cursor = p->item_count - 1;
        if (p->cursor >= p->scroll + visible)
            p->scroll = p->cursor - visible + 1;
    } else if (key == 0x105) {  /* Home */
        p->cursor = 0;
        p->scroll = 0;
    } else if (key == 0x106) {  /* End */
        p->cursor = p->item_count - 1;
        p->scroll = p->cursor - visible + 1;
        if (p->scroll < 0) p->scroll = 0;
    }
}

int fme_get_state(file_manager_enhanced_t *state)
{
    if (!state) return -1;
    memcpy(state, &fme, sizeof(file_manager_enhanced_t));
    return 0;
}

const char *fme_get_current_path(int pane)
{
    if (pane < 0 || pane > 1) return NULL;
    return fme.panes[pane].current_path;
}
