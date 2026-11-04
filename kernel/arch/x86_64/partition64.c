/* ============================================================================
 * 赫普OS 64位 - 分区表扫描（P1, V0.13.51）
 * 读取MBR → 识别四主分区 → 检测ext2文件系统格式
 * 为 /cfg(用户配置分区) /data(自由分区) 开机挂载表打地基
 * ========================================================================== */
#include <stdint.h>

#define PART_SECTOR_SIZE  512
#define MBR_PART_START    0x1BE
#define MBR_PART_ENTRY    16
#define EXT2_MAGIC_OFFSET (1024 + 56)   /* 超级块偏移1024, 魔数在+56 */

typedef struct {
    int     present;      /* 分区项有效 */
    uint8_t type;         /* 分区类型(0x83=Linux) */
    uint32_t lba_start;   /* 起始扇区 */
    uint32_t sectors;     /* 扇区数 */
    int     fs_ext2;      /* 1=ext2文件系统, 0=未格式化/未知 */
} partition64_info_t;

/* 扫描MBR分区表: parts[4]. 返回0成功 */
int partition64_scan(partition64_info_t *parts)
{
    extern int storage_hal_read(uint32_t lba, uint8_t count, uint8_t *buffer);
    uint8_t mbr[PART_SECTOR_SIZE];

    if (storage_hal_read(0, 1, mbr) != 0) return -1;
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) return -2;  /* 无MBR签名 */

    for (int i = 0; i < 4; i++) {
        uint8_t *e = mbr + MBR_PART_START + i * MBR_PART_ENTRY;
        parts[i].present = 0;
        parts[i].type = 0;
        parts[i].lba_start = 0;
        parts[i].sectors = 0;
        parts[i].fs_ext2 = 0;

        uint32_t lba = (uint32_t)e[8] | ((uint32_t)e[9] << 8) |
                       ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
        uint32_t sz  = (uint32_t)e[12] | ((uint32_t)e[13] << 8) |
                       ((uint32_t)e[14] << 16) | ((uint32_t)e[15] << 24);
        if (lba == 0 && sz == 0) continue;   /* 空项 */

        parts[i].present = 1;
        parts[i].type = e[4];
        parts[i].lba_start = lba;
        parts[i].sectors = sz;

        /* 检测ext2: 读分区偏移1024字节处的超级块魔数 0xEF53(小端) */
        if (sz > 4) {
            uint8_t sb[2048];
            if (storage_hal_read(lba + 2, 4, sb) == 0) {
                uint16_t magic = (uint16_t)sb[EXT2_MAGIC_OFFSET - 1024] |
                                 ((uint16_t)sb[EXT2_MAGIC_OFFSET - 1024 + 1] << 8);
                if (magic == 0xEF53) parts[i].fs_ext2 = 1;
            }
        }
    }
    return 0;
}
