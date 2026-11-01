/* ============================================================================
 * 赫普OS - initrd VFS 适配层
 * 将 initrd 中的文件注册为 VFS 节点
 * ========================================================================== */

#include <stdint.h>
#include "vfs.h"
#include "initrd.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"
#include "serial.h"

/* initrd 文件的私有数据 */
typedef struct {
    uint32_t offset;  /* 文件数据在initrd中的偏移 */
} initrd_private_t;

/* initrd 文件读操作 */
static int initrd_vfs_read(vfs_node_t *node, uint32_t offset, void *buf, uint32_t size)
{
    if (!node || !node->private_data) return -1;

    initrd_private_t *priv = (initrd_private_t *)node->private_data;
    uint8_t *data = (uint8_t *)priv->offset;  /* 直接是数据指针 */

    if (offset >= node->size) return 0;
    if (offset + size > node->size) size = node->size - offset;

    memcpy(buf, data + offset, size);
    return size;
}

static vfs_file_ops_t initrd_file_ops = {
    .open = NULL,
    .close = NULL,
    .read = initrd_vfs_read,
    .write = NULL,  /* initrd只读 */
    .ioctl = NULL,
};

/* 将initrd中的文件注册到VFS */
int initrd_vfs_mount(const char *mount_path)
{
    if (!initrd_ready()) {
        vga_puts("[INITRD-VFS] initrd not load \n");
        return -1;
    }

    vfs_node_t *mount_dir;
    if (strcmp(mount_path, "/") == 0) {
        /* 挂载到根目录，直接使用根节点 */
        mount_dir = vfs_get_root();
    } else {
        /* 创建挂载点目录 */
        mount_dir = vfs_create_node(mount_path + 1, VFS_DIRECTORY);  /* 跳过开头的/ */
        if (!mount_dir) return -1;
        vfs_node_t *root = vfs_get_root();
        vfs_add_child(root, mount_dir);
    }

    /* 遍历initrd中的文件 */
    int count = 0;
    for (int i = 0; ; i++) {
        initrd_file_entry_t *entry = initrd_get_entry(i);
        if (!entry) break;

        /* 创建VFS节点 */
        vfs_node_t *file = vfs_create_node(entry->name, VFS_REGULAR);
        if (!file) continue;

        file->size = entry->size;
        file->ops = &initrd_file_ops;

        /* 私有数据: 直接存储数据指针 */
        initrd_private_t *priv = (initrd_private_t *)kmalloc(sizeof(initrd_private_t));
        if (priv) {
            priv->offset = (uint32_t)initrd_get_data(i);
            file->private_data = priv;
        }

        vfs_add_child(mount_dir, file);
        count++;
    }

    vga_printf("[INITRD-VFS] mounted %d file to %s\n", count, mount_path);
    return count;
}
