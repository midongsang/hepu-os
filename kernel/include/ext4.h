#ifndef EXT4_H
#define EXT4_H

#include <stdint.h>
#include <stdint.h>

/* ext4扩展特性 */
#define EXT4_FEATURE_COMPAT_DIR_PREALLOC  0x0001
#define EXT4_FEATURE_COMPAT_IMAGIC_INODES 0x0002
#define EXT4_FEATURE_COMPAT_HAS_JOURNAL   0x0004
#define EXT4_FEATURE_COMPAT_EXT_ATTR     0x0008
#define EXT4_FEATURE_COMPAT_RESIZE_INODE 0x0010
#define EXT4_FEATURE_COMPAT_DIR_INDEX    0x0020

#define EXT4_FEATURE_INCOMPAT_COMPRESSION 0x0001
#define EXT4_FEATURE_INCOMPAT_FILETYPE    0x0002
#define EXT4_FEATURE_INCOMPAT_RECOVER     0x0004
#define EXT4_FEATURE_INCOMPAT_JOURNAL_DEV 0x0008
#define EXT4_FEATURE_INCOMPAT_META_BG     0x0010
#define EXT4_FEATURE_INCOMPAT_EXTENTS     0x0040
#define EXT4_FEATURE_INCOMPAT_64BIT       0x0080
#define EXT4_FEATURE_INCOMPAT_MMP         0x0100
#define EXT4_FEATURE_INCOMPAT_FLEX_BG     0x0200

#define EXT4_FEATURE_RO_COMPAT_SPARSE_SUPER 0x0001
#define EXT4_FEATURE_RO_COMPAT_LARGE_FILE  0x0002
#define EXT4_FEATURE_RO_COMPAT_BTREE_DIR   0x0004
#define EXT4_FEATURE_RO_COMPAT_HUGE_FILE   0x0008
#define EXT4_FEATURE_RO_COMPAT_GDT_CSUM    0x0010
#define EXT4_FEATURE_RO_COMPAT_DIR_NLINK   0x0020
#define EXT4_FEATURE_RO_COMPAT_EXTRA_ISIZE 0x0040

/* ext4 extent (替代ext2的间接块) */
typedef struct {
    uint32_t ee_block;     /* 起始逻辑块 */
    uint16_t ee_len;       /* 块数 */
    uint16_t ee_start_hi;  /* 物理块高16位 */
    uint32_t ee_start_lo;  /* 物理块低32位 */
} __attribute__((packed)) hepu_ext4_extent_t;

typedef struct {
    uint16_t eh_magic;     /* 0xF30A */
    uint16_t eh_entries;   /* 有效extent数 */
    uint16_t eh_max;       /* 最大extent数 */
    uint16_t eh_depth;     /* 0=叶子节点 */
    uint32_t eh_generation;
} __attribute__((packed)) hepu_ext4_extent_header_t;

/* ext4 inode (扩展i_block为extent树) */
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
    uint32_t i_block[15];  /* extent树根节点 */
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    /* ext4扩展字段 */
    uint16_t i_extra_isize;
    uint16_t i_checksum_hi;
    uint32_t i_ctime_extra;
    uint32_t i_mtime_extra;
    uint32_t i_atime_extra;
    uint32_t i_crtime;
    uint32_t i_crtime_extra;
    uint32_t i_version_hi;
    uint32_t i_projid;
} __attribute__((packed)) hepu_ext4_inode_t;

void ext4_init(void);
int ext4_mount(const char *device, const char *mountpoint);
int ext4_umount(const char *mountpoint);
int ext4_sync(void);
int ext4_check_features(uint32_t compat, uint32_t incompat, uint32_t ro_compat);

#endif /* EXT4_H */
