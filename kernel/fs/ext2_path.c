/* ============================================================================
 * 赫普OS - ext2路径操作（从ext2.c拆分）
 * 包含create_v2/mkdir_v2/unlink_v2/rename/symlink/readlink/access
 * ========================================================================== */

#include <stdint.h>
#include "ext2.h"
#include "vga.h"
#include "string.h"

int ext2_path_create_v2(const char *path, uint16_t mode)
{
    if (!ext2_mounted || !path) return -1;

    char filename[256];
    uint32_t dir_inode_num;
    if (ext2_resolve_path(path, &dir_inode_num, filename, 256) != 0) return -1;

    ext2_inode_t dir_inode;
    if (ext2_read_inode(dir_inode_num, &dir_inode) != 0) return -1;

    /* 检查文件是否已存在 */
    uint32_t existing;
    if (ext2_lookup(&dir_inode, filename, &existing) == 0) return -1;

    /* 分配新inode */
    uint32_t inode_num = ext2_alloc_inode();
    if (inode_num == 0) return -1;

    ext2_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.i_mode = mode | 0x8000;
    inode.i_links_count = 1;
    inode.i_atime = inode.i_ctime = inode.i_mtime = ext2_get_time();
    ext2_write_inode(inode_num, &inode);

    ext2_add_dir_entry(&dir_inode, inode_num, filename, EXT2_FT_REG_FILE);
    ext2_write_inode(dir_inode_num, &dir_inode);

    vga_printf("[EXT2] create file: %s(inode=%d)\n", path, inode_num);
    return inode_num;
}

/* 重写ext2_path_mkdir：支持多级路径 */
int ext2_path_mkdir_v2(const char *path, uint16_t mode)
{
    if (!ext2_mounted || !path) return -1;

    char dirname[256];
    uint32_t parent_inode_num;
    if (ext2_resolve_path(path, &parent_inode_num, dirname, 256) != 0) return -1;

    ext2_inode_t parent_inode;
    if (ext2_read_inode(parent_inode_num, &parent_inode) != 0) return -1;

    uint32_t existing;
    if (ext2_lookup(&parent_inode, dirname, &existing) == 0) return -1;

    uint32_t inode_num = ext2_alloc_inode();
    if (inode_num == 0) return -1;

    ext2_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.i_mode = mode | 0x4000;
    inode.i_links_count = 2;
    inode.i_atime = inode.i_ctime = inode.i_mtime = ext2_get_time();

    uint32_t block = ext2_alloc_block();
    if (block == 0) { ext2_free_inode(inode_num); return -1; }
    inode.i_block[0] = block;
    inode.i_size = ext2_block_size;

    /* V3.89-E22: 修复——原代码直接写 ext2_disk 内存指针(IDE 模式无效),
     * 目录 . 与 .. 条目丢失。改用块缓存写回。 */
    {
        static uint8_t dot_blk[4096];
        if (ext2_read_block(block, dot_blk) != 0) { ext2_free_inode(inode_num); return -1; }
        ext2_dir_entry_t *dot = (ext2_dir_entry_t *)dot_blk;
        dot->inode = inode_num;
        dot->rec_len = 12;
        dot->name_len = 1;
        dot->file_type = EXT2_FT_DIR;
        dot->name[0] = '.';

        ext2_dir_entry_t *dotdot = (ext2_dir_entry_t *)(dot_blk + 12);
        dotdot->inode = parent_inode_num;
        dotdot->rec_len = ext2_block_size - 12;
        dotdot->name_len = 2;
        dotdot->file_type = EXT2_FT_DIR;
        dotdot->name[0] = '.'; dotdot->name[1] = '.';
        if (ext2_write_block(block, dot_blk) != 0) { ext2_free_inode(inode_num); return -1; }
    }

    ext2_write_inode(inode_num, &inode);
    ext2_add_dir_entry(&parent_inode, inode_num, dirname, EXT2_FT_DIR);
    ext2_write_inode(parent_inode_num, &parent_inode);

    vga_printf("[EXT2] create dir: %s(inode=%d)\n", path, inode_num);
    return inode_num;
}

/* 重写ext2_path_unlink：支持多级路径 */
int ext2_path_unlink_v2(const char *path)
{
    if (!ext2_mounted || !path) return -1;

    char filename[256];
    uint32_t dir_inode_num;
    if (ext2_resolve_path(path, &dir_inode_num, filename, 256) != 0) return -1;

    ext2_inode_t dir_inode;
    if (ext2_read_inode(dir_inode_num, &dir_inode) != 0) return -1;

    uint32_t inode_num;
    if (ext2_lookup(&dir_inode, filename, &inode_num) != 0) return -1;

    ext2_inode_t inode;
    if (ext2_read_inode(inode_num, &inode) != 0) return -1;

    /* V3.89-E21: 完整释放全部数据块(含间接/双级间接), 修复块泄漏 */
    ext2_free_file_blocks(&inode);
    ext2_free_inode(inode_num);

    /* 从目录中移除 */
    uint32_t offset = 0;
    static uint8_t block[4096];
    while (offset < dir_inode.i_size) {
        uint32_t to_read = dir_inode.i_size - offset;
        if (to_read > 4096) to_read = 4096;
        ext2_read_data(&dir_inode, offset, block, to_read);
        uint32_t pos = 0;
        while (pos + 8 <= to_read) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(block + pos);
            if (entry->rec_len < 8) break;
            if (entry->inode == inode_num) {
                entry->inode = 0;
                ext2_write_data(&dir_inode, offset + pos, entry, entry->rec_len);
                ext2_write_inode(dir_inode_num, &dir_inode);
                vga_printf("[EXT2] delete: %s(inode=%d)\n", path, inode_num);
                return 0;
            }
            pos += entry->rec_len;
        }
        offset += to_read;
    }
    return -1;
}

/* 文件重命名 */
int ext2_path_rename(const char *old_path, const char *new_path)
{
    if (!ext2_mounted || !old_path || !new_path) return -1;

    char old_name[256], new_name[256];
    uint32_t old_dir_num, new_dir_num;
    if (ext2_resolve_path(old_path, &old_dir_num, old_name, 256) != 0) return -1;
    if (ext2_resolve_path(new_path, &new_dir_num, new_name, 256) != 0) return -1;

    ext2_inode_t old_dir, new_dir;
    if (ext2_read_inode(old_dir_num, &old_dir) != 0) return -1;
    if (ext2_read_inode(new_dir_num, &new_dir) != 0) return -1;

    /* 查找旧文件inode */
    uint32_t inode_num;
    if (ext2_lookup(&old_dir, old_name, &inode_num) != 0) return -1;

    /* 如果目标已存在，先删除 */
    uint32_t existing;
    if (ext2_lookup(&new_dir, new_name, &existing) == 0) {
        ext2_inode_t ex_inode;
        ext2_read_inode(existing, &ex_inode);
        for (int i = 0; i < 12; i++) {
            if (ex_inode.i_block[i]) ext2_free_block(ex_inode.i_block[i]);
        }
        ext2_free_inode(existing);
    }

    /* 在新目录添加条目 */
    ext2_inode_t inode;
    ext2_read_inode(inode_num, &inode);
    uint8_t file_type = (inode.i_mode & 0x4000) ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    ext2_add_dir_entry(&new_dir, inode_num, new_name, file_type);
    ext2_write_inode(new_dir_num, &new_dir);

    /* 从旧目录移除 (V3.78: 幽灵项修复——删除条目时把 rec_len 合并进前一条目,
     * 目录块不再残留 inode=0 的空洞, 磁盘 debugfs 也干净) */
    uint32_t offset = 0;
    static uint8_t block[4096];
    while (offset < old_dir.i_size) {
        uint32_t to_read = old_dir.i_size - offset;
        if (to_read > 4096) to_read = 4096;
        ext2_read_data(&old_dir, offset, block, to_read);
        uint32_t pos = 0;
        ext2_dir_entry_t *prev = NULL;
        while (pos + 8 <= to_read) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(block + pos);
            if (entry->rec_len < 8) break;
            if (entry->inode == inode_num &&
                entry->name_len == (int)strlen(old_name) &&
                memcmp(entry->name, old_name, entry->name_len) == 0) {
                /* V4.46-E91: 修复——弃用 prev rec_len 合并(多次 rename 累积使
                 * 前项 rec_len 虚大, 目录项链错位 → add/remove 遍历跳项 → 幽灵目录项)。
                 * 与 unlink_v2 一致: 直接置 inode=0, 空项由 add 复用, rec_len 链稳定。 */
                entry->inode = 0;
                ext2_write_data(&old_dir, offset + pos, entry, entry->rec_len);
                ext2_write_inode(old_dir_num, &old_dir);
                vga_printf("[EXT2] rename: %s -> %s\n", old_path, new_path);
                return 0;
            }
            prev = entry;
            pos += entry->rec_len;
        }
        offset += to_read;
    }
    return -1;
}

/* === V0.47 新增：符号链接 + 权限检查 === */

/* 创建符号链接 */
int ext2_path_symlink(const char *target, const char *linkpath)
{
    if (!ext2_mounted || !target || !linkpath) return -1;
    int target_len = strlen(target);
    if (target_len > 60) return -1;  /* 只支持快速符号链接 */

    char linkname[256];
    uint32_t dir_inode_num;
    if (ext2_resolve_path(linkpath, &dir_inode_num, linkname, 256) != 0) return -1;

    ext2_inode_t dir_inode;
    if (ext2_read_inode(dir_inode_num, &dir_inode) != 0) return -1;

    /* 检查是否已存在 */
    uint32_t existing;
    if (ext2_lookup(&dir_inode, linkname, &existing) == 0) return -1;

    /* 分配inode */
    uint32_t inode_num = ext2_alloc_inode();
    if (inode_num == 0) return -1;

    ext2_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.i_mode = 0xA000 | 0777;  /* 符号链接 */
    inode.i_size = target_len;
    inode.i_links_count = 1;
    /* 快速符号链接：目标路径存在i_block中 */
    memcpy(inode.i_block, target, target_len);
    ext2_write_inode(inode_num, &inode);

    ext2_add_dir_entry(&dir_inode, inode_num, linkname, EXT2_FT_SYMLINK);
    ext2_write_inode(dir_inode_num, &dir_inode);

    vga_printf("[EXT2] symlink: %s -> %s\n", linkpath, target);
    return 0;
}

/* 读取符号链接目标 */
int ext2_path_readlink(const char *path, char *buf, int bufsize)
{
    if (!ext2_mounted || !path || !buf || bufsize <= 0) return -1;

    char filename[256];
    uint32_t dir_inode_num;
    if (ext2_resolve_path(path, &dir_inode_num, filename, 256) != 0) return -1;

    ext2_inode_t dir_inode;
    if (ext2_read_inode(dir_inode_num, &dir_inode) != 0) return -1;

    uint32_t inode_num;
    if (ext2_lookup(&dir_inode, filename, &inode_num) != 0) return -1;

    ext2_inode_t inode;
    if (ext2_read_inode(inode_num, &inode) != 0) return -1;

    if (!(inode.i_mode & 0xA000)) return -1;  /* 不是符号链接 */

    /* 快速符号链接：从i_block读取目标路径 */
    int len = inode.i_size;
    if (len >= bufsize) len = bufsize - 1;
    memcpy(buf, inode.i_block, len);
    buf[len] = 0;

    vga_printf("[EXT2] readlink: %s -> %s\n", path, buf);
    return len;
}

/* 权限检查 */
#define EXT2_S_IRUSR 0400  /* 所有者读 */
#define EXT2_S_IWUSR 0200  /* 所有者写 */
#define EXT2_S_IXUSR 0100  /* 所有者执行 */
#define EXT2_S_IRGRP 0040  /* 组读 */
#define EXT2_S_IWGRP 0020  /* 组写 */
#define EXT2_S_IXGRP 0010  /* 组执行 */
#define EXT2_S_IROTH 0004  /* 其他读 */
#define EXT2_S_IWOTH 0002  /* 其他写 */
#define EXT2_S_IXOTH 0001  /* 其他执行 */

/* 检查文件访问权限
 * mode: R_OK=4, W_OK=2, X_OK=1
 */
int ext2_path_access(const char *path, int mode)
{
    if (!ext2_mounted || !path) return -1;

    char filename[256];
    uint32_t dir_inode_num;
    if (ext2_resolve_path(path, &dir_inode_num, filename, 256) != 0) return -1;

    ext2_inode_t dir_inode;
    if (ext2_read_inode(dir_inode_num, &dir_inode) != 0) return -1;

    uint32_t inode_num;
    if (ext2_lookup(&dir_inode, filename, &inode_num) != 0) return -1;

    ext2_inode_t inode;
    if (ext2_read_inode(inode_num, &inode) != 0) return -1;

    /* 简化：当前用户为root(uid=0)，有权限访问所有文件 */
    /* 非root用户需要检查i_mode权限位 */
    uint16_t perm = inode.i_mode & 0777;
    (void)perm;

    /* 检查请求的权限 */
    if (mode & 4) {  /* R_OK */
        if (!(perm & (EXT2_S_IRUSR | EXT2_S_IRGRP | EXT2_S_IROTH))) return -1;
    }
    if (mode & 2) {  /* W_OK */
        if (!(perm & (EXT2_S_IWUSR | EXT2_S_IWGRP | EXT2_S_IWOTH))) return -1;
    }
    if (mode & 1) {  /* X_OK */
        if (!(perm & (EXT2_S_IXUSR | EXT2_S_IXGRP | EXT2_S_IXOTH))) return -1;
    }

    return 0;  /* 有权限 */
}

/* 磁盘同步：将所有脏数据写回磁盘
 * 当前ext2直接操作disk内存镜像，无需额外同步
 * 此函数为未来真实磁盘驱动预留接口
 */
