/* ============================================================================
 * 赫普OS - 文件系统单元测试
 * 覆盖：路径解析/ext2超级块/VFS操作/文件描述符
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === 路径解析测试 === */
static void path_split(const char *path, char *dirname, char *basename)
{
    const char *last_slash = strrchr(path, '/');
    if (!last_slash) {
        strcpy(dirname, ".");
        strcpy(basename, path);
        return;
    }
    if (last_slash == path) {
        strcpy(dirname, "/");
        strcpy(basename, last_slash + 1);
    } else {
        u32 len = (u32)(last_slash - path);
        memcpy(dirname, path, len);
        dirname[len] = '\0';
        strcpy(basename, last_slash + 1);
    }
}

static int path_normalize(const char *path, char *result, u32 result_len)
{
    char components[32][64];
    u32 comp_count = 0;
    const char *p = path;

    while (*p && comp_count < 32) {
        while (*p == '/') p++;
        if (!*p) break;

        const char *start = p;
        while (*p && *p != '/') p++;
        u32 len = (u32)(p - start);

        if (len == 1 && start[0] == '.') continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (comp_count > 0) comp_count--;
            continue;
        }
        if (len >= 64) len = 63;
        memcpy(components[comp_count], start, len);
        components[comp_count][len] = '\0';
        comp_count++;
    }

    u32 pos = 0;
    if (path[0] == '/') result[pos++] = '/';

    /* E-43 修复: 分隔符统一——仅当 pos>0 且非"绝对路径首组件"(path[0]=='/' 时
     * i==0 前导斜杠已写)才补 '/'. 消除 "a//c" 与 "//usr" 双斜杠. */
    for (u32 i = 0; i < comp_count; i++) {
        u32 clen = strlen(components[i]);
        if (pos + clen + 1 >= result_len) return -1;
        if (pos > 0 && (path[0] != '/' || i > 0)) result[pos++] = '/';
        memcpy(result + pos, components[i], clen);
        pos += clen;
    }
    result[pos] = '\0';
    return 0;
}

static kunit_result_t test_path_split(void)
{
    char dirname[64], basename[64];

    path_split("/usr/bin/hepu", dirname, basename);
    KUNIT_ASSERT_EQ(strcmp(dirname, "/usr/bin"), 0, " dir 名 ");
    KUNIT_ASSERT_EQ(strcmp(basename, "hepu"), 0, " 基名 ");

    path_split("file.txt", dirname, basename);
    KUNIT_ASSERT_EQ(strcmp(dirname, "."), 0, " 相 pair path dir 名 ");
    KUNIT_ASSERT_EQ(strcmp(basename, "file.txt"), 0, " 相 pair path 基名 ");

    path_split("/", dirname, basename);
    KUNIT_ASSERT_EQ(strcmp(dirname, "/"), 0, " 根 dir dir 名 ");
    return KUNIT_PASS;
}

static kunit_result_t test_path_normalize(void)
{
    char result[128];

    path_normalize("/usr/./bin/../local/bin", result, 128);
    KUNIT_ASSERT_EQ(strcmp(result, "/usr/local/bin"), 0, " 规范化 path 1");

    path_normalize("/a/b/c/../../d", result, 128);
    KUNIT_ASSERT_EQ(strcmp(result, "/a/d"), 0, " 规范化 path 2");

    path_normalize("./a/./b/../c", result, 128);
    KUNIT_ASSERT_EQ(strcmp(result, "a/c"), 0, " 相 pair path 规范化 ");
    return KUNIT_PASS;
}

/* === ext2超级块测试 === */
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
} ext2_superblock_test_t;

static kunit_result_t test_ext2_superblock_magic(void)
{
    ext2_superblock_test_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.s_magic = 0xEF53;
    sb.s_inodes_count = 1024;
    sb.s_blocks_count = 8192;
    sb.s_blocks_per_group = 8192;
    sb.s_inodes_per_group = 1024;
    sb.s_log_block_size = 0;  /* 1024字节块 */

    KUNIT_ASSERT_EQ(sb.s_magic, 0xEF53, "ext2 magic ");
    KUNIT_ASSERT_EQ(sb.s_inodes_count, 1024, "inode count ");
    KUNIT_ASSERT_EQ(sb.s_blocks_count, 8192, " block count ");

    u32 block_size = 1024 << sb.s_log_block_size;
    KUNIT_ASSERT_EQ(block_size, 1024, " block size 计算 ");

    u32 groups = (sb.s_blocks_count + sb.s_blocks_per_group - 1) /
                 sb.s_blocks_per_group;
    KUNIT_ASSERT_EQ(groups, 1, " block 组 count ");
    return KUNIT_PASS;
}

static kunit_result_t test_ext2_inode_size(void)
{
    /* ext2 inode大小通常为128字节 */
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
    } ext2_inode_test_t;

    KUNIT_ASSERT_EQ(sizeof(ext2_inode_test_t), 128, "ext2 inode size ");
    return KUNIT_PASS;
}

/* === VFS文件描述符测试 === */
#define VFS_MAX_FDS 64

typedef struct {
    int  used;
    u32  offset;
    char name[64];
} vfs_fd_test_t;

static vfs_fd_test_t test_fds[VFS_MAX_FDS];

static int vfs_fd_alloc(void)
{
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        if (!test_fds[i].used) {
            test_fds[i].used = 1;
            test_fds[i].offset = 0;
            return i;
        }
    }
    return -1;
}

static int vfs_fd_free(int fd)
{
    if (fd < 0 || fd >= VFS_MAX_FDS || !test_fds[fd].used) return -1;
    test_fds[fd].used = 0;
    return 0;
}

static kunit_result_t test_vfs_fd_alloc(void)
{
    memset(test_fds, 0, sizeof(test_fds));

    int fd1 = vfs_fd_alloc();
    int fd2 = vfs_fd_alloc();
    int fd3 = vfs_fd_alloc();

    KUNIT_ASSERT_EQ(fd1, 0, " 第一 fd");
    KUNIT_ASSERT_EQ(fd2, 1, " 第二 fd");
    KUNIT_ASSERT_EQ(fd3, 2, " 第三 fd");

    vfs_fd_free(fd2);
    int fd4 = vfs_fd_alloc();
    KUNIT_ASSERT_EQ(fd4, 1, " 重用 release fd");

    vfs_fd_free(fd1);
    vfs_fd_free(fd3);
    vfs_fd_free(fd4);
    return KUNIT_PASS;
}

static kunit_result_t test_vfs_fd_exhaust(void)
{
    memset(test_fds, 0, sizeof(test_fds));

    int fds[VFS_MAX_FDS];
    for (int i = 0; i < VFS_MAX_FDS; i++) {
        fds[i] = vfs_fd_alloc();
        KUNIT_ASSERT_EQ(fds[i], i, " alloc all fd");
    }

    int overflow = vfs_fd_alloc();
    KUNIT_ASSERT_EQ(overflow, -1, " 超出 max fd 返回 -1");

    for (int i = 0; i < VFS_MAX_FDS; i++) {
        vfs_fd_free(fds[i]);
    }
    return KUNIT_PASS;
}

/* === 文件权限测试 === */
#define PERM_READ  0x4
#define PERM_WRITE 0x2
#define PERM_EXEC  0x1

static int perm_check(u16 mode, u8 user_type, u8 requested)
{
    u16 perm;
    switch (user_type) {
        case 0: perm = (mode >> 6) & 0x7; break;  /* owner */
        case 1: perm = (mode >> 3) & 0x7; break;  /* group */
        case 2: perm = mode & 0x7; break;         /* other */
        default: return -1;
    }
    return (perm & requested) == requested ? 0 : -1;
}

static kunit_result_t test_perm_check(void)
{
    u16 mode = 0755;  /* rwxr-xr-x */

    KUNIT_ASSERT_EQ(perm_check(mode, 0, PERM_READ), 0, "owner 读 ");
    KUNIT_ASSERT_EQ(perm_check(mode, 0, PERM_WRITE), 0, "owner 写 ");
    KUNIT_ASSERT_EQ(perm_check(mode, 0, PERM_EXEC), 0, "owner exec ");
    KUNIT_ASSERT_EQ(perm_check(mode, 1, PERM_WRITE), -1, "group no 写 ");
    KUNIT_ASSERT_EQ(perm_check(mode, 2, PERM_WRITE), -1, "other no 写 ");
    KUNIT_ASSERT_EQ(perm_check(mode, 2, PERM_READ), 0, "other 读 ");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t fs_tests[] = {
    {" path 分割 ", test_path_split, "fs"},
    {" path 规范化 ", test_path_normalize, "fs"},
    {"ext2 superblock ", test_ext2_superblock_magic, "fs"},
    {"ext2 inode size ", test_ext2_inode_size, "fs"},
    {"VFS fd alloc ", test_vfs_fd_alloc, "fs"},
    {"VFS fd 耗尽 ", test_vfs_fd_exhaust, "fs"},
    {" perm check ", test_perm_check, "fs"},
};
static const kunit_suite_t kunit_fs_suite = {
    " fs ", fs_tests, 7
};

void kunit_fs_register(void)
{
    kunit_register_suite(&kunit_fs_suite);
    vga_puts("[KUNIT] fs test suite registered(7 cases)\n");
}
