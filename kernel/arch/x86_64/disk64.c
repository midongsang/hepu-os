/* ============================================================================
 * 赫普OS 64位 - 磁盘管理工具（底层存储/文件系统状态与检查）
 * V0.13.21: P1 系统底层 — 磁盘管理
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);
extern int storage_hal_get_info(uint32_t *total_sectors, uint32_t *sector_size);
extern int storage_hal_is_initialized(void);
extern int ext2_64_get_fs_info_ex(uint64_t *total_blocks, uint64_t *free_blocks,
                                  uint32_t *block_size, uint64_t *total_inodes,
                                  uint64_t *free_inodes, uint32_t *groups,
                                  char *volume_name);
extern int ext2_64_is_mounted(void);
extern int ext2_fsck_run(void *superblock_ptr, void *gdt_ptr);
extern void ext2_fsck_get_stats(void *stats);
extern int ext2_64_get_superblock(void **sb, void **gdt);

/* ---------- 十进制输出工具 ---------- */
static void disk_put_dec_u64(uint64_t v)
{
    char t[24];
    int tp = 0;
    if (v == 0) t[tp++] = '0';
    while (v && tp < 23) { t[tp++] = '0' + v % 10; v /= 10; }
    for (int k = 0; k < tp / 2; k++) { char c = t[k]; t[k] = t[tp - 1 - k]; t[tp - 1 - k] = c; }
    t[tp] = 0;
    vga_puts(t);
}

/* 容量格式化: MB / GB */
static void disk_put_size(uint64_t bytes)
{
    if (bytes >= (uint64_t)1024 * 1024 * 1024) {
        disk_put_dec_u64(bytes / (1024 * 1024 * 1024));
        vga_puts(".");
        disk_put_dec_u64((bytes % (1024 * 1024 * 1024)) / (100 * 1024 * 1024));
        vga_puts(" GB");
    } else if (bytes >= 1024 * 1024) {
        disk_put_dec_u64(bytes / (1024 * 1024));
        vga_puts(".");
        disk_put_dec_u64((bytes % (1024 * 1024)) / (100 * 1024));
        vga_puts(" MB");
    } else {
        disk_put_dec_u64(bytes / 1024);
        vga_puts(" KB");
    }
}

/* ---------- 1. 磁盘信息 ---------- */
int disk64_info(void)
{
    uint32_t total_sectors = 0, sector_size = 512;
    if (storage_hal_is_initialized()) {
        storage_hal_get_info(&total_sectors, &sector_size);
    }

    vga_puts("== Disk Device ==\n");
    if (total_sectors > 0) {
        vga_puts("  Storage: ");
        disk_put_dec_u64((uint64_t)total_sectors * sector_size);
        vga_puts(" bytes (");
        disk_put_size((uint64_t)total_sectors * sector_size);
        vga_puts(")\n");
        vga_puts("  Sectors: ");
        disk_put_dec_u64(total_sectors);
        vga_puts(" x ");
        disk_put_dec_u64(sector_size);
        vga_puts(" B\n");
    } else {
        vga_puts("  Storage: not initialized\n");
    }

    vga_puts("== Filesystem (ext2) ==\n");
    if (!ext2_64_is_mounted()) {
        vga_puts("  Not mounted\n");
        return 0;
    }
    uint64_t tb = 0, fb = 0, ti = 0, fi = 0;
    uint32_t bs = 1024, groups = 0;
    char vol[32];
    ext2_64_get_fs_info_ex(&tb, &fb, &bs, &ti, &fi, &groups, vol);

    uint64_t total_bytes = tb * bs;
    uint64_t used_bytes = (tb - fb) * bs;
    uint64_t free_bytes = fb * bs;

    vga_puts("  Volume: ");
    if (vol[0]) { vga_puts(vol); } else { vga_puts("(unnamed)"); }
    vga_puts("\n");
    vga_puts("  Total: ");
    disk_put_size(total_bytes);
    vga_puts("\n  Used: ");
    disk_put_size(used_bytes);
    vga_puts("\n  Free: ");
    disk_put_size(free_bytes);
    vga_puts("\n  Blocks: ");
    disk_put_dec_u64(tb);
    vga_puts(" (");
    disk_put_dec_u64(bs);
    vga_puts(" B), free ");
    disk_put_dec_u64(fb);
    vga_puts("\n  Inodes: ");
    disk_put_dec_u64(ti);
    vga_puts(" total, ");
    disk_put_dec_u64(fi);
    vga_puts(" free, ");
    disk_put_dec_u64(groups);
    vga_puts(" groups\n");

    /* 使用率 */
    if (tb > 0) {
        vga_puts("  Usage: ");
        disk_put_dec_u64((tb - fb) * 100 / tb);
        vga_puts("%\n");
    }
    return 0;
}

/* ---------- 2. 文件系统检查 ---------- */
int disk64_fsck(void)
{
    vga_puts("== ext2 fsck ==\n");
    if (!ext2_64_is_mounted()) {
        vga_puts("  Filesystem not mounted\n");
        return -1;
    }
    void *sb = 0, *gdt = 0;
    if (ext2_64_get_superblock(&sb, &gdt) != 0 || !sb) {
        vga_puts("  Cannot access superblock\n");
        return -1;
    }
    int rc = ext2_fsck_run(sb, gdt);
    if (rc == 0) {
        vga_puts("  fsck: PASS (no corruption)\n");
    } else if (rc > 0) {
        vga_puts("  fsck: issues found, see stats below\n");
    } else {
        vga_puts("  fsck: failed to run\n");
        return -1;
    }
    return rc;
}

/* ---------- 3. 设备列表 ---------- */
int disk64_list(void)
{
    vga_puts("== Storage Devices ==\n");
    if (storage_hal_is_initialized()) {
        vga_puts("  [0] hda - storage_hal (IDE/AHCI) - active\n");
    } else {
        vga_puts("  (no storage device initialized)\n");
    }
    if (ext2_64_is_mounted()) {
        vga_puts("  [fs] ext2 mounted at /\n");
    } else {
        vga_puts("  [fs] ext2 not mounted\n");
    }
    return 0;
}
