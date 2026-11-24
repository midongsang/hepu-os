/* ============================================================================
 * 赫普OS - 32/64 共享路径工具实现（无 libc 依赖）
 * ========================================================================== */
#include "fs_path.h"

static size_t fs_strlen(const char *s)
{
    size_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

static void fs_copy_trunc(char *dst, size_t dst_sz, const char *src, size_t src_len)
{
    size_t i;
    if (!dst || dst_sz == 0) return;
    for (i = 0; i < src_len && i + 1 < dst_sz; i++) dst[i] = src[i];
    dst[i] = 0;
}

int fs_path_split(const char *path, char *parent, size_t parent_sz,
                  char *name, size_t name_sz)
{
    const char *last = 0;
    const char *p;

    if (!path || !path[0] || !parent || parent_sz == 0 || !name || name_sz == 0)
        return -1;

    for (p = path; *p; p++) {
        if (*p == '/') last = p;
    }
    if (!last) {
        fs_copy_trunc(parent, parent_sz, "/", 1);
        fs_copy_trunc(name, name_sz, path, fs_strlen(path));
        return 0;
    }

    if (last == path) {
        fs_copy_trunc(parent, parent_sz, "/", 1);
    } else {
        fs_copy_trunc(parent, parent_sz, path, (size_t)(last - path));
    }
    fs_copy_trunc(name, name_sz, last + 1, fs_strlen(last + 1));
    return 0;
}

int fs_path_prefix_match(const char *prefix, const char *path)
{
    size_t pl;
    if (!prefix || !path) return 0;
    pl = fs_strlen(prefix);

    /* 根挂载点 "/" 是所有绝对路径的公共前缀，不能要求 path[1] 也是 '/'。 */
    if (pl == 1 && prefix[0] == '/') return path[0] == '/';

    for (size_t i = 0; i < pl; i++) {
        if (prefix[i] != path[i]) return 0;
    }
    if (path[pl] == '\0') return 1;
    return path[pl] == '/';
}

int fs_dirent64_reclen(int name_len)
{
    if (name_len < 0) name_len = 0;
    if (name_len > 255) name_len = 255;
    return (19 + name_len + 7) & ~7;
}
