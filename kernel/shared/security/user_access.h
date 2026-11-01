/* ============================================================================
 * 赫普OS - 统一源码树：用户空间访问安全接口
 * 适用于32位(x86)和64位(x86_64)架构
 *
 * 所有系统调用在访问用户指针前必须调用这些函数，
 * 防止内核访问非法用户地址导致信息泄露或内存破坏。
 * ========================================================================== */

#ifndef HEPU_SHARED_USER_ACCESS_H
#define HEPU_SHARED_USER_ACCESS_H

#include <stdint.h>
#include <stddef.h>

/* 架构相关的用户空间上限（在架构相关代码中定义）
 * 32位: 0xBFFFFFFF (用户空间3GB)
 * 64位: 0x00007FFFFFFFFFFF (规范地址下半部)
 */
extern uintptr_t hepu_user_space_limit;

/* 从用户空间复制到内核空间
 * 参数: kernel_dst - 内核目标地址
 *       user_src    - 用户源地址
 *       size        - 复制字节数
 * 返回: 0=成功, -1=失败（用户地址无效）
 */
int copy_from_user(void *kernel_dst, const void *user_src, size_t size);

/* 从内核空间复制到用户空间
 * 参数: user_dst   - 用户目标地址
 *       kernel_src - 内核源地址
 *       size       - 复制字节数
 * 返回: 0=成功, -1=失败（用户地址无效）
 */
int copy_to_user(void *user_dst, const void *kernel_src, size_t size);

/* 从用户空间复制字符串（带长度限制和终止符）
 * 参数: kernel_dst - 内核目标缓冲区
 *       user_src    - 用户源字符串
 *       max_size    - 目标缓冲区最大大小（含终止符）
 * 返回: 复制的字节数（不含终止符），-1=失败
 */
int copy_str_from_user(char *kernel_dst, const char *user_src, size_t max_size);

/* 获取用户字符串长度（带安全检查和上限）
 * 参数: user_str - 用户字符串地址
 *       max_size - 最大检查长度
 * 返回: 字符串长度，-1=失败（地址无效或超过上限）
 */
int user_strlen(const char *user_str, size_t max_size);

/* 验证用户缓冲区是否可读
 * 参数: user_ptr - 用户缓冲区地址
 *       size     - 缓冲区大小
 * 返回: 1=有效, 0=无效
 */
int verify_user_read(const void *user_ptr, size_t size);

/* 验证用户缓冲区是否可写
 * 参数: user_ptr - 用户缓冲区地址
 *       size     - 缓冲区大小
 * 返回: 1=有效, 0=无效
 */
int verify_user_write(void *user_ptr, size_t size);

/* 检查地址是否在用户空间（内部用，架构相关实现）
 * 参数: addr - 起始地址
 *       size - 区域大小
 * 返回: 1=在用户空间, 0=不在
 */
int hepu_is_user_address(uintptr_t addr, size_t size);

#endif /* HEPU_SHARED_USER_ACCESS_H */
