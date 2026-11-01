/* ============================================================================
 * 赫普OS 64位 - 用户空间访问安全（架构相关实现）
 * 接口定义见 kernel/shared/security/user_access.h
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "../../shared/security/user_access.h"

extern void vga_puts(const char *s);

/* 64位用户空间上限（规范地址下半部） */
#define USER_SPACE_LIMIT 0x00007FFFFFFFFFFFULL
#define KERNEL_SPACE_BASE 0xFFFF800000000000ULL

/* 共享变量：用户空间上限 */
uintptr_t hepu_user_space_limit = USER_SPACE_LIMIT;

/* 检查地址是否在用户空间（共享接口实现） */
int hepu_is_user_address(uintptr_t addr, size_t size)
{
    if (addr == 0) return 0;
    if (addr > USER_SPACE_LIMIT) return 0;
    if (addr + size < addr) return 0;  /* 溢出 */
    if (addr + size > USER_SPACE_LIMIT + 1) return 0;
    return 1;
}

/* 检查地址是否在用户空间（内部兼容） */
static int is_user_address(uint64_t addr, size_t size)
{
    return hepu_is_user_address(addr, size);
}

/* 从用户空间复制到内核空间
 * 返回：0=成功，-1=失败（地址无效）
 */
int copy_from_user(void *kernel_dst, const void *user_src, size_t size)
{
    if (!kernel_dst || !user_src || size == 0) {
        return -1;
    }

    uint64_t src_addr = (uint64_t)user_src;
    if (!is_user_address(src_addr, size)) {
        vga_puts("[SECURITY] copy_from_user: invalid user address 0x");
        /* 不打印完整地址，避免信息泄露 */
        vga_puts("...\n");
        return -1;
    }

    /* 安全复制 */
    uint8_t *dst = (uint8_t*)kernel_dst;
    const uint8_t *src = (const uint8_t*)user_src;
    for (size_t i = 0; i < size; i++) {
        dst[i] = src[i];
    }

    return 0;
}

/* 从内核空间复制到用户空间
 * 返回：0=成功，-1=失败（地址无效）
 */
int copy_to_user(void *user_dst, const void *kernel_src, size_t size)
{
    if (!user_dst || !kernel_src || size == 0) {
        return -1;
    }

    uint64_t dst_addr = (uint64_t)user_dst;
    if (!is_user_address(dst_addr, size)) {
        vga_puts("[SECURITY] copy_to_user: invalid user address\n");
        return -1;
    }

    /* 安全复制 */
    uint8_t *dst = (uint8_t*)user_dst;
    const uint8_t *src = (const uint8_t*)kernel_src;
    for (size_t i = 0; i < size; i++) {
        dst[i] = src[i];
    }

    return 0;
}

/* 从用户空间复制字符串（带长度限制）
 * 返回：复制的字节数（不含终止符），-1=失败
 */
int copy_str_from_user(char *kernel_dst, const char *user_src, size_t max_size)
{
    if (!kernel_dst || !user_src || max_size == 0) {
        return -1;
    }

    uint64_t src_addr = (uint64_t)user_src;
    if (!is_user_address(src_addr, 1)) {
        return -1;
    }

    size_t i = 0;
    while (i < max_size - 1) {
        /* 逐字节检查地址有效性 */
        if (!is_user_address(src_addr + i, 1)) {
            kernel_dst[i] = 0;
            return -1;
        }
        kernel_dst[i] = user_src[i];
        if (kernel_dst[i] == 0) break;
        i++;
    }
    kernel_dst[i] = 0;
    return (int)i;
}

/* 获取用户字符串长度（带安全检查）
 * 返回：字符串长度，-1=失败
 */
int user_strlen(const char *user_str, size_t max_size)
{
    if (!user_str || max_size == 0) return -1;

    uint64_t addr = (uint64_t)user_str;
    size_t len = 0;

    while (len < max_size) {
        if (!is_user_address(addr + len, 1)) {
            return -1;
        }
        if (user_str[len] == 0) break;
        len++;
    }

    if (len >= max_size) return -1;  /* 字符串太长 */
    return (int)len;
}

/* 验证用户缓冲区是否可读 */
int verify_user_read(const void *user_ptr, size_t size)
{
    if (!user_ptr || size == 0) return 0;
    return is_user_address((uint64_t)user_ptr, size);
}

/* 验证用户缓冲区是否可写 */
int verify_user_write(void *user_ptr, size_t size)
{
    if (!user_ptr || size == 0) return 0;
    return is_user_address((uint64_t)user_ptr, size);
}

/* 初始化用户空间访问安全模块 */
void user_access64_init(void)
{
    vga_puts("[USERACC] User space access protection initialized\n");
    vga_puts("[USERACC] User space limit: 0x00007FFFFFFFFFFF\n");
}
