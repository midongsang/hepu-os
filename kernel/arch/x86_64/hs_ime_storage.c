/* ============================================================================
 * 64 位 IME 存储适配（M1）
 *
 * 共享 ime_core 仍使用 32 位时代留下的 ide_ext2_* 接口名。64 位没有这套
 * 驱动，但已经有 ext2_64_* 路径 API；这里做最薄的同名适配，避免把 IME 核心
 * 再分叉成两份。
 *
 * M2：词频经 ext2_64_write_file 落盘；路径仍沿用 ime_core 的
 * data/ime/freq.txt，写入前会由 ime_core 先补建 data 父目录。
 * 返回值与 ide_ext2.h 契约一致，后续替换实现不影响 ime_core。
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "ide_ext2.h"

extern int ext2_64_read_file(const char *path, uint8_t *buffer, uint32_t max_size);
extern int ext2_64_write_file(const char *path, const uint8_t *data, uint32_t size);
extern int ext2_64_mkdir(const char *path);

static void ime_join_path(char *out, int cap, const char *dir, const char *name)
{
    int n = 0;
    if (dir) {
        while (dir[n] && n < cap - 1) { out[n] = dir[n]; n++; }
    }
    if (n > 0 && out[n - 1] != '/' && n < cap - 1) out[n++] = '/';
    int i = 0;
    while (name && name[i] && n < cap - 1) { out[n++] = name[i]; i++; }
    out[n] = 0;
}

int ide_ext2_read_file(const char *dirpath, const char *name, char *buf, int max)
{
    if (!buf || max <= 0) return -1;
    char path[192];
    ime_join_path(path, sizeof(path), dirpath, name);
    int n = ext2_64_read_file(path, (uint8_t *)buf, (uint32_t)(max - 1));
    if (n < 0) return -1;
    if (n >= max) n = max - 1;
    buf[n] = 0;
    return n;
}

int ide_ext2_writefile(const char *name, const char *data)
{
    if (!name || !data) return -1;
    char path[192];
    /* ime_core 的历史路径不带前导 '/'；64 位挂载点解析只认 /data。 */
    if (name[0] != '/' && name[0] == 'd' && name[1] == 'a' &&
        name[2] == 't' && name[3] == 'a' && name[4] == '/') {
        path[0] = '/';
        int i = 0;
        while (name[i] && i < (int)sizeof(path) - 2) { path[i + 1] = name[i]; i++; }
        path[i + 1] = 0;
    } else {
        int i = 0;
        while (name[i] && i < (int)sizeof(path) - 1) { path[i] = name[i]; i++; }
        path[i] = 0;
    }
    return ext2_64_write_file(path, (const uint8_t *)data,
                              (uint32_t)strlen(data));
}

int ide_ext2_mkdir(const char *path)
{
    if (!path) return -1;
    char npath[192];
    if (path[0] != '/' && path[0] == 'd' && path[1] == 'a' &&
        path[2] == 't' && path[3] == 'a' && (path[4] == '/' || path[4] == 0)) {
        npath[0] = '/';
        int i = 0;
        while (path[i] && i < (int)sizeof(npath) - 2) { npath[i + 1] = path[i]; i++; }
        npath[i + 1] = 0;
        return ext2_64_mkdir(npath);
    }
    return ext2_64_mkdir(path);
}

int ide_ext2_sync(void)
{
    /* ext2_64_write_file 已在内部提交；这里保持旧 API 的幂等语义。 */
    return 0;
}
