#ifndef FS2_H
#define FS2_H

#include <stdint.h>

#define FS2_MAX_MOUNTS 16
#define FS2_MAX_FILES 256
#define FS2_MAX_SYMLINKS 128
#define FS2_MAX_QUOTAS 64

/* 文件系统类型 */
typedef enum {
    FS_TYPE_EXT2 = 0,
    FS_TYPE_EXT3,
    FS_TYPE_EXT4,
    FS_TYPE_F2FS,
    FS_TYPE_NTFS,
    FS_TYPE_EXFAT,
    FS_TYPE_FAT32,
    FS_TYPE_XFS,
    FS_TYPE_BTRFS,
    FS_TYPE_TMPFS,
    FS_TYPE_PROCFS,
    FS_TYPE_SYSFS,
    FS_TYPE_DEVFS,
    FS_TYPE_INITRD,
    FS_TYPE_UNKNOWN,
} fs_type_t;

/* 文件权限 */
#define FS_PERM_READ    0x01
#define FS_PERM_WRITE   0x02
#define FS_PERM_EXECUTE 0x04
#define FS_PERM_SETUID  0x08
#define FS_PERM_SETGID  0x10
#define FS_PERM_STICKY  0x20

/* 文件模式 */
typedef struct {
    uint16_t owner_perm;   /* 所有者权限 */
    uint16_t group_perm;   /* 组权限 */
    uint16_t other_perm;   /* 其他权限 */
    uint32_t owner_uid;
    uint32_t group_gid;
} fs_permissions_t;

/* 符号链接 */
typedef struct {
    int used;
    int id;
    char link_path[256];
    char target_path[256];
    int is_relative;
    uint64_t create_time;
    uint64_t last_access;
} fs_symlink_t;

/* 磁盘配额 */
typedef struct {
    int used;
    int id;
    uint32_t uid;
    uint64_t block_limit;     /* 块限制 (硬限制) */
    uint64_t block_soft;      /* 软限制 */
    uint64_t block_used;
    uint64_t inode_limit;
    uint64_t inode_soft;
    uint64_t inode_used;
    uint64_t grace_period;    /* 宽限期 (秒) */
    int grace_active;
} fs_quota_t;

/* 挂载点 */
typedef struct {
    int used;
    int id;
    char device[128];
    char mount_point[256];
    fs_type_t fs_type;
    int read_only;
    int mounted;
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint64_t total_inodes;
    uint64_t free_inodes;
    uint32_t block_size;
    int options;  /* 位掩码: 1=ro,2=noexec,4=nosuid,8=nodev,16=sync */
    uint64_t mount_time;
} fs_mount_t;

/* 文件属性扩展 */
typedef struct {
    uint64_t create_time;
    uint64_t access_time;
    uint64_t modify_time;
    uint64_t change_time;
    uint64_t file_size;
    uint64_t blocks_used;
    uint32_t links_count;
    uint32_t flags;  /* 1=compressed,2=encrypted,4=immutable,8=append-only */
    fs_permissions_t permissions;
} fs_attr_t;

/* F2FS 超级块 */
typedef struct {
    uint32_t magic;           /* 0xF2F52010 */
    uint16_t major_ver;
    uint16_t minor_ver;
    uint32_t log_sectorsize;
    uint32_t log_sectors_per_block;
    uint32_t log_blocksize;
    uint32_t log_blocks_per_seg;
    uint32_t segs_per_sec;
    uint32_t secs_per_zone;
    uint32_t checksum_offset;
    uint64_t block_count;
    uint8_t uuid[16];
    char volume_name[512];
    uint32_t extension_count;
} f2fs_superblock_t;

/* NTFS 引导扇区 */
typedef struct {
    uint8_t jump[3];
    char oem_id[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t zeros[3];
    uint16_t unused1;
    uint8_t media_descriptor;
    uint16_t unused2;
    uint16_t sectors_per_track;
    uint16_t number_of_heads;
    uint32_t hidden_sectors;
    uint32_t unused3;
    uint32_t unused4;
    uint64_t total_sectors;
    uint64_t mft_cluster;
    uint64_t mft_mirror_cluster;
    int8_t clusters_per_mft_record;
    uint8_t unused5[3];
    int8_t clusters_per_index;
    uint8_t unused6[3];
    uint64_t volume_serial;
    uint32_t checksum;
} ntfs_boot_t;

/* exFAT 引导扇区 */
typedef struct {
    uint8_t jump[3];
    char oem_id[8];
    uint8_t reserved[53];
    uint64_t partition_offset;
    uint64_t volume_length;
    uint32_t fat_offset;
    uint32_t fat_length;
    uint32_t cluster_heap_offset;
    uint32_t cluster_count;
    uint32_t root_cluster;
    uint32_t volume_serial;
    uint16_t fs_revision;
    uint16_t volume_flags;
    uint8_t bytes_per_sector_shift;
    uint8_t sectors_per_cluster_shift;
    uint8_t number_of_fats;
    uint8_t drive_select;
    uint8_t percent_in_use;
    uint8_t reserved2[7];
    uint8_t boot_code[390];
    uint16_t boot_signature;
} exfat_boot_t;

/* 文件系统增强状态 */
typedef struct {
    int initialized;
    fs_mount_t mounts[FS2_MAX_MOUNTS];
    int mount_count;
    fs_symlink_t symlinks[FS2_MAX_SYMLINKS];
    int symlink_count;
    fs_quota_t quotas[FS2_MAX_QUOTAS];
    int quota_count;
    int quota_enabled;
    /* 统计 */
    uint64_t total_reads;
    uint64_t total_writes;
    uint64_t total_bytes_read;
    uint64_t total_bytes_written;
    uint64_t cache_hits;
    uint64_t cache_misses;
} fs2_t;

/* 初始化文件系统增强 */
void fs2_init(void);

/* 挂载管理 */
int fs2_mount(const char *device, const char *mount_point, fs_type_t type, int flags);
int fs2_umount(const char *mount_point);
int fs2_remount(const char *mount_point, int flags);
int fs2_get_mount(const char *mount_point, fs_mount_t *mount);
int fs2_list_mounts(fs_mount_t *mounts, int max);

/* 文件系统检测 */
fs_type_t fs2_detect_type(const char *device);
int fs2_check_filesystem(const char *device);
int fs2_repair_filesystem(const char *device);

/* F2FS */
int f2fs_mount(const char *device, const char *mount_point, int read_only);
int f2fs_read_superblock(const char *device, f2fs_superblock_t *sb);
int f2fs_get_stats(const char *mount_point, uint64_t *total, uint64_t *free, uint64_t *available);

/* NTFS */
int ntfs_mount(const char *device, const char *mount_point, int read_only);
int ntfs_read_boot(const char *device, ntfs_boot_t *boot);
int ntfs_get_stats(const char *mount_point, uint64_t *total, uint64_t *free, uint64_t *available);

/* exFAT */
int exfat_mount(const char *device, const char *mount_point, int read_only);
int exfat_read_boot(const char *device, exfat_boot_t *boot);
int exfat_get_stats(const char *mount_point, uint64_t *total, uint64_t *free, uint64_t *available);

/* 权限管理 */
int fs2_set_permissions(const char *path, fs_permissions_t *perm);
int fs2_get_permissions(const char *path, fs_permissions_t *perm);
int fs2_chmod(const char *path, uint16_t mode);
int fs2_chown(const char *path, uint32_t uid, uint32_t gid);
int fs2_check_permission(const char *path, uint32_t uid, uint32_t gid, int operation);

/* 符号链接 */
int fs2_symlink_create(const char *target, const char *linkpath);
int fs2_symlink_read(const char *linkpath, char *target, int len);
int fs2_symlink_remove(const char *linkpath);
int fs2_symlink_list(fs_symlink_t *links, int max);
int fs2_resolve_path(const char *path, char *resolved, int len);

/* 磁盘配额 */
int fs2_quota_enable(int enable);
int fs2_quota_set(uint32_t uid, uint64_t block_limit, uint64_t inode_limit);
int fs2_quota_get(uint32_t uid, fs_quota_t *quota);
int fs2_quota_remove(uint32_t uid);
int fs2_quota_list(fs_quota_t *quotas, int max);
int fs2_quota_check(uint32_t uid, uint64_t blocks_needed, uint64_t inodes_needed);

/* 文件属性 */
int fs2_get_attr(const char *path, fs_attr_t *attr);
int fs2_set_attr(const char *path, fs_attr_t *attr);
int fs2_touch(const char *path);
int fs2_truncate(const char *path, uint64_t size);

/* 统计 */
int fs2_get_stats(uint64_t *reads, uint64_t *writes, uint64_t *bytes_read, uint64_t *bytes_written);
int fs2_get_cache_stats(uint64_t *hits, uint64_t *misses, float *hit_rate);

/* 辅助 */
const char *fs2_type_name(fs_type_t type);
int fs2_format(const char *device, fs_type_t type, const char *label);

#endif /* FS2_H */
