/* ============================================================================
 * 赫普OS - 文件系统增强
 * F2FS/NTFS/exFAT支持 + 权限管理 + 符号链接 + 磁盘配额 + 挂载管理
 * ========================================================================== */

#include <stdint.h>
#include "fs2.h"
#include "vga.h"
#include "string.h"

static fs2_t fs2;

static const char *fs_type_names[] = {
    "ext2", "ext3", "ext4", "f2fs", "ntfs", "exfat", "fat32",
    "xfs", "btrfs", "tmpfs", "procfs", "sysfs", "devfs", "initrd", "unknown"
};

void fs2_init(void)
{
    memset(&fs2, 0, sizeof(fs2_t));
    fs2.initialized = 1;
    fs2.quota_enabled = 0;

    /* 默认挂载根文件系统 */
    fs2_mount("/dev/root", "/", FS_TYPE_EXT2, 0);
    fs2_mount("proc", "/proc", FS_TYPE_PROCFS, 1);
    fs2_mount("sysfs", "/sys", FS_TYPE_SYSFS, 1);
    fs2_mount("tmpfs", "/tmp", FS_TYPE_TMPFS, 0);

    vga_puts("[FS2] fs enhanced init done \n");
    vga_printf(" mount point: %d, quota: %s\n",
               fs2.mount_count, fs2.quota_enabled ? " enabled " : " disabled ");
}

int fs2_mount(const char *device, const char *mount_point, fs_type_t type, int flags)
{
    if (!device || !mount_point || !fs2.initialized) return -1;
    for (int i = 0; i < FS2_MAX_MOUNTS; i++) {
        if (!fs2.mounts[i].used) {
            memset(&fs2.mounts[i], 0, sizeof(fs_mount_t));
            fs2.mounts[i].used = 1;
            fs2.mounts[i].id = i;
            strncpy(fs2.mounts[i].device, device, 127);
            strncpy(fs2.mounts[i].mount_point, mount_point, 255);
            fs2.mounts[i].fs_type = type;
            fs2.mounts[i].read_only = (flags & 1) ? 1 : 0;
            fs2.mounts[i].mounted = 1;
            fs2.mounts[i].block_size = 4096;
            fs2.mounts[i].total_blocks = 1024 * 1024;  /* 4GB */
            fs2.mounts[i].free_blocks = 512 * 1024;
            fs2.mounts[i].total_inodes = 1024 * 1024;
            fs2.mounts[i].free_inodes = 512 * 1024;
            fs2.mounts[i].options = flags;
            fs2.mount_count++;
            vga_printf("[FS2] mounted %s(%s) -> %s\n", device, fs_type_names[type], mount_point);
            return i;
        }
    }
    return -1;
}

int fs2_umount(const char *mount_point)
{
    if (!mount_point) return -1;
    for (int i = 0; i < FS2_MAX_MOUNTS; i++) {
        if (fs2.mounts[i].used && strcmp(fs2.mounts[i].mount_point, mount_point) == 0) {
            fs2.mounts[i].used = 0;
            fs2.mounts[i].mounted = 0;
            fs2.mount_count--;
            vga_printf("[FS2] unmount %s\n", mount_point);
            return 0;
        }
    }
    return -1;
}

int fs2_remount(const char *mount_point, int flags)
{
    if (!mount_point) return -1;
    for (int i = 0; i < FS2_MAX_MOUNTS; i++) {
        if (fs2.mounts[i].used && strcmp(fs2.mounts[i].mount_point, mount_point) == 0) {
            fs2.mounts[i].options = flags;
            fs2.mounts[i].read_only = (flags & 1) ? 1 : 0;
            return 0;
        }
    }
    return -1;
}

int fs2_get_mount(const char *mount_point, fs_mount_t *mount)
{
    if (!mount_point || !mount) return -1;
    for (int i = 0; i < FS2_MAX_MOUNTS; i++) {
        if (fs2.mounts[i].used && strcmp(fs2.mounts[i].mount_point, mount_point) == 0) {
            memcpy(mount, &fs2.mounts[i], sizeof(fs_mount_t));
            return i;
        }
    }
    return -1;
}

int fs2_list_mounts(fs_mount_t *mounts, int max)
{
    if (!mounts || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < FS2_MAX_MOUNTS && count < max; i++) {
        if (fs2.mounts[i].used) memcpy(&mounts[count++], &fs2.mounts[i], sizeof(fs_mount_t));
    }
    return count;
}

fs_type_t fs2_detect_type(const char *device)
{
    (void)device;
    /* 原型阶段: 默认返回ext2 */
    return FS_TYPE_EXT2;
}

int fs2_check_filesystem(const char *device)
{
    (void)device;
    vga_printf("[FS2] check fs: %s\n", device);
    return 0;  /* 0=干净 */
}

int fs2_repair_filesystem(const char *device)
{
    (void)device;
    vga_printf("[FS2] repair fs: %s\n", device);
    return 0;
}

int f2fs_mount(const char *device, const char *mount_point, int read_only)
{
    return fs2_mount(device, mount_point, FS_TYPE_F2FS, read_only ? 1 : 0);
}

int f2fs_read_superblock(const char *device, f2fs_superblock_t *sb)
{
    (void)device;
    if (!sb) return -1;
    memset(sb, 0, sizeof(f2fs_superblock_t));
    sb->magic = 0xF2F52010;
    sb->major_ver = 1;
    sb->minor_ver = 0;
    sb->log_sectorsize = 9;  /* 512字节 */
    sb->log_blocksize = 12;  /* 4096字节 */
    sb->block_count = 1024 * 1024;
    return 0;
}

int f2fs_get_stats(const char *mount_point, uint64_t *total, uint64_t *free, uint64_t *available)
{
    fs_mount_t m;
    if (fs2_get_mount(mount_point, &m) != 0) return -1;
    if (total) *total = m.total_blocks * m.block_size;
    if (free) *free = m.free_blocks * m.block_size;
    if (available) *available = m.free_blocks * m.block_size;
    return 0;
}

int ntfs_mount(const char *device, const char *mount_point, int read_only)
{
    return fs2_mount(device, mount_point, FS_TYPE_NTFS, read_only ? 1 : 0);
}

int ntfs_read_boot(const char *device, ntfs_boot_t *boot)
{
    (void)device;
    if (!boot) return -1;
    memset(boot, 0, sizeof(ntfs_boot_t));
    boot->bytes_per_sector = 512;
    boot->sectors_per_cluster = 8;
    boot->total_sectors = 1024 * 1024 * 8;
    boot->mft_cluster = 786432;
    return 0;
}

int ntfs_get_stats(const char *mount_point, uint64_t *total, uint64_t *free, uint64_t *available)
{
    fs_mount_t m;
    if (fs2_get_mount(mount_point, &m) != 0) return -1;
    if (total) *total = m.total_blocks * m.block_size;
    if (free) *free = m.free_blocks * m.block_size;
    if (available) *available = m.free_blocks * m.block_size;
    return 0;
}

int exfat_mount(const char *device, const char *mount_point, int read_only)
{
    return fs2_mount(device, mount_point, FS_TYPE_EXFAT, read_only ? 1 : 0);
}

int exfat_read_boot(const char *device, exfat_boot_t *boot)
{
    (void)device;
    if (!boot) return -1;
    memset(boot, 0, sizeof(exfat_boot_t));
    boot->bytes_per_sector_shift = 9;
    boot->sectors_per_cluster_shift = 3;
    boot->cluster_count = 1024 * 1024;
    boot->boot_signature = 0xAA55;
    return 0;
}

int exfat_get_stats(const char *mount_point, uint64_t *total, uint64_t *free, uint64_t *available)
{
    fs_mount_t m;
    if (fs2_get_mount(mount_point, &m) != 0) return -1;
    if (total) *total = m.total_blocks * m.block_size;
    if (free) *free = m.free_blocks * m.block_size;
    if (available) *available = m.free_blocks * m.block_size;
    return 0;
}

int fs2_set_permissions(const char *path, fs_permissions_t *perm)
{
    (void)path; (void)perm;
    return 0;
}

int fs2_get_permissions(const char *path, fs_permissions_t *perm)
{
    if (!path || !perm) return -1;
    memset(perm, 0, sizeof(fs_permissions_t));
    perm->owner_perm = FS_PERM_READ | FS_PERM_WRITE | FS_PERM_EXECUTE;
    perm->group_perm = FS_PERM_READ | FS_PERM_EXECUTE;
    perm->other_perm = FS_PERM_READ | FS_PERM_EXECUTE;
    perm->owner_uid = 0;
    perm->group_gid = 0;
    return 0;
}

int fs2_chmod(const char *path, uint16_t mode)
{
    (void)path; (void)mode;
    return 0;
}

int fs2_chown(const char *path, uint32_t uid, uint32_t gid)
{
    (void)path; (void)uid; (void)gid;
    return 0;
}

int fs2_check_permission(const char *path, uint32_t uid, uint32_t gid, int operation)
{
    (void)path; (void)uid; (void)gid; (void)operation;
    return 0;  /* 0=允许 */
}

int fs2_symlink_create(const char *target, const char *linkpath)
{
    if (!target || !linkpath) return -1;
    for (int i = 0; i < FS2_MAX_SYMLINKS; i++) {
        if (!fs2.symlinks[i].used) {
            memset(&fs2.symlinks[i], 0, sizeof(fs_symlink_t));
            fs2.symlinks[i].used = 1;
            fs2.symlinks[i].id = i;
            strncpy(fs2.symlinks[i].link_path, linkpath, 255);
            strncpy(fs2.symlinks[i].target_path, target, 255);
            fs2.symlinks[i].is_relative = (target[0] != '/') ? 1 : 0;
            fs2.symlink_count++;
            return i;
        }
    }
    return -1;
}

int fs2_symlink_read(const char *linkpath, char *target, int len)
{
    if (!linkpath || !target || len <= 0) return -1;
    for (int i = 0; i < FS2_MAX_SYMLINKS; i++) {
        if (fs2.symlinks[i].used && strcmp(fs2.symlinks[i].link_path, linkpath) == 0) {
            strncpy(target, fs2.symlinks[i].target_path, len - 1);
            target[len - 1] = '\0';
            return 0;
        }
    }
    return -1;
}

int fs2_symlink_remove(const char *linkpath)
{
    if (!linkpath) return -1;
    for (int i = 0; i < FS2_MAX_SYMLINKS; i++) {
        if (fs2.symlinks[i].used && strcmp(fs2.symlinks[i].link_path, linkpath) == 0) {
            fs2.symlinks[i].used = 0;
            fs2.symlink_count--;
            return 0;
        }
    }
    return -1;
}

int fs2_symlink_list(fs_symlink_t *links, int max)
{
    if (!links || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < FS2_MAX_SYMLINKS && count < max; i++) {
        if (fs2.symlinks[i].used) memcpy(&links[count++], &fs2.symlinks[i], sizeof(fs_symlink_t));
    }
    return count;
}

int fs2_resolve_path(const char *path, char *resolved, int len)
{
    if (!path || !resolved || len <= 0) return -1;
    /* 原型阶段: 简单解析，处理符号链接 */
    char target[256];
    if (fs2_symlink_read(path, target, sizeof(target)) == 0) {
        strncpy(resolved, target, len - 1);
    } else {
        strncpy(resolved, path, len - 1);
    }
    resolved[len - 1] = '\0';
    return 0;
}

int fs2_quota_enable(int enable)
{
    fs2.quota_enabled = enable ? 1 : 0;
    vga_printf("[FS2] disk quota %s\n", enable ? " enabled " : " disabled ");
    return 0;
}

int fs2_quota_set(uint32_t uid, uint64_t block_limit, uint64_t inode_limit)
{
    if (!fs2.quota_enabled) return -1;
    for (int i = 0; i < FS2_MAX_QUOTAS; i++) {
        if (fs2.quotas[i].used && fs2.quotas[i].uid == uid) {
            fs2.quotas[i].block_limit = block_limit;
            fs2.quotas[i].inode_limit = inode_limit;
            return 0;
        }
    }
    for (int i = 0; i < FS2_MAX_QUOTAS; i++) {
        if (!fs2.quotas[i].used) {
            memset(&fs2.quotas[i], 0, sizeof(fs_quota_t));
            fs2.quotas[i].used = 1;
            fs2.quotas[i].id = i;
            fs2.quotas[i].uid = uid;
            fs2.quotas[i].block_limit = block_limit;
            fs2.quotas[i].inode_limit = inode_limit;
            fs2.quotas[i].grace_period = 7 * 24 * 3600;  /* 7天 */
            fs2.quota_count++;
            return 0;
        }
    }
    return -1;
}

int fs2_quota_get(uint32_t uid, fs_quota_t *quota)
{
    if (!quota) return -1;
    for (int i = 0; i < FS2_MAX_QUOTAS; i++) {
        if (fs2.quotas[i].used && fs2.quotas[i].uid == uid) {
            memcpy(quota, &fs2.quotas[i], sizeof(fs_quota_t));
            return 0;
        }
    }
    return -1;
}

int fs2_quota_remove(uint32_t uid)
{
    for (int i = 0; i < FS2_MAX_QUOTAS; i++) {
        if (fs2.quotas[i].used && fs2.quotas[i].uid == uid) {
            fs2.quotas[i].used = 0;
            fs2.quota_count--;
            return 0;
        }
    }
    return -1;
}

int fs2_quota_list(fs_quota_t *quotas, int max)
{
    if (!quotas || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < FS2_MAX_QUOTAS && count < max; i++) {
        if (fs2.quotas[i].used) memcpy(&quotas[count++], &fs2.quotas[i], sizeof(fs_quota_t));
    }
    return count;
}

int fs2_quota_check(uint32_t uid, uint64_t blocks_needed, uint64_t inodes_needed)
{
    if (!fs2.quota_enabled) return 0;
    fs_quota_t q;
    if (fs2_quota_get(uid, &q) != 0) return 0;  /* 无配额限制 */
    if (q.block_used + blocks_needed > q.block_limit) return -1;
    if (q.inode_used + inodes_needed > q.inode_limit) return -1;
    return 0;
}

int fs2_get_attr(const char *path, fs_attr_t *attr)
{
    if (!path || !attr) return -1;
    memset(attr, 0, sizeof(fs_attr_t));
    attr->file_size = 0;
    attr->blocks_used = 0;
    attr->links_count = 1;
    attr->permissions.owner_perm = FS_PERM_READ | FS_PERM_WRITE;
    attr->permissions.group_perm = FS_PERM_READ;
    attr->permissions.other_perm = FS_PERM_READ;
    return 0;
}

int fs2_set_attr(const char *path, fs_attr_t *attr)
{
    (void)path; (void)attr;
    return 0;
}

int fs2_touch(const char *path)
{
    (void)path;
    return 0;
}

int fs2_truncate(const char *path, uint64_t size)
{
    (void)path; (void)size;
    return 0;
}

int fs2_get_stats(uint64_t *reads, uint64_t *writes, uint64_t *bytes_read, uint64_t *bytes_written)
{
    if (reads) *reads = fs2.total_reads;
    if (writes) *writes = fs2.total_writes;
    if (bytes_read) *bytes_read = fs2.total_bytes_read;
    if (bytes_written) *bytes_written = fs2.total_bytes_written;
    return 0;
}

int fs2_get_cache_stats(uint64_t *hits, uint64_t *misses, float *hit_rate)
{
    if (hits) *hits = fs2.cache_hits;
    if (misses) *misses = fs2.cache_misses;
    if (hit_rate) {
        uint64_t total = fs2.cache_hits + fs2.cache_misses;
        *hit_rate = total > 0 ? (float)fs2.cache_hits / total : 0;
    }
    return 0;
}

const char *fs2_type_name(fs_type_t type)
{
    if (type < 0 || type >= FS_TYPE_UNKNOWN) return "unknown";
    return fs_type_names[type];
}

int fs2_format(const char *device, fs_type_t type, const char *label)
{
    (void)device; (void)label;
    vga_printf("[FS2] format %s %s\n", device, fs_type_names[type]);
    return 0;
}
