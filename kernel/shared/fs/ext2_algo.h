/* ============================================================================
 * 赫普OS - ext2文件系统共享算法
 * 架构无关，32位和64位共用
 * V0.10.120: 统一源码树P0-2，从ext2_64.c提取核心算法
 * ========================================================================== */

#ifndef EXT2_ALGO_H
#define EXT2_ALGO_H

#include <stdint.h>
#include "ext2_shared.h"

/* 块读写回调（架构相关） */
typedef int (*ext2_read_block_fn)(uint32_t block, uint8_t *buffer);
typedef int (*ext2_write_block_fn)(uint32_t block, const uint8_t *buffer);

/* ext2上下文（包含超级块和回调） */
typedef struct {
    ext2_superblock_t sb;
    uint32_t block_size;
    uint32_t group_count;
    ext2_read_block_fn read_block;
    ext2_write_block_fn write_block;
    uint8_t *block_buf;  /* 临时块缓冲区（至少block_size字节） */
} ext2_context_t;

/* 核心算法函数 */
int ext2_algo_init(ext2_context_t *ctx, ext2_read_block_fn read_fn, ext2_write_block_fn write_fn, uint8_t *buf);

/* 安全修复：校验磁盘提供的超级块参数（块大小、每组块/inode数、inode大小等）。
 * 挂载路径必须在使用这些字段做除法/移位/缓冲区索引之前调用。
 * 返回 0=合法，<0=非法 */
int ext2_algo_validate_sb(const ext2_superblock_t *sb);
int ext2_algo_read_inode(ext2_context_t *ctx, uint32_t ino, ext2_inode_t *inode);
int ext2_algo_write_inode(ext2_context_t *ctx, uint32_t ino, const ext2_inode_t *inode);
int ext2_algo_read_data_block(ext2_context_t *ctx, const ext2_inode_t *inode, uint32_t block_idx, uint8_t *buffer);
int ext2_algo_write_data_block(ext2_context_t *ctx, ext2_inode_t *inode, uint32_t block_idx, const uint8_t *buffer);
int ext2_algo_find_in_dir(ext2_context_t *ctx, const ext2_inode_t *dir_inode, const char *name, uint32_t *found_ino);
int ext2_algo_lookup(ext2_context_t *ctx, const char *path, uint32_t *ino);
uint32_t ext2_algo_alloc_block(ext2_context_t *ctx);
int ext2_algo_free_block(ext2_context_t *ctx, uint32_t block_num);
uint32_t ext2_algo_alloc_inode(ext2_context_t *ctx);
int ext2_algo_free_inode(ext2_context_t *ctx, uint32_t ino);
void ext2_algo_free_data_blocks(ext2_context_t *ctx, ext2_inode_t *inode);
void ext2_algo_sync_sb(ext2_context_t *ctx);
int ext2_algo_add_dir_entry(ext2_context_t *ctx, uint32_t dir_ino, const char *name, uint32_t ino, uint8_t file_type);
int ext2_algo_read_file(ext2_context_t *ctx, const char *path, uint8_t *buffer, uint32_t max_size);
int ext2_algo_read_file_offset(ext2_context_t *ctx, const char *path, uint8_t *buffer, uint32_t max_size, uint32_t offset);

#endif /* EXT2_ALGO_H */
