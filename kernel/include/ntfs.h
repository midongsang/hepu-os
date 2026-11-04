#ifndef NTFS_H
#define NTFS_H

#include <stdint.h>

#define NTFS_SECTOR_SIZE 512
#define NTFS_MAX_MFT_ENTRIES 4096
#define NTFS_MFT_ENTRY_SIZE 1024

/* NTFS引导扇区 */
typedef struct {
    uint8_t  jump[3];           /* 0x00 跳转指令 */
    uint8_t  oem_id[8];         /* 0x03 OEM ID "NTFS    " */
    uint16_t bytes_per_sector;  /* 0x0B 每扇区字节数 */
    uint8_t  sectors_per_cluster; /* 0x0D 每簇扇区数 */
    uint16_t reserved_sectors;  /* 0x0E 保留扇区 */
    uint8_t  reserved1[3];      /* 0x10 保留 */
    uint16_t unused1;           /* 0x13 未使用 */
    uint8_t  media_descriptor;  /* 0x15 介质描述符 */
    uint16_t unused2;           /* 0x16 未使用 */
    uint16_t sectors_per_track; /* 0x18 每磁道扇区数 */
    uint16_t heads;             /* 0x1A 磁头数 */
    uint32_t hidden_sectors;    /* 0x1C 隐藏扇区 */
    uint32_t unused3;           /* 0x20 未使用 */
    uint32_t unused4;           /* 0x24 未使用 */
    uint64_t total_sectors;     /* 0x28 总扇区数 */
    uint64_t mft_cluster;       /* 0x30 MFT首簇 */
    uint64_t mft_mirror_cluster;/* 0x38 MFT镜像首簇 */
    int8_t   clusters_per_mft;  /* 0x40 每MFT记录簇数 */
    uint8_t  reserved2[3];      /* 0x41 保留 */
    int8_t   clusters_per_index;/* 0x44 每索引块簇数 */
    uint8_t  reserved3[3];      /* 0x45 保留 */
    uint64_t volume_serial;     /* 0x48 卷序列号 */
    uint32_t checksum;          /* 0x50 校验和 */
    uint8_t  boot_code[426];    /* 0x54 引导代码 */
    uint16_t boot_signature;    /* 0x1FE 引导签名0xAA55 */
} __attribute__((packed)) hepu_ntfs_boot_t;

/* NTFS MFT记录头 */
typedef struct {
    uint32_t magic;             /* "FILE" */
    uint16_t update_seq_offset; /* 更新序列偏移 */
    uint16_t update_seq_size;   /* 更新序列大小 */
    uint64_t logfile_seq;       /* 日志文件序列号 */
    uint16_t seq_number;        /* 序列号 */
    uint16_t hard_link_count;   /* 硬链接数 */
    uint16_t first_attr_offset; /* 第一个属性偏移 */
    uint16_t flags;             /* 标志 */
    uint32_t used_size;         /* 记录使用大小 */
    uint32_t allocated_size;    /* 记录分配大小 */
    uint64_t base_record;       /* 基础记录引用 */
    uint16_t next_attr_id;      /* 下一个属性ID */
    uint16_t reserved;
    uint32_t record_number;     /* 记录号 */
} __attribute__((packed)) hepu_ntfs_mft_header_t;

/* NTFS属性头 */
typedef struct {
    uint32_t type;              /* 属性类型 */
    uint32_t length;            /* 属性长度 */
    uint8_t  non_resident;      /* 非常驻标志 */
    uint8_t  name_length;       /* 名称长度 */
    uint16_t name_offset;       /* 名称偏移 */
    uint16_t flags;             /* 标志 */
    uint16_t attribute_id;      /* 属性ID */
} __attribute__((packed)) hepu_ntfs_attr_header_t;

/* NTFS属性类型 */
#define NTFS_ATTR_STANDARD_INFORMATION 0x10
#define NTFS_ATTR_ATTRIBUTE_LIST       0x20
#define NTFS_ATTR_FILE_NAME            0x30
#define NTFS_ATTR_OBJECT_ID            0x40
#define NTFS_ATTR_SECURITY_DESCRIPTOR  0x50
#define NTFS_ATTR_VOLUME_NAME          0x60
#define NTFS_ATTR_VOLUME_INFORMATION   0x70
#define NTFS_ATTR_DATA                 0x80
#define NTFS_ATTR_INDEX_ROOT           0x90
#define NTFS_ATTR_INDEX_ALLOCATION     0xA0
#define NTFS_ATTR_BITMAP               0xB0
#define NTFS_ATTR_REPARSE_POINT        0xC0
#define NTFS_ATTR_EA_INFORMATION       0xD0
#define NTFS_ATTR_EA                   0xE0
#define NTFS_ATTR_LOGGED_UTILITY_STREAM 0x100

/* NTFS卷信息 */
typedef struct {
    int mounted;
    hepu_ntfs_boot_t boot;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint64_t total_sectors;
    uint64_t mft_cluster;
    uint64_t mft_mirror_cluster;
    uint32_t mft_entry_size;
    uint64_t volume_serial;
    char label[12];
} hepu_ntfs_volume_t;

void hepu_ntfs_init(void);
int hepu_ntfs_mount(hepu_ntfs_volume_t *vol, const uint8_t *boot_sector);
int hepu_ntfs_read_mft_record(hepu_ntfs_volume_t *vol, uint64_t mft_ref,
                              uint8_t *buffer, uint32_t size);
int hepu_ntfs_read_file(hepu_ntfs_volume_t *vol, uint64_t mft_ref,
                        uint64_t offset, uint8_t *buffer, uint32_t size);
int hepu_ntfs_get_volume_info(hepu_ntfs_volume_t *vol);

#endif /* NTFS_H */
