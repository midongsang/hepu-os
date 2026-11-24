/* ============================================================================
 * 赫普OS - 32/64 共享 ext2 目录遍历算法实现
 * ========================================================================== */
#include "ext2_dir_algo.h"

int ext2_dir_walk(const ext2_inode_t *dir, uint32_t block_size,
                  uint8_t *block_buf,
                  ext2_dir_read_block_fn read_block, void *read_ctx,
                  ext2_dir_entry_fn on_entry, void *entry_ctx)
{
    uint32_t blocks;
    int count = 0;

    if (!dir || !block_buf || !read_block || !on_entry) return -1;
    if (block_size == 0 || block_size > 4096) return -1;
    if ((dir->i_mode & EXT2_S_IFMT) != EXT2_S_IFDIR) return -1;

    blocks = (dir->i_size + block_size - 1) / block_size;

    for (uint32_t b = 0; b < blocks; b++) {
        uint32_t offset = 0;
        uint32_t guard = 0;

        /* 直接块为空说明该目录块尚未分配，跳过。间接块由读块回调解析。 */
        if (b < EXT2_NDIR_BLOCKS && dir->i_block[b] == 0) continue;
        if (read_block(read_ctx, b, block_buf) != 0) return -1;

        while (offset + 8 <= block_size && guard++ < 512) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(block_buf + offset);
            uint32_t rec = entry->rec_len;
            uint32_t nlen = entry->name_len;

            /* ext2 rec_len 必须 4 字节对齐，且不能跨出当前块。 */
            if (rec < 8 || (rec & 3) || rec > block_size - offset) break;
            if (nlen + 8 > rec || nlen > 255) break;

            if (entry->inode != 0 && nlen > 0) {
                int is_dot = (nlen == 1 && entry->name[0] == '.');
                int is_dotdot = (nlen == 2 && entry->name[0] == '.' &&
                                 entry->name[1] == '.');
                if (!is_dot && !is_dotdot) {
                    int r = on_entry(entry_ctx, entry, b * block_size + offset);
                    if (r < 0) return r;
                    count++;
                    if (r > 0) return count;
                }
            }
            offset += rec;
        }
    }
    return count;
}
