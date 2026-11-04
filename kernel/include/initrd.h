#ifndef INITRD_H
#define INITRD_H

#include <stdint.h>
#include <stddef.h>

/* initrd 魔数 */
#define INITRD_MAGIC "HEPURD\0"
#define INITRD_MAGIC_LEN 8
#define INITRD_VERSION 1
#define INITRD_MAX_FILENAME 48
#define INITRD_MAX_FILES 64

/* initrd 文件头 */
typedef struct {
    uint8_t  magic[8];       /* "HEPURD\0" */
    uint32_t version;        /* 版本号 */
    uint32_t file_count;     /* 文件数量 */
    uint32_t reserved;       /* 保留 */
} __attribute__((packed)) initrd_header_t;

/* initrd 文件条目 */
typedef struct {
    char     name[INITRD_MAX_FILENAME];  /* 文件名 */
    uint32_t offset;                     /* 数据偏移(相对于initrd起始) */
    uint32_t size;                       /* 文件大小 */
    uint32_t type;                       /* 0=普通文件, 1=目录 */
    uint32_t reserved;                   /* 保留 */
} __attribute__((packed)) initrd_file_entry_t;

/* 文件描述符 */
typedef struct {
    int      used;
    int      index;          /* 文件条目索引 */
    uint32_t position;       /* 当前读写位置 */
} initrd_fd_t;

/* 初始化initrd */
int initrd_init(uint32_t addr, uint32_t size);

/* 打开文件 */
int initrd_open(const char *path);

/* 关闭文件 */
int initrd_close(int fd);

/* 读取文件 */
int initrd_read(int fd, void *buf, uint32_t count);

/* 获取文件大小 */
int initrd_size(int fd);

/* 查找文件 */
int initrd_find(const char *path);

/* 获取文件数据指针 */
void *initrd_get_data(int index);

/* 获取文件条目 */
initrd_file_entry_t *initrd_get_entry(int index);

/* 列出文件 */
void initrd_list(void);

/* initrd是否已加载 */
int initrd_ready(void);

#endif /* INITRD_H */
