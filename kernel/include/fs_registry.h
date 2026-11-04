#ifndef FS_REGISTRY_H
#define FS_REGISTRY_H

#include <stdint.h>
#include "vfs.h"

#define MAX_FILESYSTEMS 8

/* 文件系统操作接口 */
typedef struct {
    const char *name;
    int (*mount)(const char *device, const char *mountpoint);
    int (*umount)(const char *mountpoint);
    int (*sync)(void);
    vfs_node_t *(*get_root)(void);
} fs_driver_t;

void fs_registry_init(void);
int fs_register(fs_driver_t *fs);
fs_driver_t *fs_find(const char *name);
int fs_list(fs_driver_t **list, int max);
int fs_mount(const char *fs_name, const char *device, const char *mountpoint);

#endif /* FS_REGISTRY_H */
