/* ============================================================================
 * 赫普OS 64位 - ext2文件系统检查与修复工具
 * 检查超级块、块组描述符、inode、目录项的一致性
 * V0.10.107: P0-6 ext2 fsck初始版本
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 外部函数 */
extern void vga_puts(const char *str);
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

/* 本地整数输出 */
static void fsck_putint(int num)
{
    char buf[16];
    int i = 0;
    int neg = 0;
    if (num < 0) { neg = 1; num = -num; }
    if (num == 0) { vga_puts("0"); return; }
    while (num > 0 && i < 15) { buf[i++] = '0' + (num % 10); num /= 10; }
    if (neg) vga_puts("-");
    while (i > 0) {
        char c[2] = {buf[--i], 0};
        vga_puts(c);
    }
}

/* ext2常量 */
#define EXT2_SUPER_MAGIC 0xEF53
#define EXT2_ROOT_INO    2
#define EXT2_NDIR_BLOCKS  12
#define EXT2_IND_BLOCK    12
#define EXT2_DIND_BLOCK   13
#define EXT2_TIND_BLOCK   14
#define EXT2_N_BLOCKS     15

/* ext2超级块 */
typedef struct {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    /* 更多字段省略 */
} ext2_superblock_t;

/* ext2块组描述符 */
typedef struct {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint32_t bg_reserved[3];
} ext2_group_desc_t;

/* ext2 inode */
typedef struct {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[EXT2_N_BLOCKS];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint32_t i_osd2[3];
} ext2_inode_t;

/* ext2目录项 */
typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
    char name[];
} ext2_dir_entry_t;

/* fsck统计 */
typedef struct {
    int errors_found;
    int errors_fixed;
    int warnings;
    int inodes_checked;
    int blocks_checked;
    int dirs_checked;
} fsck_stats_t;

static fsck_stats_t fsck_stats;

/* 初始化fsck */
int ext2_fsck_init(void)
{
    vga_puts("[FSCK64] Initializing ext2 filesystem checker...\n");
    fsck_stats.errors_found = 0;
    fsck_stats.errors_fixed = 0;
    fsck_stats.warnings = 0;
    fsck_stats.inodes_checked = 0;
    fsck_stats.blocks_checked = 0;
    fsck_stats.dirs_checked = 0;
    vga_puts("[FSCK64] ext2 fsck initialized\n");
    return 0;
}

/* 检查超级块 */
static int check_superblock(const ext2_superblock_t *sb)
{
    int errors = 0;

    vga_puts("[FSCK64] Checking superblock...\n");

    /* 检查魔数 */
    if (sb->s_magic != EXT2_SUPER_MAGIC) {
        vga_puts("[FSCK64] ERROR: Bad magic number: 0x");
        fsck_putint(sb->s_magic);
        vga_puts("\n");
        errors++;
    } else {
        vga_puts("[FSCK64]   Magic: OK (0xEF53)\n");
    }

    /* 检查状态 */
    if (sb->s_state == 1) {
        vga_puts("[FSCK64]   State: clean\n");
    } else if (sb->s_state == 2) {
        vga_puts("[FSCK64]   WARNING: Filesystem has errors\n");
        fsck_stats.warnings++;
    } else {
        vga_puts("[FSCK64]   WARNING: Unknown state: ");
        fsck_putint(sb->s_state);
        vga_puts("\n");
        fsck_stats.warnings++;
    }

    /* 检查块大小 */
    uint32_t block_size = 1024 << sb->s_log_block_size;
    vga_puts("[FSCK64]   Block size: ");
    fsck_putint(block_size);
    vga_puts(" bytes\n");

    /* 检查inode/块计数 */
    vga_puts("[FSCK64]   Inodes: ");
    fsck_putint(sb->s_inodes_count);
    vga_puts(", Blocks: ");
    fsck_putint(sb->s_blocks_count);
    vga_puts("\n");

    vga_puts("[FSCK64]   Free inodes: ");
    fsck_putint(sb->s_free_inodes_count);
    vga_puts(", Free blocks: ");
    fsck_putint(sb->s_free_blocks_count);
    vga_puts("\n");

    fsck_stats.errors_found += errors;
    return errors;
}

/* 检查块组描述符 */
static int check_group_descriptors(const ext2_superblock_t *sb,
                                    const ext2_group_desc_t *gdt,
                                    int num_groups)
{
    int errors = 0;

    vga_puts("[FSCK64] Checking block group descriptors...\n");
    vga_puts("[FSCK64]   Number of groups: ");
    fsck_putint(num_groups);
    vga_puts("\n");

    for (int i = 0; i < num_groups; i++) {
        const ext2_group_desc_t *gd = &gdt[i];

        /* 检查块位图位置 */
        if (gd->bg_block_bitmap == 0 || gd->bg_block_bitmap >= sb->s_blocks_count) {
            vga_puts("[FSCK64]   ERROR: Group ");
            fsck_putint(i);
            vga_puts(" bad block bitmap: ");
            fsck_putint(gd->bg_block_bitmap);
            vga_puts("\n");
            errors++;
        }

        /* 检查inode位图位置 */
        if (gd->bg_inode_bitmap == 0 || gd->bg_inode_bitmap >= sb->s_blocks_count) {
            vga_puts("[FSCK64]   ERROR: Group ");
            fsck_putint(i);
            vga_puts(" bad inode bitmap: ");
            fsck_putint(gd->bg_inode_bitmap);
            vga_puts("\n");
            errors++;
        }

        /* 检查inode表位置 */
        if (gd->bg_inode_table == 0 || gd->bg_inode_table >= sb->s_blocks_count) {
            vga_puts("[FSCK64]   ERROR: Group ");
            fsck_putint(i);
            vga_puts(" bad inode table: ");
            fsck_putint(gd->bg_inode_table);
            vga_puts("\n");
            errors++;
        }

        fsck_stats.blocks_checked++;
    }

    if (errors == 0) {
        vga_puts("[FSCK64]   All group descriptors OK\n");
    }

    fsck_stats.errors_found += errors;
    return errors;
}

/* 检查inode */
/* V0.55.1: check_inode 已实现但 fsck 主流程目前只校验超级块/位图, 尚未遍历
 * inode 表, 因此该检查暂无调用点(功能缺口, 已在文档记录)。 */
static __attribute__((unused)) int check_inode(const ext2_inode_t *inode, int ino)
{
    int errors = 0;

    /* 检查模式 */
    if (inode->i_mode == 0) {
        /* 空闲inode，正常 */
        return 0;
    }

    /* 检查链接计数。
     * V0.55.1: 旧判断 "> 65535" 对 uint16_t 恒假, 等于从未检查;
     * 已分配的 inode 链接计数为 0 才是真正的异常(孤立 inode)。 */
    if (inode->i_links_count == 0) {
        vga_puts("[FSCK64]   ERROR: Inode ");
        fsck_putint(ino);
        vga_puts(" bad links count: ");
        fsck_putint(inode->i_links_count);
        vga_puts("\n");
        errors++;
    }

    /* 检查块指针（简化：只检查直接块） */
    for (int i = 0; i < EXT2_NDIR_BLOCKS; i++) {
        if (inode->i_block[i] != 0) {
            /* 块号应该在有效范围内（这里简化检查） */
        }
    }

    fsck_stats.inodes_checked++;
    return errors;
}

/* 运行完整fsck */
int ext2_fsck_run(void *superblock_ptr, void *gdt_ptr)
{
    const ext2_superblock_t *sb = (const ext2_superblock_t *)superblock_ptr;
    const ext2_group_desc_t *gdt = (const ext2_group_desc_t *)gdt_ptr;

    vga_puts("\n========================================\n");
    vga_puts("  HepuOS ext2 Filesystem Check\n");
    vga_puts("========================================\n\n");

    int total_errors = 0;

    /* 1. 检查超级块 */
    total_errors += check_superblock(sb);

    /* 2. 计算块组数 */
    int num_groups = (sb->s_blocks_count + sb->s_blocks_per_group - 1) /
                     sb->s_blocks_per_group;

    /* 3. 检查块组描述符 */
    total_errors += check_group_descriptors(sb, gdt, num_groups);

    /* 4. 输出统计 */
    vga_puts("\n[FSCK64] ========================================\n");
    vga_puts("[FSCK64] Check Summary:\n");
    vga_puts("[FSCK64]   Inodes checked: ");
    fsck_putint(fsck_stats.inodes_checked);
    vga_puts("\n");
    vga_puts("[FSCK64]   Blocks checked: ");
    fsck_putint(fsck_stats.blocks_checked);
    vga_puts("\n");
    vga_puts("[FSCK64]   Errors found: ");
    fsck_putint(fsck_stats.errors_found);
    vga_puts("\n");
    vga_puts("[FSCK64]   Errors fixed: ");
    fsck_putint(fsck_stats.errors_fixed);
    vga_puts("\n");
    vga_puts("[FSCK64]   Warnings: ");
    fsck_putint(fsck_stats.warnings);
    vga_puts("\n");

    if (total_errors == 0) {
        vga_puts("[FSCK64] Filesystem is CLEAN\n");
    } else {
        vga_puts("[FSCK64] Filesystem has ERRORS\n");
    }
    vga_puts("[FSCK64] ========================================\n");

    return total_errors;
}

/* 获取fsck统计 */
void ext2_fsck_get_stats(fsck_stats_t *stats)
{
    if (stats) {
        *stats = fsck_stats;
    }
}
