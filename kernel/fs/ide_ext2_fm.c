/* ============================================================================
 * 赫普OS - ext2 文件系统：文件管理器 API
 * 从 ide_ext2.c 拆分（V6.46 代码重构，大文件拆分目标<500行）
 * 包含：fm_dir_ino/ide_ext2_list_entries/ide_ext2_delete/ide_ext2_read_file/
 *       ide_ext2_write_raw/ide_ext2_create_file/ide_ext2_ls_dir
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "ext2.h"
#include "ide_ext2.h"
#include "vga.h"
#include "kheap.h"

/* 依赖 ide_ext2.c 中的全局变量 */
extern int fs_mounted;

int fm_dir_ino(const char *dirpath, uint32_t *dir_ino)
{
    if (dirpath && dirpath[0] == '/' && dirpath[1] == 0) {
        *dir_ino = EXT2_ROOT_INODE;
        return 0;
    }
    char fname[256];
    uint32_t pino;
    if (ext2_resolve_path(dirpath, &pino, fname, 256) != 0) return -1;
    ext2_inode_t pdir;
    if (ext2_read_inode(pino, &pdir) != 0) return -1;
    if (ext2_lookup(&pdir, fname, dir_ino) != 0) return -1;
    return 0;
}

/* V2.58: 手机文件管理器列表接口 */
int ide_ext2_list_entries(const char *dirpath, fm_entry_t *entries, int max)
{
    if (!entries || max <= 0) return -1;
    if (!fs_mounted) {
        if (ide_ext2_mount() != 0) return -1;
    }
    ext2_inode_t dir;
    uint32_t dir_ino;
    if (fm_dir_ino(dirpath, &dir_ino) != 0) return -1;
    if (ext2_read_inode(dir_ino, &dir) != 0) return -1;
    if (!(dir.i_mode & 0x4000)) return -1;

    uint32_t offset = 0;
    static uint8_t block[4096];
    int count = 0;
    while (offset < dir.i_size && count < max) {
        /* 按块读取，避免目录项跨块丢失 */
        uint32_t to_read = dir.i_size - offset;
        if (to_read > ext2_block_size) to_read = ext2_block_size;
        uint32_t n = ext2_read_data(&dir, offset, block, to_read);
        if (n <= 0) break;
        uint32_t pos = 0;
        while (pos + 8 <= n && count < max) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(block + pos);
            if (entry->rec_len < 8 || entry->name_len >= 255) break;
            if (pos + entry->rec_len > n) break;
            /* 跳过删除项(ino=0)与 . .. */
            if (entry->inode != 0 && entry->name_len > 0 && entry->name[0] != '.') {
                ext2_inode_t fi;
                if (ext2_read_inode(entry->inode, &fi) == 0) {
                    uint32_t nl = entry->name_len;
                    if (nl > 127) nl = 127;
                    memcpy(entries[count].name, entry->name, nl);
                    entries[count].name[nl] = 0;
                    entries[count].size = fi.i_size;
                    entries[count].is_dir = (fi.i_mode & 0x4000) ? 1 : 0;
                    count++;
                }
            }
            pos += entry->rec_len;
        }
        offset += to_read;
    }
    return count;
}

/* V2.58: 删除文件（根目录或子目录） */
int ide_ext2_delete(const char *dirpath, const char *name)
{
    if (!fs_mounted) {
        if (ide_ext2_mount() != 0) return -1;
    }
    static char path[320];
    int n = 0;
    const char *d = dirpath;
    while (*d && n < 300) { path[n++] = *d; d++; }
    if (n > 1 && path[n-1] == '/') n--;   /* 去尾斜杠 */
    if (n > 0 && path[0] != '/') { /* 前面补 / */ }
    path[n++] = '/';
    for (int i = 0; name[i] && n < 318; i++) path[n++] = name[i];
    path[n] = 0;
    return ext2_path_unlink_v2(path);
}

/* V7.47: 获取文件大小（不读取内容，用于先分配精确大小缓冲区） */
int ide_ext2_get_file_size(const char *dirpath, const char *name)
{
    if (!fs_mounted) {
        if (ide_ext2_mount() != 0) return -1;
    }
    ext2_inode_t dir;
    uint32_t dir_ino;
    if (fm_dir_ino(dirpath, &dir_ino) != 0) return -1;
    if (ext2_read_inode(dir_ino, &dir) != 0) return -1;
    uint32_t ino;
    if (ext2_lookup(&dir, name, &ino) != 0) return -1;
    ext2_inode_t fi;
    if (ext2_read_inode(ino, &fi) != 0) return -1;
    if (fi.i_mode & 0x4000) return -2;   /* 目录 */
    return (int)fi.i_size;
}

/* V2.58: 读取文件内容到 buffer（供手机 UI 查看） */
int ide_ext2_read_file(const char *dirpath, const char *name, char *buf, int max)
{
    if (!fs_mounted) {
        if (ide_ext2_mount() != 0) return -1;
    }
    ext2_inode_t dir;
    uint32_t dir_ino;
    if (fm_dir_ino(dirpath, &dir_ino) != 0) return -1;
    if (ext2_read_inode(dir_ino, &dir) != 0) return -1;
    uint32_t ino;
    if (ext2_lookup(&dir, name, &ino) != 0) return -1;
    ext2_inode_t fi;
    if (ext2_read_inode(ino, &fi) != 0) return -1;
    if (fi.i_mode & 0x4000) return -2;   /* 目录不可直接读 */
    uint32_t sz = fi.i_size;
    if (sz > (uint32_t)max - 1) sz = (uint32_t)max - 1;
    uint32_t rd = ext2_read_data(&fi, 0, buf, sz);
    /* V4.47-E92: 防御——读取失败(rd<0)或读数异常时不得越界写 buf[rd] */
    if ((int32_t)rd < 0 || rd > sz) return (int)rd;
    buf[rd] = 0;
    return (int)rd;
}

/* V2.80: 任意长度二进制写入 (BMP 等, 无 \0 截断 / 无 4095 上限)。
 * dirpath 如 "/data/photos", name 如 "photo_1.bmp", len 为数据字节数。 */
int ide_ext2_write_raw(const char *dirpath, const char *name,
                       const char *data, int len)
{
    if (!fs_mounted) {
        if (ide_ext2_mount() != 0) return -1;
    }
    static char path[320];
    int n = 0;
    if (dirpath[0] != '/') path[n++] = '/';   /* V4.44-E89: 兼容无前导斜杠的 dirpath */
    const char *d = dirpath;
    while (*d && n < 300) { path[n++] = *d; d++; }
    if (n > 1 && path[n - 1] == '/') n--;
    path[n++] = '/';
    for (int i = 0; name[i] && n < 318; i++) path[n++] = name[i];
    path[n] = 0;
    char fname[256];
    uint32_t dino;
    if (ext2_resolve_path(path, &dino, fname, 256) != 0) return -1;
    ext2_inode_t dir;
    if (ext2_read_inode(dino, &dir) != 0) return -1;
    uint32_t ex;
    if (ext2_lookup(&dir, fname, &ex) != 0) {
        int pc = ext2_path_create(path, 0x81A4);
        if (pc <= 0) return -1;
        if (ext2_resolve_path(path, &dino, fname, 256) != 0) return -1;
        if (ext2_read_inode(dino, &dir) != 0) return -1;
        if (ext2_lookup(&dir, fname, &ex) != 0) return -1;
    }
    ext2_inode_t fi;
    if (ext2_read_inode(ex, &fi) != 0) return -1;
    if (len < 0) len = 0;
    if (ext2_write_data(&fi, 0, data, (uint32_t)len) < 0) return -1;
    fi.i_size = (uint32_t)len;
    ext2_write_inode(ex, &fi);
    return len;
}

/* V2.58: 在目录下新建文件（供手机文件管理器"新建"） */
int ide_ext2_create_file(const char *dirpath, const char *name, const char *content)
{
    if (!fs_mounted) {
        if (ide_ext2_mount() != 0) return -1;
    }
    static char path[320];
    int n = 0;
    const char *d = dirpath;
    while (*d && n < 300) { path[n++] = *d; d++; }
    if (n > 1 && path[n-1] == '/') n--;
    path[n++] = '/';
    for (int i = 0; name[i] && n < 318; i++) path[n++] = name[i];
    path[n] = 0;
    /* 已存在则直接返回 */
    char fname[256];
    uint32_t dino;
    if (ext2_resolve_path(path, &dino, fname, 256) != 0) return -1;
    ext2_inode_t dir;
    if (ext2_read_inode(dino, &dir) != 0) return -1;
    uint32_t ex;
    if (ext2_lookup(&dir, fname, &ex) == 0) {
        /* 覆盖内容 */
        ext2_inode_t fi;
        if (ext2_read_inode(ex, &fi) != 0) return -1;
        uint32_t dl = 0;
        while (content[dl] && dl < 4095) dl++;
        if (ext2_write_data(&fi, 0, content, dl) >= 0) {
            fi.i_size = dl;
            ext2_write_inode(ex, &fi);
            extern int ide_ext2_sync(void);
            ide_ext2_sync();
            return (int)dl;
        }
        return -1;
    }
    int pc = ext2_path_create(path, 0x81A4);
    if (pc <= 0) return -1;
    /* 写入内容 */
    if (ext2_resolve_path(path, &dino, fname, 256) != 0) return -1;
    if (ext2_read_inode(dino, &dir) != 0) return -1;
    if (ext2_lookup(&dir, fname, &ex) != 0) return -1;
    ext2_inode_t fi;
    if (ext2_read_inode(ex, &fi) != 0) return -1;
    uint32_t dl = 0;
    while (content[dl] && dl < 4095) dl++;
    if (ext2_write_data(&fi, 0, content, dl) >= 0) {
        fi.i_size = dl;
        ext2_write_inode(ex, &fi);
        extern int ide_ext2_sync(void);
        ide_ext2_sync();
        return (int)dl;
    }
    return -1;
}
int ide_ext2_ls_dir(const char *name)
{
    if (!fs_mounted) {
        if (ide_ext2_mount() != 0) return -1;
    }
    ext2_inode_t root, dir;
    uint32_t dino = 0;
    if (ext2_read_inode(EXT2_ROOT_INODE, &root) != 0) return -1;
    if (ext2_lookup(&root, name, &dino) != 0) return -1;
    if (ext2_read_inode(dino, &dir) != 0) return -1;
    vga_printf(" /%s/:\n", name);
    uint32_t offset = 0;
    static uint8_t block[4096];
    while (offset < dir.i_size) {
        uint32_t to_read = dir.i_size - offset;
        if (to_read > 4096) to_read = 4096;
        uint32_t n = ext2_read_data(&dir, offset, block, to_read);
        if (n <= 0) break;
        uint32_t pos = 0;
        while (pos + 8 <= n) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(block + pos);
            if (entry->rec_len < 8 || entry->name_len >= 255) break;
            if (pos + entry->rec_len > n) break;
            if (entry->inode != 0) {
                char nm[256];
                uint32_t nl = entry->name_len;
                if (nl > 255) nl = 255;
                memcpy(nm, entry->name, nl);
                nm[nl] = 0;
                vga_printf("   %d  %s\n", entry->inode, nm);
            }
            pos += entry->rec_len;
        }
        offset += to_read;
    }
    return 0;
}

/* V3.87-D2: 数据盘剩余可写字节 (供安装器空间预检) */
