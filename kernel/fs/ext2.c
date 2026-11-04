/* ============================================================================
 * 赫普OS - ext2 文件系统 (只读)
 * ========================================================================== */

#include <stdint.h>
#include "ext2.h"
#include "ide_ext2.h"
#include "vfs.h"
#include "vga.h"
#include "string.h"
#include "mac.h"
#include "process.h"
#include "kheap.h"

uint8_t *ext2_disk = NULL;
uint32_t ext2_disk_size = 0;
ext2_superblock_t *ext2_sb = NULL;
uint32_t ext2_block_size = 0;
ext2_group_desc_t *ext2_gdt = NULL;
int ext2_mounted = 0;
/* V2.26: inode 大小(字节)——mke2fs 默认 256, 用 s_inode_size 而非 sizeof */
uint32_t ext2_inode_size = 128;

int ext2_init(void *disk_image, uint32_t size)
{
    ext2_disk = (uint8_t *)disk_image;
    ext2_disk_size = size;

    /* V7.68安全修复: 校验磁盘镜像大小，防止小镜像越界读 */
    if (size < 2048) {
        vga_puts("[EXT2] error: disk image too small\n");
        return -1;
    }

    /* 超级块在偏移1024字节 */
    ext2_sb = (ext2_superblock_t *)(ext2_disk + 1024);
    if (ext2_sb->s_magic != EXT2_SUPER_MAGIC) {
        vga_puts("[EXT2] error: magic mismatch \n");
        return -1;
    }

    /* V7.68安全修复: 校验块大小，防止缓冲区溢出
     * 支持的块大小: 1024(0), 2048(1), 4096(2)
     * 缓冲区固定4096字节，超过会溢出 */
    if (ext2_sb->s_log_block_size > 2) {
        vga_printf("[EXT2] error: block size %d too large (max 4096)\n",
                   1024 << ext2_sb->s_log_block_size);
        return -1;
    }
    ext2_block_size = 1024 << ext2_sb->s_log_block_size;

    /* V7.68安全修复: 校验s_inodes_per_group，防止除零 */
    if (ext2_sb->s_inodes_per_group == 0) {
        vga_puts("[EXT2] error: s_inodes_per_group is zero\n");
        return -1;
    }

    /* V7.68安全修复: 校验基本字段合理性 */
    if (ext2_sb->s_blocks_count == 0) {
        vga_puts("[EXT2] error: s_blocks_count is zero\n");
        return -1;
    }
    if (ext2_sb->s_inodes_count == 0) {
        vga_puts("[EXT2] error: s_inodes_count is zero\n");
        return -1;
    }

    /* V7.68安全修复: 校验s_blocks_per_group，防止除零 */
    if (ext2_sb->s_blocks_per_group == 0) {
        vga_puts("[EXT2] error: s_blocks_per_group is zero\n");
        return -1;
    }

    /* s_inode_size 在 superblock 偏移88(u16); 旧 ext2 无此字段则默认128 */
    ext2_inode_size = *(uint16_t *)(ext2_disk + 1024 + 88);
    if (ext2_inode_size < 128) ext2_inode_size = 128;
    /* V7.68安全修复: inode大小不能超过块大小 */
    if (ext2_inode_size > ext2_block_size) {
        vga_printf("[EXT2] error: inode size %d > block size %d\n",
                   ext2_inode_size, ext2_block_size);
        return -1;
    }

    vga_printf("[EXT2] block size: %d, inode count: %d, block count: %d\n",
               ext2_block_size, ext2_sb->s_inodes_count, ext2_sb->s_blocks_count);

    /* 块组描述符表在超级块之后的第一个块 */
    uint32_t gdt_block = (ext2_block_size == 1024) ? 2 : 1;
    /* V7.68安全修复: 校验GDT位置在镜像范围内 */
    if ((gdt_block + 1) * ext2_block_size > size) {
        vga_puts("[EXT2] error: GDT beyond disk image\n");
        return -1;
    }
    ext2_gdt = (ext2_group_desc_t *)(ext2_disk + gdt_block * ext2_block_size);

    ext2_mounted = 1;
    vga_puts("[EXT2] fs init done \n");
    return 0;
}

int ext2_read_inode(uint32_t inode_num, ext2_inode_t *inode)
{
    if (!ext2_mounted || inode_num == 0) return -1;

    /* V7.68安全修复: 校验inode号范围 */
    if (inode_num > ext2_sb->s_inodes_count) {
        vga_printf("[EXT2] inode %d > max %d\n", inode_num, ext2_sb->s_inodes_count);
        return -1;
    }

    uint32_t group = (inode_num - 1) / ext2_sb->s_inodes_per_group;
    uint32_t index = (inode_num - 1) % ext2_sb->s_inodes_per_group;

    /* V7.68安全修复: 校验group上限，防止ext2_gdt越界 */
    uint32_t max_groups = (ext2_sb->s_blocks_count + ext2_sb->s_blocks_per_group - 1) /
                           ext2_sb->s_blocks_per_group;
    if (group >= max_groups) {
        vga_printf("[EXT2] group %d >= max %d\n", group, max_groups);
        return -1;
    }

    uint32_t inode_table = ext2_gdt[group].bg_inode_table;
    /* V3.87-D6: inode 表按需块访问(不再假定整盘镜像) */
    uint32_t abs_off = index * ext2_inode_size;
    uint32_t b_idx = inode_table + abs_off / ext2_block_size;
    uint32_t b_off = abs_off % ext2_block_size;
    uint8_t *p = ext2_block_access(b_idx);
    if (!p) return -1;
    memcpy(inode, p + b_off, ext2_inode_size > sizeof(ext2_inode_t)
               ? sizeof(ext2_inode_t) : ext2_inode_size);
    return 0;
}

uint32_t ext2_get_block(ext2_inode_t *inode, uint32_t block_idx)
{
    if (block_idx < 12) {
        return inode->i_block[block_idx];
    }
    /* 间接块 (V3.87-D3: 单级 + 双级间接, 单文件 12+256+65536=65804 块)
     * V3.87-D6: 间接表按需 IDE 读(ext2_read_block), 不再假定整盘内存镜像 */
    uint32_t per = ext2_block_size / 4;   /* 每间接块指针数 (1024B/4=256) */
    static uint8_t ind_buf[4096];
    block_idx -= 12;
    if (block_idx < per) {
        uint32_t tbl = inode->i_block[12];
        if (tbl == 0) return 0;
        if (ext2_read_block(tbl, ind_buf) != 0) return 0;
        uint32_t *indirect = (uint32_t *)ind_buf;
        return indirect[block_idx];
    }
    /* 双级间接: i_block[13] → 一级表 → 二级表 → 数据块 (单缓冲顺序读) */
    block_idx -= per;
    if (block_idx < per * per) {
        uint32_t tbl = inode->i_block[13];
        if (tbl == 0) return 0;
        if (ext2_read_block(tbl, ind_buf) != 0) return 0;
        uint32_t *dind = (uint32_t *)ind_buf;
        uint32_t l1 = block_idx / per;
        uint32_t l0 = block_idx % per;
        uint32_t l1blk = dind[l1];
        if (l1blk == 0) return 0;
        if (ext2_read_block(l1blk, ind_buf) != 0) return 0;
        uint32_t *ind = (uint32_t *)ind_buf;
        return ind[l0];
    }
    return 0;
}

int ext2_read_data(ext2_inode_t *inode, uint32_t offset, void *buf, uint32_t size)
{
    if (!ext2_mounted || !inode) return -1;
    if (offset >= inode->i_size) return 0;

    uint32_t available = inode->i_size - offset;
    if (size > available) size = available;
    if (size == 0) return 0;

    uint32_t block_idx = offset / ext2_block_size;
    uint32_t block_off = offset % ext2_block_size;
    uint8_t *dst = (uint8_t *)buf;
    uint32_t remaining = size;

    while (remaining > 0) {
        uint32_t phys_block = ext2_get_block(inode, block_idx);
        if (phys_block == 0) break;

        uint32_t chunk = ext2_block_size - block_off;
        if (chunk > remaining) chunk = remaining;

        /* V3.87-D6: 数据块按需 IDE 读 */
        static uint8_t databuf[4096];
        if (ext2_read_block(phys_block, databuf) != 0) break;
        memcpy(dst, databuf + block_off, chunk);
        dst += chunk;
        remaining -= chunk;
        block_idx++;
        block_off = 0;
    }

    return size - remaining;
}

int ext2_lookup(ext2_inode_t *dir, const char *name, uint32_t *inode_num)
{
    if (!dir || !name || !inode_num) return -1;
    if (!(dir->i_mode & 0x4000)) return -1;  /* 不是目录 */

    uint32_t offset = 0;
    static uint8_t block[4096];
    int name_len = strlen(name);

    while (offset < dir->i_size) {
        uint32_t to_read = dir->i_size - offset;
        if (to_read > ext2_block_size) to_read = ext2_block_size;
        uint32_t n = ext2_read_data(dir, offset, block, to_read);
        if (n <= 0) break;

        uint32_t pos = 0;
        while (pos + 8 <= n) {  /* 最小目录项8字节 */
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(block + pos);
            if (entry->rec_len < 8 || entry->name_len >= 255) break;
            if (pos + entry->rec_len > n) break;

            if (entry->inode != 0 &&
                entry->name_len == name_len &&
                memcmp(entry->name, name, name_len) == 0) {
                *inode_num = entry->inode;
                return 0;
            }
            pos += entry->rec_len;
        }
        offset += to_read;
    }
    return -1;
}

/* ext2 VFS读操作 */
int ext2_vfs_write(vfs_node_t *node, uint32_t offset, const void *buf, uint32_t size);
static int ext2_vfs_read(vfs_node_t *node, uint32_t offset, void *buf, uint32_t size)
{
    /* V4.18-E63: MAC 读检查——subject=当前进程名, object=文件名 */
    {
        extern process_t *current;
        extern int mac_check(const char *, const char *, uint32_t);
        if (mac_check(current ? current->name : "kernel", node->name, MAC_PERM_R) != 0) {
            return -3;   /* EACCES */
        }
    }
    ext2_inode_t inode;
    uint32_t inode_num = node->inode_num;
    if (ext2_read_inode(inode_num, &inode) != 0) return -1;
    return ext2_read_data(&inode, offset, buf, size);
}

static vfs_file_ops_t ext2_file_ops = {
    .open = NULL,
    .close = NULL,
    .read = ext2_vfs_read,
    .write = ext2_vfs_write,
    .ioctl = NULL,
};

int ext2_mount(const char *mountpoint)
{
    if (!ext2_mounted) return -1;

    vfs_node_t *mnt = vfs_find(mountpoint);
    if (!mnt) {
        mnt = vfs_create_node(mountpoint, VFS_DIRECTORY);
        if (!mnt) return -1;
    }

    /* 读取根目录inode并注册为VFS节点 */
    ext2_inode_t root_inode;
    if (ext2_read_inode(EXT2_ROOT_INODE, &root_inode) != 0) return -1;

    /* 遍历根目录，注册文件 */
    uint32_t offset = 0;
    static uint8_t block[4096];
    while (offset < root_inode.i_size) {
        uint32_t to_read = root_inode.i_size - offset;
        if (to_read > 4096) to_read = 4096;
        uint32_t n = ext2_read_data(&root_inode, offset, block, to_read);
        if (n <= 0) break;
        uint32_t pos = 0;
        while (pos + 8 <= n) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(block + pos);
            if (entry->rec_len < 8 || entry->name_len >= 255) break;
            if (pos + entry->rec_len > n) break;
            if (entry->name_len > 0 && entry->name[0] != '.') {
                char path[256];
                strcpy(path, mountpoint);
                int plen = strlen(path);
                if (plen + 1 + entry->name_len >= 255) { pos += entry->rec_len; continue; }
                path[plen] = '/';
                for (int k = 0; k < entry->name_len; k++) {
                    path[plen + 1 + k] = entry->name[k];
                }
                path[plen + 1 + entry->name_len] = '\0';
                vfs_node_t *node = vfs_create_node(path,
                    (entry->file_type == EXT2_FT_DIR) ? VFS_DIRECTORY : VFS_REGULAR);
                if (node) {
                    ext2_inode_t finode;
                    ext2_read_inode(entry->inode, &finode);
                    node->size = finode.i_size;
                    node->inode_num = entry->inode;
                    node->ops = &ext2_file_ops;
                    node->private_data = (void *)(uintptr_t)entry->inode;
                }
            }
            pos += entry->rec_len;
        }
        offset += to_read;
    }

    vga_printf("[EXT2] mount to %s\n", mountpoint);
    return 0;
}

/* ---- 写入支持 ---- */

int ext2_sync(void)
{
    if (!ext2_mounted) return -1;
    ext2_block_flush();  /* V3.87-D6: 脏缓存(位图/inode表/间接表/数据块)落盘 */
    /* 写回超级块 */
    memcpy(ext2_disk + 1024, ext2_sb, sizeof(ext2_superblock_t));
    /* 写回组描述符 */
    uint32_t gdt_block = (ext2_block_size == 1024) ? 2 : 1;
    memcpy(ext2_disk + gdt_block * ext2_block_size, ext2_gdt,
           ((ext2_sb->s_blocks_count + ext2_sb->s_blocks_per_group - 1) / ext2_sb->s_blocks_per_group) * sizeof(ext2_group_desc_t));
    vga_puts("[EXT2] disk sync done \n");
    return 0;
}

/* === V0.48 新增：时间戳 + 硬链接 + 目录项整理 === */

/* 系统时间（从启动开始的秒数，简化实现） */
static uint32_t ext2_system_time = 1000000;  /* 起始时间戳 */

uint32_t ext2_get_time(void)
{
    return ext2_system_time++;
}

/* 更新inode时间戳 */
void ext2_update_atime(ext2_inode_t *inode)
{
    inode->i_atime = ext2_get_time();
}

void ext2_update_mtime(ext2_inode_t *inode)
{
    inode->i_mtime = ext2_get_time();
    inode->i_ctime = ext2_get_time();
}

/* 硬链接：创建一个已存在文件的新目录项 */
int ext2_path_link(const char *old_path, const char *new_path)
{
    if (!ext2_mounted || !old_path || !new_path) return -1;

    char old_name[256], new_name[256];
    uint32_t old_dir_num, new_dir_num;
    if (ext2_resolve_path(old_path, &old_dir_num, old_name, 256) != 0) return -1;
    if (ext2_resolve_path(new_path, &new_dir_num, new_name, 256) != 0) return -1;

    ext2_inode_t old_dir, new_dir;
    if (ext2_read_inode(old_dir_num, &old_dir) != 0) return -1;
    if (ext2_read_inode(new_dir_num, &new_dir) != 0) return -1;

    /* 查找原文件inode */
    uint32_t inode_num;
    if (ext2_lookup(&old_dir, old_name, &inode_num) != 0) return -1;

    /* 不能硬链接目录 */
    ext2_inode_t inode;
    if (ext2_read_inode(inode_num, &inode) != 0) return -1;
    if (inode.i_mode & 0x4000) return -1;  /* 目录不支持硬链接 */

    /* 检查目标是否已存在 */
    uint32_t existing;
    if (ext2_lookup(&new_dir, new_name, &existing) == 0) return -1;

    /* 增加链接计数 */
    inode.i_links_count++;
    ext2_update_mtime(&inode);
    ext2_write_inode(inode_num, &inode);

    /* 在新目录添加条目 */
    uint8_t file_type = (inode.i_mode & 0x4000) ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    ext2_add_dir_entry(&new_dir, inode_num, new_name, file_type);
    ext2_update_mtime(&new_dir);
    ext2_write_inode(new_dir_num, &new_dir);

    vga_printf("[EXT2] hardlink: %s -> %s(inode=%d, links=%d)\n",
               new_path, old_path, inode_num, inode.i_links_count);
    return 0;
}

/* 目录项整理：合并相邻空闲项，压缩目录 */
int ext2_dir_compact(const char *path)
{
    if (!ext2_mounted || !path) return -1;

    char dirname[256];
    uint32_t dir_inode_num;
    if (ext2_resolve_path(path, &dir_inode_num, dirname, 256) != 0) {
        /* 如果是根目录 */
        if (strcmp(path, "/") == 0) {
            dir_inode_num = EXT2_ROOT_INODE;
        } else {
            return -1;
        }
    }

    ext2_inode_t dir_inode;
    if (ext2_read_inode(dir_inode_num, &dir_inode) != 0) return -1;
    if (!(dir_inode.i_mode & 0x4000)) return -1;  /* 不是目录 */

    /* 遍历目录块，合并inode=0的空闲项 */
    uint32_t offset = 0;
    static uint8_t block[4096];
    int compacted = 0;

    while (offset < dir_inode.i_size) {
        uint32_t to_read = dir_inode.i_size - offset;
        if (to_read > 4096) to_read = 4096;
        ext2_read_data(&dir_inode, offset, block, to_read);

        uint32_t pos = 0;
        while (pos + 8 <= to_read) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(block + pos);
            if (entry->rec_len < 8) break;

            /* 如果当前项是空的，尝试合并到前一项 */
            if (entry->inode == 0 && pos > 0) {
                /* 找到前一项的实际起始位置 */
                uint32_t prev_pos = 0;
                while (prev_pos < pos) {
                    ext2_dir_entry_t *p = (ext2_dir_entry_t *)(block + prev_pos);
                    if (prev_pos + p->rec_len == pos) {
                        /* 合并：前一项的rec_len加上当前项的rec_len */
                        p->rec_len += entry->rec_len;
                        compacted++;
                        break;
                    }
                    prev_pos += p->rec_len;
                }
            }
            pos += entry->rec_len;
        }

        /* 写回整理后的块 */
        if (compacted > 0) {
            ext2_write_data(&dir_inode, offset, block, to_read);
        }
        offset += to_read;
    }

    ext2_update_mtime(&dir_inode);
    ext2_write_inode(dir_inode_num, &dir_inode);

    vga_printf("[EXT2] dir compact: %s(merge %d free)\n", path, compacted);
    return compacted;
}

/* 获取超级块指针（供fsck使用） */
ext2_superblock_t *ext2_get_sb(void) { return ext2_sb; }



