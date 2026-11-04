#ifndef FS_REAL_H
#define FS_REAL_H

#include <stdint.h>

#define FS_R_MAX_MOUNTS 16
#define FS_R_MAX_FILES 64
#define FS_R_MAX_DIRS 32
#define FS_R_MAX_INODES 256
#define FS_R_MAX_BLOCKS 512

/* ext2超级块 */
typedef struct {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;  /* 0xEF53 */
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    /* 动态修订 */
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t s_uuid[16];
    char s_volume_name[16];
    char s_last_mounted[64];
    uint32_t s_algo_bitmap;
} ext2_superblock_t;

/* ext2块组描述符 */
typedef struct {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint8_t bg_reserved[12];
} ext2_group_desc_t;

/* ext2 inode */
typedef struct {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];  /* 0-11直接,12间接,13双间接,14三间接 */
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t i_osd2[12];
} ext2_inode_t;

/* ext2目录项 */
typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
    char name[255];
} ext2_dir_entry_t;

/* 文件类型 */
typedef enum {
    FS_FILE_REGULAR = 1,
    FS_FILE_DIRECTORY = 2,
    FS_FILE_CHARDEV = 3,
    FS_FILE_BLOCKDEV = 4,
    FS_FILE_FIFO = 5,
    FS_FILE_SOCKET = 6,
    FS_FILE_SYMLINK = 7,
} fs_file_type_t;

/* 文件句柄 */
typedef struct {
    int used;
    int fd;
    char path[256];
    fs_file_type_t type;
    int mode;  /* 0=read,1=write,2=readwrite */
    uint64_t offset;
    uint64_t size;
    int inode_number;
    int mount_id;
    int permissions;
    int owner;
    int group;
    uint64_t atime;
    uint64_t ctime;
    uint64_t mtime;
    int links;
    int dirty;
} fs_file_t;

/* 目录条目 */
typedef struct {
    char name[256];
    fs_file_type_t type;
    int inode_number;
    uint64_t size;
    int permissions;
    int owner;
    int group;
    uint64_t mtime;
} fs_dir_entry_t;

/* 挂载点 */
typedef struct {
    int used;
    int id;
    char device[64];
    char mount_point[256];
    char filesystem[32];
    int readonly;
    uint64_t total_size;
    uint64_t used_size;
    uint64_t free_size;
    uint64_t block_size;
    uint64_t total_blocks;
    uint64_t free_blocks;
    int inode_count;
    int free_inodes;
    int mounted;
} fs_r_mount_t;

/* initrd文件 */
typedef struct {
    int used;
    int id;
    char path[256];
    uint8_t *data;
    uint32_t size;
    uint32_t offset;  /* 在initrd镜像中的偏移 */
    int compressed;
    int compression_type;  /* 0=none,1=gzip,2=lz4,3=zstd */
    uint32_t compressed_size;
} initrd_file_t;

/* initrd镜像 */
typedef struct {
    int initialized;
    uint8_t *data;
    uint32_t size;
    uint32_t file_count;
    initrd_file_t files[FS_R_MAX_FILES];
    char format[32];  /* cpio/tar/initramfs */
    int compressed;
} initrd_image_t;

/* 文件系统真实化状态 */
typedef struct {
    int initialized;
    /* ext2 */
    ext2_superblock_t ext2_sb;
    ext2_group_desc_t ext2_groups[8];
    int ext2_group_count;
    ext2_inode_t ext2_inodes[FS_R_MAX_INODES];
    int ext2_inode_count;
    uint8_t ext2_blocks[FS_R_MAX_BLOCKS][1024];
    int ext2_block_count;
    /* 挂载 */
    fs_r_mount_t mounts[FS_R_MAX_MOUNTS];
    int mount_count;
    /* 文件句柄 */
    fs_file_t files[FS_R_MAX_FILES];
    int file_count;
    /* initrd */
    initrd_image_t initrd;
    /* 统计 */
    uint64_t total_reads;
    uint64_t total_writes;
    uint64_t total_bytes_read;
    uint64_t total_bytes_written;
    uint64_t cache_hits;
    uint64_t cache_misses;
} fs_real_t;

/* 初始化文件系统真实化 */
void fs_real_init(void);

/* ext2 */
int fs_r_ext2_mount(const char *device, const char *mount_point, int readonly);
int fs_r_ext2_unmount(int mount_id);
int fs_r_ext2_read_inode(int mount_id, int inode_num, ext2_inode_t *inode);
int fs_r_ext2_write_inode(int mount_id, int inode_num, ext2_inode_t *inode);
int fs_r_ext2_alloc_inode(int mount_id);
int fs_r_ext2_free_inode(int mount_id, int inode_num);
int fs_r_ext2_alloc_block(int mount_id);
int fs_r_ext2_free_block(int mount_id, int block_num);
int fs_r_ext2_read_block(int mount_id, int block_num, void *buffer);
int fs_r_ext2_write_block(int mount_id, int block_num, const void *buffer);
int fs_r_ext2_get_superblock(int mount_id, ext2_superblock_t *sb);

/* 文件操作 */
int fs_r_open(const char *path, int mode);
int fs_r_close(int fd);
int fs_r_read(int fd, void *buffer, int size);
int fs_r_write(int fd, const void *buffer, int size);
int fs_r_lseek(int fd, int64_t offset, int whence);
int fs_r_stat(const char *path, fs_file_t *stat);
int fs_r_fstat(int fd, fs_file_t *stat);
int fs_r_unlink(const char *path);
int fs_r_rename(const char *old_path, const char *new_path);
int fs_r_chmod(const char *path, int mode);
int fs_r_chown(const char *path, int owner, int group);
int fs_r_truncate(const char *path, uint64_t size);

/* 目录操作 */
int fs_r_mkdir(const char *path, int mode);
int fs_r_rmdir(const char *path);
int fs_r_opendir(const char *path);
int fs_r_readdir(int dir_fd, fs_dir_entry_t *entry);
int fs_r_closedir(int dir_fd);
int fs_r_getcwd(char *buf, int size);
int fs_r_chdir(const char *path);

/* 挂载 */
int fs_r_mount(const char *device, const char *mount_point, const char *filesystem, int flags);
int fs_r_umount(const char *mount_point);
int fs_r_list_mounts(fs_r_mount_t *mounts, int max);
int fs_r_get_mount(const char *mount_point, fs_r_mount_t *mount);

/* initrd */
int fs_r_initrd_load(void *data, uint32_t size);
int fs_r_initrd_get_file(const char *path, initrd_file_t *file);
int fs_r_initrd_list_files(initrd_file_t *files, int max);
int fs_r_initrd_read_file(const char *path, void *buffer, uint32_t max_size);
int fs_r_initrd_create(const char *path, const void *data, uint32_t size);

/* 统计 */
int fs_r_get_stats(fs_real_t *stats);

#endif /* FS_REAL_H */
