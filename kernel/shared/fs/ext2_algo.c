/* ============================================================================
 * 赫普OS - ext2文件系统共享算法实现
 * 架构无关，32位和64位共用
 * V0.10.120: 统一源码树P0-2
 * ========================================================================== */

#include "ext2_algo.h"
#include <stddef.h>

/* 内核提供的标准库函数 */
extern void *memcpy(void *dst, const void *src, size_t n);
extern void *memset(void *dst, int val, size_t n);
extern int strlen(const char *s);
extern int strncmp(const char *s1, const char *s2, size_t n);

/* 初始化ext2上下文，读取超级块 */
int ext2_algo_init(ext2_context_t *ctx, ext2_read_block_fn read_fn, ext2_write_block_fn write_fn, uint8_t *buf) {
    ctx->read_block = read_fn;
    ctx->write_block = write_fn;
    ctx->block_buf = buf;

    /* 读取超级块（块2，LBA 2） */
    if (read_fn(2, buf) != 0) return -1;
    memcpy(&ctx->sb, buf, sizeof(ext2_superblock_t));

    if (ctx->sb.s_magic != EXT2_SUPER_MAGIC) return -2;

    ctx->block_size = 1024 << ctx->sb.s_log_block_size;
    ctx->group_count = (ctx->sb.s_blocks_count + ctx->sb.s_blocks_per_group - 1) / ctx->sb.s_blocks_per_group;

    return 0;
}

/* 读取inode */
int ext2_algo_read_inode(ext2_context_t *ctx, uint32_t ino, ext2_inode_t *inode) {
    if (ino == 0 || ino > ctx->sb.s_inodes_count) return -1;

    uint32_t group = (ino - 1) / ctx->sb.s_inodes_per_group;
    uint32_t index = (ino - 1) % ctx->sb.s_inodes_per_group;
    uint32_t inode_size = ctx->sb.s_inode_size ? ctx->sb.s_inode_size : 128;

    /* 读取块组描述符（块3开始，每个描述符32字节） */
    uint32_t gd_block = 3 + (group * sizeof(ext2_group_desc_t)) / ctx->block_size;
    uint32_t gd_offset = (group * sizeof(ext2_group_desc_t)) % ctx->block_size;

    if (ctx->read_block(gd_block, ctx->block_buf) != 0) return -2;
    ext2_group_desc_t *gd = (ext2_group_desc_t *)(ctx->block_buf + gd_offset);

    /* 读取inode表块 */
    uint32_t inode_table_block = gd->bg_inode_table + (index * inode_size) / ctx->block_size;
    uint32_t inode_offset = (index * inode_size) % ctx->block_size;

    if (ctx->read_block(inode_table_block, ctx->block_buf) != 0) return -3;
    memcpy(inode, ctx->block_buf + inode_offset, sizeof(ext2_inode_t));

    return 0;
}

/* 写入inode */
int ext2_algo_write_inode(ext2_context_t *ctx, uint32_t ino, const ext2_inode_t *inode) {
    if (ino == 0 || ino > ctx->sb.s_inodes_count) return -1;

    uint32_t group = (ino - 1) / ctx->sb.s_inodes_per_group;
    uint32_t index = (ino - 1) % ctx->sb.s_inodes_per_group;
    uint32_t inode_size = ctx->sb.s_inode_size ? ctx->sb.s_inode_size : 128;

    uint32_t gd_block = 3 + (group * sizeof(ext2_group_desc_t)) / ctx->block_size;
    uint32_t gd_offset = (group * sizeof(ext2_group_desc_t)) % ctx->block_size;

    if (ctx->read_block(gd_block, ctx->block_buf) != 0) return -2;
    ext2_group_desc_t *gd = (ext2_group_desc_t *)(ctx->block_buf + gd_offset);

    uint32_t inode_table_block = gd->bg_inode_table + (index * inode_size) / ctx->block_size;
    uint32_t inode_offset = (index * inode_size) % ctx->block_size;

    if (ctx->read_block(inode_table_block, ctx->block_buf) != 0) return -3;
    memcpy(ctx->block_buf + inode_offset, inode, sizeof(ext2_inode_t));
    if (ctx->write_block(inode_table_block, ctx->block_buf) != 0) return -4;

    return 0;
}

/* 读取数据块（支持直接/间接/双重间接/三重间接） */
int ext2_algo_read_data_block(ext2_context_t *ctx, const ext2_inode_t *inode, uint32_t block_idx, uint8_t *buffer) {
    uint32_t pointers_per_block = ctx->block_size / 4;
    uint32_t block_num;

    if (block_idx < 12) {
        /* 直接块 */
        block_num = inode->i_block[block_idx];
        if (block_num == 0) { memset(buffer, 0, ctx->block_size); return 0; }
        return ctx->read_block(block_num, buffer);
    }

    block_idx -= 12;

    if (block_idx < pointers_per_block) {
        /* 单次间接 */
        uint32_t indirect_block = inode->i_block[12];
        if (indirect_block == 0) { memset(buffer, 0, ctx->block_size); return 0; }
        if (ctx->read_block(indirect_block, ctx->block_buf) != 0) return -1;
        block_num = ((uint32_t *)ctx->block_buf)[block_idx];
        if (block_num == 0) { memset(buffer, 0, ctx->block_size); return 0; }
        return ctx->read_block(block_num, buffer);
    }

    block_idx -= pointers_per_block;

    if (block_idx < pointers_per_block * pointers_per_block) {
        /* 双重间接 */
        uint32_t double_indirect = inode->i_block[13];
        if (double_indirect == 0) { memset(buffer, 0, ctx->block_size); return 0; }
        if (ctx->read_block(double_indirect, ctx->block_buf) != 0) return -1;
        uint32_t indirect_block = ((uint32_t *)ctx->block_buf)[block_idx / pointers_per_block];
        if (indirect_block == 0) { memset(buffer, 0, ctx->block_size); return 0; }
        if (ctx->read_block(indirect_block, ctx->block_buf) != 0) return -2;
        block_num = ((uint32_t *)ctx->block_buf)[block_idx % pointers_per_block];
        if (block_num == 0) { memset(buffer, 0, ctx->block_size); return 0; }
        return ctx->read_block(block_num, buffer);
    }

    block_idx -= pointers_per_block * pointers_per_block;

    /* 三重间接 */
    uint32_t triple_indirect = inode->i_block[14];
    if (triple_indirect == 0) { memset(buffer, 0, ctx->block_size); return 0; }
    if (ctx->read_block(triple_indirect, ctx->block_buf) != 0) return -1;
    uint32_t double_block = ((uint32_t *)ctx->block_buf)[block_idx / (pointers_per_block * pointers_per_block)];
    if (double_block == 0) { memset(buffer, 0, ctx->block_size); return 0; }
    if (ctx->read_block(double_block, ctx->block_buf) != 0) return -2;
    uint32_t indirect_block = ((uint32_t *)ctx->block_buf)[(block_idx / pointers_per_block) % pointers_per_block];
    if (indirect_block == 0) { memset(buffer, 0, ctx->block_size); return 0; }
    if (ctx->read_block(indirect_block, ctx->block_buf) != 0) return -3;
    block_num = ((uint32_t *)ctx->block_buf)[block_idx % pointers_per_block];
    if (block_num == 0) { memset(buffer, 0, ctx->block_size); return 0; }
    return ctx->read_block(block_num, buffer);
}

/* 写入数据块 */
int ext2_algo_write_data_block(ext2_context_t *ctx, ext2_inode_t *inode, uint32_t block_idx, const uint8_t *buffer) {
    uint32_t pointers_per_block = ctx->block_size / 4;
    uint32_t block_num;

    if (block_idx < 12) {
        if (inode->i_block[block_idx] == 0) {
            block_num = ext2_algo_alloc_block(ctx);
            if (block_num == 0) return -1;
            inode->i_block[block_idx] = block_num;
        } else {
            block_num = inode->i_block[block_idx];
        }
        return ctx->write_block(block_num, buffer);
    }

    /* 间接块分配和写入（简化版，完整实现需要分配间接块） */
    return -1;
}

/* 在目录中查找文件 */
int ext2_algo_find_in_dir(ext2_context_t *ctx, const ext2_inode_t *dir_inode, const char *name, uint32_t *found_ino) {
    uint32_t dir_size = dir_inode->i_size;
    uint32_t offset = 0;
    int name_len = strlen(name);

    while (offset < dir_size) {
        uint32_t block_idx = offset / ctx->block_size;
        uint32_t block_off = offset % ctx->block_size;

        if (ctx->read_block(0, ctx->block_buf) != 0) {} /* 占位 */
        if (ext2_algo_read_data_block(ctx, dir_inode, block_idx, ctx->block_buf) != 0) return -1;

        ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(ctx->block_buf + block_off);
        if (entry->inode == 0 || entry->rec_len == 0) break;

        if (entry->name_len == name_len && strncmp(entry->name, name, name_len) == 0) {
            *found_ino = entry->inode;
            return 0;
        }

        offset += entry->rec_len;
    }

    return -1;
}

/* 路径查找 */
int ext2_algo_lookup(ext2_context_t *ctx, const char *path, uint32_t *ino) {
    uint32_t current_ino = EXT2_ROOT_INODE;
    ext2_inode_t inode;
    const char *p = path;

    if (*p == '/') p++;

    while (*p) {
        if (ext2_algo_read_inode(ctx, current_ino, &inode) != 0) return -1;

        char name[256];
        int i = 0;
        while (*p && *p != '/' && i < 255) name[i++] = *p++;
        name[i] = 0;

        if (i == 0) { if (*p == '/') p++; continue; }

        uint32_t found_ino;
        if (ext2_algo_find_in_dir(ctx, &inode, name, &found_ino) != 0) return -2;
        current_ino = found_ino;

        if (*p == '/') p++;
    }

    *ino = current_ino;
    return 0;
}

/* 分配块 */
uint32_t ext2_algo_alloc_block(ext2_context_t *ctx) {
    for (uint32_t group = 0; group < ctx->group_count; group++) {
        uint32_t gd_block = 3 + (group * sizeof(ext2_group_desc_t)) / ctx->block_size;
        uint32_t gd_offset = (group * sizeof(ext2_group_desc_t)) % ctx->block_size;

        if (ctx->read_block(gd_block, ctx->block_buf) != 0) continue;
        ext2_group_desc_t *gd = (ext2_group_desc_t *)(ctx->block_buf + gd_offset);

        if (gd->bg_free_blocks_count == 0) continue;

        /* 读取块位图 */
        if (ctx->read_block(gd->bg_block_bitmap, ctx->block_buf) != 0) continue;

        for (uint32_t bit = 0; bit < ctx->sb.s_blocks_per_group; bit++) {
            uint32_t byte_idx = bit / 8;
            uint8_t bit_mask = 1 << (bit % 8);
            if (!(ctx->block_buf[byte_idx] & bit_mask)) {
                /* 找到空闲块 */
                ctx->block_buf[byte_idx] |= bit_mask;
                ctx->write_block(gd->bg_block_bitmap, ctx->block_buf);

                uint32_t block_num = group * ctx->sb.s_blocks_per_group + bit + ctx->sb.s_first_data_block;

                /* 更新块组描述符 */
                if (ctx->read_block(gd_block, ctx->block_buf) != 0) return 0;
                gd = (ext2_group_desc_t *)(ctx->block_buf + gd_offset);
                gd->bg_free_blocks_count--;
                ctx->write_block(gd_block, ctx->block_buf);

                /* 更新超级块 */
                ctx->sb.s_free_blocks_count--;
                /* 超级块写回需要额外处理，这里简化 */

                return block_num;
            }
        }
    }
    return 0;
}

/* 释放块 */
int ext2_algo_free_block(ext2_context_t *ctx, uint32_t block_num) {
    if (block_num < ctx->sb.s_first_data_block) return -1;

    uint32_t group = (block_num - ctx->sb.s_first_data_block) / ctx->sb.s_blocks_per_group;
    uint32_t bit = (block_num - ctx->sb.s_first_data_block) % ctx->sb.s_blocks_per_group;

    uint32_t gd_block = 3 + (group * sizeof(ext2_group_desc_t)) / ctx->block_size;
    uint32_t gd_offset = (group * sizeof(ext2_group_desc_t)) % ctx->block_size;

    if (ctx->read_block(gd_block, ctx->block_buf) != 0) return -2;
    ext2_group_desc_t *gd = (ext2_group_desc_t *)(ctx->block_buf + gd_offset);

    if (ctx->read_block(gd->bg_block_bitmap, ctx->block_buf) != 0) return -3;

    uint32_t byte_idx = bit / 8;
    uint8_t bit_mask = 1 << (bit % 8);
    ctx->block_buf[byte_idx] &= ~bit_mask;
    ctx->write_block(gd->bg_block_bitmap, ctx->block_buf);

    gd->bg_free_blocks_count++;
    ctx->write_block(gd_block, ctx->block_buf);

    return 0;
}

/* 分配inode */
uint32_t ext2_algo_alloc_inode(ext2_context_t *ctx) {
    for (uint32_t group = 0; group < ctx->group_count; group++) {
        uint32_t gd_block = 3 + (group * sizeof(ext2_group_desc_t)) / ctx->block_size;
        uint32_t gd_offset = (group * sizeof(ext2_group_desc_t)) % ctx->block_size;

        if (ctx->read_block(gd_block, ctx->block_buf) != 0) continue;
        ext2_group_desc_t *gd = (ext2_group_desc_t *)(ctx->block_buf + gd_offset);

        if (gd->bg_free_inodes_count == 0) continue;

        if (ctx->read_block(gd->bg_inode_bitmap, ctx->block_buf) != 0) continue;

        for (uint32_t bit = 0; bit < ctx->sb.s_inodes_per_group; bit++) {
            uint32_t byte_idx = bit / 8;
            uint8_t bit_mask = 1 << (bit % 8);
            if (!(ctx->block_buf[byte_idx] & bit_mask)) {
                ctx->block_buf[byte_idx] |= bit_mask;
                ctx->write_block(gd->bg_inode_bitmap, ctx->block_buf);

                uint32_t ino = group * ctx->sb.s_inodes_per_group + bit + 1;

                if (ctx->read_block(gd_block, ctx->block_buf) != 0) return 0;
                gd = (ext2_group_desc_t *)(ctx->block_buf + gd_offset);
                gd->bg_free_inodes_count--;
                ctx->write_block(gd_block, ctx->block_buf);

                return ino;
            }
        }
    }
    return 0;
}

/* 添加目录项 */
int ext2_algo_add_dir_entry(ext2_context_t *ctx, uint32_t dir_ino, const char *name, uint32_t ino, uint8_t file_type) {
    ext2_inode_t dir_inode;
    if (ext2_algo_read_inode(ctx, dir_ino, &dir_inode) != 0) return -1;

    int name_len = strlen(name);
    uint16_t entry_size = ((name_len + 8 + 3) / 4) * 4;  /* 4字节对齐 */

    uint32_t dir_size = dir_inode.i_size;
    uint32_t offset = 0;

    /* 查找空闲目录项空间 */
    while (offset < dir_size) {
        uint32_t block_idx = offset / ctx->block_size;
        uint32_t block_off = offset % ctx->block_size;

        if (ext2_algo_read_data_block(ctx, &dir_inode, block_idx, ctx->block_buf) != 0) return -2;

        ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(ctx->block_buf + block_off);
        if (entry->rec_len == 0) break;

        int actual_len = 8 + entry->name_len;
        int aligned_len = ((actual_len + 3) / 4) * 4;

        if (entry->rec_len >= aligned_len + entry_size) {
            /* 可以分割 */
            uint16_t old_rec_len = entry->rec_len;
            entry->rec_len = aligned_len;

            ext2_dir_entry_t *new_entry = (ext2_dir_entry_t *)((uint8_t *)entry + aligned_len);
            new_entry->inode = ino;
            new_entry->rec_len = old_rec_len - aligned_len;
            new_entry->name_len = name_len;
            new_entry->file_type = file_type;
            memcpy(new_entry->name, name, name_len);

            ext2_algo_write_data_block(ctx, &dir_inode, block_idx, ctx->block_buf);

            /* 更新目录inode */
            dir_inode.i_links_count++;
            dir_inode.i_size += entry_size;
            ext2_algo_write_inode(ctx, dir_ino, &dir_inode);

            return 0;
        }

        offset += entry->rec_len;
    }

    /* 需要分配新块 */
    uint32_t new_block_idx = dir_size / ctx->block_size;
    uint32_t new_block = ext2_algo_alloc_block(ctx);
    if (new_block == 0) return -3;

    memset(ctx->block_buf, 0, ctx->block_size);
    ext2_dir_entry_t *new_entry = (ext2_dir_entry_t *)ctx->block_buf;
    new_entry->inode = ino;
    new_entry->rec_len = ctx->block_size;
    new_entry->name_len = name_len;
    new_entry->file_type = file_type;
    memcpy(new_entry->name, name, name_len);

    dir_inode.i_block[new_block_idx] = new_block;
    dir_inode.i_size += ctx->block_size;
    ext2_algo_write_inode(ctx, dir_ino, &dir_inode);
    ctx->write_block(new_block, ctx->block_buf);

    return 0;
}

/* 读取文件 */
int ext2_algo_read_file(ext2_context_t *ctx, const char *path, uint8_t *buffer, uint32_t max_size) {
    uint32_t ino;
    if (ext2_algo_lookup(ctx, path, &ino) != 0) return -1;

    ext2_inode_t inode;
    if (ext2_algo_read_inode(ctx, ino, &inode) != 0) return -2;

    uint32_t file_size = inode.i_size;
    if (file_size > max_size) file_size = max_size;

    uint32_t blocks = (file_size + ctx->block_size - 1) / ctx->block_size;
    uint8_t *block_buf = ctx->block_buf;

    for (uint32_t i = 0; i < blocks; i++) {
        if (ext2_algo_read_data_block(ctx, &inode, i, block_buf) != 0) return -3;
        uint32_t copy_size = ctx->block_size;
        if (i == blocks - 1 && file_size % ctx->block_size != 0)
            copy_size = file_size % ctx->block_size;
        memcpy(buffer + i * ctx->block_size, block_buf, copy_size);
    }

    return file_size;
}

/* 从偏移读取文件 */
int ext2_algo_read_file_offset(ext2_context_t *ctx, const char *path, uint8_t *buffer, uint32_t max_size, uint32_t offset) {
    uint32_t ino;
    if (ext2_algo_lookup(ctx, path, &ino) != 0) return -1;

    ext2_inode_t inode;
    if (ext2_algo_read_inode(ctx, ino, &inode) != 0) return -2;

    if (offset >= inode.i_size) return 0;

    uint32_t remaining = inode.i_size - offset;
    if (remaining > max_size) remaining = max_size;

    uint32_t start_block = offset / ctx->block_size;
    uint32_t start_off = offset % ctx->block_size;
    uint32_t bytes_read = 0;
    uint8_t *block_buf = ctx->block_buf;

    while (bytes_read < remaining) {
        if (ext2_algo_read_data_block(ctx, &inode, start_block, block_buf) != 0) return -3;
        uint32_t src_off = (start_block == offset / ctx->block_size) ? start_off : 0;
        uint32_t copy_size = ctx->block_size - src_off;
        if (copy_size > remaining - bytes_read) copy_size = remaining - bytes_read;
        memcpy(buffer + bytes_read, block_buf + src_off, copy_size);
        bytes_read += copy_size;
        start_block++;
    }

    return bytes_read;
}
