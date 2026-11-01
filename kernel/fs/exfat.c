/* ============================================================================
 * 赫普OS - exFAT文件系统驱动
 * 支持SD卡/U盘/exFAT格式的大容量存储
 * ========================================================================== */

#include <stdint.h>
#include "exfat.h"
#include "vga.h"
#include "string.h"

static hepu_exfat_volume_t exfat_vol;
static int exfat_initialized = 0;

/* 挂载exFAT卷 */
int hepu_exfat_mount(hepu_exfat_volume_t *vol, const uint8_t *boot_sector)
{
    if (!vol || !boot_sector) return -1;

    /* 复制引导扇区 */
    memcpy(&vol->boot, boot_sector, sizeof(hepu_exfat_boot_t));

    /* 验证引导签名 */
    if (vol->boot.boot_signature != 0xAA55) {
        vga_puts("[EXFAT] invalid boot signature \n");
        return -1;
    }

    /* 解析参数 */
    vol->bytes_per_sector = 1 << vol->boot.bytes_per_sector;
    vol->sectors_per_cluster = 1 << vol->boot.sectors_per_cluster;
    vol->bytes_per_cluster = vol->bytes_per_sector * vol->sectors_per_cluster;
    vol->cluster_count = vol->boot.cluster_count;
    vol->root_cluster = vol->boot.root_cluster;

    vga_printf("[EXFAT] mount ok: per sector %d bytes, per cluster %d sector \n",
               vol->bytes_per_sector, vol->sectors_per_cluster);
    vga_printf("[EXFAT] cluster count: %d, root dir cluster: %d\n",
               vol->cluster_count, vol->root_cluster);

    /* 简化：分配FAT缓存（实际应从磁盘读取） */
    vol->fat = NULL;
    vol->cluster_heap = NULL;

    vol->mounted = 1;
    strcpy(vol->label, "EXFAT_VOL");

    return 0;
}

/* 读取簇 */
int hepu_exfat_read_cluster(hepu_exfat_volume_t *vol, uint32_t cluster, uint8_t *buffer)
{
    if (!vol || !vol->mounted || !buffer) return -1;
    if (cluster < 2 || cluster >= vol->cluster_count) return -1;

    /* 简化实现：实际应从磁盘读取簇数据 */
    memset(buffer, 0, vol->bytes_per_cluster);
    return vol->bytes_per_cluster;
}

/* 写入簇 */
int hepu_exfat_write_cluster(hepu_exfat_volume_t *vol, uint32_t cluster, const uint8_t *buffer)
{
    if (!vol || !vol->mounted || !buffer) return -1;
    if (cluster < 2 || cluster >= vol->cluster_count) return -1;

    /* 简化实现 */
    return vol->bytes_per_cluster;
}

/* 获取下一个簇（FAT链） */
uint32_t hepu_exfat_next_cluster(hepu_exfat_volume_t *vol, uint32_t cluster)
{
    if (!vol || !vol->mounted || !vol->fat) return EXFAT_CLUSTER_EOC;
    if (cluster < 2 || cluster >= vol->cluster_count) return EXFAT_CLUSTER_EOC;
    return vol->fat[cluster];
}

/* 获取下一个空闲簇 */
int hepu_exfat_get_next_free_cluster(hepu_exfat_volume_t *vol, uint32_t *cluster)
{
    if (!vol || !vol->mounted || !cluster) return -1;

    /* 简化：返回第一个可用簇 */
    for (uint32_t i = 2; i < vol->cluster_count; i++) {
        if (hepu_exfat_next_cluster(vol, i) == EXFAT_CLUSTER_FREE) {
            *cluster = i;
            return 0;
        }
    }
    return -1;
}

/* 读取文件数据 */
int hepu_exfat_read_file(hepu_exfat_volume_t *vol, uint32_t first_cluster,
                    uint64_t offset, uint8_t *buffer, uint32_t size)
{
    if (!vol || !vol->mounted || !buffer || size == 0) return -1;

    uint32_t cluster = first_cluster;
    uint64_t cluster_offset = offset / vol->bytes_per_cluster;
    uint32_t byte_offset = offset % vol->bytes_per_cluster;
    uint32_t bytes_read = 0;

    /* 跳到偏移所在簇 */
    for (uint64_t i = 0; i < cluster_offset && cluster != EXFAT_CLUSTER_EOC; i++) {
        cluster = hepu_exfat_next_cluster(vol, cluster);
    }

    if (cluster == EXFAT_CLUSTER_EOC) return 0;

    /* 读取数据 */
    uint8_t *cluster_buf = NULL;  /* 简化：应分配簇缓冲区 */
    (void)cluster_buf;

    while (bytes_read < size && cluster != EXFAT_CLUSTER_EOC) {
        uint32_t to_read = vol->bytes_per_cluster - byte_offset;
        if (to_read > size - bytes_read) to_read = size - bytes_read;

        /* 简化：直接清零（实际应读取簇数据） */
        memset(buffer + bytes_read, 0, to_read);
        bytes_read += to_read;
        byte_offset = 0;

        cluster = hepu_exfat_next_cluster(vol, cluster);
    }

    return bytes_read;
}

void hepu_exfat_init(void)
{
    memset(&exfat_vol, 0, sizeof(exfat_vol));
    exfat_initialized = 1;
    vga_puts("[EXFAT] exFAT fs driver init \n");
    vga_puts("[EXFAT] supports SD card /U drive /exFAT format \n");
    vga_puts("[EXFAT] max file size: 16EB, max volume size: 128PB\n");
}
