/* ============================================================================
 * 赫普OS - 系统调用：USER
 * V8.46: 关联multi_user，添加密码认证
 * ========================================================================== */

#include "types.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "syscall.h"
#include "vga.h"
#include "kheap.h"
#include "sched.h"
#include "hepu.h"
#include "fingerprint.h"
#include "vfs.h"
#include "ipc.h"
#include "multi_user.h"
extern const char *hepu_kernel_version;

/* 密码哈希（简化版，实际应使用bcrypt/argon2） */
static void hash_password(const char *password, uint8_t hash[32])
{
    /* 使用简单的SHA-256风格哈希（内核已有sha256实现） */
    extern void sha256(const uint8_t*, uint32_t, uint8_t*);
    sha256((const uint8_t*)password, strlen(password), hash);
}

/* 密码存储（最多16个用户） */
static uint8_t password_hashes[16][32];
static int password_set[16] = {0};

/* 获取当前用户ID */
static int get_current_uid(void)
{
    mu_user_t user;
    if (mu_user_get_current(&user) == 0) {
        return user.id;
    }
    return 0;  /* 默认root */
}

u32 sys_getuid(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4;
    return get_current_uid();
}

u32 sys_geteuid(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4;
    return get_current_uid();
}

u32 sys_getgid(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4;
    return get_current_uid();  /* 简化：gid=uid */
}

u32 sys_getegid(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4;
    return get_current_uid();
}

/* V8.46: 设置用户密码 */
u32 sys_set_password(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    const char *old_password = (const char*)arg1;
    const char *new_password = (const char*)arg2;
    (void)arg3; (void)arg4;

    int uid = get_current_uid();
    if (uid < 0 || uid >= 16) return -1;

    /* 如果已有密码，验证旧密码 */
    if (password_set[uid] && old_password) {
        uint8_t old_hash[32];
        hash_password(old_password, old_hash);
        if (memcmp(old_hash, password_hashes[uid], 32) != 0) {
            vga_puts("[USER] Old password incorrect\n");
            return -2;  /* 旧密码错误 */
        }
    }

    /* 设置新密码 */
    if (new_password && strlen(new_password) > 0) {
        hash_password(new_password, password_hashes[uid]);
        password_set[uid] = 1;
        vga_printf("[USER] Password set for user %d\n", uid);
    } else {
        /* 清空密码 */
        password_set[uid] = 0;
        memset(password_hashes[uid], 0, 32);
        vga_printf("[USER] Password cleared for user %d\n", uid);
    }

    return 0;
}

/* V8.46: 验证密码 */
u32 sys_verify_password(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    int uid = (int)arg1;
    const char *password = (const char*)arg2;
    (void)arg3; (void)arg4;

    if (uid < 0 || uid >= 16 || !password) return -1;

    /* 如果没有设置密码，直接通过 */
    if (!password_set[uid]) {
        return 1;  /* 无密码，认证通过 */
    }

    /* 验证密码 */
    uint8_t hash[32];
    hash_password(password, hash);
    if (memcmp(hash, password_hashes[uid], 32) == 0) {
        vga_printf("[USER] User %d authenticated\n", uid);
        return 1;  /* 认证通过 */
    }

    vga_printf("[USER] User %d authentication failed\n", uid);
    return 0;  /* 认证失败 */
}

/* V8.46: 切换用户 */
u32 sys_switch_user(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    int user_id = (int)arg1;
    const char *password = (const char*)arg2;
    (void)arg3; (void)arg4;

    /* 验证密码 */
    if (sys_verify_password(user_id, (u32)password, 0, 0) != 1) {
        return -1;  /* 认证失败 */
    }

    /* 切换用户 */
    if (mu_user_switch(user_id) != 0) {
        return -2;  /* 切换失败 */
    }

    vga_printf("[USER] Switched to user %d\n", user_id);
    return 0;
}

/* V8.46: 创建用户 */
u32 sys_create_user(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    const char *name = (const char*)arg1;
    int user_type = (int)arg2;
    const char *password = (const char*)arg3;
    (void)arg4;

    if (!name) return -1;

    /* 只有机主(uid=0)可以创建用户 */
    if (get_current_uid() != 0) {
        vga_puts("[USER] Permission denied: only owner can create users\n");
        return -2;
    }

    /* 创建用户 */
    int user_id = mu_user_create(name, (mu_user_type_t)user_type);
    if (user_id < 0) {
        return -3;
    }

    /* 设置密码 */
    if (password && strlen(password) > 0) {
        hash_password(password, password_hashes[user_id]);
        password_set[user_id] = 1;
    }

    vga_printf("[USER] User created: %s (id=%d)\n", name, user_id);
    return user_id;
}

/* V8.46: 删除用户 */
u32 sys_remove_user(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    int user_id = (int)arg1;
    (void)arg2; (void)arg3; (void)arg4;

    /* 只有机主可以删除用户 */
    if (get_current_uid() != 0) {
        return -1;
    }

    /* 不能删除机主自己 */
    if (user_id == 0) {
        return -2;
    }

    /* 删除用户 */
    if (mu_user_remove(user_id) != 0) {
        return -3;
    }

    /* 清除密码 */
    password_set[user_id] = 0;
    memset(password_hashes[user_id], 0, 32);

    vga_printf("[USER] User %d removed\n", user_id);
    return 0;
}

/* V8.46: 锁定当前用户 */
u32 sys_lock_user(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4;

    int uid = get_current_uid();
    mu_user_lock(uid);
    vga_printf("[USER] User %d locked\n", uid);
    return 0;
}

/* V8.46: 解锁用户 */
u32 sys_unlock_user(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    int user_id = (int)arg1;
    const char *password = (const char*)arg2;
    (void)arg3; (void)arg4;

    /* 验证密码 */
    if (sys_verify_password(user_id, (u32)password, 0, 0) != 1) {
        return -1;
    }

    /* 解锁用户 */
    mu_user_unlock(user_id);
    vga_printf("[USER] User %d unlocked\n", user_id);
    return 0;
}

/* V8.46: 获取用户列表 */
u32 sys_list_users(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    mu_user_t *users = (mu_user_t*)arg1;
    int max = (int)arg2;
    (void)arg3; (void)arg4;

    if (!users || max <= 0) return -1;
    return mu_user_list(users, max);
}

/* V8.46: 获取当前用户信息 */
u32 sys_get_current_user(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
    mu_user_t *user = (mu_user_t*)arg1;
    (void)arg2; (void)arg3; (void)arg4;

    if (!user) return -1;
    return mu_user_get_current(user);
}
