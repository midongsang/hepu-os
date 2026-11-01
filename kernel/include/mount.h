#ifndef MOUNT_H
#define MOUNT_H

#include <stdint.h>
#include "vfs.h"

#define MAX_MOUNTS 16

typedef struct {
    int used;
    char path[256];
    vfs_node_t *mount_point;
    vfs_node_t *saved_children;  /* 保存原挂载点子节点，umount时恢复 */
    void *fs_data;  /* 文件系统私有数据 */
    char fs_type[16];
} mount_entry_t;

void mount_init(void);
int mount_fs(const char *path, const char *fs_type, vfs_node_t *root, void *fs_data);
int umount_fs(const char *path);
int mount_list(mount_entry_t *entries, int max);

#endif /* MOUNT_H */
