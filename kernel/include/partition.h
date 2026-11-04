#ifndef PARTITION_H
#define PARTITION_H

#include <stdint.h>

#define MAX_PARTITIONS 16
#define PARTITION_NAME_LEN 32

/* MBR分区表项 */
typedef struct {
    uint8_t  boot_flag;      /* 0x80=可引导 */
    uint8_t  start_chs[3];   /* 起始CHS */
    uint8_t  type;           /* 分区类型 */
    uint8_t  end_chs[3];     /* 结束CHS */
    uint32_t start_lba;      /* 起始LBA */
    uint32_t size_sectors;   /* 扇区数 */
} __attribute__((packed)) mbr_partition_t;

/* MBR引导扇区 */
typedef struct {
    uint8_t  boot_code[446];
    mbr_partition_t partitions[4];
    uint16_t signature;      /* 0xAA55 */
} __attribute__((packed)) mbr_t;

/* GPT头 */
typedef struct {
    uint64_t signature;      /* "EFI PART" = 0x5452415020494645 */
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t partition_entry_lba;
    uint32_t num_partition_entries;
    uint32_t partition_entry_size;
    uint32_t partition_array_crc32;
} __attribute__((packed)) gpt_header_t;

/* GPT分区项 */
typedef struct {
    uint8_t  type_guid[16];
    uint8_t  partition_guid[16];
    uint64_t start_lba;
    uint64_t end_lba;
    uint64_t attributes;
    uint16_t name[36];       /* UTF-16LE */
} __attribute__((packed)) gpt_partition_t;

/* 通用分区信息 */
typedef struct {
    int      valid;
    int      index;
    char     name[PARTITION_NAME_LEN];
    uint8_t  type;           /* MBR类型或GPT类型简化 */
    uint64_t start_lba;
    uint64_t size_sectors;
    int      bootable;
    char     fs_type[16];    /* 检测到的文件系统类型 */
} partition_info_t;

/* 分区类型 */
#define PART_TYPE_EMPTY     0x00
#define PART_TYPE_FAT32     0x0C
#define PART_TYPE_NTFS      0x07
#define PART_TYPE_EXT2      0x83
#define PART_TYPE_EXT4      0x83
#define PART_TYPE_LINUX_SWAP 0x82
#define PART_TYPE_EXTENDED  0x05
#define PART_TYPE_GPT       0xEE

void partition_init(void);
int partition_parse_mbr(const uint8_t *sector, partition_info_t *parts, int max);
int partition_parse_gpt(const uint8_t *header_sector, const uint8_t *entry_sector,
                        partition_info_t *parts, int max);
int partition_detect(const uint8_t *mbr_sector, partition_info_t *parts, int max);
const char *partition_type_name(uint8_t type);
int partition_list(partition_info_t *parts, int max);

#endif /* PARTITION_H */
