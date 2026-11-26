/* ============================================================================
 * 赫普OS - 系统管理命令 KUnit 用例
 *
 * 覆盖：
 *   - 纯解析：chmod 八进制模式、chown owner[:group]、fsck 参数
 *   - 挂载表：启动根种子、未知设备/缺失目录拒绝、根挂载不可卸载
 *   - 落盘（带盘 ext2）：chmod/chown 写入 inode 权限位/uid/gid；
 *     无盘环境整组 SKIP（ext2 backend unavailable）。
 *
 * 32 位 CLI 无串口交互输入；命令交互路径（shell 分派/打印文案）由带盘
 * 冒烟取证，本套件只覆盖可在 KUnit 内确定性验证的逻辑。
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"
#include "vfs.h"
#include "ext2.h"
#include "shell_admin.h"

static int admin_ext2_ready(void)
{
    extern int ext2_mounted;
    return ext2_mounted != 0;
}

static void admin_skip_no_ext2(const char *name)
{
    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[KUNIT-SKIP] admin/");
    serial_puts(0x3F8, name);
    serial_puts(0x3F8, " (ext2 backend unavailable)\n");
}

/* ---- 纯解析：八进制模式 ---- */
static kunit_result_t test_admin_mode_parse(void)
{
    uint32_t m = 0;
    KUNIT_ASSERT(admin_parse_mode("755", &m) == 1, " parse 755 ok ");
    KUNIT_ASSERT_EQ(m, (uint32_t)0755, " 755 value ");
    KUNIT_ASSERT(admin_parse_mode("0755", &m) == 1, " parse 0755 ok ");
    KUNIT_ASSERT_EQ(m, (uint32_t)0755, " 0755 value ");
    KUNIT_ASSERT(admin_parse_mode("0", &m) == 1, " parse 0 ok ");
    KUNIT_ASSERT_EQ(m, (uint32_t)0, " 0 value ");

    KUNIT_ASSERT(admin_parse_mode("89", &m) == 0, " reject octal 8/9 ");
    KUNIT_ASSERT(admin_parse_mode("xyz", &m) == 0, " reject alpha ");
    KUNIT_ASSERT(admin_parse_mode("7778", &m) == 0, " reject digit 8 ");
    KUNIT_ASSERT(admin_parse_mode("77777", &m) == 0, " reject >4 digits ");
    KUNIT_ASSERT(admin_parse_mode("", &m) == 0, " reject empty ");
    return KUNIT_PASS;
}

/* ---- 纯解析：owner[:group] ---- */
static kunit_result_t test_admin_owner_parse(void)
{
    uint32_t uid = 0, gid = 0; int hu = 0, hg = 0;

    KUNIT_ASSERT(admin_parse_owner("1:2", &uid, &hu, &gid, &hg) == 0, " parse 1:2 ");
    KUNIT_ASSERT_EQ(hu, 1, " has uid ");
    KUNIT_ASSERT_EQ(hg, 1, " has gid ");
    KUNIT_ASSERT_EQ(uid, (uint32_t)1, " uid=1 ");
    KUNIT_ASSERT_EQ(gid, (uint32_t)2, " gid=2 ");

    KUNIT_ASSERT(admin_parse_owner("3:", &uid, &hu, &gid, &hg) == 0, " parse 3: ");
    KUNIT_ASSERT_EQ(hu, 1, " has uid only ");
    KUNIT_ASSERT_EQ(hg, 0, " no gid ");

    KUNIT_ASSERT(admin_parse_owner(":4", &uid, &hu, &gid, &hg) == 0, " parse :4 ");
    KUNIT_ASSERT_EQ(hu, 0, " no uid ");
    KUNIT_ASSERT_EQ(hg, 1, " has gid ");

    KUNIT_ASSERT(admin_parse_owner("abc", &uid, &hu, &gid, &hg) != 0, " reject alpha uid ");
    KUNIT_ASSERT(admin_parse_owner("1:z", &uid, &hu, &gid, &hg) != 0, " reject alpha gid ");
    return KUNIT_PASS;
}

/* ---- 纯解析：fsck 参数 ---- */
static kunit_result_t test_admin_fsck_parse(void)
{
    int fix = 9;
    KUNIT_ASSERT(admin_fsck_parse("", &fix) == 0, " default ok ");
    KUNIT_ASSERT_EQ(fix, 0, " default check-only ");
    KUNIT_ASSERT(admin_fsck_parse("--check-only", &fix) == 0, " --check-only ");
    KUNIT_ASSERT_EQ(fix, 0, " check-only flag ");
    KUNIT_ASSERT(admin_fsck_parse("-c", &fix) == 0, " -c ");
    KUNIT_ASSERT(admin_fsck_parse("--fix", &fix) == 0, " --fix ");
    KUNIT_ASSERT_EQ(fix, 1, " fix flag ");
    KUNIT_ASSERT(admin_fsck_parse("-f", &fix) == 0, " -f ");
    KUNIT_ASSERT(admin_fsck_parse("--bogus", &fix) != 0, " reject unknown flag ");
    return KUNIT_PASS;
}

/* ---- 挂载表：启动根种子 + 不可卸载 ---- */
static kunit_result_t test_admin_mount_root_seed(void)
{
    int rc = 0;
    /* 启动根记录存在且不可卸载 */
    KUNIT_ASSERT(admin_mount_remove("/", &rc) == -1, " umount / rejected ");
    KUNIT_ASSERT_EQ(rc, (uint32_t)FS_EBUSY, " root busy EBUSY ");
    /* 未挂载目录卸载拒绝 */
    KUNIT_ASSERT(admin_mount_remove("/no/such/mp", &rc) == -1, " unknown mp rejected ");
    KUNIT_ASSERT_EQ(rc, (uint32_t)FS_ENOENT, " unknown mp ENOENT ");
    return KUNIT_PASS;
}

/* ---- 挂载：未知设备拒绝；带盘下重复挂载拒绝（表不留半挂） ---- */
static kunit_result_t test_admin_mount_rejects(void)
{
    int rc = 0;
    KUNIT_ASSERT(admin_mount_add("hd9p9", "/data", &rc) == -1, " unknown device rejected ");
    KUNIT_ASSERT_EQ(rc, (uint32_t)FS_ENOENT, " unknown device ENOENT ");

    /* 以下断言依赖启动根设备已挂盘；无盘环境显式 SKIP，不算 PASS。 */
    if (!admin_ext2_ready()) {
        admin_skip_no_ext2("mount_dup_device");
        return KUNIT_SKIP;
    }
    KUNIT_ASSERT(admin_mount_add("ide0p1", "/mnt", &rc) == -1, " dup device rejected ");
    KUNIT_ASSERT_EQ(rc, (uint32_t)FS_EEXIST, " dup device EEXIST ");
    return KUNIT_PASS;
}

/* ---- 落盘：chmod/chown 写回 ext2 inode（带盘） ---- */
static kunit_result_t test_admin_chmod_chown_persist(void)
{
    if (!admin_ext2_ready()) {
        admin_skip_no_ext2("chmod_chown_persist");
        return KUNIT_SKIP;
    }

    const char *dir = "/data/adminkt";
    const char *file = "/data/adminkt/prop.txt";

    if (!vfs_find("/data")) KUNIT_ASSERT(vfs_mkdir("/data") == 0, " mkdir /data ");
    if (!vfs_find(dir))    KUNIT_ASSERT(vfs_mkdir(dir) == 0, " mkdir test dir ");

    int fd = vfs_open(file, VFS_O_CREAT | VFS_O_WRONLY);
    KUNIT_ASSERT(fd >= 0, " create prop.txt ");
    vfs_write(fd, "KT", 2);
    vfs_close(fd);

    /* 走命令分派：chmod 640, chown 2:2, chgrp 3 */
    KUNIT_ASSERT(shell_admin_dispatch("chmod", "640 /data/adminkt/prop.txt") == 1,
                 " dispatch chmod ");
    KUNIT_ASSERT(shell_admin_dispatch("chown", "2:2 /data/adminkt/prop.txt") == 1,
                 " dispatch chown ");
    KUNIT_ASSERT(shell_admin_dispatch("chgrp", "3 /data/adminkt/prop.txt") == 1,
                 " dispatch chgrp ");

    vfs_stat_t st;
    KUNIT_ASSERT(vfs_stat(file, &st) == 0, " stat back ");
    KUNIT_ASSERT(st.inode != 0, " inode number present ");

    ext2_inode_t in;
    KUNIT_ASSERT(ext2_read_inode(st.inode, &in) == 0, " read disk inode ");
    KUNIT_ASSERT_EQ(in.i_mode & 07777, (uint32_t)0640, " persisted mode 0640 ");
    KUNIT_ASSERT_EQ(in.i_uid, (uint16_t)2, " persisted uid 2 ");
    KUNIT_ASSERT_EQ(in.i_gid, (uint16_t)3, " persisted gid 3 (chgrp) ");

    /* 清理 */
    vfs_unlink(file);
    vfs_rmdir(dir);
    return KUNIT_PASS;
}

/* ---- 拒绝型命令：mkfs / useradd / userdel 显式拒绝路径（不依赖 ext2） ---- */
static kunit_result_t test_admin_reject_stubs(void)
{
    KUNIT_ASSERT(shell_admin_dispatch("mkfs", "/dev/hd0p1") == 1, " dispatch mkfs ");
    KUNIT_ASSERT(shell_admin_dispatch("useradd", "alice") == 1, " dispatch useradd ");
    KUNIT_ASSERT(shell_admin_dispatch("userdel", "alice") == 1, " dispatch userdel ");
    return KUNIT_PASS;
}

static const kunit_test_t admin_tests[] = {
    {"chmod mode parse",       test_admin_mode_parse,            "admin"},
    {"chown owner parse",      test_admin_owner_parse,          "admin"},
    {"fsck arg parse",         test_admin_fsck_parse,         "admin"},
    {"mount root seed/umount", test_admin_mount_root_seed,     "admin"},
    {"mount rejects",          test_admin_mount_rejects,       "admin"},
    {"chmod/chown persist",    test_admin_chmod_chown_persist,  "admin"},
    {"mkfs/useradd/userdel reject", test_admin_reject_stubs,    "admin"},
};

static const kunit_suite_t kunit_admin_suite = {
    " system admin commands ", admin_tests, 7
};

void kunit_admin_register(void)
{
    kunit_register_suite(&kunit_admin_suite);
    vga_puts("[KUNIT] system admin command suite registered (7 cases)\n");
}
