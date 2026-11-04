/* ============================================================================
 * 赫普OS 64位 - 文件管理器
 * 支持目录浏览、文件查看、创建文件/目录
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);
extern uint32_t ext2_64_lookup(const char *path);
extern int ext2_64_list_dir(const char *path);
extern int ext2_64_read_file(const char *path, uint8_t *buffer, uint32_t max_size);
extern int ext2_64_create_file(const char *path);
extern int ext2_64_create_dir(const char *path);

#define FM_MAX_PATH 256
#define FM_MAX_ENTRIES 64

typedef struct {
    char     current_path[FM_MAX_PATH];
    int      initialized;
    int      selected;
} file_mgr_t;

static file_mgr_t fm;

/* 字符串复制 */
static void fm_strcpy(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

/* 字符串比较 */
static int fm_strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

/* 拼接路径 */
static void fm_join_path(char *dst, const char *base, const char *name, int max)
{
    int i = 0;
    while (base[i] && i < max - 1) {
        dst[i] = base[i];
        i++;
    }
    /* 添加斜杠 */
    if (i > 0 && dst[i-1] != '/' && i < max - 1) {
        dst[i++] = '/';
    }
    /* 添加名称 */
    int j = 0;
    while (name[j] && i < max - 1) {
        dst[i++] = name[j++];
    }
    dst[i] = 0;
}

/* 获取父目录 */
static void fm_parent_dir(char *path)
{
    int len = 0;
    while (path[len]) len++;
    /* 从后往前找斜杠 */
    while (len > 0 && path[len-1] != '/') len--;
    if (len > 1) path[len-1] = 0;  /* 去掉末尾斜杠 */
    else if (len == 1) path[1] = 0; /* 根目录 */
}

/* 初始化文件管理器 */
void fm64_init(void)
{
    fm_strcpy(fm.current_path, "/", FM_MAX_PATH);
    fm.initialized = 1;
    fm.selected = 0;
    vga_puts("[FM64] File manager initialized\n");
}

/* 列出当前目录 */
void fm64_list(void)
{
    vga_puts("\n=== Directory: ");
    vga_puts(fm.current_path);
    vga_puts(" ===\n");

    /* 显示上级目录 */
    if (fm_strcmp(fm.current_path, "/") != 0) {
        vga_puts("  ..\n");
    }

    ext2_64_list_dir(fm.current_path);
    vga_puts("========================\n");
}

/* 更改目录 */
int fm64_cd(const char *name)
{
    if (!name || !name[0]) return -1;

    /* 上级目录 */
    if (name[0] == '.' && name[1] == '.' && name[2] == 0) {
        fm_parent_dir(fm.current_path);
        return 0;
    }

    /* 根目录 */
    if (name[0] == '/' && name[1] == 0) {
        fm_strcpy(fm.current_path, "/", FM_MAX_PATH);
        return 0;
    }

    /* 绝对路径 */
    if (name[0] == '/') {
        uint32_t ino = ext2_64_lookup(name);
        if (ino == 0) {
            vga_puts("[FM64] Directory not found: ");
            vga_puts(name);
            vga_puts("\n");
            return -1;
        }
        fm_strcpy(fm.current_path, name, FM_MAX_PATH);
        return 0;
    }

    /* 相对路径 */
    char new_path[FM_MAX_PATH];
    fm_join_path(new_path, fm.current_path, name, FM_MAX_PATH);

    uint32_t ino = ext2_64_lookup(new_path);
    if (ino == 0) {
        vga_puts("[FM64] Directory not found: ");
        vga_puts(name);
        vga_puts("\n");
        return -1;
    }

    fm_strcpy(fm.current_path, new_path, FM_MAX_PATH);
    return 0;
}

/* 查看文件内容 */
int fm64_cat(const char *name)
{
    if (!name || !name[0]) return -1;

    char path[FM_MAX_PATH];
    if (name[0] == '/') {
        fm_strcpy(path, name, FM_MAX_PATH);
    } else {
        fm_join_path(path, fm.current_path, name, FM_MAX_PATH);
    }

    static uint8_t buffer[4096];
    int size = ext2_64_read_file(path, buffer, sizeof(buffer) - 1);
    if (size <= 0) {
        vga_puts("[FM64] Cannot read file: ");
        vga_puts(name);
        vga_puts("\n");
        return -1;
    }

    buffer[size] = 0;
    vga_puts("\n--- ");
    vga_puts(name);
    vga_puts(" (");
    char numbuf[16];
    int i = 0;
    int s = size;
    if (s == 0) numbuf[i++] = '0';
    while (s > 0) { numbuf[i++] = '0' + s % 10; s /= 10; }
    numbuf[i] = 0;
    /* 反转 */
    for (int j = 0; j < i / 2; j++) {
        char t = numbuf[j]; numbuf[j] = numbuf[i-1-j]; numbuf[i-1-j] = t;
    }
    vga_puts(numbuf);
    vga_puts(" bytes) ---\n");
    vga_puts((const char *)buffer);
    vga_puts("\n------------------------\n");
    return 0;
}

/* 创建文件 */
int fm64_touch(const char *name)
{
    if (!name || !name[0]) return -1;

    char path[FM_MAX_PATH];
    if (name[0] == '/') {
        fm_strcpy(path, name, FM_MAX_PATH);
    } else {
        fm_join_path(path, fm.current_path, name, FM_MAX_PATH);
    }

    int result = ext2_64_create_file(path);
    if (result == 0) {
        vga_puts("[FM64] Created file: ");
        vga_puts(name);
        vga_puts("\n");
    } else {
        vga_puts("[FM64] Failed to create file\n");
    }
    return result;
}

/* 创建目录 */
int fm64_mkdir(const char *name)
{
    if (!name || !name[0]) return -1;

    char path[FM_MAX_PATH];
    if (name[0] == '/') {
        fm_strcpy(path, name, FM_MAX_PATH);
    } else {
        fm_join_path(path, fm.current_path, name, FM_MAX_PATH);
    }

    int result = ext2_64_create_dir(path);
    if (result == 0) {
        vga_puts("[FM64] Created directory: ");
        vga_puts(name);
        vga_puts("\n");
    } else {
        vga_puts("[FM64] Failed to create directory\n");
    }
    return result;
}

/* 获取当前路径 */
const char* fm64_get_path(void)
{
    return fm.current_path;
}

/* 图形模式文件管理器绘制 */
void fm64_draw(void *fb, int fb_w, int fb_h, int x, int y, int w, int h)
{
    if (!fb) return;

    extern void font64_draw_string(void *fb, int fw, int fh,
                                   int x, int y, const char *str,
                                   uint32_t color, uint16_t size);

    /* 标题栏 */
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 10,
                       "File Manager", 0x333333, 16);

    /* 当前路径 */
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 35,
                       "Path: ", 0x666666, 16);
    font64_draw_string(fb, fb_w, fb_h, x + 60, y + 35,
                       fm.current_path, 0x333333, 16);

    /* 分隔线 */
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 55,
                       "------------------------", 0x999999, 16);

    /* 目录列表（文本模式输出，图形模式下简化显示） */
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 75,
                       "..  (parent directory)", 0x2C5F8D, 16);
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 95,
                       "Use 'ls' to list files", 0x666666, 16);
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 115,
                       "Use 'cd <dir>' to navigate", 0x666666, 16);
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + 135,
                       "Use 'cat <file>' to view", 0x666666, 16);

    /* 底部提示 */
    font64_draw_string(fb, fb_w, fb_h, x + 10, y + h - 25,
                       "Commands: ls cd cat touch mkdir pwd", 0x999999, 16);
}
