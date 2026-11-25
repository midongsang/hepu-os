/* ============================================================================
 * P3 安全最小闭环：VFS 权限 / POSIX 错误码 / fd→路径 / 32 位账户
 * ========================================================================== */
#include <stdint.h>
#include "kunit.h"
#include "vfs.h"
#include "multi_user.h"
#include "string.h"
#include "vga.h"

static void p3_log(const char *s) { vga_puts(s); }

static int p3_ext2_ready(void)
{
    extern int ext2_mounted;
    return ext2_mounted != 0;
}

static void p3_skip_no_ext2(const char *case_name)
{
    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[KUNIT-SKIP] p3-security/");
    serial_puts(0x3F8, case_name);
    serial_puts(0x3F8, " (ext2 backend unavailable)\n");
}

static void p3_cleanup(void)
{
    /* 先恢复 root，避免上一轮测试失败时残留普通用户身份 */
    extern int mu_user_switch(int);
    mu_user_switch(0);

    const char *files[] = {
        "/data/p3sec/own.txt",
        "/data/p3sec/dir/child.txt",
        "/data/p3sec/enospc",
    };
    for (uint32_t i = 0; i < sizeof(files)/sizeof(files[0]); i++) {
        if (vfs_find(files[i])) vfs_unlink(files[i]);
    }
    if (vfs_find("/data/p3sec/dir")) vfs_rmdir("/data/p3sec/dir");
    if (vfs_find("/data/p3sec")) vfs_rmdir("/data/p3sec");
}

/* S3: 普通用户写系统目录被拒，返回 EACCES；自己的可写根下可正常建/写/删 */
static kunit_result_t test_s3_normal_user_denied(void)
{
    extern int mu_user_switch(int);
    extern int hepu_current_uid(void);
    extern int auth64_is_configured(void);
    extern int auth64_current_role(void);

    p3_cleanup();
    if (!p3_ext2_ready()) {
        p3_skip_no_ext2("s3_normal_user_denied");
        return KUNIT_SKIP;
    }

    /* 账户最小实现：root + 普通用户都在，root 角色=2 */
    KUNIT_ASSERT(auth64_is_configured() == 1, " account configured ");
    KUNIT_ASSERT(hepu_current_uid() == 0, " root uid=0 ");
    KUNIT_ASSERT(auth64_current_role() == 2, " root role=developer ");
    mu_user_t u;
    KUNIT_ASSERT(mu_user_get(1, &u) == 0, " normal user exists ");
    KUNIT_ASSERT(u.type == MU_USER_SECONDARY, " normal user type ");

    /* root 建可写根，交给普通用户 */
    if (!vfs_find("/data")) {
        KUNIT_ASSERT(vfs_mkdir("/data") == 0, " create /data ");
    }
    KUNIT_ASSERT(vfs_mkdir("/data/p3sec") == 0, " create /data/p3sec ");
    KUNIT_ASSERT(vfs_chmod("/data/p3sec", 0777) == 0, " chmod 0777 ");
    KUNIT_ASSERT(vfs_chown("/data/p3sec", 1, 1) == 0, " chown uid1 ");

    /* 切普通用户 */
    KUNIT_ASSERT(mu_user_switch(1) == 0, " switch to user1 ");
    KUNIT_ASSERT(hepu_current_uid() == 1, " normal uid=1 ");
    KUNIT_ASSERT(auth64_current_role() == 1, " normal role ");

    /* 系统目录写入必须 EACCES（即使 /system 节点不存在也先暴露 EACCES） */
    int rc = vfs_mkdir("/system/p3denied");
    KUNIT_ASSERT(rc == VFS_EACCES, " mkdir /system denied EACCES ");
    rc = vfs_open("/system/p3denied", VFS_O_CREAT | VFS_O_WRONLY);
    KUNIT_ASSERT(rc == VFS_EACCES, " open /system O_CREAT denied EACCES ");

    /* 自己的可写根下：建、写、fd→路径、删都要成功 */
    int fd = vfs_open("/data/p3sec/own.txt", VFS_O_CREAT | VFS_O_WRONLY);
    KUNIT_ASSERT(fd >= 0, " create own.txt ");
    KUNIT_ASSERT(vfs_write(fd, "P3", 2) == 2, " write own.txt ");
    char path[VFS_MAX_PATH];
    KUNIT_ASSERT(vfs_fd_get_path(fd, path, sizeof(path)) == 0, " fd path ");
    KUNIT_ASSERT(strcmp(path, "/data/p3sec/own.txt") == 0, " fd path value ");
    vfs_close(fd);

    KUNIT_ASSERT(vfs_mkdir("/data/p3sec/dir") == 0, " mkdir own dir ");

    /* 目录全部清空后，普通用户删自己的文件 */
    KUNIT_ASSERT(vfs_unlink("/data/p3sec/own.txt") == 0, " unlink own file ");

    /* 恢复 root，清理 */
    KUNIT_ASSERT(mu_user_switch(0) == 0, " switch back root ");
    if (vfs_find("/data/p3sec/dir")) vfs_rmdir("/data/p3sec/dir");
    if (vfs_find("/data/p3sec")) vfs_rmdir("/data/p3sec");
    return KUNIT_PASS;
}

/* 错误码表：ENOENT / EEXIST / EACCES / EISDIR / ENOTEMPTY / ENOSPC */
static kunit_result_t test_posix_error_table(void)
{
    extern int mu_user_switch(int);
    p3_cleanup();
    if (!p3_ext2_ready()) {
        p3_skip_no_ext2("posix_error_table");
        return KUNIT_SKIP;
    }

    if (!vfs_find("/data")) {
        KUNIT_ASSERT(vfs_mkdir("/data") == 0, " create /data ");
    }
    if (!vfs_find("/data/p3sec")) {
        KUNIT_ASSERT(vfs_mkdir("/data/p3sec") == 0, " create base ");
    }

    KUNIT_ASSERT(vfs_mkdir("/data/p3sec") == VFS_EEXIST, " EEXIST mkdir ");
    KUNIT_ASSERT(vfs_mkdir("/no_such_parent_p3/x") == VFS_ENOENT, " ENOENT parent ");
    KUNIT_ASSERT(vfs_open("/data/p3sec/absent", VFS_O_RDONLY) == VFS_ENOENT, " ENOENT open ");
    KUNIT_ASSERT(vfs_unlink("/data/p3sec/absent") == VFS_ENOENT, " ENOENT unlink ");
    KUNIT_ASSERT(vfs_rmdir("/data/p3sec/absent") == VFS_ENOENT, " ENOENT rmdir ");
    KUNIT_ASSERT(vfs_rename("/data/p3sec/absent", "/data/p3sec/x") == VFS_ENOENT, " ENOENT rename ");
    KUNIT_ASSERT(vfs_unlink("/data/p3sec") == VFS_EISDIR, " EISDIR unlink ");

    int fd = vfs_open("/data/p3sec/keep", VFS_O_CREAT | VFS_O_WRONLY);
    KUNIT_ASSERT(fd >= 0, " create keep ");
    vfs_close(fd);
    KUNIT_ASSERT(vfs_rmdir("/data/p3sec") == VFS_ENOTEMPTY, " ENOTEMPTY rmdir ");
    KUNIT_ASSERT(vfs_unlink("/data/p3sec/keep") == 0, " cleanup keep ");

    /* 故障注入只验证 ENOSPC 映射路径，不依赖把 58MB ext2 写满 */
    vfs_test_inject_enospc(1);
    int rc = vfs_mkdir("/data/p3sec/enospc");
    vfs_test_inject_enospc(0);
    KUNIT_ASSERT(rc == VFS_ENOSPC, " ENOSPC injected mkdir ");

    if (vfs_find("/data/p3sec")) vfs_rmdir("/data/p3sec");
    return KUNIT_PASS;
}

static const kunit_test_t p3_tests[] = {
    { "s3_normal_user_denied", test_s3_normal_user_denied, "p3-security" },
    { "posix_error_table",     test_posix_error_table,     "p3-security" },
};

const kunit_suite_t kunit_p3_security_suite = {
    "p3-security",
    p3_tests,
    sizeof(p3_tests) / sizeof(p3_tests[0])
};

void kunit_p3_security_register(void)
{
    extern void kunit_register_suite(const kunit_suite_t *);
    kunit_register_suite(&kunit_p3_security_suite);
    p3_log("[KUNIT] p3-security suite registered\n");
}
