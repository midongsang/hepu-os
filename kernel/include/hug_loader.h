#ifndef HUG_LOADER_H
#define HUG_LOADER_H

#include <stdint.h>

/* .hug 文件魔数 "HUG\0" */
#define HUG_MAGIC 0x00475548
#define HUG_VERSION 1
#define HUG_HEADER_SIZE 64

/* .hug 文件头 */
typedef struct {
    uint32_t magic;           /* "HUG\0" = 0x00475548 */
    uint32_t version;         /* 格式版本 */
    uint32_t manifest_offset; /* manifest偏移 */
    uint32_t manifest_size;   /* manifest大小 */
    uint32_t code_offset;     /* 代码段偏移 */
    uint32_t code_size;       /* 代码段大小 */
    uint32_t data_offset;     /* 数据段偏移 */
    uint32_t data_size;       /* 数据段大小 */
    uint32_t entry_point;     /* 入口点(相对代码段) */
    uint32_t flags;           /* 标志位 */
    uint8_t  reserved[24];    /* 保留 */
} __attribute__((packed)) hug_header_t;

/* .hug 应用信息 */
typedef struct {
    hug_header_t header;
    const char  *manifest;    /* manifest JSON字符串 */
    void        *code;        /* 代码段指针 */
    void        *data;        /* 数据段指针 */
    uint32_t     entry;       /* 入口地址 */
} hug_app_t;

/* 解析.hug文件 */
int hug_parse(const void *data, uint32_t size, hug_app_t *app);

/* 从initrd加载并运行.hug应用 */
int hug_run(const char *path);

/* 验证.hug文件 */
int hug_verify(const void *data, uint32_t size);

/* 显示.hug应用信息 */
void hug_info(const hug_app_t *app);

#endif /* HUG_LOADER_H */
