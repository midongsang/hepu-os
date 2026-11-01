/* ============================================================================
 * 赫普OS - ext2文件系统检查修复工具 (fsck)
 * ========================================================================== */

#include <stdint.h>
#include "ext2_fsck.h"
#include "ext2.h"
#include "vga.h"
#include "string.h"

/* 检查文件系统一致性 */
int ext2_fsck_check(fsck_result_t *result)
{
    if (!result) return -1;
    memset(result, 0, sizeof(fsck_result_t));

    vga_puts("[FSCK] start ext2 fs check ...\n");

    /* 检查超级块 */
    extern ext2_superblock_t *ext2_get_sb(void);
    ext2_superblock_t *sb = ext2_get_sb();
    if (!sb) {
        vga_puts("[FSCK] error: cannot get superblock \n");
        result->errors_found++;
        return -1;
    }

    result->total_inodes = sb->s_inodes_count;
    result->total_blocks = sb->s_blocks_count;
    result->free_inodes = sb->s_free_inodes_count;
    result->free_blocks = sb->s_free_blocks_count;

    vga_printf("[FSCK] superblock: %d inodes, %d blocks\n",
               result->total_inodes, result->total_blocks);

    /* 遍历所有inode，检查一致性 */
    uint8_t *inode_bitmap = (uint8_t *)0;  /* 简化：跳过实际位图检查 */
    (void)inode_bitmap;

    for (uint32_t ino = 1; ino <= result->total_inodes; ino++) {
        ext2_inode_t inode;
        if (ext2_read_inode(ino, &inode) != 0) {
            result->bad_inodes++;
            continue;
        }

        if (inode.i_mode == 0) continue;  /* 未使用的inode */

        /* 统计文件类型 */
        if (inode.i_mode & 0x4000) {
            result->directories++;
        } else if (inode.i_mode & 0x8000) {
            result->regular_files++;
        } else if (inode.i_mode & 0xA000) {
            result->symlinks++;
        }

        /* 检查链接计数 */
        if (inode.i_links_count == 0 && inode.i_mode != 0) {
            result->unreferenced_inodes++;
            result->errors_found++;
        }

        /* 检查块指针有效性 */
        for (int i = 0; i < 12; i++) {
            if (inode.i_block[i] != 0) {
                if (inode.i_block[i] > result->total_blocks) {
                    result->bad_blocks++;
                    result->errors_found++;
                }
            }
        }

        /* 检查i_blocks与i_size一致性 */
        uint32_t expected_blocks = (inode.i_size + 511) / 512;
        if (inode.i_blocks > 0 && inode.i_blocks < expected_blocks) {
            result->errors_found++;
        }
    }

    vga_printf("[FSCK] check done: dir =%d, file =%d, symlink =%d\n",
               result->directories, result->regular_files, result->symlinks);
    vga_printf("[FSCK] errors found: %d(bad inode=%d, bad block =%d, not referenced inode=%d)\n",
               result->errors_found, result->bad_inodes,
               result->bad_blocks, result->unreferenced_inodes);

    return result->errors_found;
}

/* 修复文件系统错误 */
int ext2_fsck_repair(fsck_result_t *result)
{
    if (!result) return -1;

    vga_puts("[FSCK] start repair ...\n");

    /* 修复未引用的inode：清除inode */
    for (uint32_t ino = 1; ino <= result->total_inodes; ino++) {
        ext2_inode_t inode;
        if (ext2_read_inode(ino, &inode) != 0) continue;
        if (inode.i_mode == 0) continue;

        if (inode.i_links_count == 0) {
            /* 释放数据块 */
            for (int i = 0; i < 12; i++) {
                if (inode.i_block[i]) {
                    ext2_free_block(inode.i_block[i]);
                }
            }
            /* 清除inode */
            memset(&inode, 0, sizeof(inode));
            ext2_write_inode(ino, &inode);
            ext2_free_inode(ino);
            result->errors_fixed++;
        }
    }

    /* 同步超级块 */
    extern int ext2_sync(void);
    ext2_sync();

    vga_printf("[FSCK] repair done: repair %d error \n", result->errors_fixed);
    return result->errors_fixed;
}
