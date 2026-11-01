/* ============================================================================
 * 赫普OS - 文件系统挂载管理
 * ========================================================================== */

#include <stdint.h>
#include "mount.h"
#include "vga.h"
#include "string.h"

static mount_entry_t mounts[MAX_MOUNTS];
static int mount_initialized = 0;

void mount_init(void)
{
    memset(mounts, 0, sizeof(mounts));
    mount_initialized = 1;
    vga_puts("[MOUNT] mount mgr init \n");
}

int mount_fs(const char *path, const char *fs_type, vfs_node_t *root, void *fs_data)
{
    if (!mount_initialized || !path || !fs_type || !root) return -1;

    /* 找空闲挂载项 */
    mount_entry_t *entry = NULL;
    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (!mounts[i].used) {
            entry = &mounts[i];
            break;
        }
    }
    if (!entry) {
        vga_puts("[MOUNT] mount table full \n");
        return -1;
    }

    /* 检查挂载点是否存在 */
    vfs_node_t *mp = vfs_find(path);
    if (!mp) {
        vga_printf("[MOUNT] mount point missing: %s\n", path);
        return -1;
    }

    /* 保存原挂载点状态 */
    entry->used = 1;
    strncpy(entry->path, path, 255);
    entry->path[255] = 0;
    strncpy(entry->fs_type, fs_type, 15);
    entry->fs_type[15] = 0;
    entry->mount_point = mp;
    entry->saved_children = mp->children;
    entry->fs_data = fs_data;

    /* 执行挂载 */
    mp->children = root->children;
    mp->ops = root->ops;
    mp->private_data = root->private_data;
    mp->size = root->size;

    /* 更新子节点parent */
    vfs_node_t *child = mp->children;
    while (child) {
        child->parent = mp;
        child = child->next;
    }

    vga_printf("[MOUNT] mounted %s to %s\n", fs_type, path);
    return 0;
}

int umount_fs(const char *path)
{
    if (!mount_initialized || !path) return -1;

    for (int i = 0; i < MAX_MOUNTS; i++) {
        if (mounts[i].used && strcmp(mounts[i].path, path) == 0) {
            vfs_node_t *mp = mounts[i].mount_point;

            /* 恢复原挂载点状态 */
            mp->children = mounts[i].saved_children;

            /* 清除挂载项 */
            mounts[i].used = 0;

            vga_printf("[MOUNT] unmount %s\n", path);
            return 0;
        }
    }

    vga_printf("[MOUNT] mount point not found: %s\n", path);
    return -1;
}

int mount_list(mount_entry_t *entries, int max)
{
    if (!entries || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < MAX_MOUNTS && count < max; i++) {
        if (mounts[i].used) {
            entries[count++] = mounts[i];
        }
    }
    return count;
}
