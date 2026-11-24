/* ============================================================================
 * 赫普OS - 32/64 共享路径工具
 *
 * 只放与架构、VFS 内部数据结构无关的纯字符串规则：
 *   - 最后一级路径拆分（32 位层级 VFS 与 64 位挂载表 VFS 共用）
 *   - 挂载点最长前缀边界判断
 *   - Linux dirent64 reclen 计算（getdents 打包共用）
 * ========================================================================== */
#ifndef HEPU_FS_PATH_H
#define HEPU_FS_PATH_H

#include <stddef.h>

/* 把 path 拆成父目录与最后一级名字。
 * 返回值：0 成功，-1 参数非法。
 * 说明：parent 在 path 为相对路径时返回 "/"；name 超长时截断而不是越界写。 */
int fs_path_split(const char *path, char *parent, size_t parent_sz,
                  char *name, size_t name_sz);

/* 挂载点前缀匹配：prefix 必须整体匹配，且边界为串尾或 '/'。
 * 例："/dev" 匹配 "/dev"、"/dev/null"，不匹配 "/devx"。 */
int fs_path_prefix_match(const char *prefix, const char *path);

/* Linux dirent64 记录长度：19 字节固定头 + 名字，8 字节对齐。 */
int fs_dirent64_reclen(int name_len);

#endif /* HEPU_FS_PATH_H */
