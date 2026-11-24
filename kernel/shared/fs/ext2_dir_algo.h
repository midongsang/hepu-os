/* ============================================================================
 * 赫普OS - 32/64 共享 ext2 目录遍历算法
 *
 * 两个架构各自负责读块（32 位 ext2_read_data / 64 位 ext2_read_data_block），
 * 但目录项的长度、边界、对齐、'.'/'..' 过滤只保留这一份实现。
 * ========================================================================== */
#ifndef EXT2_DIR_ALGO_H
#define EXT2_DIR_ALGO_H

#include <stdint.h>
#include "ext2_shared.h"

/* 读目录逻辑块：block_index 从 0 开始，buf 至少 block_size 字节。
 * 返回 0 成功，<0 失败。 */
typedef int (*ext2_dir_read_block_fn)(void *ctx, uint32_t block_index, uint8_t *buf);

/* 有效目录项回调。abs_off 是该目录项在目录文件内的字节偏移。
 * 返回 0 继续，>0 提前停止（仍算成功），<0 中止并返回错误。 */
typedef int (*ext2_dir_entry_fn)(void *ctx, const ext2_dir_entry_t *entry,
                                 uint32_t abs_off);

/* 遍历目录：跳过 inode=0、'.'、'..' 与非法 rec_len，返回有效条目数；
 * 参数非法或读块失败返回 -1。block_buf 由调用方提供，至少 block_size 字节。 */
int ext2_dir_walk(const ext2_inode_t *dir, uint32_t block_size,
                  uint8_t *block_buf,
                  ext2_dir_read_block_fn read_block, void *read_ctx,
                  ext2_dir_entry_fn on_entry, void *entry_ctx);

#endif /* EXT2_DIR_ALGO_H */
