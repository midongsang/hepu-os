#ifndef FS3_H
#define FS3_H

#include <stdint.h>

#define FS3_MAX_FILESYSTEMS 32  /* V6.76: 从16扩展到32 */
#define FS3_MAX_EXTENTS 64
#define FS3_MAX_XATTRS 32
#define FS3_MAX_ACES 16
#define FS3_MAX_SNAPSHOTS 32
#define FS3_MAX_SUBVOLUMES 16
#define FS3_MAX_DATASETS 16

/* 文件系统类型 */
typedef enum {
    FS3_TYPE_EXT2 = 0,
    FS3_TYPE_EXT3,
    FS3_TYPE_EXT4,
    FS3_TYPE_XFS,
    FS3_TYPE_BTRFS,
    FS3_TYPE_ZFS,
    FS3_TYPE_F2FS,
    FS3_TYPE_NILFS2,
    FS3_TYPE_NFS,
    FS3_TYPE_CIFS,
    FS3_TYPE_TMPFS,
    FS3_TYPE_PROCFS,
    FS3_TYPE_SYSFS,
    FS3_TYPE_DEVTMPFS,
} fs3_type_t;

/* ext4特性 */
typedef struct {
    int has_journal;
    int extents;
    int huge_file;
    int dir_index;
    int dir_nlink;
    int extra_isize;
    int flex_bg;
    int metadata_csum;
    int feature_64bit;
    int inline_data;
    int encrypt;
    int project;
    int metadata_csum_seed;
    int lazy_itable_init;
    int orphan_file;
    int fast_commit;
} ext4_features_t;

/* ext4 extent */
typedef struct {
    uint32_t block;
    uint32_t start;
    uint16_t len;
    uint16_t init_len;
    int flags;
} ext4_extent_t;

/* ext4 inode */
typedef struct {
    uint32_t inode_no;
    uint16_t mode;
    uint16_t uid;
    uint32_t size;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t dtime;
    uint16_t gid;
    uint16_t links_count;
    uint32_t blocks;
    uint32_t flags;
    uint32_t generation;
    uint32_t file_acl;
    uint32_t dir_acl;
    uint32_t fragment_addr;
    uint16_t fragment_index;
    ext4_extent_t extents[FS3_MAX_EXTENTS];
    int extent_count;
} ext4_inode_t;

/* ext4超级块 */
typedef struct {
    uint32_t inodes_count;
    uint32_t blocks_count;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint32_t first_data_block;
    uint32_t block_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t magic;  /* 0xEF53 */
    uint16_t state;
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;
    ext4_features_t features;
    uint32_t block_group_count;
    uint32_t journal_inode;
    uint16_t desc_size;
    uint32_t default_mount_opts;
    uint32_t first_meta_bg;
    uint32_t mkfs_time;
    uint16_t min_extra_isize;
    uint16_t want_extra_isize;
    uint32_t checksum;
} ext4_superblock_t;

/* F2FS信息 */
typedef struct {
    uint32_t magic;  /* 0xF2F52010 */
    uint32_t version;
    uint32_t block_size;
    uint32_t segment_size;
    uint32_t section_size;
    uint32_t zone_size;
    uint32_t total_segments;
    uint32_t valid_blocks;
    uint32_t free_segments;
    uint32_t node_count;
    uint32_t meta_count;
    int gc_enabled;
    int gc_threshold;
    int write_mode;  /* 0=normal,1=log,2=node,3=attr */
    int fsync_mode;
    int checkpoint_interval;
} f2fs_info_t;

/* Btrfs信息 */
typedef struct {
    uint8_t magic[8];  /* "_BHRfS_M" */
    uint32_t version;
    uint32_t generation;
    uint64_t total_bytes;
    uint64_t bytes_used;
    uint32_t sectorsize;
    uint32_t nodesize;
    uint32_t leafsize;
    uint32_t stripesize;
    int checksum_type;  /* 0=CRC32C,1=XXHASH,2=SHA256,3=BLAKE2 */
    int compression;  /* 0=none,1=zlib,2=lzo,3=zstd */
    int compression_level;
    int subvolumes[FS3_MAX_SUBVOLUMES];
    int subvolume_count;
    int snapshots[FS3_MAX_SNAPSHOTS];
    int snapshot_count;
    int quota_enabled;
    int send_enabled;
    int receive_enabled;
} btrfs_info_t;

/* ZFS数据集 */
typedef struct {
    int used;
    int id;
    char name[128];
    uint64_t used_bytes;
    uint64_t available_bytes;
    uint64_t referenced_bytes;
    int compression;  /* 0=off,1=lz4,2=gzip,3=zstd */
    int dedup;  /* 0=off,1=on,2=verify */
    int checksum;  /* 0=on,1=fletcher4,2=sha256,3=skein,4=edonr */
    int copies;  /* 1/2/3 */
    int readonly;
    int snapshot_count;
    char mountpoint[256];
} zfs_dataset_t;

/* ZFS信息 */
typedef struct {
    uint64_t version;
    uint64_t pool_guid;
    uint64_t state;  /* 0=exported,1=exported,2=active,3=destroyed,4=spare,5=l2cache */
    uint64_t total_bytes;
    uint64_t allocated_bytes;
    uint64_t free_bytes;
    uint64_t expansion_size;
    int ashift;
    int feature_count;
    char features[16][64];
    zfs_dataset_t datasets[FS3_MAX_DATASETS];
    int dataset_count;
    int raidz_level;  /* 0=none,1=raidz1,2=raidz2,3=raidz3 */
    int mirror_count;
} zfs_info_t;

/* NFS信息 */
typedef struct {
    char server[128];
    char export_path[256];
    int version;  /* 3/4/4.1/4.2 */
    int port;
    int protocol;  /* 0=tcp,1=udp */
    int rsize;
    int wsize;
    int readahead;
    int acregmin;
    int acregmax;
    int acdirmin;
    int acdirmax;
    int hard;  /* 0=soft,1=hard */
    int intr;
    int sec;  /* 0=sys,1=krb5,2=krb5i,3=krb5p */
    int connected;
    uint64_t read_bytes;
    uint64_t write_bytes;
} nfs_info_t;

/* 文件系统挂载信息 */
typedef struct {
    int used;
    int id;
    char device[128];
    char mount_point[256];
    fs3_type_t type;
    int read_only;
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint64_t used_bytes;
    uint64_t total_inodes;
    uint64_t free_inodes;
    int block_size;
    int mounted;
    /* 特定文件系统信息 */
    union {
        ext4_superblock_t ext4;
        f2fs_info_t f2fs;
        btrfs_info_t btrfs;
        zfs_info_t zfs;
        nfs_info_t nfs;
    } fs_info;
} filesystem_t;

/* 扩展属性 */
typedef struct {
    char name[128];
    char value[256];
    int size;
    int namespace;  /* 0=user,1=trusted,2=security,3=system */
} xattr_t;

/* ACL条目 */
typedef struct {
    int type;  /* 0=user,1=group,2=mask,3=other */
    int id;
    int permissions;  /* r=4,w=2,x=1 */
} ace_t;

/* 文件系统深化状态 */
typedef struct {
    int initialized;
    filesystem_t filesystems[FS3_MAX_FILESYSTEMS];
    int fs_count;
    /* 统计 */
    uint64_t total_reads;
    uint64_t total_writes;
    uint64_t total_read_bytes;
    uint64_t total_write_bytes;
    uint64_t metadata_ops;
    int cache_hits;
    int cache_misses;
} fs3_t;

/* 初始化文件系统深化 */
void fs3_init(void);

/* 文件系统管理 */
int fs3_mount(const char *device, const char *mount_point, fs3_type_t type, int read_only);
int fs3_umount(int fs_id);
int fs3_get_filesystem(int fs_id, filesystem_t *fs);
int fs3_list_filesystems(filesystem_t *fss, int max);
int fs3_get_by_mountpoint(const char *mount_point, filesystem_t *fs);
int fs3_remount(int fs_id, int read_only);

/* ext4 */
int fs3_ext4_create(const char *device, uint64_t size, int block_size, int has_journal);
int fs3_ext4_check(const char *device);
int fs3_ext4_resize(int fs_id, uint64_t new_size);
int fs3_ext4_get_superblock(int fs_id, ext4_superblock_t *sb);
int fs3_ext4_get_inode(int fs_id, uint32_t inode_no, ext4_inode_t *inode);
int fs3_ext4_defrag(int fs_id);
int fs3_ext4_fsck(int fs_id);

/* F2FS */
int fs3_f2fs_create(const char *device, uint64_t size, int segment_size);
int fs3_f2fs_gc(int fs_id, int full);
int fs3_f2fs_get_info(int fs_id, f2fs_info_t *info);
int fs3_f2fs_set_gc_threshold(int fs_id, int threshold);

/* Btrfs */
int fs3_btrfs_create(const char *device, uint64_t size, int compression, int checksum);
int fs3_btrfs_subvolume_create(int fs_id, const char *name);
int fs3_btrfs_subvolume_delete(int fs_id, int subvol_id);
int fs3_btrfs_snapshot_create(int fs_id, int subvol_id, const char *name);
int fs3_btrfs_snapshot_delete(int fs_id, int snap_id);
int fs3_btrfs_balance(int fs_id);
int fs3_btrfs_scrub(int fs_id);
int fs3_btrfs_get_info(int fs_id, btrfs_info_t *info);

/* ZFS */
int fs3_zfs_create_pool(const char *name, const char **devices, int count, int raidz_level);
int fs3_zfs_destroy_pool(int pool_id);
int fs3_zfs_create_dataset(int pool_id, const char *name, int compression, int dedup);
int fs3_zfs_destroy_dataset(int dataset_id);
int fs3_zfs_snapshot(int dataset_id, const char *name);
int fs3_zfs_rollback(int dataset_id, int snap_id);
int fs3_zfs_scrub(int pool_id);
int fs3_zfs_get_info(int pool_id, zfs_info_t *info);
int fs3_zfs_list_datasets(int pool_id, zfs_dataset_t *datasets, int max);

/* NFS */
int fs3_nfs_mount(const char *server, const char *export_path, const char *mount_point, int version);
int fs3_nfs_umount(int fs_id);
int fs3_nfs_get_info(int fs_id, nfs_info_t *info);
int fs3_nfs_reconnect(int fs_id);

/* 扩展属性 */
int fs3_set_xattr(int fs_id, const char *path, const char *name, const char *value, int size, int namespace);
int fs3_get_xattr(int fs_id, const char *path, const char *name, char *value, int *size);
int fs3_list_xattr(int fs_id, const char *path, xattr_t *xattrs, int max);
int fs3_remove_xattr(int fs_id, const char *path, const char *name);

/* ACL */
int fs3_set_acl(int fs_id, const char *path, ace_t *aces, int count);
int fs3_get_acl(int fs_id, const char *path, ace_t *aces, int max);

/* 统计 */
int fs3_get_stats(uint64_t *reads, uint64_t *writes, uint64_t *read_bytes, uint64_t *write_bytes);

#endif /* FS3_H */
