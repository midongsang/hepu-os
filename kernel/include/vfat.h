#ifndef VFAT_H
#define VFAT_H

#include <stdint.h>

#define FAT32_EOC_MARKER 0x0FFFFFF8
#define FAT32_BAD_CLUSTER 0x0FFFFFF7
#define VFAT_MAX_PATH 256
#define VFAT_NAME_LEN 13  /* 8.3格式短文件名 */

/* FAT32引导扇区 */
typedef struct {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t sectors_per_fat_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    /* FAT32扩展 */
    uint32_t sectors_per_fat_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} __attribute__((packed)) fat32_boot_t;

/* FAT目录项 */
typedef struct {
    char     name[8];
    char     ext[3];
    uint8_t  attr;
    uint8_t  reserved;
    uint8_t  create_time_tenths;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_high;
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t cluster_low;
    uint32_t file_size;
} __attribute__((packed)) fat_dirent_t;

#define FAT_ATTR_READONLY 0x01
#define FAT_ATTR_HIDDEN   0x02
#define FAT_ATTR_SYSTEM   0x04
#define FAT_ATTR_VOLUME   0x08
#define FAT_ATTR_DIRECTORY 0x10
#define FAT_ATTR_ARCHIVE  0x20
#define FAT_ATTR_LFN      0x0F

void vfat_init(void);
int vfat_mount(const char *device, const char *mountpoint);
int vfat_umount(const char *mountpoint);
int vfat_sync(void);

#endif /* VFAT_H */
