/* ============================================================================
 * 赫普OS - ext2目录与路径操作（从ext2.c拆分）
 * 目录条目添加/路径创建/路径解析/VFS写入
 * ========================================================================== */

#include <stdint.h>
#include "ext2.h"
#include "vga.h"
#include "string.h"
#include "mac.h"
#include "process.h"

extern uint32_t ext2_disk_size;
extern uint32_t ext2_block_size;
extern int ext2_mounted;
extern ext2_superblock_t *ext2_sb;

int ext2_add_dir_entry(ext2_inode_t *dir, uint32_t inode_num,
                              const char *name, uint8_t file_type)
{
    if (!dir || !name) return -1;
    int name_len = strlen(name);
    if (name_len > 255) return -1;

    /* 计算目录项大小（4字节对齐） */
    uint16_t entry_size = 8 + name_len;
    entry_size = (entry_size + 3) & ~3;

    /* 遍历目录寻找空位 */
    uint32_t offset = 0;
    static uint8_t block[4096];
    while (offset < dir->i_size) {
        uint32_t to_read = dir->i_size - offset;
        if (to_read > 4096) to_read = 4096;
        ext2_read_data(dir, offset, block, to_read);
        uint32_t pos = 0;
        while (pos + 8 <= to_read) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(block + pos);
            if (entry->rec_len < 8) break;
            /* 检查是否可以拆分当前目录项 */
            uint16_t actual_size = 8 + entry->name_len;
            actual_size = (actual_size + 3) & ~3;
            uint16_t free_space = entry->rec_len - actual_size;
            if (free_space >= entry_size && entry->inode != 0) {
                /* 拆分：原项缩小，新项插入 */
                entry->rec_len = actual_size;
                ext2_dir_entry_t *new_entry = (ext2_dir_entry_t *)(block + pos + actual_size);
                new_entry->inode = inode_num;
                new_entry->rec_len = free_space;
                new_entry->name_len = name_len;
                new_entry->file_type = file_type;
                memcpy(new_entry->name, name, name_len);
                ext2_write_data(dir, offset + pos, block + pos, actual_size + entry_size);
                return 0;
            }
            /* 空目录项（inode=0） */
            if (entry->inode == 0 && entry->rec_len >= entry_size) {
                entry->inode = inode_num;
                entry->name_len = name_len;
                entry->file_type = file_type;
                memcpy(entry->name, name, name_len);
                ext2_write_data(dir, offset + pos, entry, entry_size);
                return 0;
            }
            pos += entry->rec_len;
        }
        offset += to_read;
    }

    /* 目录末尾追加新块 */
    uint32_t new_block = ext2_alloc_block();
    if (new_block == 0) return -1;
    /* V3.89-E22: 修复——原代码直接写 ext2_disk 内存指针(旧内存盘架构,
     * IDE 模式下无效), 目录条目丢失且落盘全 0。改用块缓存读-改-写回。 */
    {
        static uint8_t blk[4096];
        if (ext2_read_block(new_block, blk) != 0) return -1;
        ext2_dir_entry_t *entry = (ext2_dir_entry_t *)blk;
        entry->inode = inode_num;
        entry->rec_len = ext2_block_size;
        entry->name_len = name_len;
        entry->file_type = file_type;
        memcpy(entry->name, name, name_len);
        if (ext2_write_block(new_block, blk) != 0) return -1;
    }

    /* 将新块添加到目录inode */
    uint32_t block_idx = dir->i_size / ext2_block_size;
    if (block_idx < 12) {
        dir->i_block[block_idx] = new_block;
    }
    dir->i_size += ext2_block_size;
    return 0;
}

/* 创建文件 */
int ext2_path_create(const char *path, uint16_t mode)
{
    return ext2_path_create_v2(path, mode);
}

/* 创建目录 */
int ext2_path_mkdir(const char *path, uint16_t mode)
{
    return ext2_path_mkdir_v2(path, mode);
}

/* 删除文件 */
int ext2_path_unlink(const char *path)
{
    return ext2_path_unlink_v2(path);
}

/* ext2 VFS写操作 */
int ext2_vfs_write(vfs_node_t *node, uint32_t offset, const void *buf, uint32_t size)
{
    /* V4.18-E63: MAC 写检查——subject=当前进程名, object=文件名 */
    {
        extern process_t *current;
        extern int mac_check(const char *, const char *, uint32_t);
        if (mac_check(current ? current->name : "kernel", node->name, MAC_PERM_W) != 0) {
            return -3;   /* EACCES */
        }
    }
    ext2_inode_t inode;
    if (ext2_read_inode(node->inode_num, &inode) != 0) return -1;
    int written = ext2_write_data(&inode, offset, buf, size);
    ext2_write_inode(node->inode_num, &inode);
    node->size = inode.i_size;
    return written;
}

/* === V0.46 新增：多级路径解析 + rename === */

/* 解析路径，返回最终目录的inode号和最后一级名称
 * 例如 "/a/b/c.txt" -> dir_inode=/a/b 的inode, name="c.txt"
 */
int ext2_resolve_path(const char *path, uint32_t *dir_inode_num,
                             char *name, int name_max)
{
    if (!path || !dir_inode_num || !name) return -1;
    if (path[0] != '/') return -1;  /* 只支持绝对路径 */

    uint32_t current_inode = EXT2_ROOT_INODE;
    const char *p = path + 1;  /* 跳过开头的 / */
    const char *component_start = p;

    while (*p) {
        if (*p == '/') {
            /* 遇到目录分隔符，解析当前组件 */
            int len = p - component_start;
            if (len > 0 && len < name_max) {
                char comp[256];
                memcpy(comp, component_start, len);
                comp[len] = 0;
                ext2_inode_t dir;
                if (ext2_read_inode(current_inode, &dir) != 0) return -1;
                uint32_t next_inode;
                if (ext2_lookup(&dir, comp, &next_inode) != 0) return -1;
                current_inode = next_inode;
            }
            component_start = p + 1;
        }
        p++;
    }

    /* 最后一级是文件名 */
    int len = p - component_start;
    if (len <= 0 || len >= name_max) return -1;
    memcpy(name, component_start, len);
    name[len] = 0;

    *dir_inode_num = current_inode;
    return 0;
}

/* 重写ext2_path_create：支持多级路径 */
