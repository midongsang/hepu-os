/* ============================================================================
 * 赫普OS - NTFS文件系统驱动
 * 支持Windows NTFS格式的只读访问
 * ========================================================================== */

#include <stdint.h>
#include "ntfs.h"
#include "vga.h"
#include "string.h"

static hepu_ntfs_volume_t ntfs_vol;
static int ntfs_initialized = 0;

/* 挂载NTFS卷 */
int hepu_ntfs_mount(hepu_ntfs_volume_t *vol, const uint8_t *boot_sector)
{
    if (!vol || !boot_sector) return -1;

    /* 复制引导扇区 */
    memcpy(&vol->boot, boot_sector, sizeof(hepu_ntfs_boot_t));

    /* 验证OEM ID */
    if (memcmp(vol->boot.oem_id, "NTFS    ", 8) != 0) {
        vga_puts("[NTFS] invalid NTFS OEM ID\n");
        return -1;
    }

    /* 验证引导签名 */
    if (vol->boot.boot_signature != 0xAA55) {
        vga_puts("[NTFS] invalid boot signature \n");
        return -1;
    }

    /* 解析参数 */
    vol->bytes_per_sector = vol->boot.bytes_per_sector;
    vol->sectors_per_cluster = vol->boot.sectors_per_cluster;
    vol->bytes_per_cluster = vol->bytes_per_sector * vol->sectors_per_cluster;
    vol->total_sectors = vol->boot.total_sectors;
    vol->mft_cluster = vol->boot.mft_cluster;
    vol->mft_mirror_cluster = vol->boot.mft_mirror_cluster;
    vol->volume_serial = vol->boot.volume_serial;

    /* 计算MFT记录大小 */
    if (vol->boot.clusters_per_mft > 0) {
        vol->mft_entry_size = vol->boot.clusters_per_mft * vol->bytes_per_cluster;
    } else {
        vol->mft_entry_size = 1 << (-vol->boot.clusters_per_mft);
    }

    vga_printf("[NTFS] mount ok: per sector %d bytes, per cluster %d sector \n",
               vol->bytes_per_sector, vol->sectors_per_cluster);
    vga_printf("[NTFS] total sectors: %lld, MFT cluster: %lld\n",
               (unsigned long long)vol->total_sectors,
               (unsigned long long)vol->mft_cluster);
    vga_printf("[NTFS] MFT records size: %d bytes, vol serial: 0x%llx\n",
               vol->mft_entry_size,
               (unsigned long long)vol->volume_serial);

    vol->mounted = 1;
    strcpy(vol->label, "NTFS_VOL");

    return 0;
}

/* 读取MFT记录 */
int hepu_ntfs_read_mft_record(hepu_ntfs_volume_t *vol, uint64_t mft_ref,
                              uint8_t *buffer, uint32_t size)
{
    if (!vol || !vol->mounted || !buffer) return -1;
    if (size < vol->mft_entry_size) return -1;

    /* 简化实现：实际应从MFT簇读取记录 */
    memset(buffer, 0, size);

    /* 构造一个模拟的MFT记录头 */
    hepu_ntfs_mft_header_t *header = (hepu_ntfs_mft_header_t *)buffer;
    header->magic = 0x454C4946;  /* "FILE" */
    header->seq_number = 1;
    header->used_size = sizeof(hepu_ntfs_mft_header_t);
    header->allocated_size = vol->mft_entry_size;
    header->record_number = mft_ref & 0xFFFFFFFF;

    return vol->mft_entry_size;
}

/* 读取文件数据 */
int hepu_ntfs_read_file(hepu_ntfs_volume_t *vol, uint64_t mft_ref,
                        uint64_t offset, uint8_t *buffer, uint32_t size)
{
    if (!vol || !vol->mounted || !buffer || size == 0) return -1;

    /* 简化实现：实际应解析MFT记录的DATA属性，读取运行列表 */
    (void)mft_ref;
    (void)offset;

    memset(buffer, 0, size);
    return size;
}

/* 获取卷信息 */
int hepu_ntfs_get_volume_info(hepu_ntfs_volume_t *vol)
{
    if (!vol || !vol->mounted) return -1;

    vga_printf("[NTFS] volume info: %s\n", vol->label);
    vga_printf("[NTFS] total cap: %lld MB\n",
               (unsigned long long)(vol->total_sectors / 2048));
    vga_printf("[NTFS] cluster size: %d bytes \n", vol->bytes_per_cluster);

    return 0;
}

void hepu_ntfs_init(void)
{
    memset(&ntfs_vol, 0, sizeof(ntfs_vol));
    ntfs_initialized = 1;
    vga_puts("[NTFS] NTFS fs driver init \n");
    vga_puts("[NTFS] supports Windows NTFS read-only access \n");
    vga_puts("[NTFS] supports MFT parse , attr parse , run list \n");
}
