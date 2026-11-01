/* ============================================================================
 * 赫普OS - 存储分区显存驱动 (Storage-Backed VRAM)
 * 从存储设备划分专用分区作为显存使用，支持无显卡/小显存设备
 *
 * 分区格式:
 *   +------------------+ 0
 *   |  VRAM分区头      | 4KB (魔数/版本/大小/位图偏移)
 *   +------------------+ 4096
 *   |  分配位图        | size/8 字节 (每4KB块一位)
 *   +------------------+ 4096 + bitmap_size
 *   |  显存数据区      | 剩余空间
 *   +------------------+
 *
 * V6.77: 初始实现
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "vga.h"
#include "kheap.h"
#include "serial.h"

/* 分区魔数: "HEPUVRAM" */
#define SBVR_MAGIC        0x4D41525655504548ULL  /* "HEPUVRAM" little-endian */
#define SBVR_VERSION      1
#define SBVR_BLOCK_SIZE   4096
#define SBVR_HEADER_SIZE  4096

/* 分区头部 */
typedef struct {
    uint64_t magic;           /* 魔数 */
    uint32_t version;         /* 版本号 */
    uint32_t header_size;     /* 头部大小 */
    uint64_t total_size;      /* 分区总大小 */
    uint64_t data_offset;     /* 数据区偏移 */
    uint64_t bitmap_offset;   /* 位图偏移 */
    uint32_t bitmap_size;     /* 位图大小 */
    uint32_t total_blocks;    /* 总块数 */
    uint32_t used_blocks;     /* 已用块数 */
    uint32_t block_size;      /* 块大小 */
    char     label[32];       /* 分区标签 */
    uint8_t  reserved[4096 - 8 - 4 - 4 - 8 - 8 - 8 - 4 - 4 - 4 - 4 - 32];
} sbvr_header_t;

/* 存储-backed显存区域状态 */
typedef struct {
    int      initialized;
    int      dev_id;          /* 存储设备ID */
    uint64_t start_lba;       /* 分区起始LBA */
    uint64_t total_sectors;   /* 总扇区数 */
    uint64_t total_size;      /* 总大小 */
    uint32_t total_blocks;    /* 总块数 */
    uint32_t used_blocks;     /* 已用块数 */
    uint8_t *bitmap;          /* 内存中的分配位图缓存 */
    uint8_t *cache;           /* 读写缓存 (1MB) */
    uint32_t cache_block;     /* 当前缓存的块号 */
    int      cache_dirty;     /* 缓存是否脏 */
    int      cache_valid;     /* 缓存是否有效 */
} sbvr_t;

static sbvr_t sbvr;
static int sbvr_available = 0;

/* 向前声明: IDE读写 */
extern int ide_read_sectors(uint32_t lba, uint8_t *buffer, uint32_t count);
extern int ide_write_sectors(uint32_t lba, const uint8_t *buffer, uint32_t count);

/* 读取分区头部 */
static int sbvr_read_header(sbvr_header_t *hdr)
{
    if (!sbvr.initialized) return -1;
    return ide_read_sectors((uint32_t)(sbvr.start_lba), (uint8_t *)hdr, 8);  /* 8扇区=4KB */
}

/* 写入分区头部 */
static int sbvr_write_header(sbvr_header_t *hdr)
{
    if (!sbvr.initialized) return -1;
    return ide_write_sectors((uint32_t)(sbvr.start_lba), (const uint8_t *)hdr, 8);
}

/* 读取位图到内存 */
static int sbvr_load_bitmap(void)
{
    if (!sbvr.initialized) return -1;
    sbvr_header_t hdr;
    if (sbvr_read_header(&hdr) != 0) return -1;

    sbvr.bitmap = (uint8_t *)kmalloc(hdr.bitmap_size);
    if (!sbvr.bitmap) return -2;

    uint32_t bitmap_lba = (uint32_t)(sbvr.start_lba + hdr.bitmap_offset / 512);
    uint32_t bitmap_sectors = (hdr.bitmap_size + 511) / 512;
    return ide_read_sectors(bitmap_lba, sbvr.bitmap, bitmap_sectors);
}

/* 写回位图到存储 */
static int sbvr_save_bitmap(void)
{
    if (!sbvr.initialized || !sbvr.bitmap) return -1;
    sbvr_header_t hdr;
    if (sbvr_read_header(&hdr) != 0) return -1;

    hdr.used_blocks = sbvr.used_blocks;
    sbvr_write_header(&hdr);

    uint32_t bitmap_lba = (uint32_t)(sbvr.start_lba + hdr.bitmap_offset / 512);
    uint32_t bitmap_sectors = (hdr.bitmap_size + 511) / 512;
    return ide_write_sectors(bitmap_lba, sbvr.bitmap, bitmap_sectors);
}

/* 刷新缓存 */
static int sbvr_flush_cache(void)
{
    if (!sbvr.cache_valid || !sbvr.cache_dirty) return 0;

    sbvr_header_t hdr;
    if (sbvr_read_header(&hdr) != 0) return -1;

    uint64_t data_lba = sbvr.start_lba + hdr.data_offset / 512;
    uint64_t block_lba = data_lba + (uint64_t)sbvr.cache_block * (SBVR_BLOCK_SIZE / 512);
    int ret = ide_write_sectors((uint32_t)block_lba, sbvr.cache, SBVR_BLOCK_SIZE / 512);
    if (ret == 0) sbvr.cache_dirty = 0;
    return ret;
}

/* 读取块到缓存 */
static int sbvr_load_block(uint32_t block_idx)
{
    if (sbvr.cache_valid && sbvr.cache_block == block_idx) return 0;

    sbvr_flush_cache();

    sbvr_header_t hdr;
    if (sbvr_read_header(&hdr) != 0) return -1;

    uint64_t data_lba = sbvr.start_lba + hdr.data_offset / 512;
    uint64_t block_lba = data_lba + (uint64_t)block_idx * (SBVR_BLOCK_SIZE / 512);
    int ret = ide_read_sectors((uint32_t)block_lba, sbvr.cache, SBVR_BLOCK_SIZE / 512);
    if (ret == 0) {
        sbvr.cache_block = block_idx;
        sbvr.cache_valid = 1;
        sbvr.cache_dirty = 0;
    }
    return ret;
}

/* 初始化存储-backed显存分区 */
int sbvr_init(uint32_t dev_id, uint64_t start_lba, uint64_t total_sectors)
{
    memset(&sbvr, 0, sizeof(sbvr));
    sbvr.dev_id = dev_id;
    sbvr.start_lba = start_lba;
    sbvr.total_sectors = total_sectors;
    sbvr.total_size = total_sectors * 512;

    /* 分配1MB读写缓存 */
    sbvr.cache = (uint8_t *)kmalloc(SBVR_BLOCK_SIZE);
    if (!sbvr.cache) {
        vga_puts("[SBVR] failed to alloc cache\n");
        return -1;
    }
    sbvr.cache_valid = 0;
    sbvr.cache_dirty = 0;

    /* 读取并验证头部 */
    sbvr_header_t hdr;
    if (ide_read_sectors((uint32_t)start_lba, (uint8_t *)&hdr, 8) < 8) {
        vga_puts("[SBVR] failed to read header\n");
        kfree(sbvr.cache);
        return -2;
    }

    if (hdr.magic != SBVR_MAGIC) {
        vga_printf("[SBVR] invalid magic 0x%llx (expected 0x%llx)\n",
                   (unsigned long long)hdr.magic, (unsigned long long)SBVR_MAGIC);
        kfree(sbvr.cache);
        return -3;
    }

    sbvr.total_blocks = hdr.total_blocks;
    sbvr.used_blocks = hdr.used_blocks;
    sbvr.initialized = 1;

    /* 加载位图 */
    if (sbvr_load_bitmap() != 0) {
        vga_puts("[SBVR] failed to load bitmap\n");
        kfree(sbvr.cache);
        sbvr.initialized = 0;
        return -4;
    }

    sbvr_available = 1;
    vga_printf("[SBVR] storage VRAM mounted: %dMB, %d blocks, %d used\n",
               (uint32_t)(sbvr.total_size / (1024 * 1024)),
               sbvr.total_blocks, sbvr.used_blocks);
    return 0;
}

/* 在存储设备上创建显存分区 */
int sbvr_format(uint32_t dev_id, uint64_t start_lba, uint64_t total_sectors,
                const char *label)
{
    sbvr_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));

    hdr.magic = SBVR_MAGIC;
    hdr.version = SBVR_VERSION;
    hdr.header_size = SBVR_HEADER_SIZE;
    hdr.total_size = total_sectors * 512;
    hdr.block_size = SBVR_BLOCK_SIZE;
    hdr.total_blocks = (uint32_t)(hdr.total_size / SBVR_BLOCK_SIZE);
    hdr.used_blocks = 0;
    hdr.bitmap_offset = SBVR_HEADER_SIZE;
    hdr.bitmap_size = (hdr.total_blocks + 7) / 8;
    hdr.data_offset = SBVR_HEADER_SIZE + ((hdr.bitmap_size + 4095) & ~4095);

    if (label) {
        strncpy(hdr.label, label, 31);
        hdr.label[31] = '\0';
    } else {
        strcpy(hdr.label, "HepuVRAM");
    }

    /* 写入头部 */
    if (ide_write_sectors((uint32_t)start_lba, (const uint8_t *)&hdr, 8) < 8) {
        vga_puts("[SBVR] failed to write header\n");
        return -1;
    }

    /* 清零位图 */
    uint32_t bitmap_sectors = (hdr.bitmap_size + 511) / 512;
    uint8_t *zero_buf = (uint8_t *)kmalloc(bitmap_sectors * 512);
    if (zero_buf) {
        memset(zero_buf, 0, bitmap_sectors * 512);
        uint32_t bitmap_lba = (uint32_t)(start_lba + hdr.bitmap_offset / 512);
        ide_write_sectors(bitmap_lba, zero_buf, bitmap_sectors);
        kfree(zero_buf);
    }

    vga_printf("[SBVR] partition formatted: %dMB, label=%s\n",
               (uint32_t)(hdr.total_size / (1024 * 1024)), hdr.label);
    return 0;
}

/* 分配显存块 */
int sbvr_alloc_block(void)
{
    if (!sbvr_available) return -1;

    for (uint32_t i = 0; i < sbvr.total_blocks; i++) {
        if (!(sbvr.bitmap[i / 8] & (1 << (i % 8)))) {
            sbvr.bitmap[i / 8] |= (1 << (i % 8));
            sbvr.used_blocks++;
            sbvr_save_bitmap();
            return (int)i;
        }
    }
    return -1;  /* 空间不足 */
}

/* 释放显存块 */
int sbvr_free_block(uint32_t block_idx)
{
    if (!sbvr_available || block_idx >= sbvr.total_blocks) return -1;

    sbvr.bitmap[block_idx / 8] &= ~(1 << (block_idx % 8));
    sbvr.used_blocks--;
    sbvr_save_bitmap();
    return 0;
}

/* 读取显存数据 */
int sbvr_read(uint32_t block_idx, uint32_t offset, void *buf, uint32_t size)
{
    if (!sbvr_available || block_idx >= sbvr.total_blocks) return -1;
    if (offset + size > SBVR_BLOCK_SIZE) return -2;

    if (sbvr_load_block(block_idx) != 0) return -3;
    memcpy(buf, sbvr.cache + offset, size);
    return 0;
}

/* 写入显存数据 */
int sbvr_write(uint32_t block_idx, uint32_t offset, const void *buf, uint32_t size)
{
    if (!sbvr_available || block_idx >= sbvr.total_blocks) return -1;
    if (offset + size > SBVR_BLOCK_SIZE) return -2;

    if (sbvr_load_block(block_idx) != 0) return -3;
    memcpy(sbvr.cache + offset, buf, size);
    sbvr.cache_dirty = 1;
    return 0;
}

/* 同步所有脏数据到存储 */
int sbvr_sync(void)
{
    if (!sbvr_available) return -1;
    sbvr_flush_cache();
    sbvr_save_bitmap();
    return 0;
}

/* 获取统计信息 */
void sbvr_stats(uint32_t *total_mb, uint32_t *used_mb, uint32_t *free_mb)
{
    if (!sbvr_available) {
        if (total_mb) *total_mb = 0;
        if (used_mb) *used_mb = 0;
        if (free_mb) *free_mb = 0;
        return;
    }
    if (total_mb) *total_mb = (uint32_t)(sbvr.total_size / (1024 * 1024));
    if (used_mb) *used_mb = (sbvr.used_blocks * SBVR_BLOCK_SIZE) / (1024 * 1024);
    if (free_mb) *free_mb = ((sbvr.total_blocks - sbvr.used_blocks) * SBVR_BLOCK_SIZE) / (1024 * 1024);
}

int sbvr_is_available(void)
{
    return sbvr_available;
}
