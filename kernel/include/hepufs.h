#ifndef HEPUFS_H
#define HEPUFS_H

#include <stdint.h>

/* HepuFS常量 */
#define HEPUFS_MAGIC          0x48455055  /* "HEPU" */
#define HEPUFS_BLOCK_SIZE     4096
#define HEPUFS_INODE_SIZE     128
#define HEPUFS_INODES_PER_BLOCK (HEPUFS_BLOCK_SIZE / HEPUFS_INODE_SIZE)  /* 32 */
#define HEPUFS_MAX_NAME_LEN   255
#define HEPUFS_DIRECT_BLOCKS   12
#define HEPUFS_INDIRECT_BLOCKS 1
#define HEPUFS_TOTAL_BLOCKS    (HEPUFS_DIRECT_BLOCKS + HEPUFS_INDIRECT_BLOCKS)

/* 布局（以块为单位） */
#define HEPUFS_SUPERBLOCK_BLOCK   0
#define HEPUFS_INODE_BITMAP_BLOCK 1
#define HEPUFS_BLOCK_BITMAP_BLOCK 2
#define HEPUFS_INODE_TABLE_START  3
#define HEPUFS_INODE_TABLE_BLOCKS 128  /* 32 inodes/block * 128 = 4096 inodes */
#define HEPUFS_DATA_BLOCK_START    (HEPUFS_INODE_TABLE_START + HEPUFS_INODE_TABLE_BLOCKS)

/* inode模式 */
#define HEPUFS_S_IFMT   0xF000
#define HEPUFS_S_IFREG  0x8000  /* 普通文件 */
#define HEPUFS_S_IFDIR  0x4000  /* 目录 */

/* 特殊inode号 */
#define HEPUFS_ROOT_INODE 1

/* HepuFS超级块（在磁盘上的布局） */
typedef struct {
    uint32_t magic;           /* 魔数 0x48455055 */
    uint32_t total_blocks;    /* 总块数 */
    uint32_t total_inodes;    /* 总inode数 */
    uint32_t free_blocks;     /* 空闲块数 */
    uint32_t free_inodes;     /* 空闲inode数 */
    uint32_t block_size;      /* 块大小 */
    uint32_t inode_size;      /* inode大小 */
    uint32_t data_block_start;/* 数据块起始位置 */
    uint8_t  padding[4064];   /* 填充到4096字节 */
} __attribute__((packed)) hepu_superblock_t;

/* HepuFS inode（在磁盘上的布局，128字节） */
typedef struct {
    uint16_t mode;            /* 模式（文件/目录） */
    uint16_t uid;             /* 用户ID */
    uint32_t size;            /* 文件大小（字节） */
    uint32_t atime;           /* 访问时间 */
    uint32_t ctime;           /* 创建时间 */
    uint32_t mtime;           /* 修改时间 */
    uint32_t dtime;           /* 删除时间 */
    uint16_t gid;             /* 组ID */
    uint16_t links_count;     /* 硬链接数 */
    uint32_t blocks;          /* 占用块数 */
    uint32_t flags;           /* 标志 */
    uint32_t direct[HEPUFS_DIRECT_BLOCKS];    /* 直接块指针 */
    uint32_t indirect[HEPUFS_INDIRECT_BLOCKS]; /* 间接块指针 */
    uint8_t  padding[28];     /* 填充到128字节 */
} __attribute__((packed)) hepu_inode_t;

/* HepuFS目录项（在磁盘上的布局） */
typedef struct {
    uint32_t inode;           /* inode号 */
    uint16_t rec_len;         /* 目录项长度 */
    uint8_t  name_len;        /* 文件名长度 */
    uint8_t  file_type;       /* 文件类型 */
    char     name[HEPUFS_MAX_NAME_LEN]; /* 文件名 */
} __attribute__((packed)) hepu_dirent_t;

/* 文件类型 */
#define HEPUFS_FT_UNKNOWN 0
#define HEPUFS_FT_REG_FILE 1
#define HEPUFS_FT_DIR 2

/* HepuFS文件句柄 */
typedef struct {
    uint32_t inode_no;        /* inode号 */
    hepu_inode_t inode;       /* inode缓存 */
    uint32_t pos;             /* 当前读写位置 */
    int      dirty;           /* inode是否脏 */
} hepu_file_t;

/* 初始化HepuFS（格式化指定设备） */
int hepufs_format(uint32_t dev, uint32_t total_blocks);

/* 挂载HepuFS */
int hepufs_mount(uint32_t dev);

/* 卸载HepuFS */
int hepufs_umount(void);

/* 创建文件 */
int hepufs_create(const char *path, uint16_t mode);

/* 删除文件 */
int hepufs_unlink(const char *path);

/* 创建目录 */
int hepufs_mkdir(const char *path, uint16_t mode);

/* 删除目录 */
int hepufs_rmdir(const char *path);

/* 打开文件 */
hepu_file_t *hepufs_open(const char *path);

/* 关闭文件 */
int hepufs_close(hepu_file_t *file);

/* 读文件 */
int hepufs_read(hepu_file_t *file, void *buf, uint32_t count);

/* 写文件 */
int hepufs_write(hepu_file_t *file, const void *buf, uint32_t count);

/* 定位文件指针 */
int hepufs_lseek(hepu_file_t *file, uint32_t offset);

/* 读取目录 */
int hepufs_readdir(const char *path, char *name_buf, uint32_t buf_size, uint32_t index);

/* 获取文件状态 */
int hepufs_stat(const char *path, hepu_inode_t *inode);

/* 自测 */
void hepufs_selftest(void);

#endif
