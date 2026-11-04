/* ============================================================================
 * 赫普OS - ext2底层操作（从fs_real.c拆分）
 * mount/unmount/inode/block操作
 * ========================================================================== */

#include <stdint.h>
#include "fs_real.h"
#include "vga.h"
#include "string.h"

extern fs_real_t fsr;

int fs_r_ext2_mount(const char *device, const char *mount_point, int readonly)
{
    for (int i = 0; i < FS_R_MAX_MOUNTS; i++) {
        if (!fsr.mounts[i].used) {
            memset(&fsr.mounts[i], 0, sizeof(fs_r_mount_t));
            fsr.mounts[i].used = 1;
            fsr.mounts[i].id = i;
            strncpy(fsr.mounts[i].device, device, 63);
            strncpy(fsr.mounts[i].mount_point, mount_point, 255);
            strncpy(fsr.mounts[i].filesystem, "ext2", 31);
            fsr.mounts[i].readonly = readonly;
            fsr.mounts[i].total_size = 4ULL * 1024 * 1024 * 1024;
            fsr.mounts[i].block_size = 1024;
            fsr.mounts[i].total_blocks = FS_R_MAX_BLOCKS;
            fsr.mounts[i].free_blocks = FS_R_MAX_BLOCKS - 100;
            fsr.mounts[i].inode_count = FS_R_MAX_INODES;
            fsr.mounts[i].free_inodes = FS_R_MAX_INODES - 10;
            fsr.mounts[i].mounted = 1;
            fsr.mount_count++;
            return i;
        }
    }
    return -1;
}

int fs_r_ext2_unmount(int mount_id)
{
    if (mount_id < 0 || mount_id >= FS_R_MAX_MOUNTS || !fsr.mounts[mount_id].used) return -1;
    fsr.mounts[mount_id].used = 0;
    fsr.mounts[mount_id].mounted = 0;
    fsr.mount_count--;
    return 0;
}

int fs_r_ext2_read_inode(int mount_id, int inode_num, ext2_inode_t *inode)
{
    (void)mount_id;
    if (inode_num < 0 || inode_num >= FS_R_MAX_INODES || !inode) return -1;
    memcpy(inode, &fsr.ext2_inodes[inode_num], sizeof(ext2_inode_t));
    return 0;
}

int fs_r_ext2_write_inode(int mount_id, int inode_num, ext2_inode_t *inode)
{
    (void)mount_id;
    if (inode_num < 0 || inode_num >= FS_R_MAX_INODES || !inode) return -1;
    memcpy(&fsr.ext2_inodes[inode_num], inode, sizeof(ext2_inode_t));
    return 0;
}

int fs_r_ext2_alloc_inode(int mount_id)
{
    (void)mount_id;
    for (int i = 1; i < FS_R_MAX_INODES; i++) {
        if (fsr.ext2_inodes[i].i_mode == 0) {
            memset(&fsr.ext2_inodes[i], 0, sizeof(ext2_inode_t));
            fsr.ext2_inodes[i].i_mode = 0x81A4;  /* 普通文件, 644 */
            fsr.ext2_inode_count++;
            return i;
        }
    }
    return -1;
}

int fs_r_ext2_free_inode(int mount_id, int inode_num)
{
    (void)mount_id;
    if (inode_num < 0 || inode_num >= FS_R_MAX_INODES) return -1;
    memset(&fsr.ext2_inodes[inode_num], 0, sizeof(ext2_inode_t));
    fsr.ext2_inode_count--;
    return 0;
}

int fs_r_ext2_alloc_block(int mount_id)
{
    (void)mount_id;
    for (int i = 100; i < FS_R_MAX_BLOCKS; i++) {
        if (fsr.ext2_blocks[i][0] == 0 && fsr.ext2_blocks[i][1] == 0) {
            fsr.ext2_block_count++;
            return i;
        }
    }
    return -1;
}

int fs_r_ext2_free_block(int mount_id, int block_num)
{
    (void)mount_id;
    if (block_num < 0 || block_num >= FS_R_MAX_BLOCKS) return -1;
    memset(fsr.ext2_blocks[block_num], 0, 1024);
    fsr.ext2_block_count--;
    return 0;
}

int fs_r_ext2_read_block(int mount_id, int block_num, void *buffer)
{
    (void)mount_id;
    if (block_num < 0 || block_num >= FS_R_MAX_BLOCKS || !buffer) return -1;
    memcpy(buffer, fsr.ext2_blocks[block_num], 1024);
    fsr.total_reads++;
    fsr.total_bytes_read += 1024;
    return 1024;
}

int fs_r_ext2_write_block(int mount_id, int block_num, const void *buffer)
{
    (void)mount_id;
    if (block_num < 0 || block_num >= FS_R_MAX_BLOCKS || !buffer) return -1;
    memcpy(fsr.ext2_blocks[block_num], buffer, 1024);
    fsr.total_writes++;
    fsr.total_bytes_written += 1024;
    return 1024;
}

int fs_r_ext2_get_superblock(int mount_id, ext2_superblock_t *sb)
{
    (void)mount_id;
    if (!sb) return -1;
    memcpy(sb, &fsr.ext2_sb, sizeof(ext2_superblock_t));
    return 0;
}

/* 文件操作 */
