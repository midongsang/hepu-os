/* ============================================================================
 * 赫普OS - MBR/GPT分区表解析
 * ========================================================================== */

#include <stdint.h>
#include "partition.h"
#include "vga.h"
#include "string.h"
#include "kstdio.h"

static partition_info_t detected_partitions[MAX_PARTITIONS];
static int partition_count = 0;
static int partition_initialized = 0;

void partition_init(void)
{
    memset(detected_partitions, 0, sizeof(detected_partitions));
    partition_count = 0;
    partition_initialized = 1;
    vga_puts("[PART] partition table parse module init \n");
}

/* 解析MBR分区表 */
int partition_parse_mbr(const uint8_t *sector, partition_info_t *parts, int max)
{
    if (!sector || !parts || max <= 0) return -1;

    mbr_t *mbr = (mbr_t *)sector;

    /* 检查MBR签名 */
    if (mbr->signature != 0xAA55) {
        vga_puts("[PART] invalid MBR signature \n");
        return -1;
    }

    int count = 0;
    for (int i = 0; i < 4 && count < max; i++) {
        if (mbr->partitions[i].type == PART_TYPE_EMPTY) continue;

        parts[count].valid = 1;
        parts[count].index = i;
        parts[count].type = mbr->partitions[i].type;
        parts[count].start_lba = mbr->partitions[i].start_lba;
        parts[count].size_sectors = mbr->partitions[i].size_sectors;
        parts[count].bootable = (mbr->partitions[i].boot_flag == 0x80);

        /* 根据类型推断文件系统 */
        switch (mbr->partitions[i].type) {
            case PART_TYPE_FAT32:
                strcpy(parts[count].fs_type, "vfat");
                break;
            case PART_TYPE_NTFS:
                strcpy(parts[count].fs_type, "ntfs");
                break;
            case PART_TYPE_EXT2:
                strcpy(parts[count].fs_type, "ext4");
                break;
            case PART_TYPE_LINUX_SWAP:
                strcpy(parts[count].fs_type, "swap");
                break;
            case PART_TYPE_EXTENDED:
                strcpy(parts[count].fs_type, "extended");
                break;
            case PART_TYPE_GPT:
                strcpy(parts[count].fs_type, "gpt");
                break;
            default:
                strcpy(parts[count].fs_type, "unknown");
                break;
        }

        snprintf(parts[count].name, PARTITION_NAME_LEN, "partition%d", i);

        vga_printf("[PART] MBR partition %d: type=0x%02X start=%d size=%d(%s)\n",
                   i, parts[count].type,
                   (uint32_t)parts[count].start_lba,
                   (uint32_t)parts[count].size_sectors,
                   parts[count].fs_type);
        count++;
    }

    return count;
}

/* 解析GPT分区表 */
int partition_parse_gpt(const uint8_t *header_sector, const uint8_t *entry_sector,
                        partition_info_t *parts, int max)
{
    if (!header_sector || !entry_sector || !parts || max <= 0) return -1;

    gpt_header_t *gpt = (gpt_header_t *)header_sector;

    /* 检查GPT签名 */
    if (gpt->signature != 0x5452415020494645ULL) {
        vga_puts("[PART] invalid GPT signature \n");
        return -1;
    }

    vga_printf("[PART] GPT header: version =%d partition count =%d entry size =%d\n",
               gpt->revision, gpt->num_partition_entries,
               gpt->partition_entry_size);

    int count = 0;
    uint32_t entry_size = gpt->partition_entry_size;
    if (entry_size < sizeof(gpt_partition_t)) entry_size = sizeof(gpt_partition_t);

    for (uint32_t i = 0; i < gpt->num_partition_entries && count < max; i++) {
        gpt_partition_t *entry = (gpt_partition_t *)(entry_sector + i * entry_size);

        /* 检查是否为空分区（type_guid全零） */
        int empty = 1;
        for (int j = 0; j < 16; j++) {
            if (entry->type_guid[j] != 0) { empty = 0; break; }
        }
        if (empty) continue;

        parts[count].valid = 1;
        parts[count].index = i;
        parts[count].start_lba = entry->start_lba;
        parts[count].size_sectors = entry->end_lba - entry->start_lba + 1;
        parts[count].bootable = 0;
        parts[count].type = 0x83;  /* 默认Linux */

        /* 转换UTF-16名称为ASCII（简化） */
        int name_len = 0;
        for (int j = 0; j < 36 && name_len < PARTITION_NAME_LEN - 1; j++) {
            if (entry->name[j] == 0) break;
            if (entry->name[j] < 128) {
                parts[count].name[name_len++] = (char)entry->name[j];
            }
        }
        parts[count].name[name_len] = 0;
        if (name_len == 0) {
            snprintf(parts[count].name, PARTITION_NAME_LEN, "gpt_part%d", i);
        }

        strcpy(parts[count].fs_type, "unknown");

        vga_printf("[PART] GPT partition %d: %s start=%lld size=%lld\n",
                   i, parts[count].name,
                   (long long)parts[count].start_lba,
                   (long long)parts[count].size_sectors);
        count++;
    }

    return count;
}

/* 自动检测分区表类型并解析 */
int partition_detect(const uint8_t *mbr_sector, partition_info_t *parts, int max)
{
    if (!mbr_sector || !parts || max <= 0) return -1;

    mbr_t *mbr = (mbr_t *)mbr_sector;

    /* 检查是否为GPT保护MBR */
    if (mbr->signature == 0xAA55 &&
        mbr->partitions[0].type == PART_TYPE_GPT) {
        vga_puts("[PART] detected GPT partition table ( protected MBR)\n");
        /* GPT需要额外读取头和分区项，这里简化返回MBR解析 */
        return partition_parse_mbr(mbr_sector, parts, max);
    }

    /* 标准MBR */
    if (mbr->signature == 0xAA55) {
        vga_puts("[PART] detected MBR partition table \n");
        int count = partition_parse_mbr(mbr_sector, parts, max);
        if (count > 0) {
            memcpy(detected_partitions, parts,
                   count * sizeof(partition_info_t));
            partition_count = count;
        }
        return count;
    }

    vga_puts("[PART] unrecognized partition table format \n");
    return -1;
}

/* 获取分区类型名称 */
const char *partition_type_name(uint8_t type)
{
    switch (type) {
        case PART_TYPE_EMPTY:     return "Empty";
        case PART_TYPE_FAT32:     return "FAT32";
        case PART_TYPE_NTFS:      return "NTFS";
        case PART_TYPE_EXT2:      return "Linux/ext2";
        case PART_TYPE_LINUX_SWAP: return "Linux Swap";
        case PART_TYPE_EXTENDED:  return "Extended";
        case PART_TYPE_GPT:       return "GPT Protective";
        default:                  return "Unknown";
    }
}

/* 列出已检测的分区 */
int partition_list(partition_info_t *parts, int max)
{
    if (!parts || max <= 0) return 0;
    int count = partition_count < max ? partition_count : max;
    for (int i = 0; i < count; i++) {
        parts[i] = detected_partitions[i];
    }
    return count;
}
