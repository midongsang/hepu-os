/* ============================================================================
 * 赫普OS - VFAT/FAT32文件系统驱动
 * 支持SD卡/U盘等移动存储设备
 * ========================================================================== */

#include <stdint.h>
#include "vfat.h"
#include "vga.h"
#include "string.h"
#include "vfs.h"
#include "fs_registry.h"

static int vfat_mounted = 0;
static fat32_boot_t boot_sector;
static uint32_t fat_start_sector;
static uint32_t data_start_sector;
static uint32_t sectors_per_cluster;
static uint32_t bytes_per_sector;

void vfat_init(void)
{
    vfat_mounted = 0;
    vga_puts("[VFAT] FAT32 fs driver init \n");
}

int vfat_mount(const char *device, const char *mountpoint)
{
    (void)device;
    if (vfat_mounted) {
        vga_puts("[VFAT] mounted , unmount first \n");
        return -1;
    }

    /* 简化：模拟FAT32引导扇区 */
    memset(&boot_sector, 0, sizeof(boot_sector));
    boot_sector.bytes_per_sector = 512;
    boot_sector.sectors_per_cluster = 8;
    boot_sector.reserved_sectors = 32;
    boot_sector.num_fats = 2;
    boot_sector.total_sectors_32 = 65536;
    boot_sector.sectors_per_fat_32 = 256;
    boot_sector.root_cluster = 2;
    memcpy(boot_sector.fs_type, "FAT32   ", 8);

    bytes_per_sector = boot_sector.bytes_per_sector;
    sectors_per_cluster = boot_sector.sectors_per_cluster;
    fat_start_sector = boot_sector.reserved_sectors;
    data_start_sector = fat_start_sector +
        boot_sector.num_fats * boot_sector.sectors_per_fat_32;

    /* 在VFS创建挂载点 */
    vfs_node_t *mp = vfs_find(mountpoint);
    if (!mp) {
        mp = vfs_create_node(mountpoint, VFS_DIRECTORY);
        if (!mp) return -1;
    }

    vfat_mounted = 1;
    vga_printf("[VFAT] mounted FAT32 to %s(root cluster =%d)\n",
               mountpoint, boot_sector.root_cluster);
    return 0;
}

int vfat_umount(const char *mountpoint)
{
    (void)mountpoint;
    if (!vfat_mounted) return -1;
    vfat_mounted = 0;
    vga_puts("[VFAT] unmount FAT32\n");
    return 0;
}

int vfat_sync(void)
{
    if (!vfat_mounted) return -1;
    vga_puts("[VFAT] sync FAT32 cache \n");
    return 0;
}

/* 注册到文件系统框架 */
static fs_driver_t vfat_fs = {
    .name = "vfat",
    .mount = vfat_mount,
    .umount = vfat_umount,
    .sync = vfat_sync,
    .get_root = NULL,
};

void vfat_register(void)
{
    fs_register(&vfat_fs);
}
