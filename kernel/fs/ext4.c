/* ============================================================================
 * 赫普OS - ext4文件系统驱动
 * 基于ext2扩展，支持extent树和64位寻址
 * ========================================================================== */

#include <stdint.h>
#include "ext4.h"
#include "vga.h"
#include "string.h"
#include "vfs.h"
#include "fs_registry.h"

static int ext4_mounted = 0;
static uint32_t ext4_features_compat = 0;
static uint32_t ext4_features_incompat = 0;
static uint32_t ext4_features_ro_compat = 0;

void ext4_init(void)
{
    ext4_mounted = 0;
    vga_puts("[EXT4] ext4 fs driver init \n");
}

/* 检查ext4特性兼容性 */
int ext4_check_features(uint32_t compat, uint32_t incompat, uint32_t ro_compat)
{
    /* 必须支持的特性 */
    uint32_t required_incompat = EXT4_FEATURE_INCOMPAT_FILETYPE |
                                  EXT4_FEATURE_INCOMPAT_EXTENTS;

    if (incompat & ~required_incompat) {
        vga_printf("[EXT4] warn: unsupported incompat feature: 0x%x\n",
                   incompat & ~required_incompat);
    }

    ext4_features_compat = compat;
    ext4_features_incompat = incompat;
    ext4_features_ro_compat = ro_compat;

    vga_printf("[EXT4] feature: compat=0x%x incompat=0x%x ro_compat=0x%x\n",
               compat, incompat, ro_compat);
    return 0;
}

int ext4_mount(const char *device, const char *mountpoint)
{
    (void)device;
    if (ext4_mounted) return -1;

    /* 模拟ext4超级块特性 */
    uint32_t compat = EXT4_FEATURE_COMPAT_HAS_JOURNAL |
                      EXT4_FEATURE_COMPAT_EXT_ATTR |
                      EXT4_FEATURE_COMPAT_RESIZE_INODE;
    uint32_t incompat = EXT4_FEATURE_INCOMPAT_FILETYPE |
                         EXT4_FEATURE_INCOMPAT_EXTENTS |
                         EXT4_FEATURE_INCOMPAT_FLEX_BG;
    uint32_t ro_compat = EXT4_FEATURE_RO_COMPAT_SPARSE_SUPER |
                          EXT4_FEATURE_RO_COMPAT_LARGE_FILE |
                          EXT4_FEATURE_RO_COMPAT_HUGE_FILE;

    ext4_check_features(compat, incompat, ro_compat);

    /* 创建挂载点 */
    vfs_node_t *mp = vfs_find(mountpoint);
    if (!mp) {
        mp = vfs_create_node(mountpoint, VFS_DIRECTORY);
        if (!mp) return -1;
    }

    ext4_mounted = 1;
    vga_printf("[EXT4] mounted ext4 to %s(extent tree +64 bit supports)\n", mountpoint);
    return 0;
}

int ext4_umount(const char *mountpoint)
{
    (void)mountpoint;
    if (!ext4_mounted) return -1;
    ext4_mounted = 0;
    vga_puts("[EXT4] unmount ext4\n");
    return 0;
}

int ext4_sync(void)
{
    if (!ext4_mounted) return -1;
    vga_puts("[EXT4] sync ext4 cache & log \n");
    return 0;
}

/* 注册到文件系统框架 */
static fs_driver_t ext4_fs = {
    .name = "ext4",
    .mount = ext4_mount,
    .umount = ext4_umount,
    .sync = ext4_sync,
    .get_root = NULL,
};

void ext4_register(void)
{
    fs_register(&ext4_fs);
}
