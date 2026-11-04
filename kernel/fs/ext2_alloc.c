/* ============================================================================
 * 赫普OS - ext2分配与写入（从ext2.c拆分）
 * 块分配/释放, inode分配/释放/写入, 数据写入
 * ========================================================================== */

#include <stdint.h>
#include "ext2.h"
#include "ide_ext2.h"
#include "vga.h"
#include "string.h"
#include "quota.h"
#include "process.h"

extern uint32_t ext2_disk_size;
extern uint32_t ext2_block_size;
extern int ext2_mounted;
extern ext2_superblock_t *ext2_sb;

uint32_t ext2_alloc_block(void)
{
    if (!ext2_mounted) return 0;
    uint32_t groups = (ext2_sb->s_blocks_count + ext2_sb->s_blocks_per_group - 1) / ext2_sb->s_blocks_per_group;
    for (uint32_t g = 0; g < groups; g++) {
        if (ext2_gdt[g].bg_free_blocks_count == 0) continue;
        uint8_t *bitmap = ext2_block_access(ext2_gdt[g].bg_block_bitmap);
        if (!bitmap) return 0;
        for (uint32_t b = 0; b < ext2_sb->s_blocks_per_group; b++) {
            if (!(bitmap[b / 8] & (1 << (b % 8)))) {
                bitmap[b / 8] |= (1 << (b % 8));
                ext2_block_dirty(ext2_gdt[g].bg_block_bitmap);
                ext2_gdt[g].bg_free_blocks_count--;
                ext2_sb->s_free_blocks_count--;
                uint32_t block_num = g * ext2_sb->s_blocks_per_group + b + ext2_sb->s_first_data_block;
                /* V3.87-D6: 新分配数据块在 IDE 上清零(不再依赖整盘内存镜像) */
                {
                    static uint8_t zero_blk[4096];
                    memset(zero_blk, 0, ext2_block_size);
                    if (ext2_write_block(block_num, zero_blk) != 0) {
                        /* 写失败: 回滚位图 */
                        bitmap[b / 8] &= ~(1 << (b % 8));
                        ext2_block_dirty(ext2_gdt[g].bg_block_bitmap);
                        ext2_gdt[g].bg_free_blocks_count++;
                        ext2_sb->s_free_blocks_count++;
                        return 0;
                    }
                }
                return block_num;
            }
        }
    }
    return 0;
}

void ext2_free_block(uint32_t block)
{
    if (!ext2_mounted || block == 0) return;
    uint32_t g = (block - ext2_sb->s_first_data_block) / ext2_sb->s_blocks_per_group;
    uint32_t b = (block - ext2_sb->s_first_data_block) % ext2_sb->s_blocks_per_group;
    uint8_t *bitmap = ext2_block_access(ext2_gdt[g].bg_block_bitmap);
    if (!bitmap) return;
    bitmap[b / 8] &= ~(1 << (b % 8));
    ext2_block_dirty(ext2_gdt[g].bg_block_bitmap);
    ext2_gdt[g].bg_free_blocks_count++;
    ext2_sb->s_free_blocks_count++;
}

uint32_t ext2_alloc_inode(void)
{
    if (!ext2_mounted) return 0;
    uint32_t groups = (ext2_sb->s_inodes_count + ext2_sb->s_inodes_per_group - 1) / ext2_sb->s_inodes_per_group;
    for (uint32_t g = 0; g < groups; g++) {
        if (ext2_gdt[g].bg_free_inodes_count == 0) continue;
        uint8_t *bitmap = ext2_block_access(ext2_gdt[g].bg_inode_bitmap);
        if (!bitmap) return 0;
        for (uint32_t i = 0; i < ext2_sb->s_inodes_per_group; i++) {
            if (!(bitmap[i / 8] & (1 << (i % 8)))) {
                bitmap[i / 8] |= (1 << (i % 8));
                ext2_block_dirty(ext2_gdt[g].bg_inode_bitmap);
                ext2_gdt[g].bg_free_inodes_count--;
                ext2_sb->s_free_inodes_count--;
                return g * ext2_sb->s_inodes_per_group + i + 1;
            }
        }
    }
    return 0;
}

/* V3.89-E21: 完整释放文件占用的全部块(直接+单级间接+双级间接+表块)。
 * 修复: ext2_path_unlink_v2 只释放 12 个直接块, 大文件(>12 块, 如自检 big.bin
 * 512KB)的间接/双级间接数据块与表块从不释放 → 每次启动净消耗 ~500 块 →
 * 数据盘 32768 块耗尽 → 新文件分配失败(静默写 0)。 */
void ext2_free_file_blocks(ext2_inode_t *inode)
{
    if (!inode) return;
    uint32_t per = ext2_block_size / 4;
    /* 直接块 */
    for (int i = 0; i < 12; i++) {
        if (inode->i_block[i]) {
            ext2_free_block(inode->i_block[i]);
            inode->i_block[i] = 0;
        }
    }
    /* 单级间接: 表块 + 表内指向的数据块 */
    if (inode->i_block[12]) {
        static uint8_t ind_buf[4096];
        if (ext2_read_block(inode->i_block[12], ind_buf) == 0) {
            uint32_t *ind = (uint32_t *)ind_buf;
            for (uint32_t i = 0; i < per; i++)
                if (ind[i]) ext2_free_block(ind[i]);
        }
        ext2_free_block(inode->i_block[12]);
        inode->i_block[12] = 0;
    }
    /* 双级间接: 一级表块 + 二级表块 + 二级表指向的数据块 */
    if (inode->i_block[13]) {
        static uint8_t dind_buf[4096];
        if (ext2_read_block(inode->i_block[13], dind_buf) == 0) {
            uint32_t *dind = (uint32_t *)dind_buf;
            for (uint32_t l1 = 0; l1 < per; l1++) {
                if (dind[l1]) {
                    static uint8_t l1_buf[4096];
                    if (ext2_read_block(dind[l1], l1_buf) == 0) {
                        uint32_t *ind = (uint32_t *)l1_buf;
                        for (uint32_t l0 = 0; l0 < per; l0++)
                            if (ind[l0]) ext2_free_block(ind[l0]);
                    }
                    ext2_free_block(dind[l1]);
                }
            }
        }
        ext2_free_block(inode->i_block[13]);
        inode->i_block[13] = 0;
    }
}

void ext2_free_inode(uint32_t inode_num)
{
    if (!ext2_mounted || inode_num == 0) return;
    uint32_t g = (inode_num - 1) / ext2_sb->s_inodes_per_group;
    uint32_t i = (inode_num - 1) % ext2_sb->s_inodes_per_group;
    uint8_t *bitmap = ext2_block_access(ext2_gdt[g].bg_inode_bitmap);
    if (!bitmap) return;
    bitmap[i / 8] &= ~(1 << (i % 8));
    ext2_block_dirty(ext2_gdt[g].bg_inode_bitmap);
    ext2_gdt[g].bg_free_inodes_count++;
    ext2_sb->s_free_inodes_count++;
}

int ext2_write_inode(uint32_t inode_num, ext2_inode_t *inode)
{
    if (!ext2_mounted || inode_num == 0 || !inode) return -1;
    uint32_t group = (inode_num - 1) / ext2_sb->s_inodes_per_group;
    uint32_t index = (inode_num - 1) % ext2_sb->s_inodes_per_group;
    uint32_t inode_table = ext2_gdt[group].bg_inode_table;
    /* V3.87-D6: inode 表按需块访问 */
    uint32_t abs_off = index * ext2_inode_size;
    uint32_t b_idx = inode_table + abs_off / ext2_block_size;
    uint32_t b_off = abs_off % ext2_block_size;
    uint8_t *p = ext2_block_access(b_idx);
    if (!p) return -1;
    memcpy(p + b_off, inode, ext2_inode_size > sizeof(ext2_inode_t)
               ? sizeof(ext2_inode_t) : ext2_inode_size);
    ext2_block_dirty(b_idx);
    return 0;
}

int ext2_write_data(ext2_inode_t *inode, uint32_t offset, const void *buf, uint32_t size)
{
    if (!ext2_mounted || !inode || !buf) return -1;
    /* V4.17-E62: 磁盘配额——写前预检新增块数上界(数据+间接), 超限拒绝(EDQUOT).
     * uid 暂用 current->pid 演示; root(uid=0) 无限制(quota_init 已设 0xFFFFFFFF). */
    {
        extern process_t *current;
        extern int quota_check_block(uint32_t, uint32_t);
        uint32_t max_new = size / ext2_block_size + 4;   /* 上界: 数据+间接表 */
        if (quota_check_block(current ? current->pid : 0, max_new) != 0) {
            return -2;   /* EDQUOT 语义: 配额超限 */
        }
    }
    uint32_t block_idx = offset / ext2_block_size;
    uint32_t block_off = offset % ext2_block_size;
    const uint8_t *src = (const uint8_t *)buf;
    uint32_t remaining = size;
    uint32_t per = ext2_block_size / 4;   /* 每间接块指针数 */
    uint32_t new_blocks = 0;              /* E-48: 本次实际新分配块数(数据+间接) */
    while (remaining > 0) {
        /* V3.87-D3: 双级间接上限 12+per+per*per (1024B块: 65804 块≈64MB), 超出停止 */
        if (block_idx >= 12 + per + per * per) break;
        uint32_t phys_block = ext2_get_block(inode, block_idx);
        if (phys_block == 0) {
            phys_block = ext2_alloc_block();
            if (phys_block == 0) break;
            new_blocks++;
            if (block_idx < 12) {
                inode->i_block[block_idx] = phys_block;
            } else if (block_idx < 12 + per) {
                /* V3.17修复: 间接块分配失败(返回0)时必须停止——
                 * 原代码未判空, i_block[12]=0 时 indirect 指向磁盘起始区,
                 * 写 indirect[block_idx-12] 会覆盖超级块 → 文件系统损坏 */
                if (inode->i_block[12] == 0) {
                    uint32_t ind = ext2_alloc_block();
                    if (ind == 0) break;  /* 分配失败，停止写入 */
                    inode->i_block[12] = ind;
                    new_blocks++;
                }
                /* V3.87-D6: 间接表按需读-改-写回 */
                static uint8_t ind_blk[4096];
                if (ext2_read_block(inode->i_block[12], ind_blk) != 0) break;
                uint32_t *indirect = (uint32_t *)ind_blk;
                indirect[block_idx - 12] = phys_block;
                if (ext2_write_block(inode->i_block[12], ind_blk) != 0) break;
            } else {
                /* V3.87-D3: 双级间接分配——i_block[13] → 一级表 → 二级表 */
                uint32_t d_idx = block_idx - 12 - per;
                uint32_t l1 = d_idx / per;
                uint32_t l0 = d_idx % per;
                if (inode->i_block[13] == 0) {
                    uint32_t dind_blk = ext2_alloc_block();
                    if (dind_blk == 0) break;
                    inode->i_block[13] = dind_blk;
                    new_blocks++;
                }
                /* V3.87-D6: 双级表按需读-改-写回 (单缓冲顺序操作) */
                static uint8_t dblk[4096];
                if (ext2_read_block(inode->i_block[13], dblk) != 0) break;
                uint32_t *dind = (uint32_t *)dblk;
                uint32_t l1blk = dind[l1];
                if (l1blk == 0) {
                    uint32_t ind_blk2 = ext2_alloc_block();
                    if (ind_blk2 == 0) break;
                    dind[l1] = ind_blk2;
                    l1blk = ind_blk2;
                    new_blocks++;
                    if (ext2_write_block(inode->i_block[13], dblk) != 0) break;
                }
                if (ext2_read_block(l1blk, dblk) != 0) break;
                uint32_t *indirect = (uint32_t *)dblk;
                indirect[l0] = phys_block;
                if (ext2_write_block(l1blk, dblk) != 0) break;
            }
        }
        uint32_t chunk = ext2_block_size - block_off;
        if (chunk > remaining) chunk = remaining;
        /* V3.87-D6: 数据块读-改-写回 (保留块内未写部分) */
        {
            static uint8_t data_blk[4096];
            if (ext2_read_block(phys_block, data_blk) != 0) break;
            memcpy(data_blk + block_off, src, chunk);
            if (ext2_write_block(phys_block, data_blk) != 0) break;
        }
        src += chunk;
        remaining -= chunk;
        block_idx++;
        block_off = 0;
    }
    uint32_t written = size - remaining;
    if (offset + written > inode->i_size) inode->i_size = offset + written;
    /* E-48: i_blocks 按实际分配块数(512B 单位), 而非按 size 推算——
     * 旧式 (size+511)/512 对 4B 文件得 1(实际占 1KB 块=2), 与磁盘真实
     * 分配不符(debugfs/fsck 可见). */
    inode->i_blocks += new_blocks * (ext2_block_size / 512);
    inode->i_mtime = ext2_get_time();
    /* V4.17-E62: 实际分配记账 */
    if (new_blocks > 0) {
        extern process_t *current;
        extern int quota_alloc_block(uint32_t, uint32_t);
        quota_alloc_block(current ? current->pid : 0, new_blocks);
    }
    return written;
}

/* === V0.45 新增：ext2高层文件操作 === */

/* 在目录中添加目录项 */
