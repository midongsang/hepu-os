#ifndef EXFAT_H
#define EXFAT_H

#include <stdint.h>

#define EXFAT_SECTOR_SIZE 512
#define EXFAT_MAX_CLUSTERS 0xFFFFFFF7
#define EXFAT_CLUSTER_FREE 0x00000000
#define EXFAT_CLUSTER_EOC 0xFFFFFFFF
#define EXFAT_CLUSTER_BAD 0xFFFFFFF7

/* exFAT引导扇区 */
typedef struct {
    uint8_t  jump[3];           /* 0x00 跳转指令 */
    uint8_t  oem_name[8];       /* 0x03 OEM名称 */
    uint8_t  reserved[53];      /* 0x0B 保留 */
    uint64_t partition_offset;  /* 0x40 分区偏移(扇区) */
    uint64_t volume_length;     /* 0x48 卷长度(扇区) */
    uint32_t fat_offset;        /* 0x50 FAT偏移(扇区) */
    uint32_t fat_length;        /* 0x54 FAT长度(扇区) */
    uint32_t cluster_heap_offset; /* 0x58 簇堆偏移(扇区) */
    uint32_t cluster_count;     /* 0x5C 簇数量 */
    uint32_t root_cluster;      /* 0x60 根目录首簇 */
    uint32_t volume_serial;     /* 0x64 卷序列号 */
    uint16_t fs_revision;       /* 0x68 文件系统版本 */
    uint16_t volume_flags;      /* 0x6A 卷标志 */
    uint8_t  bytes_per_sector;  /* 0x6C 每扇区字节数(2的幂) */
    uint8_t  sectors_per_cluster; /* 0x6D 每簇扇区数(2的幂) */
    uint8_t  fat_count;         /* 0x6E FAT数量 */
    uint8_t  drive_select;      /* 0x6F 驱动器选择 */
    uint8_t  percent_in_use;    /* 0x70 使用率百分比 */
    uint8_t  reserved2[7];      /* 0x71 保留 */
    uint8_t  boot_code[390];    /* 0x78 引导代码 */
    uint16_t boot_signature;    /* 0x1FE 引导签名(0xAA55) */
} __attribute__((packed)) hepu_exfat_boot_t;

/* exFAT目录条目类型 */
#define EXFAT_ENTRY_BITMAP   0x81
#define EXFAT_ENTRY_UPCASE   0x82
#define EXFAT_ENTRY_LABEL    0x83
#define EXFAT_ENTRY_FILE     0x85
#define EXFAT_ENTRY_STREAM   0xC0
#define EXFAT_ENTRY_NAME     0xC1

/* exFAT目录条目头部 */
typedef struct {
    uint8_t  type;              /* 条目类型 */
    uint8_t  secondary_count;   /* 次要条目数量 */
    uint16_t set_checksum;      /* 集合校验和 */
    uint16_t attributes;        /* 文件属性 */
    uint16_t reserved1;
    uint32_t create_time;       /* 创建时间 */
    uint32_t modify_time;       /* 修改时间 */
    uint32_t access_time;       /* 访问时间 */
    uint8_t  create_time_ms;    /* 创建时间毫秒 */
    uint8_t  modify_time_ms;    /* 修改时间毫秒 */
    uint8_t  create_tz;         /* 创建时区 */
    uint8_t  modify_tz;         /* 修改时区 */
    uint8_t  access_tz;         /* 访问时区 */
    uint8_t  reserved2[7];
} __attribute__((packed)) hepu_exfat_file_entry_t;

/* exFAT流扩展条目 */
typedef struct {
    uint8_t  type;              /* 0xC0 */
    uint8_t  flags;             /* 标志 */
    uint8_t  reserved1;
    uint8_t  name_length;       /* 文件名长度 */
    uint16_t name_hash;         /* 文件名哈希 */
    uint16_t reserved2;
    uint64_t valid_data_length; /* 有效数据长度 */
    uint32_t reserved3;
    uint32_t first_cluster;     /* 首簇 */
    uint64_t data_length;       /* 数据长度 */
} __attribute__((packed)) hepu_exfat_stream_entry_t;

/* exFAT文件名条目 */
typedef struct {
    uint8_t  type;              /* 0xC1 */
    uint8_t  flags;
    uint16_t unicode_name[15];  /* Unicode文件名(15字符) */
} __attribute__((packed)) hepu_exfat_name_entry_t;

/* exFAT卷信息 */
typedef struct {
    int mounted;
    hepu_exfat_boot_t boot;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint32_t cluster_count;
    uint32_t root_cluster;
    uint32_t *fat;              /* FAT表缓存 */
    uint8_t  *cluster_heap;     /* 簇堆缓存(简化) */
    char label[12];
} hepu_exfat_volume_t;

void hepu_exfat_init(void);
int hepu_exfat_mount(hepu_exfat_volume_t *vol, const uint8_t *boot_sector);
int hepu_exfat_read_cluster(hepu_exfat_volume_t *vol, uint32_t cluster, uint8_t *buffer);
int hepu_exfat_write_cluster(hepu_exfat_volume_t *vol, uint32_t cluster, const uint8_t *buffer);
uint32_t hepu_exfat_next_cluster(hepu_exfat_volume_t *vol, uint32_t cluster);
int hepu_exfat_get_next_free_cluster(hepu_exfat_volume_t *vol, uint32_t *cluster);
int hepu_exfat_read_file(hepu_exfat_volume_t *vol, uint32_t first_cluster,
                    uint64_t offset, uint8_t *buffer, uint32_t size);

#endif /* EXFAT_H */
