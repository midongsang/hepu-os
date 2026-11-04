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

/* V0.13.41: 写后读校验 (USB写后读偶发陈旧时的自愈): 写块+重读+关键字段校验, 最多重试3次 */
static int algo_write_verify(ext2_context_t *ctx, uint32_t block, const uint8_t *data,
                             uint32_t key_off, uint32_t key_mask)
{
    for (int w = 0; w < 3; w++) {
        if (ctx->write_block(block, data) != 0) return -1;
        uint8_t check[4096];
        if (ctx->read_block(block, check) != 0) continue;
        uint32_t got = 0;
        for (uint32_t k = 0; k < 4; k++) got |= ((uint32_t)check[key_off + k]) << (k * 8);
        uint32_t want = 0;
        for (uint32_t k = 0; k < 4; k++) want |= ((uint32_t)data[key_off + k]) << (k * 8);
        if ((got & key_mask) == (want & key_mask)) return 0;
    }
    return -2;
}

/* 支持的最大块大小（与调用方缓冲区 EXT2_MAX_BLOCK_SIZE 一致） */
#define EXT2_ALGO_MAX_BLOCK 4096

/* inode 大小：rev0 固定128；rev1 取超级块值（0 视为128，保持原有容错） */
static uint32_t algo_inode_size(const ext2_superblock_t *sb)
{
    if (sb->s_rev_level >= 1 && sb->s_inode_size) return sb->s_inode_size;
    return 128;
}

int ext2_algo_validate_sb(const ext2_superblock_t *sb)
{
    if (sb->s_magic != EXT2_SUPER_MAGIC) return -1;
    /* 只支持 1K/2K/4K 块（调用方的栈/静态缓冲区都按 4096 开）；
     * 旧代码对 s_log_block_size 无限制，>=3 会写穿内核栈，>=22 还是未定义移位 */
    if (sb->s_log_block_size > 2) return -2;
    uint32_t bs = 1024u << sb->s_log_block_size;
    if (sb->s_blocks_count == 0 || sb->s_inodes_count == 0) return -3;
    /* 每组块数/inode数不得为0（旧代码除零 #DE），也不得超过一个位图块能表示的位数
     * （否则位图扫描会越过 block_buf） */
    if (sb->s_blocks_per_group == 0 || sb->s_blocks_per_group > bs * 8u) return -4;
    if (sb->s_inodes_per_group == 0 || sb->s_inodes_per_group > bs * 8u) return -5;
    if (sb->s_first_data_block >= sb->s_blocks_count) return -6;
    uint32_t isz = algo_inode_size(sb);
    if (isz < 128 || isz > bs || (isz & (isz - 1))) return -7;
    uint64_t groups  = ((uint64_t)sb->s_blocks_count + sb->s_blocks_per_group - 1) / sb->s_blocks_per_group;
    uint64_t igroups = ((uint64_t)sb->s_inodes_count + sb->s_inodes_per_group - 1) / sb->s_inodes_per_group;
    if (igroups > groups) return -8;
    /* 块号→LBA 的换算在驱动层是 32 位，必须保证不回绕 */
    if ((uint64_t)sb->s_blocks_count * (bs / 512u) > 0xFFFFFFFFULL) return -9;
    return 0;
}

/* 读块前检查块号落在文件系统范围内（磁盘上的块指针不可信） */
static int algo_read_checked(ext2_context_t *ctx, uint32_t block, uint8_t *buf)
{
    if (block >= ctx->sb.s_blocks_count) return -1;
    return ctx->read_block(block, buf);
}

/* 初始化ext2上下文，读取超级块 */
int ext2_algo_init(ext2_context_t *ctx, ext2_read_block_fn read_fn, ext2_write_block_fn write_fn, uint8_t *buf) {
    ctx->read_block = read_fn;
    ctx->write_block = write_fn;
    ctx->block_buf = buf;

    /* V0.13.40: 读取超级块: 4K块盘位于block0偏移1024; 1K块盘位于block1偏移0 */
    if (read_fn(0, buf) != 0) return -1;
    memcpy(&ctx->sb, buf + 1024, sizeof(ext2_superblock_t));
    if (ctx->sb.s_magic != EXT2_SUPER_MAGIC) {
        if (read_fn(1, buf) != 0) return -1;
        memcpy(&ctx->sb, buf, sizeof(ext2_superblock_t));
        if (ctx->sb.s_magic != EXT2_SUPER_MAGIC) return -2;
    }

    if (ext2_algo_validate_sb(&ctx->sb) != 0) return -3;

    ctx->block_size = 1024u << ctx->sb.s_log_block_size;
    ctx->group_count = (uint32_t)(((uint64_t)ctx->sb.s_blocks_count + ctx->sb.s_blocks_per_group - 1) / ctx->sb.s_blocks_per_group);

    return 0;
}

/* 读取inode */
int ext2_algo_read_inode(ext2_context_t *ctx, uint32_t ino, ext2_inode_t *inode) {
    if (ino == 0 || ino > ctx->sb.s_inodes_count) return -1;

    uint32_t group = (ino - 1) / ctx->sb.s_inodes_per_group;
    uint32_t index = (ino - 1) % ctx->sb.s_inodes_per_group;
    uint32_t inode_size = algo_inode_size(&ctx->sb);
    if (group >= ctx->group_count) return -1;   /* 安全修复：组号越界（旧代码无检查） */

    /* 读取块组描述符（块3开始，每个描述符32字节） */
    uint32_t gd_block = ctx->sb.s_first_data_block + 1 + (group * sizeof(ext2_group_desc_t)) / ctx->block_size;
    uint32_t gd_offset = (group * sizeof(ext2_group_desc_t)) % ctx->block_size;

    if (ctx->read_block(gd_block, ctx->block_buf) != 0) return -2;
    ext2_group_desc_t *gd = (ext2_group_desc_t *)(ctx->block_buf + gd_offset);

    /* 读取inode表块 */
    /* 安全修复：64位运算避免 index*inode_size 回绕；inode 表块号必须在文件系统内 */
    uint64_t inode_byte = (uint64_t)index * inode_size;
    uint64_t itb64 = (uint64_t)gd->bg_inode_table + inode_byte / ctx->block_size;
    if (itb64 >= ctx->sb.s_blocks_count) return -3;
    uint32_t inode_table_block = (uint32_t)itb64;
    uint32_t inode_offset = (uint32_t)(inode_byte % ctx->block_size);
    if (inode_offset + sizeof(ext2_inode_t) > ctx->block_size) return -3;

    if (ctx->read_block(inode_table_block, ctx->block_buf) != 0) return -3;
    memcpy(inode, ctx->block_buf + inode_offset, sizeof(ext2_inode_t));

    return 0;
}

/* 写入inode */
int ext2_algo_write_inode(ext2_context_t *ctx, uint32_t ino, const ext2_inode_t *inode) {
    if (ino == 0 || ino > ctx->sb.s_inodes_count) return -1;

    uint32_t group = (ino - 1) / ctx->sb.s_inodes_per_group;
    uint32_t index = (ino - 1) % ctx->sb.s_inodes_per_group;
    uint32_t inode_size = algo_inode_size(&ctx->sb);
    if (group >= ctx->group_count) return -1;   /* 安全修复：组号越界（旧代码无检查） */

    uint32_t gd_block = ctx->sb.s_first_data_block + 1 + (group * sizeof(ext2_group_desc_t)) / ctx->block_size;
    uint32_t gd_offset = (group * sizeof(ext2_group_desc_t)) % ctx->block_size;

    if (ctx->read_block(gd_block, ctx->block_buf) != 0) return -2;
    ext2_group_desc_t *gd = (ext2_group_desc_t *)(ctx->block_buf + gd_offset);

    /* 安全修复：64位运算避免 index*inode_size 回绕；inode 表块号必须在文件系统内 */
    uint64_t inode_byte = (uint64_t)index * inode_size;
    uint64_t itb64 = (uint64_t)gd->bg_inode_table + inode_byte / ctx->block_size;
    if (itb64 >= ctx->sb.s_blocks_count) return -3;
    uint32_t inode_table_block = (uint32_t)itb64;
    uint32_t inode_offset = (uint32_t)(inode_byte % ctx->block_size);
    if (inode_offset + sizeof(ext2_inode_t) > ctx->block_size) return -3;

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
        return algo_read_checked(ctx, block_num, buffer);
    }

    block_idx -= 12;

    if (block_idx < pointers_per_block) {
        /* 单次间接 */
        uint32_t indirect_block = inode->i_block[12];
        if (indirect_block == 0) { memset(buffer, 0, ctx->block_size); return 0; }
        if (algo_read_checked(ctx, indirect_block, ctx->block_buf) != 0) return -1;
        block_num = ((uint32_t *)ctx->block_buf)[block_idx];
        if (block_num == 0) { memset(buffer, 0, ctx->block_size); return 0; }
        return algo_read_checked(ctx, block_num, buffer);
    }

    block_idx -= pointers_per_block;

    if (block_idx < pointers_per_block * pointers_per_block) {
        /* 双重间接 */
        uint32_t double_indirect = inode->i_block[13];
        if (double_indirect == 0) { memset(buffer, 0, ctx->block_size); return 0; }
        if (algo_read_checked(ctx, double_indirect, ctx->block_buf) != 0) return -1;
        uint32_t indirect_block = ((uint32_t *)ctx->block_buf)[block_idx / pointers_per_block];
        if (indirect_block == 0) { memset(buffer, 0, ctx->block_size); return 0; }
        if (algo_read_checked(ctx, indirect_block, ctx->block_buf) != 0) return -2;
        block_num = ((uint32_t *)ctx->block_buf)[block_idx % pointers_per_block];
        if (block_num == 0) { memset(buffer, 0, ctx->block_size); return 0; }
        return algo_read_checked(ctx, block_num, buffer);
    }

    block_idx -= pointers_per_block * pointers_per_block;

    /* 安全修复：三重间接的一级索引必须 < pointers_per_block，否则越过 block_buf 读 */
    if (block_idx / (pointers_per_block * pointers_per_block) >= pointers_per_block) return -4;

    /* 三重间接 */
    uint32_t triple_indirect = inode->i_block[14];
    if (triple_indirect == 0) { memset(buffer, 0, ctx->block_size); return 0; }
    if (algo_read_checked(ctx, triple_indirect, ctx->block_buf) != 0) return -1;
    uint32_t double_block = ((uint32_t *)ctx->block_buf)[block_idx / (pointers_per_block * pointers_per_block)];
    if (double_block == 0) { memset(buffer, 0, ctx->block_size); return 0; }
    if (algo_read_checked(ctx, double_block, ctx->block_buf) != 0) return -2;
    uint32_t indirect_block = ((uint32_t *)ctx->block_buf)[(block_idx / pointers_per_block) % pointers_per_block];
    if (indirect_block == 0) { memset(buffer, 0, ctx->block_size); return 0; }
    if (algo_read_checked(ctx, indirect_block, ctx->block_buf) != 0) return -3;
    block_num = ((uint32_t *)ctx->block_buf)[block_idx % pointers_per_block];
    if (block_num == 0) { memset(buffer, 0, ctx->block_size); return 0; }
    return algo_read_checked(ctx, block_num, buffer);
}

/* 分配一个间接指针块并清零 (内容必须为0, 否则残留数据会被当成块指针) */
static uint32_t algo_new_meta_block(ext2_context_t *ctx, ext2_inode_t *inode)
{
    uint32_t b = ext2_algo_alloc_block(ctx);
    if (b == 0) return 0;
    memset(ctx->block_buf, 0, ctx->block_size);
    if (ctx->write_block(b, ctx->block_buf) != 0) {
        ext2_algo_free_block(ctx, b);
        return 0;
    }
    inode->i_blocks += ctx->block_size / 512;
    return b;
}

/* 读取间接块中的第 index 个指针 (block==0 视作不存在) */
static int algo_ptr_load(ext2_context_t *ctx, uint32_t block, uint32_t index, uint32_t *val)
{
    if (block == 0) { *val = 0; return 0; }
    if (algo_read_checked(ctx, block, ctx->block_buf) != 0) return -1;
    *val = ((uint32_t *)ctx->block_buf)[index];
    return 0;
}

/* 回写间接块中的第 index 个指针 */
static int algo_ptr_store(ext2_context_t *ctx, uint32_t block, uint32_t index, uint32_t val)
{
    if (algo_read_checked(ctx, block, ctx->block_buf) != 0) return -1;
    ((uint32_t *)ctx->block_buf)[index] = val;
    return ctx->write_block(block, ctx->block_buf);
}

/* 解析数据块号: alloc=1 时按需分配元数据块/数据块并更新 i_blocks
 * 返回 0 表示不存在(alloc=0)或分配失败; 间接块支持单/双/三级 */
static uint32_t algo_map_block(ext2_context_t *ctx, ext2_inode_t *inode, uint32_t idx, int alloc)
{
    uint32_t ppb = ctx->block_size / 4;
    uint64_t max_idx = 12 + (uint64_t)ppb + (uint64_t)ppb * ppb +
                       (uint64_t)ppb * ppb * ppb;
    uint64_t blocks_per = ctx->block_size / 512;

    if ((uint64_t)idx >= max_idx) return 0;

    if (idx < 12) {
        uint32_t b = inode->i_block[idx];
        if (b == 0 && alloc) {
            b = ext2_algo_alloc_block(ctx);
            if (b == 0) return 0;
            inode->i_block[idx] = b;
            inode->i_blocks += (uint32_t)blocks_per;
        }
        return b;
    }
    idx -= 12;

    if (idx < ppb) {
        /* 单次间接 */
        uint32_t meta = inode->i_block[12];
        if (meta == 0) {
            if (!alloc) return 0;
            meta = algo_new_meta_block(ctx, inode);
            if (meta == 0) return 0;
            inode->i_block[12] = meta;
        }
        uint32_t b = 0;
        if (algo_ptr_load(ctx, meta, idx, &b) != 0) return 0;
        if (b == 0 && alloc) {
            b = ext2_algo_alloc_block(ctx);
            if (b == 0) return 0;
            inode->i_blocks += (uint32_t)blocks_per;
            if (algo_ptr_store(ctx, meta, idx, b) != 0) return 0;
        }
        return b;
    }
    idx -= ppb;

    if (idx < ppb * ppb) {
        /* 双重间接 */
        uint32_t m1 = inode->i_block[13];
        if (m1 == 0) {
            if (!alloc) return 0;
            m1 = algo_new_meta_block(ctx, inode);
            if (m1 == 0) return 0;
            inode->i_block[13] = m1;
        }
        uint32_t i1 = idx / ppb, i2 = idx % ppb;
        uint32_t m2 = 0;
        if (algo_ptr_load(ctx, m1, i1, &m2) != 0) return 0;
        if (m2 == 0) {
            if (!alloc) return 0;
            m2 = algo_new_meta_block(ctx, inode);
            if (m2 == 0) return 0;
            if (algo_ptr_store(ctx, m1, i1, m2) != 0) return 0;
        }
        uint32_t b = 0;
        if (algo_ptr_load(ctx, m2, i2, &b) != 0) return 0;
        if (b == 0 && alloc) {
            b = ext2_algo_alloc_block(ctx);
            if (b == 0) return 0;
            inode->i_blocks += (uint32_t)blocks_per;
            if (algo_ptr_store(ctx, m2, i2, b) != 0) return 0;
        }
        return b;
    }
    idx -= ppb * ppb;

    /* 三重间接 */
    {
        uint32_t m1 = inode->i_block[14];
        if (m1 == 0) {
            if (!alloc) return 0;
            m1 = algo_new_meta_block(ctx, inode);
            if (m1 == 0) return 0;
            inode->i_block[14] = m1;
        }
        uint32_t i1 = idx / (ppb * ppb);
        uint32_t rem = idx % (ppb * ppb);
        uint32_t i2 = rem / ppb, i3 = rem % ppb;

        uint32_t m2 = 0;
        if (algo_ptr_load(ctx, m1, i1, &m2) != 0) return 0;
        if (m2 == 0) {
            if (!alloc) return 0;
            m2 = algo_new_meta_block(ctx, inode);
            if (m2 == 0) return 0;
            if (algo_ptr_store(ctx, m1, i1, m2) != 0) return 0;
        }

        uint32_t m3 = 0;
        if (algo_ptr_load(ctx, m2, i2, &m3) != 0) return 0;
        if (m3 == 0) {
            if (!alloc) return 0;
            m3 = algo_new_meta_block(ctx, inode);
            if (m3 == 0) return 0;
            if (algo_ptr_store(ctx, m2, i2, m3) != 0) return 0;
        }

        uint32_t b = 0;
        if (algo_ptr_load(ctx, m3, i3, &b) != 0) return 0;
        if (b == 0 && alloc) {
            b = ext2_algo_alloc_block(ctx);
            if (b == 0) return 0;
            inode->i_blocks += (uint32_t)blocks_per;
            if (algo_ptr_store(ctx, m3, i3, b) != 0) return 0;
        }
        return b;
    }
}

/* 写入数据块 (支持直接/单/双/三级间接, 按需分配) */
int ext2_algo_write_data_block(ext2_context_t *ctx, ext2_inode_t *inode, uint32_t block_idx, const uint8_t *buffer) {
    uint32_t block_num = algo_map_block(ctx, inode, block_idx, 1);
    if (block_num == 0) return -1;
    return ctx->write_block(block_num, buffer);
}

/* 在目录中查找文件 */
int ext2_algo_find_in_dir(ext2_context_t *ctx, const ext2_inode_t *dir_inode, const char *name, uint32_t *found_ino) {
    uint32_t dir_size = dir_inode->i_size;
    uint32_t offset = 0;
    int name_len = strlen(name);
    if (name_len <= 0 || name_len > 255) return -1;

    while (offset < dir_size) {
        uint32_t block_idx = offset / ctx->block_size;
        uint32_t block_off = offset % ctx->block_size;

        if (ext2_algo_read_data_block(ctx, dir_inode, block_idx, ctx->block_buf) != 0) return -1;

        /* 安全修复：目录项来自磁盘，必须校验后再解引用
         * - 8字节头不能跨出块缓冲
         * - rec_len >= 8、4字节对齐、不跨块
         * - 名字必须完整落在本条目内（旧代码可越过 block_buf 读最多255字节） */
        if (block_off + 8 > ctx->block_size) return -1;
        ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(ctx->block_buf + block_off);
        uint32_t rec_len = entry->rec_len;
        if (rec_len < 8 || (rec_len & 3) || rec_len > ctx->block_size - block_off) return -1;
        if ((uint32_t)entry->name_len + 8 > rec_len) return -1;

        /* inode==0 表示已删除的条目，应跳过而不是终止查找
         * （旧代码在此 break，会导致删除条目之后的所有文件不可见） */
        if (entry->inode != 0 &&
            entry->name_len == name_len && strncmp(entry->name, name, name_len) == 0) {
            *found_ino = entry->inode;
            return 0;
        }

        offset += rec_len;
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

/* 超级块计数写回磁盘 (1K块盘: block1; 2K/4K: block0偏移1024)
 * 分配/释放只更新了内存副本, 不同步会让 e2fsck 报 free count 不一致 */
void ext2_algo_sync_sb(ext2_context_t *ctx)
{
    if (ctx->block_size == 1024) {
        if (ctx->read_block(1, ctx->block_buf) != 0) return;
        memcpy(ctx->block_buf, &ctx->sb, sizeof(ctx->sb));
        ctx->write_block(1, ctx->block_buf);
    } else {
        if (ctx->read_block(0, ctx->block_buf) != 0) return;
        memcpy(ctx->block_buf + 1024, &ctx->sb, sizeof(ctx->sb));
        ctx->write_block(0, ctx->block_buf);
    }
}

/* 分配块 */
uint32_t ext2_algo_alloc_block(ext2_context_t *ctx) {
    for (uint32_t group = 0; group < ctx->group_count; group++) {
        uint32_t gd_block = ctx->sb.s_first_data_block + 1 + (group * sizeof(ext2_group_desc_t)) / ctx->block_size;
        uint32_t gd_offset = (group * sizeof(ext2_group_desc_t)) % ctx->block_size;

        if (ctx->read_block(gd_block, ctx->block_buf) != 0) continue;
        ext2_group_desc_t *gd = (ext2_group_desc_t *)(ctx->block_buf + gd_offset);

        if (gd->bg_free_blocks_count == 0) continue;

        /* 位图块号必须先保存: 读位图会覆盖 ctx->block_buf (含 GDT) */
        uint32_t bbm = gd->bg_block_bitmap;

        /* 读取块位图（安全修复：位图块号来自磁盘，需检查范围） */
        if (algo_read_checked(ctx, bbm, ctx->block_buf) != 0) continue;
        for (uint32_t bit = 0; bit < ctx->sb.s_blocks_per_group; bit++) {
            uint32_t byte_idx = bit / 8;
            uint8_t bit_mask = 1 << (bit % 8);
            if (!(ctx->block_buf[byte_idx] & bit_mask)) {
                /* 找到空闲块；安全修复：结果块号必须 < s_blocks_count（最后一组位图尾部可能未置位） */
                uint64_t cand = (uint64_t)group * ctx->sb.s_blocks_per_group + bit + ctx->sb.s_first_data_block;
                if (cand >= ctx->sb.s_blocks_count) break;
                ctx->block_buf[byte_idx] |= bit_mask;
                if (ctx->write_block(bbm, ctx->block_buf) != 0) continue;

                uint32_t block_num = group * ctx->sb.s_blocks_per_group + bit + ctx->sb.s_first_data_block;

                /* V0.13.41: GDT 更新用独立缓冲+写后校验
                 * 失败时撤销位图置位, 否则块被标记已用但计数没减 → e2fsck 报不一致 */
                {
                    uint8_t gdt_buf[4096];
                    int ok = 1;
                    if (ctx->read_block(gd_block, gdt_buf) != 0) ok = 0;
                    else {
                        ext2_group_desc_t *gd2 = (ext2_group_desc_t *)(gdt_buf + gd_offset);
                        gd2->bg_free_blocks_count--;
                        if (algo_write_verify(ctx, gd_block, gdt_buf,
                                              gd_offset, 0xFFFFu) != 0) ok = 0;
                    }
                    if (!ok) {
                        ctx->block_buf[byte_idx] &= ~bit_mask;
                        ctx->write_block(bbm, ctx->block_buf);
                        return 0;
                    }
                }

                /* 更新超级块 */
                ctx->sb.s_free_blocks_count--;
                ext2_algo_sync_sb(ctx);

                return block_num;
            }
        }
    }
    return 0;
}

/* 释放块 */
int ext2_algo_free_block(ext2_context_t *ctx, uint32_t block_num) {
    if (block_num < ctx->sb.s_first_data_block || block_num >= ctx->sb.s_blocks_count) return -1;

    uint32_t group = (block_num - ctx->sb.s_first_data_block) / ctx->sb.s_blocks_per_group;
    if (group >= ctx->group_count) return -1;
    uint32_t bit = (block_num - ctx->sb.s_first_data_block) % ctx->sb.s_blocks_per_group;

    uint32_t gd_block = ctx->sb.s_first_data_block + 1 + (group * sizeof(ext2_group_desc_t)) / ctx->block_size;
    uint32_t gd_offset = (group * sizeof(ext2_group_desc_t)) % ctx->block_size;

    if (ctx->read_block(gd_block, ctx->block_buf) != 0) return -2;
    ext2_group_desc_t *gd = (ext2_group_desc_t *)(ctx->block_buf + gd_offset);
    uint32_t bbm = gd->bg_block_bitmap;

    if (algo_read_checked(ctx, bbm, ctx->block_buf) != 0) return -3;

    uint32_t byte_idx = bit / 8;
    uint8_t bit_mask = 1 << (bit % 8);
    ctx->block_buf[byte_idx] &= ~bit_mask;
    ctx->write_block(bbm, ctx->block_buf);

    {
        uint8_t gdt_buf[4096];
        if (ctx->read_block(gd_block, gdt_buf) != 0) return -4;
        ext2_group_desc_t *gd2 = (ext2_group_desc_t *)(gdt_buf + gd_offset);
        gd2->bg_free_blocks_count++;
        if (algo_write_verify(ctx, gd_block, gdt_buf, gd_offset, 0xFFFFu) != 0) return -5;
    }
    ctx->sb.s_free_blocks_count++;
    ext2_algo_sync_sb(ctx);
    return 0;
}

/* 分配inode */
uint32_t ext2_algo_alloc_inode(ext2_context_t *ctx) {
    for (uint32_t group = 0; group < ctx->group_count; group++) {
        uint32_t gd_block = ctx->sb.s_first_data_block + 1 + (group * sizeof(ext2_group_desc_t)) / ctx->block_size;
        uint32_t gd_offset = (group * sizeof(ext2_group_desc_t)) % ctx->block_size;

        if (ctx->read_block(gd_block, ctx->block_buf) != 0) continue;
        ext2_group_desc_t *gd = (ext2_group_desc_t *)(ctx->block_buf + gd_offset);

        if (gd->bg_free_inodes_count == 0) continue;

        /* 位图块号必须先保存: 读位图会覆盖 ctx->block_buf (含 GDT) */
        uint32_t ibm = gd->bg_inode_bitmap;

        if (algo_read_checked(ctx, ibm, ctx->block_buf) != 0) continue;

        for (uint32_t bit = 0; bit < ctx->sb.s_inodes_per_group; bit++) {
            uint32_t byte_idx = bit / 8;
            uint8_t bit_mask = 1 << (bit % 8);
            if (!(ctx->block_buf[byte_idx] & bit_mask)) {
                ctx->block_buf[byte_idx] |= bit_mask;
                ctx->write_block(ibm, ctx->block_buf);

                uint32_t ino = group * ctx->sb.s_inodes_per_group + bit + 1;

                /* V0.13.41: GDT 更新用独立缓冲+写后校验, 防共享缓冲陈旧写坏GDT */
                {
                    uint8_t gdt_buf[4096];
                    int ok = 1;
                    if (ctx->read_block(gd_block, gdt_buf) != 0) ok = 0;
                    else {
                        ext2_group_desc_t *gd2 = (ext2_group_desc_t *)(gdt_buf + gd_offset);
                        gd2->bg_free_inodes_count--;
                        if (algo_write_verify(ctx, gd_block, gdt_buf,
                                              gd_offset, 0xFFFFu) != 0) ok = 0;
                    }
                    if (!ok) {
                        /* 记账失败: 撤销位图置位, 避免遗留无人引用且未计数的 inode */
                        ctx->block_buf[byte_idx] &= ~bit_mask;
                        ctx->write_block(ibm, ctx->block_buf);
                        return 0;
                    }
                }
                ctx->sb.s_free_inodes_count--;
                ext2_algo_sync_sb(ctx);
                return ino;
            }
        }
    }
    return 0;
}

/* 释放inode (位图 + GDT + 超级块计数, 与分配保持对称) */
int ext2_algo_free_inode(ext2_context_t *ctx, uint32_t ino)
{
    if (ino == 0 || ino > ctx->sb.s_inodes_count) return -1;
    uint32_t group = (ino - 1) / ctx->sb.s_inodes_per_group;
    uint32_t index = (ino - 1) % ctx->sb.s_inodes_per_group;
    if (group >= ctx->group_count) return -1;

    uint32_t gd_block = ctx->sb.s_first_data_block + 1 + (group * sizeof(ext2_group_desc_t)) / ctx->block_size;
    uint32_t gd_offset = (group * sizeof(ext2_group_desc_t)) % ctx->block_size;

    if (ctx->read_block(gd_block, ctx->block_buf) != 0) return -2;
    ext2_group_desc_t *gd = (ext2_group_desc_t *)(ctx->block_buf + gd_offset);
    uint32_t ibm = gd->bg_inode_bitmap;
    uint32_t itb = gd->bg_inode_table;          /* 保存 inode 表块号 (位图读会覆盖 block_buf) */

    /* 先读 inode 表项判断是否为目录 (释放目录需递减 bg_used_dirs_count) */
    int is_dir = 0;
    {
        uint32_t inode_size = algo_inode_size(&ctx->sb);
        uint64_t inode_byte = (uint64_t)index * inode_size;
        uint32_t it_block = itb + (uint32_t)(inode_byte / ctx->block_size);
        uint32_t it_off = (uint32_t)(inode_byte % ctx->block_size);
        uint8_t it_buf[EXT2_ALGO_MAX_BLOCK];
        if (it_block < ctx->sb.s_blocks_count && ctx->read_block(it_block, it_buf) == 0) {
            ext2_inode_t *ino_p = (ext2_inode_t *)(it_buf + it_off);
            is_dir = ((ino_p->i_mode & 0xF000) == 0x4000) ? 1 : 0;
        }
    }

    if (algo_read_checked(ctx, ibm, ctx->block_buf) != 0) return -3;

    uint32_t byte_idx = index / 8;
    uint8_t bit_mask = 1 << (index % 8);
    if (!(ctx->block_buf[byte_idx] & bit_mask)) return -4;   /* 已经是空闲 inode */
    ctx->block_buf[byte_idx] &= ~bit_mask;
    if (ctx->write_block(ibm, ctx->block_buf) != 0) return -5;

    /* 清零 inode 表项: 释放后残留的 i_block/i_blocks 会被 e2fsck 误报 */
    {
        uint32_t inode_size = algo_inode_size(&ctx->sb);
        uint64_t inode_byte = (uint64_t)index * inode_size;
        uint32_t it_block = itb + (uint32_t)(inode_byte / ctx->block_size);
        uint32_t it_off = (uint32_t)(inode_byte % ctx->block_size);
        if (it_block < ctx->sb.s_blocks_count) {
            uint8_t it_buf[EXT2_ALGO_MAX_BLOCK];
            if (ctx->read_block(it_block, it_buf) == 0) {
                memset(it_buf + it_off, 0, sizeof(ext2_inode_t));
                ctx->write_block(it_block, it_buf);
            }
        }
    }

    {
        uint8_t gdt_buf[4096];
        if (ctx->read_block(gd_block, gdt_buf) != 0) return -6;
        ext2_group_desc_t *gdp = (ext2_group_desc_t *)(gdt_buf + gd_offset);
        gdp->bg_free_inodes_count++;
        if (is_dir && gdp->bg_used_dirs_count > 0) gdp->bg_used_dirs_count--;
        if (algo_write_verify(ctx, gd_block, gdt_buf, gd_offset, 0xFFFFu) != 0) return -7;
    }
    ctx->sb.s_free_inodes_count++;
    ext2_algo_sync_sb(ctx);
    return 0;
}

/* 释放 inode 的全部数据块 + 间接元数据块 (直接/单/双/三级)
 * 用 block_size/4 作为每块指针数 (旧实现硬编码 256, 4K 块盘会泄漏 256~1023 号块)
 * 注意: 内部调用 ext2_algo_free_block 会使用 ctx->block_buf, 故间接块读入用本地 buf */
void ext2_algo_free_data_blocks(ext2_context_t *ctx, ext2_inode_t *inode)
{
    uint32_t ppb = ctx->block_size / 4;

    /* 直接块 */
    for (int i = 0; i < 12; i++) {
        if (inode->i_block[i]) {
            ext2_algo_free_block(ctx, inode->i_block[i]);
            inode->i_block[i] = 0;
        }
    }

    /* 一级间接 */
    if (inode->i_block[12]) {
        uint32_t ind = inode->i_block[12];
        uint8_t buf[EXT2_ALGO_MAX_BLOCK];
        if (algo_read_checked(ctx, ind, buf) == 0) {
            uint32_t *p = (uint32_t *)buf;
            for (uint32_t i = 0; i < ppb; i++)
                if (p[i]) ext2_algo_free_block(ctx, p[i]);
        }
        ext2_algo_free_block(ctx, ind);
        inode->i_block[12] = 0;
    }

    /* 二级间接 */
    if (inode->i_block[13]) {
        uint32_t dbl = inode->i_block[13];
        uint8_t buf[EXT2_ALGO_MAX_BLOCK];
        if (algo_read_checked(ctx, dbl, buf) == 0) {
            uint32_t *p = (uint32_t *)buf;
            for (uint32_t i = 0; i < ppb; i++) {
                if (!p[i]) continue;
                uint8_t sbuf[EXT2_ALGO_MAX_BLOCK];
                if (algo_read_checked(ctx, p[i], sbuf) == 0) {
                    uint32_t *sp = (uint32_t *)sbuf;
                    for (uint32_t j = 0; j < ppb; j++)
                        if (sp[j]) ext2_algo_free_block(ctx, sp[j]);
                }
                ext2_algo_free_block(ctx, p[i]);
            }
        }
        ext2_algo_free_block(ctx, dbl);
        inode->i_block[13] = 0;
    }

    /* 三级间接 */
    if (inode->i_block[14]) {
        uint32_t tri = inode->i_block[14];
        uint8_t buf[EXT2_ALGO_MAX_BLOCK];
        if (algo_read_checked(ctx, tri, buf) == 0) {
            uint32_t *p = (uint32_t *)buf;
            for (uint32_t i = 0; i < ppb; i++) {
                if (!p[i]) continue;
                uint8_t dbuf[EXT2_ALGO_MAX_BLOCK];
                if (algo_read_checked(ctx, p[i], dbuf) == 0) {
                    uint32_t *dp = (uint32_t *)dbuf;
                    for (uint32_t j = 0; j < ppb; j++) {
                        if (!dp[j]) continue;
                        uint8_t sbuf[EXT2_ALGO_MAX_BLOCK];
                        if (algo_read_checked(ctx, dp[j], sbuf) == 0) {
                            uint32_t *sp = (uint32_t *)sbuf;
                            for (uint32_t k = 0; k < ppb; k++)
                                if (sp[k]) ext2_algo_free_block(ctx, sp[k]);
                        }
                        ext2_algo_free_block(ctx, dp[j]);
                    }
                }
                ext2_algo_free_block(ctx, p[i]);
            }
        }
        ext2_algo_free_block(ctx, tri);
        inode->i_block[14] = 0;
    }

    inode->i_blocks = 0;
    inode->i_size = 0;
}

/* 添加目录项
 * 目录块数据用独立缓冲: ctx->block_buf 会被间接块映射/写 inode 复用,
 * 若目录数据也放在 block_buf, 目录超过 12 块或写新块时内容会被覆盖写坏 */
static uint8_t algo_dir_buf[EXT2_ALGO_MAX_BLOCK];

int ext2_algo_add_dir_entry(ext2_context_t *ctx, uint32_t dir_ino, const char *name, uint32_t ino, uint8_t file_type) {
    ext2_inode_t dir_inode;
    if (ext2_algo_read_inode(ctx, dir_ino, &dir_inode) != 0) return -1;

    int name_len = strlen(name);
    if (name_len <= 0 || name_len > 255) return -1;   /* 安全修复：name_len 存入 uint8，超长会截断 */
    uint16_t entry_size = ((name_len + 8 + 3) / 4) * 4;  /* 4字节对齐 */

    uint32_t dir_size = dir_inode.i_size;
    uint32_t offset = 0;

    /* 查找空闲目录项空间 */
    while (offset < dir_size) {
        uint32_t block_idx = offset / ctx->block_size;
        uint32_t block_off = offset % ctx->block_size;

        if (ext2_algo_read_data_block(ctx, &dir_inode, block_idx, algo_dir_buf) != 0) return -2;

        /* 安全修复：校验磁盘目录项，否则分割时 new_entry 可能落到缓冲区之外 */
        if (block_off + 8 > ctx->block_size) return -2;
        ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(algo_dir_buf + block_off);
        if (entry->rec_len == 0) break;
        if (entry->rec_len < 8 || (entry->rec_len & 3) ||
            entry->rec_len > ctx->block_size - block_off ||
            (uint32_t)entry->name_len + 8 > entry->rec_len) return -2;

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

            /* 先取块号(间接块映射会复用 ctx->block_buf), 再用独立缓冲写回 */
            uint32_t blk = algo_map_block(ctx, &dir_inode, block_idx, 0);
            if (blk == 0) return -5;
            if (ctx->write_block(blk, algo_dir_buf) != 0) return -5;

            /* 更新目录inode:
             * - i_size 是目录已分配字节数(块对齐)，在已有条目余量内追加新条目
             *   不增加 i_size；旧代码每次 += entry_size 会让目录 i_size 虚增，
             *   后续 add 时 new_block_idx 越过已分配块，造成目录结构损坏。
             * - i_links_count 只对子目录+1(子目录的 ".." 指向本目录)。 */
            if (file_type == 2) dir_inode.i_links_count++;
            ext2_algo_write_inode(ctx, dir_ino, &dir_inode);

            return 0;
        }

        offset += entry->rec_len;
    }

    /* 需要分配新块 (支持间接块, 目录可无限增长到 i_block 可寻址上限) */
    uint32_t new_block_idx = dir_size / ctx->block_size;
    uint32_t new_block = algo_map_block(ctx, &dir_inode, new_block_idx, 1);
    if (new_block == 0) return -3;

    memset(algo_dir_buf, 0, ctx->block_size);
    ext2_dir_entry_t *new_entry = (ext2_dir_entry_t *)algo_dir_buf;
    new_entry->inode = ino;
    new_entry->rec_len = ctx->block_size;
    new_entry->name_len = name_len;
    new_entry->file_type = file_type;
    memcpy(new_entry->name, name, name_len);

    dir_inode.i_size += ctx->block_size;
    if (file_type == 2) dir_inode.i_links_count++;
    /* 先写目录块, 再写 inode: 写 inode 会占用 ctx->block_buf */
    if (ctx->write_block(new_block, algo_dir_buf) != 0) return -4;
    if (ext2_algo_write_inode(ctx, dir_ino, &dir_inode) != 0) return -5;

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
