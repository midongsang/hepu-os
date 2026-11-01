/* ============================================================================
 * 赫普OS - 64位ext2文件系统（简化版）
 * V0.10.6: 初始实现（只读支持）
 * V0.10.76: 引用共享ext2数据结构（统一源码树第一步）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* 共享ext2数据结构（架构无关） */
#include "../../shared/fs/ext2_shared.h"
/* 共享ext2算法（统一源码树P0-2） */
#include "../../shared/fs/ext2_algo.h"

/* 外部函数 */
extern void vga_puts(const char *str);
extern void vga_put_hex(uint64_t val);
extern void *memcpy(void *dst, const void *src, size_t n);
extern void *memset(void *dst, int val, size_t n);
extern int ide64_read_sector(uint32_t lba, uint8_t *buffer);
extern int ide64_read_sectors(uint32_t lba, uint8_t count, uint8_t *buffer);
/* 存储硬件抽象层（通过驱动框架调用） */
extern int storage_hal_read(uint32_t lba, uint8_t count, uint8_t *buffer);
extern int storage_hal_write(uint32_t lba, uint8_t count, const uint8_t *buffer);

/* 前向声明 */
static int ext2_write_block(uint32_t block, const uint8_t *buffer);

/* ext2常量 */
#define EXT2_MAX_BLOCK_SIZE 4096  /* 最大块大小 */
#define EXT2_SECTOR_SIZE    512

/* 全局状态 */
static ext2_superblock_t ext2_sb;
static ext2_group_desc_t ext2_gdt[32];  /* 最多32个块组 */
static uint32_t ext2_block_size;
static uint32_t ext2_groups_count;
static int ext2_mounted = 0;
static uint32_t ext2_partition_offset = 0;  /* 分区起始LBA（单镜像模式） */

/* 共享算法上下文（统一源码树P0-2） */
static ext2_context_t ext2_ctx;
static uint8_t ext2_algo_block_buf[EXT2_MAX_BLOCK_SIZE];
static int ext2_use_shared_algo = 0;  /* 是否使用共享算法 */

/* 设置分区偏移 */
void ext2_64_set_partition_offset(uint32_t lba)
{
    ext2_partition_offset = lba;
}

/* 读取一个块到缓冲区 */
static int ext2_read_block(uint32_t block, uint8_t *buffer)
{
    uint32_t lba = block * (ext2_block_size / EXT2_SECTOR_SIZE) + ext2_partition_offset;
    uint8_t count = ext2_block_size / EXT2_SECTOR_SIZE;
    int ret = storage_hal_read(lba, count, buffer);
    if (ret != 0) {
        vga_put_hex(block);
        vga_puts(" lba=");
        vga_put_hex(lba);
        vga_puts(" ret=");
        vga_put_hex(ret);
        vga_puts("\n");
    }
    return ret;
}

/* 读取inode */
static int ext2_read_inode(uint32_t ino, ext2_inode_t *inode)
{
    if (ext2_use_shared_algo) {
        return ext2_algo_read_inode(&ext2_ctx, ino, inode);
    }
    if (ino == 0) return -1;

    uint32_t group = (ino - 1) / ext2_sb.s_inodes_per_group;
    uint32_t index = (ino - 1) % ext2_sb.s_inodes_per_group;
    uint32_t inode_table = ext2_gdt[group].bg_inode_table;
    uint32_t inode_size = ext2_sb.s_inode_size ? ext2_sb.s_inode_size : 128;
    uint32_t block_offset = (index * inode_size) / ext2_block_size;
    uint32_t byte_offset = (index * inode_size) % ext2_block_size;


    uint8_t block_buf[EXT2_MAX_BLOCK_SIZE];
    if (ext2_read_block(inode_table + block_offset, block_buf) != 0) {
        return -1;
    }

    memcpy(inode, block_buf + byte_offset, sizeof(ext2_inode_t));

    return 0;
}

/* 从inode读取数据块 - V0.10.22: 支持二级间接块 */
static int ext2_read_data_block(ext2_inode_t *inode, uint32_t block_idx, uint8_t *buffer)
{
    if (ext2_use_shared_algo) {
        return ext2_algo_read_data_block(&ext2_ctx, inode, block_idx, buffer);
    }
    uint32_t block;

    if (block_idx < 12) {
        /* 直接块 */
        block = inode->i_block[block_idx];
    } else if (block_idx < 12 + 256) {
        /* 一级间接块 */
        uint32_t indirect = inode->i_block[12];
        if (indirect == 0) return -1;
        uint8_t ibuf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(indirect, ibuf) != 0) return -1;
        block = ((uint32_t *)ibuf)[block_idx - 12];
    } else if (block_idx < 12 + 256 + 65536) {
        /* 二级间接块 */
        uint32_t dbl_indirect = inode->i_block[13];
        if (dbl_indirect == 0) return -1;
        uint8_t dbl_buf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(dbl_indirect, dbl_buf) != 0) return -1;
        uint32_t idx = block_idx - (12 + 256);
        uint32_t indirect = ((uint32_t *)dbl_buf)[idx / 256];
        if (indirect == 0) return -1;
        uint8_t ibuf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(indirect, ibuf) != 0) return -1;
        block = ((uint32_t *)ibuf)[idx % 256];
    } else if (block_idx < 12 + 256 + 65536 + 16777216) {
        /* 三级间接块 */
        uint32_t tri_indirect = inode->i_block[14];
        if (tri_indirect == 0) return -1;
        uint8_t tri_buf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(tri_indirect, tri_buf) != 0) return -1;
        uint32_t idx = block_idx - (12 + 256 + 65536);
        uint32_t dbl_indirect = ((uint32_t *)tri_buf)[idx / 65536];
        if (dbl_indirect == 0) return -1;
        uint8_t dbl_buf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(dbl_indirect, dbl_buf) != 0) return -1;
        uint32_t idx2 = idx % 65536;
        uint32_t indirect = ((uint32_t *)dbl_buf)[idx2 / 256];
        if (indirect == 0) return -1;
        uint8_t ibuf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(indirect, ibuf) != 0) return -1;
        block = ((uint32_t *)ibuf)[idx2 % 256];
    } else {
        /* 超出范围 */
        return -1;
    }

    if (block == 0) return -1;
    return ext2_read_block(block, buffer);
}

/* 写入inode数据块 - V0.10.22: 支持二级间接块 */
static int ext2_write_data_block(ext2_inode_t *inode, uint32_t block_idx, const uint8_t *buffer)
{
    uint32_t block;

    if (block_idx < 12) {
        block = inode->i_block[block_idx];
    } else if (block_idx < 12 + 256) {
        uint32_t indirect = inode->i_block[12];
        if (indirect == 0) return -1;
        uint8_t ibuf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(indirect, ibuf) != 0) return -1;
        block = ((uint32_t *)ibuf)[block_idx - 12];
    } else if (block_idx < 12 + 256 + 65536) {
        uint32_t dbl_indirect = inode->i_block[13];
        if (dbl_indirect == 0) return -1;
        uint8_t dbl_buf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(dbl_indirect, dbl_buf) != 0) return -1;
        uint32_t idx = block_idx - (12 + 256);
        uint32_t indirect = ((uint32_t *)dbl_buf)[idx / 256];
        if (indirect == 0) return -1;
        uint8_t ibuf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(indirect, ibuf) != 0) return -1;
        block = ((uint32_t *)ibuf)[idx % 256];
    } else if (block_idx < 12 + 256 + 65536 + 16777216) {
        /* 三级间接块 */
        uint32_t tri_indirect = inode->i_block[14];
        if (tri_indirect == 0) return -1;
        uint8_t tri_buf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(tri_indirect, tri_buf) != 0) return -1;
        uint32_t idx = block_idx - (12 + 256 + 65536);
        uint32_t dbl_indirect = ((uint32_t *)tri_buf)[idx / 65536];
        if (dbl_indirect == 0) return -1;
        uint8_t dbl_buf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(dbl_indirect, dbl_buf) != 0) return -1;
        uint32_t idx2 = idx % 65536;
        uint32_t indirect = ((uint32_t *)dbl_buf)[idx2 / 256];
        if (indirect == 0) return -1;
        uint8_t ibuf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(indirect, ibuf) != 0) return -1;
        block = ((uint32_t *)ibuf)[idx2 % 256];
    } else {
        return -1;
    }

    if (block == 0) return -1;
    return ext2_write_block(block, buffer);
}

/* 在目录中查找文件 */
static int ext2_find_in_dir(ext2_inode_t *dir_inode, const char *name, uint32_t *found_ino)
{
    if (ext2_use_shared_algo) {
        return ext2_algo_find_in_dir(&ext2_ctx, dir_inode, name, found_ino);
    }
    uint8_t block_buf[EXT2_MAX_BLOCK_SIZE];
    uint32_t blocks = (dir_inode->i_size + ext2_block_size - 1) / ext2_block_size;

    for (uint32_t b = 0; b < blocks; b++) {
        if (ext2_read_data_block(dir_inode, b, block_buf) != 0) continue;

        uint32_t offset = 0;
        while (offset < ext2_block_size) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(block_buf + offset);
            if (entry->rec_len == 0) break;

            if (entry->name_len > 0 && entry->name_len <= 255) {
                int match = 1;
                for (uint8_t i = 0; i < entry->name_len; i++) {
                    if (name[i] != entry->name[i]) { match = 0; break; }
                }
                if (match && name[entry->name_len] == '\0') {
                    *found_ino = entry->inode;
                    return 0;
                }
            }
            offset += entry->rec_len;
        }
    }
    return -1;
}

/* 按路径查找inode */
static int ext2_lookup(const char *path, uint32_t *ino)
{
    if (ext2_use_shared_algo) {
        return ext2_algo_lookup(&ext2_ctx, path, ino);
    }
    if (path[0] != '/') return -1;

    uint32_t current = EXT2_ROOT_INO;
    const char *p = path + 1;

    while (*p) {
        /* 提取路径分量 */
        char name[256];
        int len = 0;
        while (*p && *p != '/' && len < 255) {
            name[len++] = *p++;
        }
        name[len] = '\0';

        if (len == 0) {
            if (*p == '/') p++;
            continue;
        }

        /* 读取当前目录inode */
        ext2_inode_t inode;
        if (ext2_read_inode(current, &inode) != 0) return -1;

        /* 检查是否是目录 */
        if ((inode.i_mode & 0xF000) != 0x4000) return -1;

        /* 在目录中查找 */
        if (ext2_find_in_dir(&inode, name, &current) != 0) {
            return -1;
        }

        if (*p == '/') p++;
    }

    *ino = current;
    return 0;
}

/* 初始化ext2文件系统 */
int ext2_64_init(void)
{
    vga_puts("[EXT2_64] Initializing ext2 filesystem...\n");

    extern void ide64_set_drive(int drive);
    uint8_t sector_buf[EXT2_SECTOR_SIZE * 2];
    int found = 0;

    /* 先尝试master，再尝试slave */
    for (int drive = 0; drive < 2 && !found; drive++) {
        ide64_set_drive(drive);
        vga_puts("[EXT2_64] Trying IDE ");
        vga_puts(drive ? "slave" : "master");
        vga_puts(" drive\n");

        /* 读取超级块（LBA 2 + 分区偏移，偏移1024字节） */
        uint32_t super_lba = 2 + ext2_partition_offset;
        if (ide64_read_sectors(super_lba, 2, sector_buf) != 0) {
            vga_puts("[EXT2_64] Failed to read superblock\n");
            continue;
        }
        memcpy(&ext2_sb, sector_buf + 1024 - 1024, sizeof(ext2_superblock_t));

        /* 验证魔数 */
        if (ext2_sb.s_magic != EXT2_SUPER_MAGIC) {
            vga_puts("[EXT2_64] Bad magic: 0x");
            vga_put_hex(ext2_sb.s_magic);
            vga_puts("\n");
            continue;
        }

        found = 1;
        vga_puts("[EXT2_64] ext2 found on ");
        vga_puts(drive ? "slave" : "master");
        vga_puts(" drive\n");
    }

    if (!found) {
        vga_puts("[EXT2_64] ERROR: No ext2 filesystem found\n");
        return -1;
    }

    ext2_block_size = 1024 << ext2_sb.s_log_block_size;
    ext2_groups_count = (ext2_sb.s_blocks_count + ext2_sb.s_blocks_per_group - 1) /
                        ext2_sb.s_blocks_per_group;

    vga_puts("[EXT2_64] Magic OK, block_size=");
    vga_put_hex(ext2_block_size);
    vga_puts(" groups=");
    vga_put_hex(ext2_groups_count);
    vga_puts(" first_data_block=");
    vga_put_hex(ext2_sb.s_first_data_block);
    vga_puts("\n");

    /* 读取块组描述符表 */
    uint32_t gdt_block = ext2_sb.s_first_data_block + 1;
    vga_puts("[EXT2_64] GDT block=");
    vga_put_hex(gdt_block);
    vga_puts("\n");
    uint8_t gdt_buf[EXT2_MAX_BLOCK_SIZE];
    if (ext2_read_block(gdt_block, gdt_buf) != 0) {
        vga_puts("[EXT2_64] ERROR: Failed to read GDT\n");
        return -1;
    }

    uint32_t gdt_entries = ext2_block_size / sizeof(ext2_group_desc_t);
    if (gdt_entries > 32) gdt_entries = 32;
    memcpy(ext2_gdt, gdt_buf, gdt_entries * sizeof(ext2_group_desc_t));

    vga_puts("[EXT2_64] GDT loaded, inodes_per_group=");
    vga_put_hex(ext2_sb.s_inodes_per_group);
    vga_puts("\n");

    /* 测试：列出根目录 */
    ext2_inode_t root;
    if (ext2_read_inode(EXT2_ROOT_INO, &root) == 0) {
        vga_puts("[EXT2_64] Root inode OK, size=");
        vga_put_hex(root.i_size);
        vga_puts("\n");

        /* 列出根目录内容 */
        uint8_t dir_buf[EXT2_MAX_BLOCK_SIZE];
        uint32_t blocks = (root.i_size + ext2_block_size - 1) / ext2_block_size;
        vga_puts("[EXT2_64] Root directory contents:\n");

        for (uint32_t b = 0; b < blocks && b < 1; b++) {
            if (ext2_read_data_block(&root, b, dir_buf) != 0) continue;
            uint32_t offset = 0;
            while (offset < ext2_block_size) {
                ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(dir_buf + offset);
                if (entry->rec_len == 0) break;
                if (entry->name_len > 0) {
                    vga_put_hex(offset);
                    vga_puts(" rec=");
                    vga_put_hex(entry->rec_len);
                    vga_puts(" nlen=");
                    vga_put_hex(entry->name_len);
                    vga_puts("\n");
                    char namebuf[64];
                    int nlen = entry->name_len;
                    if (nlen > 63) nlen = 63;
                    for (int i = 0; i < nlen; i++) namebuf[i] = entry->name[i];
                    namebuf[nlen] = '\0';
                    vga_puts("  ");
                    vga_puts(namebuf);
                    vga_puts(entry->file_type == 2 ? "/" : "");
                    vga_puts("\n");
                }
                offset += entry->rec_len;
            }
        }
    }

    ext2_mounted = 1;
    vga_puts("[OK] ext2 filesystem mounted\n");

    /* 测试：读取/hello.txt */
    uint8_t filebuf[256];
    int fsize = ext2_64_read_file("/hello.txt", filebuf, sizeof(filebuf) - 1);
    if (fsize > 0) {
        filebuf[fsize] = '\0';
        vga_puts("[EXT2_64] Read /hello.txt (");
        vga_put_hex(fsize);
        vga_puts(" bytes): ");
        vga_puts((const char *)filebuf);
        vga_puts("\n");
    } else {
        vga_puts("[EXT2_64] Failed to read /hello.txt\n");
    }

    /* 测试：写入新文件/test_write.txt */
    const char *test_data = "HepuOS 64-bit ext2 write test OK!";
    int wsize = ext2_64_write_file("/test_write.txt", (const uint8_t *)test_data, strlen(test_data));
    if (wsize > 0) {
        vga_puts("[EXT2_64] Write /test_write.txt (");
        vga_put_hex(wsize);
        vga_puts(" bytes)\n");

        /* 读回验证 */
        uint8_t verifybuf[256];
        int vsize = ext2_64_read_file("/test_write.txt", verifybuf, sizeof(verifybuf) - 1);
        if (vsize > 0) {
            verifybuf[vsize] = '\0';
            vga_puts("[EXT2_64] Verify read: ");
            vga_puts((const char *)verifybuf);
            vga_puts("\n");
        }
    } else {
        vga_puts("[EXT2_64] Write test failed (note: file not added to dir yet)\n");
    }

    /* 初始化共享算法上下文（统一源码树P0-2） */
    ext2_algo_init(&ext2_ctx, ext2_read_block, ext2_write_block, ext2_algo_block_buf);
    ext2_ctx.sb = ext2_sb;
    ext2_ctx.block_size = ext2_block_size;
    ext2_ctx.group_count = ext2_groups_count;
    ext2_use_shared_algo = 1;
    vga_puts("[EXT2_64] Shared ext2 algorithm initialized\n");

    return 0;
}

/* 读取文件内容 */
int ext2_64_read_file(const char *path, uint8_t *buffer, uint32_t max_size)
{
    if (ext2_use_shared_algo) {
        return ext2_algo_read_file(&ext2_ctx, path, buffer, max_size);
    }
    if (!ext2_mounted) return -1;

    uint32_t ino;
    if (ext2_lookup(path, &ino) != 0) return -1;

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) {
        return -1;
    }

    uint32_t size = inode.i_size;
    if (size > max_size) size = max_size;

    uint32_t blocks = (size + ext2_block_size - 1) / ext2_block_size;
    uint8_t block_buf[EXT2_MAX_BLOCK_SIZE];

    for (uint32_t b = 0; b < blocks; b++) {
        if (ext2_read_data_block(&inode, b, block_buf) != 0) break;
        uint32_t copy_size = ext2_block_size;
        if (b == blocks - 1) {
            copy_size = size - b * ext2_block_size;
        }
        memcpy(buffer + b * ext2_block_size, block_buf, copy_size);
    }

    return size;
}

/* 从指定偏移读取文件 - V0.10.24 */
int ext2_64_read_file_offset(const char *path, uint8_t *buffer, uint32_t max_size, uint32_t offset)
{
    if (ext2_use_shared_algo) {
        return ext2_algo_read_file_offset(&ext2_ctx, path, buffer, max_size, offset);
    }
    if (!ext2_mounted) return -1;

    uint32_t ino;
    if (ext2_lookup(path, &ino) != 0) return -1;

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) return -1;

    if (offset >= inode.i_size) return 0;

    uint32_t avail = inode.i_size - offset;
    uint32_t size = (avail < max_size) ? avail : max_size;

    uint32_t start_block = offset / ext2_block_size;
    uint32_t block_offset = offset % ext2_block_size;
    uint32_t blocks = (size + block_offset + ext2_block_size - 1) / ext2_block_size;

    uint8_t block_buf[EXT2_MAX_BLOCK_SIZE];
    uint32_t buf_offset = 0;

    for (uint32_t b = 0; b < blocks; b++) {
        if (ext2_read_data_block(&inode, start_block + b, block_buf) != 0) break;
        uint32_t src_offset = (b == 0) ? block_offset : 0;
        uint32_t copy_size = ext2_block_size - src_offset;
        if (buf_offset + copy_size > size) copy_size = size - buf_offset;
        memcpy(buffer + buf_offset, block_buf + src_offset, copy_size);
        buf_offset += copy_size;
    }

    return buf_offset;
}

/* ============================================================================
 * ext2写支持
 * ========================================================================== */

/* 写入一个块到磁盘 */
static int ext2_write_block(uint32_t block, const uint8_t *buffer)
{
    uint32_t lba = block * (ext2_block_size / EXT2_SECTOR_SIZE) + ext2_partition_offset;
    uint8_t count = ext2_block_size / EXT2_SECTOR_SIZE;
    return storage_hal_write(lba, count, buffer);
}

/* 分配一个空闲块 */
static uint32_t ext2_alloc_block(void)
{
    if (ext2_use_shared_algo) {
        return ext2_algo_alloc_block(&ext2_ctx);
    }
    if (!ext2_mounted) return 0;

    for (uint32_t g = 0; g < ext2_groups_count; g++) {
        if (ext2_gdt[g].bg_free_blocks_count == 0) continue;

        uint8_t bitmap[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(ext2_gdt[g].bg_block_bitmap, bitmap) != 0) continue;

        for (uint32_t b = 0; b < ext2_sb.s_blocks_per_group; b++) {
            if (!(bitmap[b / 8] & (1 << (b % 8)))) {
                bitmap[b / 8] |= (1 << (b % 8));
                ext2_write_block(ext2_gdt[g].bg_block_bitmap, bitmap);
                ext2_gdt[g].bg_free_blocks_count--;
                ext2_sb.s_free_blocks_count--;

                uint32_t block_num = g * ext2_sb.s_blocks_per_group + b +
                                     ext2_sb.s_first_data_block;

                static uint8_t zero_blk[EXT2_MAX_BLOCK_SIZE];
                memset(zero_blk, 0, ext2_block_size);
                ext2_write_block(block_num, zero_blk);

                return block_num;
            }
        }
    }
    return 0;
}

/* 释放一个块 - V0.10.18 */
static int ext2_free_block(uint32_t block_num)
{
    if (ext2_use_shared_algo) {
        return ext2_algo_free_block(&ext2_ctx, block_num);
    }
    if (!ext2_mounted || block_num == 0) return -1;

    uint32_t g = (block_num - ext2_sb.s_first_data_block) / ext2_sb.s_blocks_per_group;
    uint32_t b = (block_num - ext2_sb.s_first_data_block) % ext2_sb.s_blocks_per_group;

    if (g >= ext2_groups_count) return -1;

    uint8_t bitmap[EXT2_MAX_BLOCK_SIZE];
    if (ext2_read_block(ext2_gdt[g].bg_block_bitmap, bitmap) != 0) return -1;

    if (!(bitmap[b / 8] & (1 << (b % 8)))) {
        return -1;  /* 块已经是空闲的 */
    }

    bitmap[b / 8] &= ~(1 << (b % 8));
    ext2_write_block(ext2_gdt[g].bg_block_bitmap, bitmap);
    ext2_gdt[g].bg_free_blocks_count++;
    ext2_sb.s_free_blocks_count++;

    return 0;
}

/* 写入inode到磁盘 */
static int ext2_write_inode(uint32_t ino, const ext2_inode_t *inode)
{
    if (ext2_use_shared_algo) {
        return ext2_algo_write_inode(&ext2_ctx, ino, inode);
    }
    if (ino == 0) return -1;

    uint32_t group = (ino - 1) / ext2_sb.s_inodes_per_group;
    uint32_t index = (ino - 1) % ext2_sb.s_inodes_per_group;
    uint32_t inode_table = ext2_gdt[group].bg_inode_table;
    uint32_t inode_size = ext2_sb.s_inode_size ? ext2_sb.s_inode_size : 128;
    uint32_t block_offset = (index * inode_size) / ext2_block_size;
    uint32_t byte_offset = (index * inode_size) % ext2_block_size;

    uint8_t block_buf[EXT2_MAX_BLOCK_SIZE];
    if (ext2_read_block(inode_table + block_offset, block_buf) != 0) {
        return -1;
    }

    memcpy(block_buf + byte_offset, inode, sizeof(ext2_inode_t));
    return ext2_write_block(inode_table + block_offset, block_buf);
}

/* 分配一个空闲inode */
static uint32_t ext2_alloc_inode(void)
{
    if (ext2_use_shared_algo) {
        return ext2_algo_alloc_inode(&ext2_ctx);
    }
    if (!ext2_mounted) return 0;

    for (uint32_t g = 0; g < ext2_groups_count; g++) {
        if (ext2_gdt[g].bg_free_inodes_count == 0) continue;

        uint8_t bitmap[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(ext2_gdt[g].bg_inode_bitmap, bitmap) != 0) continue;

        for (uint32_t i = 0; i < ext2_sb.s_inodes_per_group; i++) {
            if (!(bitmap[i / 8] & (1 << (i % 8)))) {
                bitmap[i / 8] |= (1 << (i % 8));
                ext2_write_block(ext2_gdt[g].bg_inode_bitmap, bitmap);
                ext2_gdt[g].bg_free_inodes_count--;
                ext2_sb.s_free_inodes_count--;

                uint32_t ino = g * ext2_sb.s_inodes_per_group + i + 1;

                ext2_inode_t inode;
                memset(&inode, 0, sizeof(inode));
                ext2_write_inode(ino, &inode);

                return ino;
            }
        }
    }
    return 0;
}

/* 在目录中添加条目 */
static int ext2_add_dir_entry(uint32_t dir_ino, const char *name, uint32_t ino, uint8_t file_type)
{
    if (ext2_use_shared_algo) {
        return ext2_algo_add_dir_entry(&ext2_ctx, dir_ino, name, ino, file_type);
    }
    /* 注意：暂不使用共享算法，原实现更稳定 */
    ext2_inode_t dir_inode;
    if (ext2_read_inode(dir_ino, &dir_inode) != 0) return -1;

    uint32_t name_len = strlen(name);
    uint32_t needed = 8 + ((name_len + 3) & ~3);

    uint8_t dir_buf[EXT2_MAX_BLOCK_SIZE];
    uint32_t dir_blocks = (dir_inode.i_size + ext2_block_size - 1) / ext2_block_size;

    for (uint32_t b = 0; b < dir_blocks && b < 12; b++) {
        if (dir_inode.i_block[b] == 0) continue;
        if (ext2_read_block(dir_inode.i_block[b], dir_buf) != 0) continue;

        uint32_t offset = 0;
        while (offset < ext2_block_size) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(dir_buf + offset);
            if (entry->rec_len == 0) break;

            uint32_t actual = 8 + ((entry->name_len + 3) & ~3);
            uint32_t free_space = entry->rec_len - actual;

            if (free_space >= needed) {
                /* 分割当前条目 */
                uint16_t old_rec_len = entry->rec_len;
                entry->rec_len = actual;

                ext2_dir_entry_t *new_entry = (ext2_dir_entry_t *)(dir_buf + offset + actual);
                new_entry->inode = ino;
                new_entry->rec_len = old_rec_len - actual;
                new_entry->name_len = name_len;
                new_entry->file_type = file_type;
                memcpy(new_entry->name, name, name_len);

                ext2_write_block(dir_inode.i_block[b], dir_buf);

                /* 更新目录inode */
                dir_inode.i_links_count++;
                dir_inode.i_size += needed;
                ext2_write_inode(dir_ino, &dir_inode);

                return 0;
            }
            offset += entry->rec_len;
        }
    }

    /* 需要分配新块（简化：暂不支持） */
    return -1;
}

/* 创建文件 */
static int ext2_create_file(const char *path, uint16_t mode)
{
    /* 分离目录路径和文件名 */
    const char *slash = strrchr(path, '/');
    if (!slash) return -1;

    char dir_path[256];
    char file_name[256];
    int dir_len = slash - path;
    if (dir_len == 0) dir_len = 1;
    memcpy(dir_path, path, dir_len);
    dir_path[dir_len] = '\0';
    strcpy(file_name, slash + 1);

    /* 查找目录inode */
    uint32_t dir_ino;
    if (ext2_lookup(dir_path, &dir_ino) != 0) return -1;

    /* 检查是否已存在 */
    uint32_t existing;
    ext2_inode_t dir_inode;
    if (ext2_read_inode(dir_ino, &dir_inode) == 0) {
        if (ext2_find_in_dir(&dir_inode, file_name, &existing) == 0) {
            return existing;  /* 已存在 */
        }
    }

    /* 分配新inode */
    uint32_t new_ino = ext2_alloc_inode();
    if (new_ino == 0) return -1;

    /* 设置inode */
    ext2_inode_t new_inode;
    memset(&new_inode, 0, sizeof(new_inode));
    new_inode.i_mode = mode;
    new_inode.i_uid = 0;
    new_inode.i_gid = 0;
    new_inode.i_size = 0;
    new_inode.i_links_count = 1;
    new_inode.i_blocks = 0;

    /* 如果是目录，初始化.和..条目 */
    if ((mode & 0xF000) == 0x4000) {
        uint32_t dir_block = ext2_alloc_block();
        if (dir_block != 0) {
            new_inode.i_block[0] = dir_block;
            new_inode.i_size = ext2_block_size;
            new_inode.i_blocks = ext2_block_size / 512;
            new_inode.i_links_count = 2;  /* 目录本身 + . */

            uint8_t block_buf[EXT2_MAX_BLOCK_SIZE];
            memset(block_buf, 0, ext2_block_size);

            /* . 条目 */
            ext2_dir_entry_t *dot = (ext2_dir_entry_t *)block_buf;
            dot->inode = new_ino;
            dot->rec_len = 12;  /* 4+2+1+1+1 + 3字节对齐 */
            dot->name_len = 1;
            dot->file_type = 2;  /* 目录 */
            dot->name[0] = '.';

            /* .. 条目 */
            ext2_dir_entry_t *dotdot = (ext2_dir_entry_t *)(block_buf + 12);
            dotdot->inode = dir_ino;
            dotdot->rec_len = ext2_block_size - 12;  /* 占据剩余空间 */
            dotdot->name_len = 2;
            dotdot->file_type = 2;  /* 目录 */
            dotdot->name[0] = '.';
            dotdot->name[1] = '.';

            ext2_write_block(dir_block, block_buf);
        }
    }

    ext2_write_inode(new_ino, &new_inode);

    /* 添加目录项 */
    uint8_t file_type = (mode & 0xF000) == 0x4000 ? 2 : 1;
    if (ext2_add_dir_entry(dir_ino, file_name, new_ino, file_type) != 0) {
        return -1;
    }

    return new_ino;
}

/* 写入文件内容 - V0.10.22: 支持二级间接块（最大文件256MB+） */
int ext2_64_write_file(const char *path, const uint8_t *data, uint32_t size)
{
    if (!ext2_mounted) return -1;

    uint32_t ino;
    if (ext2_lookup(path, &ino) != 0) {
        ino = ext2_create_file(path, 0x81A4);
        if (ino <= 0) return -1;
    }

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) return -1;

    uint32_t blocks_needed = (size + ext2_block_size - 1) / ext2_block_size;
    uint32_t blocks_written = 0;
    uint32_t max_blocks = 12 + 256 + 65536 + 16777216;  /* 直接+一级+二级+三级间接块 */

    static uint8_t indirect_buf[EXT2_MAX_BLOCK_SIZE];
    static uint8_t dbl_indirect_buf[EXT2_MAX_BLOCK_SIZE];
    static uint8_t tri_indirect_buf[EXT2_MAX_BLOCK_SIZE];
    int indirect_loaded = 0;
    int dbl_indirect_loaded = 0;
    int tri_indirect_loaded = 0;
    uint32_t current_dbl_idx = 0xFFFFFFFF;
    uint32_t current_tri_idx = 0xFFFFFFFF;

    for (uint32_t b = 0; b < blocks_needed && b < max_blocks; b++) {
        uint32_t block_num = 0;

        if (b < 12) {
            /* 直接块 */
            if (inode.i_block[b] == 0) {
                inode.i_block[b] = ext2_alloc_block();
                if (inode.i_block[b] == 0) break;
            }
            block_num = inode.i_block[b];
        } else if (b < 12 + 256) {
            /* 一级间接块 */
            uint32_t idx = b - 12;
            if (inode.i_block[12] == 0) {
                inode.i_block[12] = ext2_alloc_block();
                if (inode.i_block[12] == 0) break;
                memset(indirect_buf, 0, ext2_block_size);
                ext2_write_block(inode.i_block[12], indirect_buf);
                indirect_loaded = 1;
            }
            if (!indirect_loaded) {
                if (ext2_read_block(inode.i_block[12], indirect_buf) != 0) break;
                indirect_loaded = 1;
            }
            uint32_t *entries = (uint32_t *)indirect_buf;
            if (entries[idx] == 0) {
                entries[idx] = ext2_alloc_block();
                if (entries[idx] == 0) break;
                ext2_write_block(inode.i_block[12], indirect_buf);
            }
            block_num = entries[idx];
        } else if (b < 12 + 256 + 65536) {
            /* 二级间接块 */
            uint32_t idx = b - (12 + 256);
            uint32_t dbl_idx = idx / 256;
            uint32_t sing_idx = idx % 256;

            /* 分配二级间接块 */
            if (inode.i_block[13] == 0) {
                inode.i_block[13] = ext2_alloc_block();
                if (inode.i_block[13] == 0) break;
                memset(dbl_indirect_buf, 0, ext2_block_size);
                ext2_write_block(inode.i_block[13], dbl_indirect_buf);
                dbl_indirect_loaded = 1;
            }
            if (!dbl_indirect_loaded) {
                if (ext2_read_block(inode.i_block[13], dbl_indirect_buf) != 0) break;
                dbl_indirect_loaded = 1;
            }
            uint32_t *dbl_entries = (uint32_t *)dbl_indirect_buf;

            /* 分配一级间接块 */
            if (dbl_entries[dbl_idx] == 0) {
                dbl_entries[dbl_idx] = ext2_alloc_block();
                if (dbl_entries[dbl_idx] == 0) break;
                ext2_write_block(inode.i_block[13], dbl_indirect_buf);
                memset(indirect_buf, 0, ext2_block_size);
                ext2_write_block(dbl_entries[dbl_idx], indirect_buf);
                current_dbl_idx = dbl_idx;
            } else if (current_dbl_idx != dbl_idx) {
                if (ext2_read_block(dbl_entries[dbl_idx], indirect_buf) != 0) break;
                current_dbl_idx = dbl_idx;
            }

            /* 分配数据块 */
            uint32_t *sing_entries = (uint32_t *)indirect_buf;
            if (sing_entries[sing_idx] == 0) {
                sing_entries[sing_idx] = ext2_alloc_block();
                if (sing_entries[sing_idx] == 0) break;
                ext2_write_block(dbl_entries[dbl_idx], indirect_buf);
            }
            block_num = sing_entries[sing_idx];
        } else {
            /* 三级间接块 */
            uint32_t idx = b - (12 + 256 + 65536);
            uint32_t tri_idx = idx / 65536;
            uint32_t dbl_idx = (idx % 65536) / 256;
            uint32_t sing_idx = idx % 256;

            /* 分配三级间接块 */
            if (inode.i_block[14] == 0) {
                inode.i_block[14] = ext2_alloc_block();
                if (inode.i_block[14] == 0) break;
                memset(tri_indirect_buf, 0, ext2_block_size);
                ext2_write_block(inode.i_block[14], tri_indirect_buf);
                tri_indirect_loaded = 1;
            }
            if (!tri_indirect_loaded) {
                if (ext2_read_block(inode.i_block[14], tri_indirect_buf) != 0) break;
                tri_indirect_loaded = 1;
            }
            uint32_t *tri_entries = (uint32_t *)tri_indirect_buf;

            /* 分配二级间接块 */
            if (tri_entries[tri_idx] == 0) {
                tri_entries[tri_idx] = ext2_alloc_block();
                if (tri_entries[tri_idx] == 0) break;
                ext2_write_block(inode.i_block[14], tri_indirect_buf);
                memset(dbl_indirect_buf, 0, ext2_block_size);
                ext2_write_block(tri_entries[tri_idx], dbl_indirect_buf);
                current_tri_idx = tri_idx;
                current_dbl_idx = 0xFFFFFFFF;
            } else if (current_tri_idx != tri_idx) {
                if (ext2_read_block(tri_entries[tri_idx], dbl_indirect_buf) != 0) break;
                current_tri_idx = tri_idx;
                current_dbl_idx = 0xFFFFFFFF;
            }
            uint32_t *dbl_entries = (uint32_t *)dbl_indirect_buf;

            /* 分配一级间接块 */
            if (dbl_entries[dbl_idx] == 0) {
                dbl_entries[dbl_idx] = ext2_alloc_block();
                if (dbl_entries[dbl_idx] == 0) break;
                ext2_write_block(tri_entries[tri_idx], dbl_indirect_buf);
                memset(indirect_buf, 0, ext2_block_size);
                ext2_write_block(dbl_entries[dbl_idx], indirect_buf);
                current_dbl_idx = dbl_idx;
            } else if (current_dbl_idx != dbl_idx) {
                if (ext2_read_block(dbl_entries[dbl_idx], indirect_buf) != 0) break;
                current_dbl_idx = dbl_idx;
            }

            /* 分配数据块 */
            uint32_t *sing_entries = (uint32_t *)indirect_buf;
            if (sing_entries[sing_idx] == 0) {
                sing_entries[sing_idx] = ext2_alloc_block();
                if (sing_entries[sing_idx] == 0) break;
                ext2_write_block(dbl_entries[dbl_idx], indirect_buf);
            }
            block_num = sing_entries[sing_idx];
        }

        if (block_num == 0) break;

        uint32_t offset = b * ext2_block_size;
        uint32_t write_size = ext2_block_size;
        if (b == blocks_needed - 1) {
            write_size = size - offset;
        }

        uint8_t block_buf[EXT2_MAX_BLOCK_SIZE];
        memset(block_buf, 0, ext2_block_size);
        memcpy(block_buf, data + offset, write_size);
        ext2_write_block(block_num, block_buf);
        blocks_written++;
    }

    inode.i_size = size;
    inode.i_blocks = blocks_written * (ext2_block_size / 512);
    ext2_write_inode(ino, &inode);

    return (blocks_written == blocks_needed) ? size : blocks_written * ext2_block_size;
}

/* 删除文件 - V0.10.18: 释放数据块+inode+目录项 */
int ext2_64_delete_file(const char *path)
{
    if (!ext2_mounted) return -1;

    /* 分离目录路径和文件名 */
    char dir_path[256];
    char file_name[256];
    int len = 0;
    while (path[len] && len < 255) len++;

    int last_slash = -1;
    for (int i = 0; i < len; i++) {
        if (path[i] == '/') last_slash = i;
    }

    if (last_slash <= 0) {
        /* 根目录下的文件 */
        dir_path[0] = '/'; dir_path[1] = 0;
        for (int i = 1; i < len && i < 255; i++) file_name[i-1] = path[i];
        file_name[len-1 < 255 ? len-1 : 255] = 0;
    } else {
        for (int i = 0; i < last_slash && i < 255; i++) dir_path[i] = path[i];
        dir_path[last_slash < 255 ? last_slash : 255] = 0;
        int j = 0;
        for (int i = last_slash + 1; i < len && j < 255; i++, j++) file_name[j] = path[i];
        file_name[j] = 0;
    }

    /* 查找文件inode */
    uint32_t ino;
    if (ext2_lookup(path, &ino) != 0) return -1;

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) return -1;

    /* 释放直接块 */
    for (int b = 0; b < 12; b++) {
        if (inode.i_block[b] != 0) {
            ext2_free_block(inode.i_block[b]);
        }
    }

    /* 释放一级间接块 */
    if (inode.i_block[12] != 0) {
        uint8_t ibuf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(inode.i_block[12], ibuf) == 0) {
            uint32_t *entries = (uint32_t *)ibuf;
            for (int i = 0; i < 256; i++) {
                if (entries[i] != 0) {
                    ext2_free_block(entries[i]);
                }
            }
        }
        ext2_free_block(inode.i_block[12]);
    }

    /* 释放二级间接块 */
    if (inode.i_block[13] != 0) {
        uint8_t dbl_buf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(inode.i_block[13], dbl_buf) == 0) {
            uint32_t *dbl_entries = (uint32_t *)dbl_buf;
            for (int i = 0; i < 256; i++) {
                if (dbl_entries[i] != 0) {
                    uint8_t sing_buf[EXT2_MAX_BLOCK_SIZE];
                    if (ext2_read_block(dbl_entries[i], sing_buf) == 0) {
                        uint32_t *sing_entries = (uint32_t *)sing_buf;
                        for (int j = 0; j < 256; j++) {
                            if (sing_entries[j] != 0) {
                                ext2_free_block(sing_entries[j]);
                            }
                        }
                    }
                    ext2_free_block(dbl_entries[i]);
                }
            }
        }
        ext2_free_block(inode.i_block[13]);
    }

    /* 释放三级间接块 */
    if (inode.i_block[14] != 0) {
        uint8_t tri_buf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(inode.i_block[14], tri_buf) == 0) {
            uint32_t *tri_entries = (uint32_t *)tri_buf;
            for (int i = 0; i < 256; i++) {
                if (tri_entries[i] != 0) {
                    uint8_t dbl_buf[EXT2_MAX_BLOCK_SIZE];
                    if (ext2_read_block(tri_entries[i], dbl_buf) == 0) {
                        uint32_t *dbl_entries = (uint32_t *)dbl_buf;
                        for (int j = 0; j < 256; j++) {
                            if (dbl_entries[j] != 0) {
                                uint8_t sing_buf[EXT2_MAX_BLOCK_SIZE];
                                if (ext2_read_block(dbl_entries[j], sing_buf) == 0) {
                                    uint32_t *sing_entries = (uint32_t *)sing_buf;
                                    for (int k = 0; k < 256; k++) {
                                        if (sing_entries[k] != 0) {
                                            ext2_free_block(sing_entries[k]);
                                        }
                                    }
                                }
                                ext2_free_block(dbl_entries[j]);
                            }
                        }
                    }
                    ext2_free_block(tri_entries[i]);
                }
            }
        }
        ext2_free_block(inode.i_block[14]);
    }

    /* 释放inode */
    uint32_t g = (ino - 1) / ext2_sb.s_inodes_per_group;
    uint32_t idx = (ino - 1) % ext2_sb.s_inodes_per_group;
    if (g < ext2_groups_count) {
        uint8_t bitmap[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(ext2_gdt[g].bg_inode_bitmap, bitmap) == 0) {
            bitmap[idx / 8] &= ~(1 << (idx % 8));
            ext2_write_block(ext2_gdt[g].bg_inode_bitmap, bitmap);
            ext2_gdt[g].bg_free_inodes_count++;
            ext2_sb.s_free_inodes_count++;
        }
    }

    /* 从目录中删除目录项（通过设置rec_len合并） */
    uint32_t dir_ino;
    if (ext2_lookup(dir_path, &dir_ino) == 0) {
        ext2_inode_t dir_inode;
        if (ext2_read_inode(dir_ino, &dir_inode) == 0) {
            uint32_t blocks = (dir_inode.i_size + ext2_block_size - 1) / ext2_block_size;
            for (uint32_t b = 0; b < blocks; b++) {
                uint8_t block_buf[EXT2_MAX_BLOCK_SIZE];
                if (ext2_read_data_block(&dir_inode, b, block_buf) != 0) continue;

                uint32_t offset = 0;
                ext2_dir_entry_t *prev = NULL;
                while (offset < ext2_block_size) {
                    ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(block_buf + offset);
                    if (entry->rec_len == 0) break;

                    int name_match = 1;
                    for (int i = 0; i < entry->name_len && i < 255; i++) {
                        if (entry->name[i] != file_name[i]) { name_match = 0; break; }
                    }
                    if (entry->name_len != (int)strlen(file_name)) name_match = 0;

                    if (name_match && entry->inode == ino) {
                        /* 删除：合并到前一个目录项 */
                        if (prev) {
                            prev->rec_len += entry->rec_len;
                        } else {
                            entry->inode = 0;  /* 第一个目录项，清空inode */
                        }
                        ext2_write_data_block(&dir_inode, b, block_buf);
                        break;
                    }

                    prev = entry;
                    offset += entry->rec_len;
                }
            }
        }
    }

    return 0;
}

/* 删除目录 - V0.10.21: 检查空目录+释放块+释放inode+删除目录项 */
int ext2_64_delete_dir(const char *path)
{
    if (!ext2_mounted) return -1;

    /* 查找目录inode */
    uint32_t ino;
    if (ext2_lookup(path, &ino) != 0) return -1;

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) return -1;

    /* 检查是否是目录 */
    if ((inode.i_mode & 0xF000) != 0x4000) return -1;

    /* 检查目录是否为空（只有.和..） */
    uint8_t dir_buf[EXT2_MAX_BLOCK_SIZE];
    uint32_t blocks = (inode.i_size + ext2_block_size - 1) / ext2_block_size;
    int entry_count = 0;
    for (uint32_t b = 0; b < blocks; b++) {
        if (ext2_read_data_block(&inode, b, dir_buf) != 0) continue;
        uint32_t offset = 0;
        while (offset < ext2_block_size) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(dir_buf + offset);
            if (entry->rec_len == 0) break;
            if (entry->inode != 0 && entry->name_len > 0) {
                /* 跳过.和.. */
                if (entry->name_len == 1 && entry->name[0] == '.') { offset += entry->rec_len; continue; }
                if (entry->name_len == 2 && entry->name[0] == '.' && entry->name[1] == '.') { offset += entry->rec_len; continue; }
                entry_count++;
            }
            offset += entry->rec_len;
        }
    }
    if (entry_count > 0) return -2;  /* 目录非空 */

    /* 分离目录路径和目录名 */
    char dir_path[256];
    char dir_name[256];
    int len = 0;
    while (path[len] && len < 255) len++;
    int last_slash = -1;
    for (int i = 0; i < len; i++) {
        if (path[i] == '/') last_slash = i;
    }
    if (last_slash <= 0) {
        dir_path[0] = '/'; dir_path[1] = 0;
        for (int i = 1; i < len && i < 255; i++) dir_name[i-1] = path[i];
        dir_name[len-1 < 255 ? len-1 : 255] = 0;
    } else {
        for (int i = 0; i < last_slash && i < 255; i++) dir_path[i] = path[i];
        dir_path[last_slash < 255 ? last_slash : 255] = 0;
        int j = 0;
        for (int i = last_slash + 1; i < len && j < 255; i++, j++) dir_name[j] = path[i];
        dir_name[j] = 0;
    }

    /* 释放目录数据块 */
    for (int b = 0; b < 12; b++) {
        if (inode.i_block[b] != 0) {
            ext2_free_block(inode.i_block[b]);
        }
    }

    /* 释放inode */
    uint32_t g = (ino - 1) / ext2_sb.s_inodes_per_group;
    uint32_t idx = (ino - 1) % ext2_sb.s_inodes_per_group;
    if (g < ext2_groups_count) {
        uint8_t bitmap[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(ext2_gdt[g].bg_inode_bitmap, bitmap) == 0) {
            bitmap[idx / 8] &= ~(1 << (idx % 8));
            ext2_write_block(ext2_gdt[g].bg_inode_bitmap, bitmap);
            ext2_gdt[g].bg_free_inodes_count++;
            ext2_sb.s_free_inodes_count++;
        }
    }

    /* 从父目录中删除目录项 */
    uint32_t parent_ino;
    if (ext2_lookup(dir_path, &parent_ino) == 0) {
        ext2_inode_t parent_inode;
        if (ext2_read_inode(parent_ino, &parent_inode) == 0) {
            uint32_t pblocks = (parent_inode.i_size + ext2_block_size - 1) / ext2_block_size;
            for (uint32_t b = 0; b < pblocks; b++) {
                uint8_t block_buf[EXT2_MAX_BLOCK_SIZE];
                if (ext2_read_data_block(&parent_inode, b, block_buf) != 0) continue;
                uint32_t offset = 0;
                ext2_dir_entry_t *prev = NULL;
                while (offset < ext2_block_size) {
                    ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(block_buf + offset);
                    if (entry->rec_len == 0) break;
                    int name_match = 1;
                    for (int i = 0; i < entry->name_len && i < 255; i++) {
                        if (entry->name[i] != dir_name[i]) { name_match = 0; break; }
                    }
                    if (entry->name_len != (int)strlen(dir_name)) name_match = 0;
                    if (name_match && entry->inode == ino) {
                        if (prev) {
                            prev->rec_len += entry->rec_len;
                        } else {
                            entry->inode = 0;
                        }
                        ext2_write_data_block(&parent_inode, b, block_buf);
                        break;
                    }
                    prev = entry;
                    offset += entry->rec_len;
                }
            }
        }
    }

    return 0;
}

/* 路径查找（公开接口） */
uint32_t ext2_64_lookup(const char *path)
{
    uint32_t ino = 0;
    if (ext2_lookup(path, &ino) != 0) return 0;
    return ino;
}

/* 列出目录内容 */
int ext2_64_list_dir(const char *path)
{
    uint32_t ino = ext2_64_lookup(path);
    if (ino == 0) {
        vga_puts("ls: cannot access '");
        vga_puts(path);
        vga_puts("': No such file or directory\n");
        return -1;
    }

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) {
        return -1;
    }

    if (!(inode.i_mode & 0x4000)) {  /* S_IFDIR */
        vga_puts("ls: '");
        vga_puts(path);
        vga_puts("' is not a directory\n");
        return -1;
    }

    uint8_t dir_buf[EXT2_MAX_BLOCK_SIZE];
    uint32_t blocks = (inode.i_size + ext2_block_size - 1) / ext2_block_size;

    for (uint32_t b = 0; b < blocks && b < 1; b++) {
        if (ext2_read_data_block(&inode, b, dir_buf) != 0) continue;
        uint32_t offset = 0;
        while (offset < ext2_block_size) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(dir_buf + offset);
            if (entry->rec_len == 0) break;
            if (entry->name_len > 0) {
                char namebuf[64];
                int nlen = entry->name_len;
                if (nlen > 63) nlen = 63;
                for (int i = 0; i < nlen; i++) namebuf[i] = entry->name[i];
                namebuf[nlen] = '\0';
                vga_puts(namebuf);
                vga_puts(entry->file_type == 2 ? "/" : "");
                vga_puts("\n");
            }
            offset += entry->rec_len;
        }
    }

    return 0;
}

/* 创建文件（公开接口） */
int ext2_64_create_file(const char *path)
{
    return ext2_create_file(path, 0x81A4);  /* 普通文件，rw-r--r-- */
}

/* 创建目录（公开接口） */
int ext2_64_create_dir(const char *path)
{
    return ext2_create_file(path, 0x41ED);  /* 目录，rwxr-xr-x */
}
