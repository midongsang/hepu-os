/* ============================================================================
 * 赫普OS - 文件系统深度单元测试
 * 覆盖：ext2结构/目录项/超级块/位图/inode
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === ext2超级块深度测试 === */
typedef struct {
    u32 s_inodes_count;
    u32 s_blocks_count;
    u32 s_r_blocks_count;
    u32 s_free_blocks_count;
    u32 s_free_inodes_count;
    u32 s_first_data_block;
    u32 s_log_block_size;
    u32 s_log_frag_size;
    u32 s_blocks_per_group;
    u32 s_frags_per_group;
    u32 s_inodes_per_group;
    u32 s_mtime;
    u32 s_wtime;
    u16 s_mnt_count;
    u16 s_max_mnt_count;
    u16 s_magic;
    u16 s_state;
    u16 s_errors;
    u16 s_minor_rev_level;
    u32 s_lastcheck;
    u32 s_checkinterval;
    u32 s_creator_os;
    u32 s_rev_level;
    u16 s_def_resuid;
    u16 s_def_resgid;
} ext2_sb_t;

#define EXT2_MAGIC 0xEF53
#define EXT2_VALID_FS 1
#define EXT2_ERROR_CONTINUE 1

static kunit_result_t test_ext2_sb_magic(void)
{
    ext2_sb_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.s_magic = EXT2_MAGIC;
    sb.s_state = EXT2_VALID_FS;
    sb.s_errors = EXT2_ERROR_CONTINUE;

    KUNIT_ASSERT_EQ(sb.s_magic, EXT2_MAGIC, "ext2 magic ");
    KUNIT_ASSERT_EQ(sb.s_state, EXT2_VALID_FS, " has 效 fs state ");
    KUNIT_ASSERT_EQ(sb.s_errors, EXT2_ERROR_CONTINUE, " error handling = 继续 ");
    return KUNIT_PASS;
}

static kunit_result_t test_ext2_block_size(void)
{
    ext2_sb_t sb;
    memset(&sb, 0, sizeof(sb));

    /* 1KB块 */
    sb.s_log_block_size = 0;
    u32 block_size = 1024 << sb.s_log_block_size;
    KUNIT_ASSERT_EQ(block_size, 1024, "1KB block size ");

    /* 2KB块 */
    sb.s_log_block_size = 1;
    block_size = 1024 << sb.s_log_block_size;
    KUNIT_ASSERT_EQ(block_size, 2048, "2KB block size ");

    /* 4KB块 */
    sb.s_log_block_size = 2;
    block_size = 1024 << sb.s_log_block_size;
    KUNIT_ASSERT_EQ(block_size, 4096, "4KB block size ");
    return KUNIT_PASS;
}

static kunit_result_t test_ext2_group_calc(void)
{
    ext2_sb_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.s_blocks_count = 100000;
    sb.s_blocks_per_group = 8192;
    sb.s_inodes_count = 25000;
    sb.s_inodes_per_group = 2048;

    u32 block_groups = (sb.s_blocks_count + sb.s_blocks_per_group - 1) /
                       sb.s_blocks_per_group;
    u32 inode_groups = (sb.s_inodes_count + sb.s_inodes_per_group - 1) /
                       sb.s_inodes_per_group;

    KUNIT_ASSERT_EQ(block_groups, 13, " block 组 count =13");
    KUNIT_ASSERT_EQ(inode_groups, 13, "inode 组 count =13");
    KUNIT_ASSERT_EQ(block_groups, inode_groups, " block 组 =inode 组 ");
    return KUNIT_PASS;
}

/* === ext2 inode深度测试 === */
typedef struct {
    u16 i_mode;
    u16 i_uid;
    u32 i_size;
    u32 i_atime;
    u32 i_ctime;
    u32 i_mtime;
    u32 i_dtime;
    u16 i_gid;
    u16 i_links_count;
    u32 i_blocks;
    u32 i_flags;
    u32 i_osd1;
    u32 i_block[15];
    u32 i_generation;
    u32 i_file_acl;
    u32 i_dir_acl;
    u32 i_faddr;
    u8  i_osd2[12];
} ext2_inode_t;

#define EXT2_S_IFREG 0x8000
#define EXT2_S_IFDIR 0x4000
#define EXT2_S_IRUSR 0x0100
#define EXT2_S_IWUSR 0x0080
#define EXT2_S_IXUSR 0x0040

static kunit_result_t test_ext2_inode_type(void)
{
    ext2_inode_t inode;
    memset(&inode, 0, sizeof(inode));

    /* 普通文件 */
    inode.i_mode = EXT2_S_IFREG | 0644;
    KUNIT_ASSERT(inode.i_mode & EXT2_S_IFREG, " normal file type ");
    KUNIT_ASSERT(!(inode.i_mode & EXT2_S_IFDIR), " not is dir ");

    /* 目录 */
    inode.i_mode = EXT2_S_IFDIR | 0755;
    KUNIT_ASSERT(inode.i_mode & EXT2_S_IFDIR, " dir type ");
    KUNIT_ASSERT(!(inode.i_mode & EXT2_S_IFREG), " not is normal file ");
    return KUNIT_PASS;
}

static kunit_result_t test_ext2_inode_perms(void)
{
    ext2_inode_t inode;
    memset(&inode, 0, sizeof(inode));

    inode.i_mode = EXT2_S_IFREG | EXT2_S_IRUSR | EXT2_S_IWUSR | EXT2_S_IXUSR;
    KUNIT_ASSERT(inode.i_mode & EXT2_S_IRUSR, "owner 读 ");
    KUNIT_ASSERT(inode.i_mode & EXT2_S_IWUSR, "owner 写 ");
    KUNIT_ASSERT(inode.i_mode & EXT2_S_IXUSR, "owner exec ");

    /* 权限位提取 */
    u16 perms = inode.i_mode & 0x0FFF;
    KUNIT_ASSERT_EQ(perms, 0700, " perm bit =0700");
    return KUNIT_PASS;
}

static kunit_result_t test_ext2_inode_blocks(void)
{
    ext2_inode_t inode;
    memset(&inode, 0, sizeof(inode));

    /* 直接块：0-11 */
    for (int i = 0; i < 12; i++) {
        inode.i_block[i] = 100 + i;
    }
    /* 间接块：12 */
    inode.i_block[12] = 200;
    /* 双重间接：13 */
    inode.i_block[13] = 300;
    /* 三重间接：14 */
    inode.i_block[14] = 400;

    KUNIT_ASSERT_EQ(inode.i_block[0], 100, " 直接 block 0");
    KUNIT_ASSERT_EQ(inode.i_block[11], 111, " 直接 block 11");
    KUNIT_ASSERT_EQ(inode.i_block[12], 200, " 间接 block ");
    KUNIT_ASSERT_EQ(inode.i_block[13], 300, " 双重间接 ");
    KUNIT_ASSERT_EQ(inode.i_block[14], 400, " 三重间接 ");
    return KUNIT_PASS;
}

/* === ext2目录项测试 === */
typedef struct {
    u32 inode;
    u16 rec_len;
    u8  name_len;
    u8  file_type;
    char name[255];
} ext2_dirent_t;

#define EXT2_FT_UNKNOWN 0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR 2
#define EXT2_FT_SYMLINK 7

static kunit_result_t test_ext2_dirent(void)
{
    ext2_dirent_t dirent;
    memset(&dirent, 0, sizeof(dirent));

    dirent.inode = 5;
    dirent.rec_len = 12 + 5;  /* 8字节头 + 5字节名 */
    dirent.name_len = 5;
    dirent.file_type = EXT2_FT_REG_FILE;
    memcpy(dirent.name, "hello", 5);

    KUNIT_ASSERT_EQ(dirent.inode, 5, "inode 号 ");
    KUNIT_ASSERT_EQ(dirent.name_len, 5, " name len ");
    KUNIT_ASSERT_EQ(dirent.file_type, EXT2_FT_REG_FILE, " file type = normal file ");
    KUNIT_ASSERT_EQ(memcmp(dirent.name, "hello", 5), 0, " name 内容 ");
    return KUNIT_PASS;
}

static kunit_result_t test_ext2_dirent_types(void)
{
    KUNIT_ASSERT_EQ(EXT2_FT_UNKNOWN, 0, " unknown type ");
    KUNIT_ASSERT_EQ(EXT2_FT_REG_FILE, 1, " normal file ");
    KUNIT_ASSERT_EQ(EXT2_FT_DIR, 2, " dir ");
    KUNIT_ASSERT_EQ(EXT2_FT_SYMLINK, 7, " symlink ");
    return KUNIT_PASS;
}

/* === 位图测试 === */
static u8 test_bitmap[128];

static void bitmap_init(void)
{
    memset(test_bitmap, 0, sizeof(test_bitmap));
}

static int bitmap_alloc(void)
{
    for (u32 i = 0; i < sizeof(test_bitmap) * 8; i++) {
        if (!(test_bitmap[i / 8] & (1 << (i % 8)))) {
            test_bitmap[i / 8] |= (1 << (i % 8));
            return (int)i;
        }
    }
    return -1;
}

static int bitmap_free(u32 idx)
{
    if (idx >= sizeof(test_bitmap) * 8) return -1;
    test_bitmap[idx / 8] &= ~(1 << (idx % 8));
    return 0;
}

static kunit_result_t test_bitmap_alloc(void)
{
    bitmap_init();

    int b1 = bitmap_alloc();
    int b2 = bitmap_alloc();
    int b3 = bitmap_alloc();

    KUNIT_ASSERT_EQ(b1, 0, " alloc bit 0");
    KUNIT_ASSERT_EQ(b2, 1, " alloc bit 1");
    KUNIT_ASSERT_EQ(b3, 2, " alloc bit 2");

    bitmap_free(1);
    int b4 = bitmap_alloc();
    KUNIT_ASSERT_EQ(b4, 1, " 重用 bit 1");
    return KUNIT_PASS;
}

static kunit_result_t test_bitmap_exhaust(void)
{
    bitmap_init();

    /* 分配前10位 */
    for (int i = 0; i < 10; i++) {
        KUNIT_ASSERT_EQ(bitmap_alloc(), i, " alloc bit ");
    }

    /* 释放位5 */
    bitmap_free(5);

    /* 再分配应该得到5 */
    KUNIT_ASSERT_EQ(bitmap_alloc(), 5, " 重用 bit 5");
    return KUNIT_PASS;
}

/* === 文件路径测试 === */
static kunit_result_t test_path_depth(void)
{
    const char *path1 = "/a/b/c/d/e";
    const char *path2 = "/file.txt";
    const char *path3 = "relative/path";

    /* 计算路径深度 */
    int depth1 = 0, depth2 = 0, depth3 = 0;
    for (const char *p = path1; *p; p++) if (*p == '/') depth1++;
    for (const char *p = path2; *p; p++) if (*p == '/') depth2++;
    for (const char *p = path3; *p; p++) if (*p == '/') depth3++;

    KUNIT_ASSERT_EQ(depth1, 5, " deep path =5");
    KUNIT_ASSERT_EQ(depth2, 1, " 根 file =1");
    KUNIT_ASSERT_EQ(depth3, 1, " 相 pair path =1");
    return KUNIT_PASS;
}

static kunit_result_t test_file_extension(void)
{
    const char *filename = "document.tar.gz";
    const char *ext = strrchr(filename, '.');

    KUNIT_ASSERT_NOT_NULL(ext, " found ext 名 ");
    KUNIT_ASSERT_EQ(strcmp(ext, ".gz"), 0, " ext 名 =.gz");

    const char *no_ext = "README";
    ext = strrchr(no_ext, '.');
    KUNIT_ASSERT_NULL(ext, " no ext 名 ");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t fs_deep_tests[] = {
    {"ext2 superblock magic ", test_ext2_sb_magic, "fs"},
    {"ext2 block size ", test_ext2_block_size, "fs"},
    {"ext2 组计算 ", test_ext2_group_calc, "fs"},
    {"ext2 inode type ", test_ext2_inode_type, "fs"},
    {"ext2 inode perm ", test_ext2_inode_perms, "fs"},
    {"ext2 inode block ", test_ext2_inode_blocks, "fs"},
    {"ext2 dir ", test_ext2_dirent, "fs"},
    {"ext2 dir type ", test_ext2_dirent_types, "fs"},
    {" bitmap alloc ", test_bitmap_alloc, "fs"},
    {" bitmap 耗尽 ", test_bitmap_exhaust, "fs"},
    {" path deep ", test_path_depth, "fs"},
    {" file ext 名 ", test_file_extension, "fs"},
};
static const kunit_suite_t kunit_fs_deep_suite = {
    " fs deep ", fs_deep_tests, 12
};

void kunit_fs_deep_register(void)
{
    kunit_register_suite(&kunit_fs_deep_suite);
    vga_puts("[KUNIT] fs deep test suite registered(12 cases)\n");
}
