/* ============================================================================
 * 赫普OS 64位 - devfs 设备文件系统（最小实现）
 *
 * 64 位主线无 VFS 抽象，故 devfs64 不依赖 vfs_node_t，
 * 直接提供设备节点注册表（name → read/write ops）。
 * 这是 CAP_TYPE_DEVICE 能力的文件表示落点，也是 HAL 前置。
 *
 * 预置节点：/dev/null, /dev/zero, /dev/console
 * ========================================================================== */

#ifndef HEPU_DEVFS64_H
#define HEPU_DEVFS64_H

#include <stdint.h>
#include <stddef.h>

#define DEVFS64_MAX_NODES  32
#define DEVFS_NAME_LEN     32

/* 设备节点操作 */
typedef struct {
    int  (*read)(uint32_t offset, void *buf, uint32_t size);
    int  (*write)(uint32_t offset, const void *buf, uint32_t size);
} devfs_ops_t;

/* 设备节点 */
typedef struct {
    char        name[DEVFS_NAME_LEN];
    devfs_ops_t ops;
    uint64_t    size;       /* 设备大小（0 表示无界） */
    int         active;
    int         capability_id;  /* 关联的能力 ID（CAP_TYPE_DEVICE），-1 表示无 */
} devfs_node_t;

/* 初始化 devfs，注册预置节点 */
void devfs64_init(void);

/* 注册设备节点，返回索引；失败返回 -1 */
int devfs64_register(const char *name, devfs_ops_t ops, uint64_t size);

/* 按名称查找节点，返回索引；失败返回 -1 */
int devfs64_find(const char *name);

/* 读设备 */
int devfs64_read(int node_idx, uint32_t offset, void *buf, uint32_t size);

/* 写设备 */
int devfs64_write(int node_idx, uint32_t offset, const void *buf, uint32_t size);

/* 列出所有节点，返回数量 */
int devfs64_list(devfs_node_t *out, int max);

#endif /* HEPU_DEVFS64_H */
