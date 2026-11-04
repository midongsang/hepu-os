/* ============================================================================
 * 赫普OS - ext2 磁盘完整性检查 (fsck)
 * V3.89-E23: 孤儿块/孤儿inode检测 + 分配一致性校验
 * 用途: 数据盘健康自检(用户命令 fsck), 验证块分配/释放正确性
 * ========================================================================== */

#include <stdint.h>
#include "ext2.h"
#include "ide_ext2.h"
#include "vga.h"
#include "string.h"

extern int ext2_mounted;
extern ext2_superblock_t *ext2_sb;
extern ext2_group_desc_t *ext2_gdt;
extern uint32_t ext2_block_size;
extern uint32_t ext2_inode_size;

#define FSCK_MAX_INDIRECT 4096
#define FSCK_MAX_VISIT    512

/* 收集 inode 引用的全部块(直接+单级间接+双级间接+表块), 返回块数 */
static uint32_t fsck_collect_blocks(uint32_t ino, uint32_t *list, uint32_t cap)
{
    ext2_inode_t in;
    if (ext2_read_inode(ino, &in) != 0) return 0;
    uint32_t n = 0;
    uint32_t per = ext2_block_size / 4;
    for (int i = 0; i < 12 && n < cap; i++)
        if (in.i_block[i]) list[n++] = in.i_block[i];
    if (in.i_block[12] && n < cap) {
        if (n < cap) list[n++] = in.i_block[12];
        static uint8_t b1[4096];
        if (ext2_read_block(in.i_block[12], b1) == 0) {
            uint32_t *p = (uint32_t *)b1;
            for (uint32_t i = 0; i < per && n < cap; i++)
                if (p[i]) list[n++] = p[i];
        }
    }
    if (in.i_block[13] && n < cap) {
        if (n < cap) list[n++] = in.i_block[13];
        static uint8_t b2[4096];
        if (ext2_read_block(in.i_block[13], b2) == 0) {
            uint32_t *p2 = (uint32_t *)b2;
            for (uint32_t l1 = 0; l1 < per && n < cap; l1++) {
                if (p2[l1]) {
                    if (n < cap) list[n++] = p2[l1];
                    static uint8_t b3[4096];
                    if (ext2_read_block(p2[l1], b3) == 0) {
                        uint32_t *p3 = (uint32_t *)b3;
                        for (uint32_t l0 = 0; l0 < per && n < cap; l0++)
                            if (p3[l0]) list[n++] = p3[l0];
                    }
                }
            }
        }
    }
    return n;
}

/* 遍历目录树收集被引用的 inode(递归, 深度限制+防环) */
/* V3.89-E26: 深度限制 8 -> 32(支持更深目录树, 减少深层目录漏检误报孤儿),
 * 子目录上限 64 -> 128(大目录支持) */
#define FSCK_MAX_DEPTH   32
#define FSCK_MAX_SUBDIRS 128
static void fsck_walk_dir(uint32_t dino, uint8_t *inode_refs, uint32_t inode_cnt,
                          uint32_t *visited, uint32_t vcap, uint32_t *vn, uint32_t depth)
{
    if (depth > FSCK_MAX_DEPTH || !inode_refs) return;
    for (uint32_t i = 0; i < *vn; i++)
        if (visited[i] == dino) return;   /* 防环 */
    if (*vn < vcap) visited[(*vn)++] = dino;

    ext2_inode_t dir;
    if (ext2_read_inode(dino, &dir) != 0) return;
    uint32_t off = 0;
    static uint8_t blk[4096];
    uint32_t subdirs[FSCK_MAX_SUBDIRS], ns = 0;
    while (off < dir.i_size) {
        uint32_t to = dir.i_size - off;
        if (to > 4096) to = 4096;
        uint32_t n = ext2_read_data(&dir, off, blk, to);
        if (n <= 0) break;
        uint32_t pos = 0;
        while (pos + 8 <= n) {
            ext2_dir_entry_t *e = (ext2_dir_entry_t *)(blk + pos);
            if (e->rec_len < 8 || e->name_len >= 255) break;
            if (pos + e->rec_len > n) break;
            if (e->inode && e->inode < inode_cnt) {
                if (e->inode != dino) inode_refs[e->inode] = 1;
                /* V3.89-E23: 先解析完本块全部条目再递归——原实现边解析边递归,
                 * 子目录递归复用同一 static blk 覆盖父目录解析缓冲, 导致根目录
                 * 条目被截断(仅 . .. + lost+found 后 break), inode 引用漏收 */
                if (e->file_type == EXT2_FT_DIR && e->inode != dino && ns < FSCK_MAX_SUBDIRS)
                    subdirs[ns++] = e->inode;
            }
            pos += e->rec_len;
        }
        off += to;
    }
    for (uint32_t i = 0; i < ns; i++)
        fsck_walk_dir(subdirs[i], inode_refs, inode_cnt, visited, vcap, vn, depth + 1);
}

/* 元数据/保留块判断: 超级块+GDT区 / 各组bitmap / inode表 / 保留块 */
static int fsck_is_metadata(uint32_t blk)
{
    if (blk <= ext2_sb->s_first_data_block) return 1;
    /* V3.89-E23: GDT 区域(块2 起, 每组32B) */
    {
        uint32_t gdt_blocks = ((ext2_sb->s_blocks_count + ext2_sb->s_blocks_per_group - 1)
                               / ext2_sb->s_blocks_per_group * 32 + ext2_block_size - 1)
                              / ext2_block_size;
        uint32_t gdt_start = ext2_sb->s_first_data_block + 1;
        if (blk >= gdt_start && blk < gdt_start + gdt_blocks) return 1;
    }
    uint32_t per_group = ext2_sb->s_blocks_per_group;
    uint32_t inode_tbl_blocks =
        (ext2_sb->s_inodes_per_group * ext2_inode_size + ext2_block_size - 1)
        / ext2_block_size;
    uint32_t g = (blk - ext2_sb->s_first_data_block) / per_group;
    uint32_t off = (blk - ext2_sb->s_first_data_block) % per_group;
    uint32_t first = ext2_gdt[g].bg_block_bitmap;
    /* 组内前部元数据(superblock 副本/GDT/保留/bitmap/ibitmap) */
    if (off <= 4) return 1;
    if (blk == first || blk == first + 1) return 1;   /* block/inode bitmap */
    if (blk >= ext2_gdt[g].bg_inode_table &&
        blk <  ext2_gdt[g].bg_inode_table + inode_tbl_blocks) return 1;  /* inode表 */
    /* 保留块(近似分摊到各组末尾) */
    uint32_t reserved_here = ext2_sb->s_r_blocks_count /
                             ((ext2_sb->s_blocks_count + per_group - 1) / per_group) + 1;
    if (off >= per_group - reserved_here) return 1;
    return 0;
}

/* 主检查: 输出统计 + 孤儿报告, 返回 0=健康 */
int ide_ext2_fsck(void)
{
    if (!ext2_mounted) {
        if (ide_ext2_mount() != 0) { vga_puts(" fsck: no ext2 disk\n"); return -1; }
    }

    uint32_t total_blocks = ext2_sb->s_blocks_count;
    uint32_t total_inodes = ext2_sb->s_inodes_count;
    uint32_t groups = (total_blocks + ext2_sb->s_blocks_per_group - 1)
                      / ext2_sb->s_blocks_per_group;
    uint32_t per_group = ext2_sb->s_blocks_per_group;

    /* 位图统计(实际使用): 逐块组读 bitmap 计数 */
    uint32_t used_blocks = 0;
    uint32_t free_blocks = 0;
    uint32_t used_inodes = 0;
    uint32_t free_inodes = 0;
    static uint8_t bmap[4096];
    static uint8_t imap[4096];
    for (uint32_t g = 0; g < groups; g++) {
        uint8_t *bm = ext2_block_access(ext2_gdt[g].bg_block_bitmap);
        if (!bm) { vga_puts(" fsck: read block bitmap FAIL\n"); return -1; }
        memcpy(bmap, bm, ext2_block_size);
        uint32_t nbits = ext2_sb->s_blocks_per_group;
        if (g == groups - 1)
            nbits = total_blocks - g * ext2_sb->s_blocks_per_group;
        for (uint32_t i = 0; i < nbits; i++)
            if (bmap[i / 8] & (1 << (i % 8))) used_blocks++; else free_blocks++;

        uint8_t *im = ext2_block_access(ext2_gdt[g].bg_inode_bitmap);
        if (!im) { vga_puts(" fsck: read inode bitmap FAIL\n"); return -1; }
        memcpy(imap, im, ext2_block_size);
        uint32_t nino = ext2_sb->s_inodes_per_group;
        if (total_inodes - g * ext2_sb->s_inodes_per_group < nino)
            nino = total_inodes - g * ext2_sb->s_inodes_per_group;
        for (uint32_t i = 0; i < nino; i++)
            if (imap[i / 8] & (1 << (i % 8))) used_inodes++; else free_inodes++;
    }

    /* 目录树引用收集 */
    static uint8_t inode_refs[8192];
    static uint32_t visited[FSCK_MAX_VISIT];
    memset(inode_refs, 0, sizeof(inode_refs));
    uint32_t vn = 0;
    if (total_inodes > sizeof(inode_refs) * 8) total_inodes = sizeof(inode_refs) * 8;
    fsck_walk_dir(EXT2_ROOT_INODE, inode_refs, total_inodes, visited, FSCK_MAX_VISIT, &vn, 0);

    /* 块引用收集: 遍历已分配 inode, 标记其块 */
    static uint8_t block_refs[4096];   /* 覆盖 <=32768 块 */
    memset(block_refs, 0, sizeof(block_refs));
    if (total_blocks > sizeof(block_refs) * 8) total_blocks = sizeof(block_refs) * 8;
    {
        static uint32_t blist[FSCK_MAX_INDIRECT];
        for (uint32_t ino = 1; ino <= total_inodes; ino++) {
            uint32_t group = (ino - 1) / ext2_sb->s_inodes_per_group;
            uint32_t index = (ino - 1) % ext2_sb->s_inodes_per_group;
            /* V3.89-E23: 必须读对应组的 inode 位图(原复用主循环末组 imap, 导致
             * 全 inode 被判未分配, block_refs 全空, 孤儿块大量误报) */
            uint8_t *im = ext2_block_access(ext2_gdt[group].bg_inode_bitmap);
            if (!im) continue;
            if (!(im[index / 8] & (1 << (index % 8)))) continue;
            uint32_t n = fsck_collect_blocks(ino, blist, FSCK_MAX_INDIRECT);
            for (uint32_t i = 0; i < n; i++)
                if (blist[i] < total_blocks) block_refs[blist[i] / 8] |= (1 << (blist[i] % 8));
        }
    }

    /* 汇总输出 */
    vga_printf(" [FSCK] blocks: total=%d used=%d free=%d sb_free=%d%s\n",
               ext2_sb->s_blocks_count, used_blocks, free_blocks,
               ext2_sb->s_free_blocks_count,
               (free_blocks == ext2_sb->s_free_blocks_count) ? " ok" : " MISMATCH");
    vga_printf(" [FSCK] inodes: total=%d used=%d free=%d sb_free=%d%s\n",
               ext2_sb->s_inodes_count, used_inodes, free_inodes,
               ext2_sb->s_free_inodes_count,
               (free_inodes == ext2_sb->s_free_inodes_count) ? " ok" : " MISMATCH");

    /* 孤儿块: 位图已分配但无文件引用(V3.89-E24: 按块组映射取位图,
     * 原复用末组 bmap 静态数组导致跨组块号错报/漏报) */
    uint32_t orphan_blocks = 0;
    uint32_t orphan_first = 0;
    uint32_t orphan_first_g = 0;   /* V3.89-E26: 首孤儿块所在块组 */
    for (uint32_t i = 1; i < total_blocks; i++) {
        uint32_t g = (i - ext2_sb->s_first_data_block) / per_group;
        uint32_t off = (i - ext2_sb->s_first_data_block) % per_group;
        uint8_t *bm = ext2_block_access(ext2_gdt[g].bg_block_bitmap);
        if (!bm) continue;
        int allocated = (bm[off / 8] & (1 << (off % 8))) ? 1 : 0;
        int referenced = (block_refs[i / 8] & (1 << (i % 8))) ? 1 : 0;
        if (allocated && !referenced && !fsck_is_metadata(i)) {
            orphan_blocks++;
            if (!orphan_first) { orphan_first = i; orphan_first_g = g; }
        }
    }
    /* 孤儿 inode: 位图已分配但 i_links_count==0(已 unlink 未释放)或目录树不可达 */
    uint32_t orphan_inodes = 0;
    uint32_t orphan_ino_first = 0;
    uint32_t orphan_ino_first_g = 0;   /* V3.89-E26: 首孤儿 inode 所在块组 */
    uint32_t unreachable_inodes = 0;
    for (uint32_t i = 11; i < total_inodes; i++) {
        uint32_t g = (i - 1) / ext2_sb->s_inodes_per_group;
        uint32_t idx = (i - 1) % ext2_sb->s_inodes_per_group;
        uint8_t *bm = ext2_block_access(ext2_gdt[g].bg_inode_bitmap);
        if (!bm) continue;
        int allocated = (bm[idx / 8] & (1 << (idx % 8))) ? 1 : 0;
        if (!allocated) continue;
        ext2_inode_t in;
        if (ext2_read_inode(i, &in) != 0) continue;
        if (in.i_links_count == 0) {            /* 真孤儿: 未链接 */
            orphan_inodes++;
            if (!orphan_ino_first) { orphan_ino_first = i; orphan_ino_first_g = g; }
        } else if (!inode_refs[i]) {            /* 已链接但目录树不可达 */
            unreachable_inodes++;
            if (!orphan_ino_first) { orphan_ino_first = i; orphan_ino_first_g = g; }
        }
    }
    if (unreachable_inodes)
        vga_printf(" [FSCK] linked-but-unreachable inodes: %d (目录树未引用, 可能目录损坏)\n",
                   unreachable_inodes);

    vga_printf(" [FSCK] orphan blocks: %d%s\n", orphan_blocks,
               orphan_blocks ? " (位图分配但无文件引用)" : "");
    if (orphan_first)
        vga_printf(" [FSCK]   first orphan block: %d (group %d)\n",
                  orphan_first, orphan_first_g);
    vga_printf(" [FSCK] orphan inodes: %d%s\n", orphan_inodes,
               orphan_inodes ? " (位图分配但目录无引用)" : "");
    if (orphan_ino_first)
        vga_printf(" [FSCK]   first orphan inode: %d (group %d)\n",
                  orphan_ino_first, orphan_ino_first_g);

    vga_puts(" [FSCK] done\n");
    return (orphan_blocks == 0 && orphan_inodes == 0) ? 0 : 1;
}


/* ============================================================================
 * V3.89-E24: fsck 修复模式 (fs fsck -f)
 * 清理孤儿块/孤儿inode(links_count==0), 同步修正超级块 free 计数, 落盘。
 * 只清理"位图已分配但无任何引用/未链接"的安全垃圾, 不触碰已链接数据。
 * ========================================================================== */
#define FSCK_FIX_MAX_PASS 8

int ide_ext2_fsck_fix(void)
{
    if (!ext2_mounted) {
        if (ide_ext2_mount() != 0) { vga_puts(" fsck: no ext2 disk\n"); return -1; }
    }

    uint32_t total_blocks = ext2_sb->s_blocks_count;
    uint32_t total_inodes = ext2_sb->s_inodes_count;
    uint32_t per_group = ext2_sb->s_blocks_per_group;

    /* 收集目录树 inode 引用 + 已分配 inode 的块引用(与检查模式同逻辑) */
    static uint8_t inode_refs[8192];
    static uint32_t visited[FSCK_MAX_VISIT];
    static uint8_t block_refs[4096];
    static uint32_t blist[FSCK_MAX_INDIRECT];
    memset(inode_refs, 0, sizeof(inode_refs));
    memset(block_refs, 0, sizeof(block_refs));
    uint32_t vn = 0;
    if (total_inodes > sizeof(inode_refs) * 8) total_inodes = sizeof(inode_refs) * 8;
    if (total_blocks > sizeof(block_refs) * 8) total_blocks = sizeof(block_refs) * 8;
    fsck_walk_dir(EXT2_ROOT_INODE, inode_refs, total_inodes, visited, FSCK_MAX_VISIT, &vn, 0);
    for (uint32_t ino = 1; ino <= total_inodes; ino++) {
        uint32_t group = (ino - 1) / ext2_sb->s_inodes_per_group;
        uint32_t index = (ino - 1) % ext2_sb->s_inodes_per_group;
        uint8_t *im = ext2_block_access(ext2_gdt[group].bg_inode_bitmap);
        if (!im) continue;
        if (!(im[index / 8] & (1 << (index % 8)))) continue;
        uint32_t n = fsck_collect_blocks(ino, blist, FSCK_MAX_INDIRECT);
        for (uint32_t i = 0; i < n; i++)
            if (blist[i] < total_blocks) block_refs[blist[i] / 8] |= (1 << (blist[i] % 8));
    }

    uint32_t fixed_blocks = 0, fixed_inodes = 0;
    int healthy = 0;
    for (uint32_t pass = 0; pass < FSCK_FIX_MAX_PASS; pass++) {
        /* 扫描本轮的孤儿块/孤儿inode (按块组映射取位图) */
        uint32_t n_ob = 0, n_oi = 0;
        for (uint32_t i = 1; i < total_blocks; i++) {
            uint32_t g = (i - ext2_sb->s_first_data_block) / per_group;
            uint32_t off = (i - ext2_sb->s_first_data_block) % per_group;
            uint8_t *bm = ext2_block_access(ext2_gdt[g].bg_block_bitmap);
            if (!bm) continue;
            int allocated = (bm[off / 8] & (1 << (off % 8))) ? 1 : 0;
            int referenced = (block_refs[i / 8] & (1 << (i % 8))) ? 1 : 0;
            if (allocated && !referenced && !fsck_is_metadata(i)) n_ob++;
        }
        for (uint32_t i = 11; i < total_inodes; i++) {
            uint32_t g = (i - 1) / ext2_sb->s_inodes_per_group;
            uint32_t idx = (i - 1) % ext2_sb->s_inodes_per_group;
            uint8_t *bm = ext2_block_access(ext2_gdt[g].bg_inode_bitmap);
            if (!bm) continue;
            if (!(bm[idx / 8] & (1 << (idx % 8)))) continue;
            ext2_inode_t in;
            if (ext2_read_inode(i, &in) != 0) continue;
            if (in.i_links_count == 0) n_oi++;
        }
        if (n_ob == 0 && n_oi == 0) { healthy = 1; break; }

        vga_printf(" [FSCK-FIX] pass %d: orphans blk=%d ino=%d\n", pass + 1, n_ob, n_oi);
        if (n_ob > 512 || n_oi > 512) {
            vga_puts(" [FSCK-FIX] too many orphans (>512), abort for safety\n");
            return -1;
        }
        /* 清理孤儿块位图(判定后重新取指针——判定过程不访问其它块, 指针安全) */
        for (uint32_t i = 1; i < total_blocks; i++) {
            uint32_t g = (i - ext2_sb->s_first_data_block) / per_group;
            uint32_t off = (i - ext2_sb->s_first_data_block) % per_group;
            uint8_t *bm = ext2_block_access(ext2_gdt[g].bg_block_bitmap);
            if (!bm) continue;
            int allocated = (bm[off / 8] & (1 << (off % 8))) ? 1 : 0;
            int referenced = (block_refs[i / 8] & (1 << (i % 8))) ? 1 : 0;
            if (allocated && !referenced && !fsck_is_metadata(i)) {
                uint8_t *bm2 = ext2_block_access(ext2_gdt[g].bg_block_bitmap);
                if (!bm2) continue;
                bm2[off / 8] &= (uint8_t)~(1 << (off % 8));
                ext2_block_dirty(ext2_gdt[g].bg_block_bitmap);
                ext2_sb->s_free_blocks_count++;
                fixed_blocks++;
            }
        }
        /* 清理孤儿 inode 位图: 先判定(读 inode 表块可能逐出位图缓存槽,
         * 使旧 bm 指针失效), 判定通过后必须重新取位图指针再修改 */
        for (uint32_t i = 11; i < total_inodes; i++) {
            uint32_t g = (i - 1) / ext2_sb->s_inodes_per_group;
            uint32_t idx = (i - 1) % ext2_sb->s_inodes_per_group;
            uint8_t *bm0 = ext2_block_access(ext2_gdt[g].bg_inode_bitmap);
            if (!bm0) continue;
            if (!(bm0[idx / 8] & (1 << (idx % 8)))) continue;
            ext2_inode_t in;
            if (ext2_read_inode(i, &in) != 0) continue;
            if (in.i_links_count == 0) {
                uint8_t *bm2 = ext2_block_access(ext2_gdt[g].bg_inode_bitmap);
                if (!bm2) continue;
                bm2[idx / 8] &= (uint8_t)~(1 << (idx % 8));
                ext2_block_dirty(ext2_gdt[g].bg_inode_bitmap);
                ext2_sb->s_free_inodes_count++;
                fixed_inodes++;
            }
        }
    }

    /* 落盘: 常驻区(超级块计数) + 脏块(位图) */
    ide_ext2_sync();

    if (healthy) {
        if (fixed_blocks || fixed_inodes) {
            vga_printf(" [FSCK-FIX] done: fixed blocks=%d inodes=%d (synced)\n",
                       fixed_blocks, fixed_inodes);
            vga_puts(" [FSCK-FIX] re-check:\n");
            return ide_ext2_fsck();
        }
        vga_puts(" [FSCK-FIX] nothing to fix\n");
        return 0;
    }
    vga_printf(" [FSCK-FIX] done: fixed blocks=%d inodes=%d (synced)\n",
               fixed_blocks, fixed_inodes);
    vga_puts(" [FSCK-FIX] WARN: pass limit reached, re-check:\n");
    return ide_ext2_fsck();
}
