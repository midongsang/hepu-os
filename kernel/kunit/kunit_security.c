/* ============================================================================
 * 赫普OS - 安全与权限单元测试
 * 覆盖：用户权限/访问控制/能力/沙箱/审计/密码哈希
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === 用户权限测试 === */
#define PERM_READ    0x0001
#define PERM_WRITE   0x0002
#define PERM_EXEC    0x0004
#define PERM_DELETE  0x0008
#define PERM_ADMIN   0x8000

#define ROLE_GUEST    0x0000
#define ROLE_USER     (PERM_READ | PERM_WRITE)
#define ROLE_OPERATOR (PERM_READ | PERM_WRITE | PERM_EXEC)
#define ROLE_ADMIN    0xFFFF

static kunit_result_t test_permission_bits(void)
{
    KUNIT_ASSERT_EQ(PERM_READ, 0x0001, " 读 perm bit ");
    KUNIT_ASSERT_EQ(PERM_WRITE, 0x0002, " 写 perm bit ");
    KUNIT_ASSERT_EQ(PERM_EXEC, 0x0004, " exec perm bit ");
    KUNIT_ASSERT_EQ(PERM_DELETE, 0x0008, " delete perm bit ");
    KUNIT_ASSERT_EQ(PERM_ADMIN, 0x8000, " mgr 员 perm bit ");
    return KUNIT_PASS;
}

static kunit_result_t test_role_permissions(void)
{
    KUNIT_ASSERT_EQ(ROLE_GUEST, 0, " 访客 no perm ");
    KUNIT_ASSERT(ROLE_USER & PERM_READ, " user 可读 ");
    KUNIT_ASSERT(ROLE_USER & PERM_WRITE, " user 可写 ");
    KUNIT_ASSERT(!(ROLE_USER & PERM_EXEC), " user not 可 exec ");
    KUNIT_ASSERT(ROLE_OPERATOR & PERM_EXEC, " ops 员可 exec ");
    KUNIT_ASSERT_EQ(ROLE_ADMIN, 0xFFFF, " mgr 员全 perm ");
    return KUNIT_PASS;
}

static int has_permission(u16 role, u16 required)
{
    return (role & required) == required ? 0 : -1;
}

static kunit_result_t test_permission_check(void)
{
    KUNIT_ASSERT_EQ(has_permission(ROLE_USER, PERM_READ), 0, " user 可读 ");
    KUNIT_ASSERT_EQ(has_permission(ROLE_USER, PERM_WRITE), 0, " user 可写 ");
    KUNIT_ASSERT_EQ(has_permission(ROLE_USER, PERM_EXEC), -1, " user not 可 exec ");
    KUNIT_ASSERT_EQ(has_permission(ROLE_GUEST, PERM_READ), -1, " 访客 not 可读 ");
    KUNIT_ASSERT_EQ(has_permission(ROLE_ADMIN, PERM_DELETE), 0, " mgr 员可 delete ");
    return KUNIT_PASS;
}

/* === 访问控制列表测试 === */
#define ACL_MAX_ENTRIES 16
typedef struct {
    u32 user_id;
    u16 permissions;
    u8  valid;
} acl_entry_t;

static acl_entry_t acl_table[ACL_MAX_ENTRIES];
static u32 acl_count;

static void acl_init(void)
{
    memset(acl_table, 0, sizeof(acl_table));
    acl_count = 0;
}

static int acl_add(u32 uid, u16 perms)
{
    if (acl_count >= ACL_MAX_ENTRIES) return -1;
    /* 检查是否已存在 */
    for (u32 i = 0; i < acl_count; i++) {
        if (acl_table[i].valid && acl_table[i].user_id == uid) {
            acl_table[i].permissions = perms;
            return 0;
        }
    }
    acl_table[acl_count].user_id = uid;
    acl_table[acl_count].permissions = perms;
    acl_table[acl_count].valid = 1;
    acl_count++;
    return 0;
}

static int acl_check(u32 uid, u16 required)
{
    for (u32 i = 0; i < acl_count; i++) {
        if (acl_table[i].valid && acl_table[i].user_id == uid) {
            return (acl_table[i].permissions & required) == required ? 0 : -1;
        }
    }
    return -1;  /* 默认拒绝 */
}

static kunit_result_t test_acl_basic(void)
{
    acl_init();
    KUNIT_ASSERT_EQ(acl_add(1001, PERM_READ | PERM_WRITE), 0, " add user 1001");
    KUNIT_ASSERT_EQ(acl_add(1002, PERM_READ), 0, " add user 1002");

    KUNIT_ASSERT_EQ(acl_check(1001, PERM_READ), 0, "1001 可读 ");
    KUNIT_ASSERT_EQ(acl_check(1001, PERM_WRITE), 0, "1001 可写 ");
    KUNIT_ASSERT_EQ(acl_check(1001, PERM_EXEC), -1, "1001 not 可 exec ");
    KUNIT_ASSERT_EQ(acl_check(1002, PERM_READ), 0, "1002 可读 ");
    KUNIT_ASSERT_EQ(acl_check(1002, PERM_WRITE), -1, "1002 not 可写 ");
    KUNIT_ASSERT_EQ(acl_check(9999, PERM_READ), -1, " unknown user default denied ");
    return KUNIT_PASS;
}

static kunit_result_t test_acl_update(void)
{
    acl_init();
    acl_add(1001, PERM_READ);
    KUNIT_ASSERT_EQ(acl_check(1001, PERM_WRITE), -1, " initial no 写 perm ");

    acl_add(1001, PERM_READ | PERM_WRITE);
    KUNIT_ASSERT_EQ(acl_check(1001, PERM_WRITE), 0, " update next has 写 perm ");
    KUNIT_ASSERT_EQ(acl_count, 1, " entry count not 变 ");
    return KUNIT_PASS;
}

/* === 能力(Capability)测试 === */
#define CAP_SYS_ADMIN   0x0001
#define CAP_NET_ADMIN   0x0002
#define CAP_NET_RAW     0x0004
#define CAP_SYS_MODULE  0x0008
#define CAP_SYS_RAWIO   0x0010
#define CAP_SYS_PTRACE  0x0020
#define CAP_SYS_BOOT    0x0040
#define CAP_SYS_TIME    0x0080

typedef struct {
    u32 uid;
    u32 capabilities;
} process_cap_t;

static int cap_check(const process_cap_t *proc, u32 cap)
{
    if (!proc) return -1;
    return (proc->capabilities & cap) ? 0 : -1;
}

static kunit_result_t test_capabilities(void)
{
    process_cap_t root = {0, 0xFFFFFFFF};
    process_cap_t net_admin = {1001, CAP_NET_ADMIN | CAP_NET_RAW};
    process_cap_t normal = {1002, 0};

    KUNIT_ASSERT_EQ(cap_check(&root, CAP_SYS_ADMIN), 0, "root has all capabilities ");
    KUNIT_ASSERT_EQ(cap_check(&root, CAP_SYS_BOOT), 0, "root 可 reboot ");
    KUNIT_ASSERT_EQ(cap_check(&net_admin, CAP_NET_ADMIN), 0, " net mgr 员可 mgr net ");
    KUNIT_ASSERT_EQ(cap_check(&net_admin, CAP_NET_RAW), 0, " net mgr 员可用原始 socket ");
    KUNIT_ASSERT_EQ(cap_check(&net_admin, CAP_SYS_ADMIN), -1, " net mgr 员 no system mgr ");
    KUNIT_ASSERT_EQ(cap_check(&normal, CAP_NET_RAW), -1, " normal user no 原始 socket ");
    KUNIT_ASSERT_EQ(cap_check(0, CAP_SYS_ADMIN), -1, " empty process check failed ");
    return KUNIT_PASS;
}

/* === 沙箱测试 === */
#define SANDBOX_LEVEL_NONE    0
#define SANDBOX_LEVEL_BASIC   1
#define SANDBOX_LEVEL_STRICT  2
#define SANDBOX_LEVEL_FULL    3

typedef struct {
    u8 level;
    u8 allow_network;
    u8 allow_filesystem;
    u8 allow_syscall;
} sandbox_t;

static void sandbox_init(sandbox_t *sb, u8 level)
{
    if (!sb) return;
    sb->level = level;
    switch (level) {
        case SANDBOX_LEVEL_NONE:
            sb->allow_network = 1;
            sb->allow_filesystem = 1;
            sb->allow_syscall = 1;
            break;
        case SANDBOX_LEVEL_BASIC:
            sb->allow_network = 1;
            sb->allow_filesystem = 0;
            sb->allow_syscall = 1;
            break;
        case SANDBOX_LEVEL_STRICT:
            sb->allow_network = 0;
            sb->allow_filesystem = 0;
            sb->allow_syscall = 1;
            break;
        case SANDBOX_LEVEL_FULL:
            sb->allow_network = 0;
            sb->allow_filesystem = 0;
            sb->allow_syscall = 0;
            break;
    }
}

static kunit_result_t test_sandbox_levels(void)
{
    sandbox_t sb;

    sandbox_init(&sb, SANDBOX_LEVEL_NONE);
    KUNIT_ASSERT(sb.allow_network && sb.allow_filesystem && sb.allow_syscall, " no 沙箱全允许 ");

    sandbox_init(&sb, SANDBOX_LEVEL_BASIC);
    KUNIT_ASSERT(sb.allow_network, " base 沙箱允许 net ");
    KUNIT_ASSERT(!sb.allow_filesystem, " base 沙箱禁止 fs ");

    sandbox_init(&sb, SANDBOX_LEVEL_STRICT);
    KUNIT_ASSERT(!sb.allow_network, " strict 沙箱禁止 net ");
    KUNIT_ASSERT(!sb.allow_filesystem, " strict 沙箱禁止 fs ");
    KUNIT_ASSERT(sb.allow_syscall, " strict 沙箱允许 syscall ");

    sandbox_init(&sb, SANDBOX_LEVEL_FULL);
    KUNIT_ASSERT(!sb.allow_network && !sb.allow_filesystem && !sb.allow_syscall, " 完全沙箱全禁止 ");
    return KUNIT_PASS;
}

/* === 审计日志测试 === */
#define AUDIT_MAX 32
typedef struct {
    u32 timestamp;
    u32 uid;
    u16 action;
    u8  result;
} audit_entry_t;

static audit_entry_t audit_log[AUDIT_MAX];
static u32 audit_count;

static void audit_init(void)
{
    memset(audit_log, 0, sizeof(audit_log));
    audit_count = 0;
}

static int audit_record(u32 uid, u16 action, u8 result)
{
    if (audit_count >= AUDIT_MAX) return -1;
    audit_log[audit_count].timestamp = audit_count * 100;
    audit_log[audit_count].uid = uid;
    audit_log[audit_count].action = action;
    audit_log[audit_count].result = result;
    audit_count++;
    return 0;
}

static kunit_result_t test_audit_log(void)
{
    audit_init();
    KUNIT_ASSERT_EQ(audit_record(1001, 0x01, 1), 0, " records 登录 ok ");
    KUNIT_ASSERT_EQ(audit_record(1001, 0x02, 0), 0, " records file 访问 failed ");
    KUNIT_ASSERT_EQ(audit_count, 2, "2 审计 records ");

    KUNIT_ASSERT_EQ(audit_log[0].uid, 1001, " 第一 uid");
    KUNIT_ASSERT_EQ(audit_log[0].action, 0x01, " 第一 动作 ");
    KUNIT_ASSERT_EQ(audit_log[0].result, 1, " 第一 result = ok ");
    KUNIT_ASSERT_EQ(audit_log[1].result, 0, " 第二 result = failed ");
    return KUNIT_PASS;
}

/* === 密码哈希测试 === */
static u32 simple_hash(const char *password)
{
    if (!password) return 0;
    u32 hash = 5381;
    for (u32 i = 0; password[i]; i++) {
        hash = ((hash << 5) + hash) + (u8)password[i];
    }
    return hash;
}

static kunit_result_t test_password_hash(void)
{
    u32 h1 = simple_hash("password123");
    u32 h2 = simple_hash("password123");
    u32 h3 = simple_hash("Password123");
    u32 h4 = simple_hash("");

    KUNIT_ASSERT_EQ(h1, h2, " 相同密码哈希相同 ");
    KUNIT_ASSERT(h1 != h3, " not 同密码哈希 not 同 ");
    KUNIT_ASSERT(h4 != 0, " empty 密码哈希 non 零 ");
    KUNIT_ASSERT_EQ(simple_hash(0), 0, " empty ptr 返回 0");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t security_tests[] = {
    {" perm bit ", test_permission_bits, "security"},
    {" role perm ", test_role_permissions, "security"},
    {" perm check ", test_permission_check, "security"},
    {"ACL basic ", test_acl_basic, "security"},
    {"ACL update ", test_acl_update, "security"},
    {" capabilities check ", test_capabilities, "security"},
    {" 沙箱 level 别 ", test_sandbox_levels, "security"},
    {" 审计 log ", test_audit_log, "security"},
    {" 密码哈希 ", test_password_hash, "security"},
};
static const kunit_suite_t kunit_security_suite = {
    " security & perm ", security_tests, 9
};

void kunit_security_register(void)
{
    kunit_register_suite(&kunit_security_suite);
    vga_puts("[KUNIT] security & perm test suite registered(9 cases)\n");
}
