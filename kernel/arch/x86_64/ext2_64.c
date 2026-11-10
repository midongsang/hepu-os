/* ============================================================================
 * 赫普OS - 64位ext2文件系统（简化版）
 * V0.10.6: 初始实现（只读支持）
 * V0.10.76: 引用共享ext2数据结构（统一源码树第一步）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* 共享ext2数据结构（架构无关） */
#include "../../shared/fs/ext2_shared.h"
/* 共享ext2算法（统一源码树P0-2） */
#include "../../shared/fs/ext2_algo.h"

/* 外部函数 */
extern void vga_puts(const char *str);
extern void vga_put_hex(uint64_t val);
extern void *memcpy(void *dst, const void *src, size_t n);
extern void *memset(void *dst, int val, size_t n);
extern int ide64_read_sector(uint32_t lba, uint8_t *buffer);
extern int ide64_read_sectors(uint32_t lba, uint8_t count, uint8_t *buffer);

/* V0.13.36: 块设备切换 (默认IDE; USB存储挂载后切到USB) */
static int (*g_ext2_block_read)(uint32_t lba, uint8_t count, uint8_t *buf) =
    ide64_read_sectors;
static int g_ext2_dev_sel = 0;   /* 0=IDE 1=USB */

void ext2_64_set_block_device(int (*rd)(uint32_t, uint8_t, uint8_t *))
{
    if (rd) g_ext2_block_read = rd;
    else g_ext2_block_read = ide64_read_sectors;
}

int ext2_64_get_block_device(void) { return g_ext2_dev_sel; }

void ext2_64_set_device_sel(int sel) { g_ext2_dev_sel = sel; }
/* 存储硬件抽象层（通过驱动框架调用） */
extern int storage_hal_read(uint32_t lba, uint8_t count, uint8_t *buffer);
extern int storage_hal_write(uint32_t lba, uint8_t count, const uint8_t *buffer);

/* 前向声明 */
static int ext2_write_block(uint32_t block, const uint8_t *buffer);

/* ext2常量 */
#define EXT2_MAX_BLOCK_SIZE 4096  /* 最大块大小 */
#define EXT2_SECTOR_SIZE    512

/* 全局状态 */
static ext2_superblock_t ext2_sb;
static ext2_group_desc_t ext2_gdt[32];  /* 最多32个块组 */
uint32_t ext2_block_size;
static uint32_t ext2_groups_count;
static int ext2_mounted = 0;
int ext2_64_is_mounted(void) { return ext2_mounted; }
static uint32_t ext2_partition_offset = 0;  /* 分区起始LBA（单镜像模式） */

/* 共享算法上下文（统一源码树P0-2） */
static ext2_context_t ext2_ctx;

/* ============================================================================
 * V0.13.48: ext2 块缓存（LRU, 64槽）
 * 大文件连续读时块命中缓存，避免每块都走磁盘裸读（P0 存储验收项）
 * 只缓存读；写路径失效对应槽
 * ========================================================================== */
#define EXT2_CACHE_SLOTS 64
typedef struct {
    uint32_t block;            /* ext2 块号, 0xFFFFFFFF=空槽 */
    uint32_t lba;              /* 对应物理 LBA（分区偏移变化时防误命中） */
    uint32_t lru;              /* 单调递增访问计数 */
    uint8_t *data;             /* V0.13.48: 动态分配 4KB 页 (避免 BSS 膨胀覆盖页表) */
} ext2_cache_slot_t;
static ext2_cache_slot_t ext2_cache[EXT2_CACHE_SLOTS];
static uint32_t ext2_cache_hits = 0;
static uint32_t ext2_cache_misses = 0;
static uint32_t ext2_cache_seq = 0;

/* V0.13.49: 缓存初始化（mount/init 共用; data 动态分配避免 BSS 越界） */
static void ext2_cache_init(void)
{
    extern void *kmalloc(uint64_t size);
    for (int ci = 0; ci < EXT2_CACHE_SLOTS; ci++) {
        ext2_cache[ci].block = 0xFFFFFFFF;
        ext2_cache[ci].lru = 0;
        ext2_cache[ci].data = (uint8_t *)kmalloc(EXT2_MAX_BLOCK_SIZE);
    }
    ext2_cache_hits = 0;
    ext2_cache_misses = 0;
    ext2_cache_seq = 0;
}

/* 失效指定块的缓存（写后调用） */
void ext2_cache_invalidate(uint32_t block)
{
    for (int i = 0; i < EXT2_CACHE_SLOTS; i++) {
        if (ext2_cache[i].block == block) {
            ext2_cache[i].block = 0xFFFFFFFF;
        }
    }
}

/* V0.13.51: 清空全部缓存槽（内存压力回收） */
void ext2_cache_flush_all(void)
{
    for (int i = 0; i < EXT2_CACHE_SLOTS; i++) {
        ext2_cache[i].block = 0xFFFFFFFF;
    }
}

/* V0.13.51: 已用缓存槽数（内存压力统计） */
int ext2_cache_used_count(void)
{
    int n = 0;
    for (int i = 0; i < EXT2_CACHE_SLOTS; i++) {
        if (ext2_cache[i].block != 0xFFFFFFFF) n++;
    }
    return n;
}

/* 缓存命中统计（供调试/监控） */
void ext2_cache_get_stats(uint32_t *hits, uint32_t *misses)
{
    if (hits) *hits = ext2_cache_hits;
    if (misses) *misses = ext2_cache_misses;
}
static uint8_t ext2_algo_block_buf[EXT2_MAX_BLOCK_SIZE];
static int ext2_use_shared_algo = 0;  /* 是否使用共享算法 */
static uint32_t ext2_mount_offsets[4] = {0, 0, 0, 0};   /* V0.13.57 多实例挂载槽: 分区LBA */
static int ext2_mount_valid[4] = {1, 0, 0, 0};          /* 槽0=根(出厂/系统分区) */

/* 设置分区偏移 */
void ext2_64_set_partition_offset(uint32_t lba)
{
    ext2_partition_offset = lba;
}

/* 读取一个块到缓冲区 */
static int ext2_read_block(uint32_t block, uint8_t *buffer)
{
    uint32_t lba = block * (ext2_block_size / EXT2_SECTOR_SIZE) + ext2_partition_offset;
    uint8_t count = ext2_block_size / EXT2_SECTOR_SIZE;

    /* 查缓存 */
    for (int i = 0; i < EXT2_CACHE_SLOTS; i++) {
        if (ext2_cache[i].block == block && ext2_cache[i].lba == lba &&
            ext2_cache[i].data != NULL) {
            ext2_cache_hits++;
            ext2_cache[i].lru = ++ext2_cache_seq;
            memcpy(buffer, ext2_cache[i].data, ext2_block_size);
            return 0;
        }
    }
    ext2_cache_misses++;

    int ret = storage_hal_read(lba, count, buffer);
    if (ret != 0) {
        vga_put_hex(block);
        vga_puts(" lba=");
        vga_put_hex(lba);
        vga_puts(" ret=");
        vga_put_hex(ret);
        vga_puts("\n");
    }
    if (ret == 0) {
        /* 入缓存: 找空槽或 LRU 最旧槽 */
        int victim = -1;
        uint32_t oldest = 0xFFFFFFFF;
        for (int i = 0; i < EXT2_CACHE_SLOTS; i++) {
            if (ext2_cache[i].block == 0xFFFFFFFF) { victim = i; break; }
            if (ext2_cache[i].lru < oldest) { oldest = ext2_cache[i].lru; victim = i; }
        }
        if (victim >= 0 && ext2_cache[victim].data != NULL) {
            ext2_cache[victim].block = block;
            ext2_cache[victim].lba = lba;
            ext2_cache[victim].lru = ++ext2_cache_seq;
            memcpy(ext2_cache[victim].data, buffer, ext2_block_size);
        }
    }
    return ret;
}

/* 读取inode */
int ext2_read_inode(uint32_t ino, ext2_inode_t *inode)
{
    if (ext2_use_shared_algo) {
        return ext2_algo_read_inode(&ext2_ctx, ino, inode);
    }
    if (ino == 0) return -1;

    uint32_t group = (ino - 1) / ext2_sb.s_inodes_per_group;
    uint32_t index = (ino - 1) % ext2_sb.s_inodes_per_group;
    /* 安全修复：ino 可来自磁盘目录项（恶意镜像），组号必须落在已加载的
     * GDT 静态数组(32项)内，否则 ext2_gdt[group] 越界读 */
    if (group >= ext2_groups_count || group >= 32) return -1;
    uint32_t inode_table = ext2_gdt[group].bg_inode_table;
    uint32_t inode_size = ext2_sb.s_inode_size ? ext2_sb.s_inode_size : 128;
    uint32_t block_offset = (index * inode_size) / ext2_block_size;
    uint32_t byte_offset = (index * inode_size) % ext2_block_size;


    uint8_t block_buf[EXT2_MAX_BLOCK_SIZE];
    if (ext2_read_block(inode_table + block_offset, block_buf) != 0) {
        return -1;
    }

    memcpy(inode, block_buf + byte_offset, sizeof(ext2_inode_t));

    return 0;
}

/* 从inode读取数据块 - V0.10.22: 支持二级间接块 */
int ext2_read_data_block(ext2_inode_t *inode, uint32_t block_idx, uint8_t *buffer)
{
    if (ext2_use_shared_algo) {
        return ext2_algo_read_data_block(&ext2_ctx, inode, block_idx, buffer);
    }
    uint32_t block;

    if (block_idx < 12) {
        /* 直接块 */
        block = inode->i_block[block_idx];
    } else if (block_idx < 12 + 256) {
        /* 一级间接块 */
        uint32_t indirect = inode->i_block[12];
        if (indirect == 0) return -1;
        uint8_t ibuf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(indirect, ibuf) != 0) return -1;
        block = ((uint32_t *)ibuf)[block_idx - 12];
    } else if (block_idx < 12 + 256 + 65536) {
        /* 二级间接块 */
        uint32_t dbl_indirect = inode->i_block[13];
        if (dbl_indirect == 0) return -1;
        uint8_t dbl_buf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(dbl_indirect, dbl_buf) != 0) return -1;
        uint32_t idx = block_idx - (12 + 256);
        uint32_t indirect = ((uint32_t *)dbl_buf)[idx / 256];
        if (indirect == 0) return -1;
        uint8_t ibuf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(indirect, ibuf) != 0) return -1;
        block = ((uint32_t *)ibuf)[idx % 256];
    } else if (block_idx < 12 + 256 + 65536 + 16777216) {
        /* 三级间接块 */
        uint32_t tri_indirect = inode->i_block[14];
        if (tri_indirect == 0) return -1;
        uint8_t tri_buf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(tri_indirect, tri_buf) != 0) return -1;
        uint32_t idx = block_idx - (12 + 256 + 65536);
        uint32_t dbl_indirect = ((uint32_t *)tri_buf)[idx / 65536];
        if (dbl_indirect == 0) return -1;
        uint8_t dbl_buf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(dbl_indirect, dbl_buf) != 0) return -1;
        uint32_t idx2 = idx % 65536;
        uint32_t indirect = ((uint32_t *)dbl_buf)[idx2 / 256];
        if (indirect == 0) return -1;
        uint8_t ibuf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(indirect, ibuf) != 0) return -1;
        block = ((uint32_t *)ibuf)[idx2 % 256];
    } else {
        /* 超出范围 */
        return -1;
    }

    if (block == 0) return -1;
    return ext2_read_block(block, buffer);
}

/* 写入inode数据块 - V0.10.22: 支持二级间接块 */
static int ext2_write_data_block(ext2_inode_t *inode, uint32_t block_idx, const uint8_t *buffer)
{
    if (ext2_use_shared_algo) {
        return ext2_algo_write_data_block(&ext2_ctx, inode, block_idx, buffer);
    }
    uint32_t block;

    if (block_idx < 12) {
        block = inode->i_block[block_idx];
    } else if (block_idx < 12 + 256) {
        uint32_t indirect = inode->i_block[12];
        if (indirect == 0) return -1;
        uint8_t ibuf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(indirect, ibuf) != 0) return -1;
        block = ((uint32_t *)ibuf)[block_idx - 12];
    } else if (block_idx < 12 + 256 + 65536) {
        uint32_t dbl_indirect = inode->i_block[13];
        if (dbl_indirect == 0) return -1;
        uint8_t dbl_buf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(dbl_indirect, dbl_buf) != 0) return -1;
        uint32_t idx = block_idx - (12 + 256);
        uint32_t indirect = ((uint32_t *)dbl_buf)[idx / 256];
        if (indirect == 0) return -1;
        uint8_t ibuf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(indirect, ibuf) != 0) return -1;
        block = ((uint32_t *)ibuf)[idx % 256];
    } else if (block_idx < 12 + 256 + 65536 + 16777216) {
        /* 三级间接块 */
        uint32_t tri_indirect = inode->i_block[14];
        if (tri_indirect == 0) return -1;
        uint8_t tri_buf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(tri_indirect, tri_buf) != 0) return -1;
        uint32_t idx = block_idx - (12 + 256 + 65536);
        uint32_t dbl_indirect = ((uint32_t *)tri_buf)[idx / 65536];
        if (dbl_indirect == 0) return -1;
        uint8_t dbl_buf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(dbl_indirect, dbl_buf) != 0) return -1;
        uint32_t idx2 = idx % 65536;
        uint32_t indirect = ((uint32_t *)dbl_buf)[idx2 / 256];
        if (indirect == 0) return -1;
        uint8_t ibuf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(indirect, ibuf) != 0) return -1;
        block = ((uint32_t *)ibuf)[idx2 % 256];
    } else {
        return -1;
    }

    if (block == 0) return -1;
    return ext2_write_block(block, buffer);
}

/* 在目录中查找文件 */
static int ext2_find_in_dir(ext2_inode_t *dir_inode, const char *name, uint32_t *found_ino)
{
    if (ext2_use_shared_algo) {
        return ext2_algo_find_in_dir(&ext2_ctx, dir_inode, name, found_ino);
    }
    uint8_t block_buf[EXT2_MAX_BLOCK_SIZE];
    uint32_t blocks = (dir_inode->i_size + ext2_block_size - 1) / ext2_block_size;

    for (uint32_t b = 0; b < blocks; b++) {
        if (ext2_read_data_block(dir_inode, b, block_buf) != 0) continue;

        uint32_t offset = 0;
        uint32_t guard = 0;  /* SEC-18: 防止目录遍历死循环 */
        while (offset < ext2_block_size && guard++ < 256) {
            /* 安全修复：目录项来自磁盘（恶意镜像/USB），先校验边界再解引用，
             * 否则名字比较会越过 block_buf 读内核栈（信息泄露预言机） */
            if (offset + 8 > ext2_block_size) break;
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(block_buf + offset);
            if (entry->rec_len < 8) break;  /* rec_len最小8字节，异常则退出 */
            if (entry->rec_len > ext2_block_size - offset) break;

            if (entry->name_len > 0 && entry->name_len <= 255 &&
                8 + (uint32_t)entry->name_len <= entry->rec_len) {
                int match = 1;
                for (uint8_t i = 0; i < entry->name_len; i++) {
                    if (name[i] != entry->name[i]) { match = 0; break; }
                }
                if (match && name[entry->name_len] == '\0') {
                    *found_ino = entry->inode;
                    return 0;
                }
            }
            offset += entry->rec_len;
        }
    }
    return -1;
}

/* 按路径查找inode */
static int ext2_lookup(const char *path, uint32_t *ino)
{
    if (ext2_use_shared_algo) {
        return ext2_algo_lookup(&ext2_ctx, path, ino);
    }
    if (path[0] != '/') return -1;

    uint32_t current = EXT2_ROOT_INO;
    const char *p = path + 1;

    while (*p) {
        /* 提取路径分量 */
        char name[256];
        int len = 0;
        while (*p && *p != '/' && len < 255) {
            name[len++] = *p++;
        }
        name[len] = '\0';

        if (len == 0) {
            if (*p == '/') p++;
            continue;
        }

        /* 读取当前目录inode */
        ext2_inode_t inode;
        if (ext2_read_inode(current, &inode) != 0) return -1;

        /* 检查是否是目录 */
        if ((inode.i_mode & 0xF000) != 0x4000) return -1;

        /* 在目录中查找 */
        if (ext2_find_in_dir(&inode, name, &current) != 0) {
            return -1;
        }

        if (*p == '/') p++;
    }

    *ino = current;
    return 0;
}

/* 初始化ext2文件系统 */
/* V0.13.37: 干净挂载(指定块设备): 读superblock+GDT+重建shared ctx, 无写测试副作用 */
int ext2_64_mount(int (*block_read)(uint32_t, uint8_t, uint8_t *), uint32_t part_offset)
{
    extern int (*g_ext2_block_read)(uint32_t lba, uint8_t count, uint8_t *buf);
    extern void ext2_64_set_block_device(int (*rd)(uint32_t, uint8_t, uint8_t *));
    extern void ext2_64_set_partition_offset(uint32_t lba);
    uint8_t sector_buf[EXT2_SECTOR_SIZE * 2];
    if (block_read) { ext2_64_set_block_device(block_read); g_ext2_block_read = block_read; }
    ext2_64_set_partition_offset(part_offset);
    uint32_t super_lba = 2 + part_offset;
    if (g_ext2_block_read(super_lba, 2, sector_buf) != 0) return -1;
    memcpy(&ext2_sb, sector_buf, sizeof(ext2_superblock_t));
    if (ext2_sb.s_magic != EXT2_SUPER_MAGIC) return -2;
    /* 安全修复：超级块来自磁盘（可能是恶意镜像/U盘），先校验再用其字段做移位/除法/缓冲区尺寸 */
    if (ext2_algo_validate_sb(&ext2_sb) != 0) {
        vga_puts("[EXT2_64] ERROR: invalid superblock parameters, refuse to mount\n");
        return -2;
    }

    /* V0.13.48: 初始化块缓存槽为空 + 动态分配数据页 (BSS 只留指针) */
    ext2_cache_init();

    ext2_block_size = 1024 << ext2_sb.s_log_block_size;
    ext2_groups_count = (ext2_sb.s_blocks_count + ext2_sb.s_blocks_per_group - 1) /
                        ext2_sb.s_blocks_per_group;

    uint32_t gdt_block = ext2_sb.s_first_data_block + 1;
    uint8_t gdt_buf[EXT2_MAX_BLOCK_SIZE];
    if (ext2_read_block(gdt_block, gdt_buf) != 0) return -3;
    uint32_t gdt_entries = ext2_block_size / sizeof(ext2_group_desc_t);
    if (gdt_entries > 32) gdt_entries = 32;
    memcpy(ext2_gdt, gdt_buf, gdt_entries * sizeof(ext2_group_desc_t));

    ext2_mounted = 1;
    ext2_algo_init(&ext2_ctx, ext2_read_block, ext2_write_block, ext2_algo_block_buf);
    ext2_ctx.sb = ext2_sb;
    ext2_ctx.block_size = ext2_block_size;
    ext2_ctx.group_count = ext2_groups_count;
    ext2_use_shared_algo = 1;
    vga_puts("[EXT2_64] Mounted (blksz=");
    vga_put_hex(ext2_block_size);
    vga_puts(" groups=");
    vga_put_hex(ext2_groups_count);
    vga_puts(")\n");
    return 0;
}

/* V0.13.39: 磁盘使用率 (已用块/总块) */
uint32_t ext2_64_used_percent(void)
{
    if (!ext2_mounted || ext2_sb.s_blocks_count == 0) return 0;
    uint64_t used = ext2_sb.s_blocks_count - ext2_sb.s_free_blocks_count;
    return (uint32_t)(used * 100 / ext2_sb.s_blocks_count);
}

int ext2_64_init(void)
{
    vga_puts("[EXT2_64] Initializing ext2 filesystem...\n");

    extern void ide64_set_drive(int drive);
    uint8_t sector_buf[EXT2_SECTOR_SIZE * 2];
    int found = 0;

    /* 先尝试master，再尝试slave */
    for (int drive = 0; drive < 2 && !found; drive++) {
        ide64_set_drive(drive);
        vga_puts("[EXT2_64] Trying IDE ");
        vga_puts(drive ? "slave" : "master");
        vga_puts(" drive\n");

                /* 读取超级块（LBA 2 + 分区偏移，偏移1024字节） */
        uint32_t super_lba = 2 + ext2_partition_offset;
        if (g_ext2_block_read(super_lba, 2, sector_buf) != 0) {
            vga_puts("[EXT2_64] Failed to read superblock\n");
            continue;
        }
        /* V0.13.37: 修复0.13.36清理时误删的superblock拷贝(此前全0导致除零崩溃) */
        memcpy(&ext2_sb, sector_buf, sizeof(ext2_superblock_t));

        found = 1;
        vga_puts("[EXT2_64] ext2 found on ");
        vga_puts(drive ? "slave" : "master");
        vga_puts(" drive\n");
    }

    /* V0.13.49 修复: init 路径未初始化块缓存(V0.13.48 的缓存初始化只在 mount, 导致 data=NULL) */
    {
        extern void *kmalloc(uint64_t size);
        for (int ci = 0; ci < EXT2_CACHE_SLOTS; ci++) {
            ext2_cache[ci].block = 0xFFFFFFFF;
            ext2_cache[ci].lru = 0;
            ext2_cache[ci].data = (uint8_t *)kmalloc(EXT2_MAX_BLOCK_SIZE);
        }
    }

    if (!found) {
        vga_puts("[EXT2_64] ERROR: No ext2 filesystem found\n");
        return -1;
    }

    /* 安全修复：同上，初始化路径也必须先校验超级块 */
    if (ext2_algo_validate_sb(&ext2_sb) != 0) {
        vga_puts("[EXT2_64] ERROR: invalid superblock parameters, refuse to mount\n");
        return -1;
    }

    /* V0.13.49: 缓存初始化（此前 init 路径缺失导致缓存 data=NULL, 命中返回全0） */
    ext2_cache_init();

    ext2_block_size = 1024 << ext2_sb.s_log_block_size;
    ext2_groups_count = (ext2_sb.s_blocks_count + ext2_sb.s_blocks_per_group - 1) /
                        ext2_sb.s_blocks_per_group;

    vga_puts("[EXT2_64] Magic OK, block_size=");
    vga_put_hex(ext2_block_size);
    vga_puts(" groups=");
    vga_put_hex(ext2_groups_count);
    vga_puts(" first_data_block=");
    vga_put_hex(ext2_sb.s_first_data_block);
    vga_puts("\n");

    /* 读取块组描述符表 */
    uint32_t gdt_block = ext2_sb.s_first_data_block + 1;
    vga_puts("[EXT2_64] GDT block=");
    vga_put_hex(gdt_block);
    vga_puts("\n");
    uint8_t gdt_buf[EXT2_MAX_BLOCK_SIZE];
    if (ext2_read_block(gdt_block, gdt_buf) != 0) {
        vga_puts("[EXT2_64] ERROR: Failed to read GDT\n");
        return -1;
    }

    uint32_t gdt_entries = ext2_block_size / sizeof(ext2_group_desc_t);
    if (gdt_entries > 32) gdt_entries = 32;
    memcpy(ext2_gdt, gdt_buf, gdt_entries * sizeof(ext2_group_desc_t));

    vga_puts("[EXT2_64] GDT loaded, inodes_per_group=");
    vga_put_hex(ext2_sb.s_inodes_per_group);
    vga_puts("\n");

    /* 测试：列出根目录 */
    ext2_inode_t root;
    if (ext2_read_inode(EXT2_ROOT_INO, &root) == 0) {
        vga_puts("[EXT2_64] Root inode OK, size=");
        vga_put_hex(root.i_size);
        vga_puts("\n");

        /* 列出根目录内容 */
        uint8_t dir_buf[EXT2_MAX_BLOCK_SIZE];
        uint32_t blocks = (root.i_size + ext2_block_size - 1) / ext2_block_size;
        vga_puts("[EXT2_64] Root directory contents:\n");

        for (uint32_t b = 0; b < blocks && b < 1; b++) {
            if (ext2_read_data_block(&root, b, dir_buf) != 0) continue;
            uint32_t offset = 0;
            while (offset < ext2_block_size) {
                /* 安全修复：根目录来自磁盘，校验目录项边界防越界读栈 */
                if (offset + 8 > ext2_block_size) break;
                ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(dir_buf + offset);
                if (entry->rec_len < 8) break;
                if (entry->rec_len > ext2_block_size - offset) break;
                if (entry->name_len > 0 &&
                    8 + (uint32_t)entry->name_len <= entry->rec_len) {
                    vga_put_hex(offset);
                    vga_puts(" rec=");
                    vga_put_hex(entry->rec_len);
                    vga_puts(" nlen=");
                    vga_put_hex(entry->name_len);
                    vga_puts("\n");
                    char namebuf[256];
                    int nlen = entry->name_len;
                    if (nlen > 255) nlen = 255;
                    for (int i = 0; i < nlen; i++) namebuf[i] = entry->name[i];
                    namebuf[nlen] = '\0';
                    vga_puts("  ");
                    vga_puts(namebuf);
                    vga_puts(entry->file_type == 2 ? "/" : "");
                    vga_puts("\n");
                }
                offset += entry->rec_len;
            }
        }
    }

    ext2_mounted = 1;
    vga_puts("[OK] ext2 filesystem mounted\n");

    /* 测试：读取/hello.txt */
    uint8_t filebuf[256];
    int fsize = ext2_64_read_file("/hello.txt", filebuf, sizeof(filebuf) - 1);
    if (fsize > 0) {
        filebuf[fsize] = '\0';
        vga_puts("[EXT2_64] Read /hello.txt (");
        vga_put_hex(fsize);
        vga_puts(" bytes): ");
        vga_puts((const char *)filebuf);
        vga_puts("\n");
    } else {
        vga_puts("[EXT2_64] Failed to read /hello.txt\n");
    }

    /* 初始化共享算法上下文（统一源码树P0-2）
     * 必须放在任何写操作自检之前: 旧本地实现只更新内存中的 GDT/超级块计数,
     * 不回写磁盘, 会产生 e2fsck 告警 (free blocks/inodes 计数不一致) */
    ext2_algo_init(&ext2_ctx, ext2_read_block, ext2_write_block, ext2_algo_block_buf);
    ext2_ctx.sb = ext2_sb;
    ext2_ctx.block_size = ext2_block_size;
    ext2_ctx.group_count = ext2_groups_count;
    ext2_use_shared_algo = 1;

    /* 测试：写入新文件/test_write.txt */
    const char *test_data = "HepuOS 64-bit ext2 write test OK!";
    int wsize = ext2_64_write_file("/test_write.txt", (const uint8_t *)test_data, strlen(test_data));
    if (wsize > 0) {
        vga_puts("[EXT2_64] Write /test_write.txt (");
        vga_put_hex(wsize);
        vga_puts(" bytes)\n");

        /* 读回验证 */
        uint8_t verifybuf[256];
        int vsize = ext2_64_read_file("/test_write.txt", verifybuf, sizeof(verifybuf) - 1);
        if (vsize > 0) {
            verifybuf[vsize] = '\0';
            vga_puts("[EXT2_64] Verify read: ");
            vga_puts((const char *)verifybuf);
            vga_puts("\n");
        }
    } else {
        vga_puts("[EXT2_64] Write test failed (note: file not added to dir yet)\n");
    }

    /* V0.13.57: 登记根分区到挂载槽0 (resolve 根路径切回用) */
    ext2_mount_offsets[0] = ext2_partition_offset;
    ext2_mount_valid[0] = 1;
    vga_puts("[EXT2_64] Shared ext2 algorithm initialized\n");



    return 0;
}

/* 读取文件内容 */
/* V0.13.9: 文件系统信息导出 (设置页磁盘管理) */
int ext2_64_get_fs_info(uint64_t *total_blocks, uint64_t *free_blocks, uint32_t *block_size)
{
    if (!ext2_mounted) return -1;
    if (total_blocks) *total_blocks = ext2_sb.s_blocks_count;
    if (free_blocks) *free_blocks = ext2_sb.s_free_blocks_count;
    if (block_size) *block_size = (uint32_t)1024 << ext2_sb.s_log_block_size;
    return 0;
}

/* V0.13.9: 磁盘管理 — 文件系统完整信息导出 */
int ext2_64_get_fs_info_ex(uint64_t *total_blocks, uint64_t *free_blocks,
                           uint32_t *block_size, uint64_t *total_inodes,
                           uint64_t *free_inodes, uint32_t *groups,
                           char *volume_name)
{
    if (!ext2_mounted) return -1;
    if (total_blocks) *total_blocks = ext2_sb.s_blocks_count;
    if (free_blocks)  *free_blocks  = ext2_sb.s_free_blocks_count;
    if (block_size)   *block_size   = (uint32_t)1024 << ext2_sb.s_log_block_size;
    if (total_inodes) *total_inodes = ext2_sb.s_inodes_count;
    if (free_inodes)  *free_inodes  = ext2_sb.s_free_inodes_count;
    if (groups)       *groups       = ext2_groups_count;
    if (volume_name) {
        int i;
        for (i = 0; i < 15 && ext2_sb.s_volume_name[i]; i++) volume_name[i] = ext2_sb.s_volume_name[i];
        volume_name[i] = 0;
        if (i == 0) { volume_name[0] = 'r'; volume_name[1] = 'o'; volume_name[2] = 'o'; volume_name[3] = 't'; volume_name[4] = 0; }
    }
    return 0;
}

/* V0.13.57: 多实例挂载接口 */
const char *ext2_64_resolve_path(const char *path);

int ext2_64_read_file(const char *path, uint8_t *buffer, uint32_t max_size)
{
    /* V0.13.57: 挂载点前缀解析 (/cfg /data) */
    const char *rpath = ext2_64_resolve_path(path);

    if (ext2_use_shared_algo) {
        return ext2_algo_read_file(&ext2_ctx, rpath, buffer, max_size);
    }
    if (!ext2_mounted) return -1;

    uint32_t ino;
    if (ext2_lookup(rpath, &ino) != 0) return -1;

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) {
        return -1;
    }

    uint32_t size = inode.i_size;
    if (size > max_size) size = max_size;

    uint32_t blocks = (size + ext2_block_size - 1) / ext2_block_size;
    uint8_t block_buf[EXT2_MAX_BLOCK_SIZE];

    for (uint32_t b = 0; b < blocks; b++) {
        if (ext2_read_data_block(&inode, b, block_buf) != 0) {
            /* V0.33: 读块失败必须如实上报，不能静默跳过。
             *
             * 原来这里是 `break` 后照样 `return size` —— 调用方以为整份文件
             * 读成功了，实际拿到的是「前半段正确、后半段是垃圾」的缓冲区。
             * 症状是偶发的、看起来毫不相关的故障：
             *   - 900KB 的 glibc_test.elf 跑到指令中间（rip 落在指令中间 ⇒
             *     控制流被破坏），或直接 #PF；
             *   - .so 模块加载偶发失败、符号解析不到（GOT 留 0 ⇒ 跳到地址 0）。
             * 文件越大（块越多）越容易命中，所以表现为「偶发」。
             *
             * 现在返回 -1 = 读取失败，让调用方按失败处理而不是用垃圾数据。 */
            return -1;
        }
        uint32_t copy_size = ext2_block_size;
        if (b == blocks - 1) {
            copy_size = size - b * ext2_block_size;
        }
        memcpy(buffer + b * ext2_block_size, block_buf, copy_size);
    }

    return size;
}

/* 从指定偏移读取文件 - V0.10.24 */
int ext2_64_read_file_offset(const char *path, uint8_t *buffer, uint32_t max_size, uint32_t offset)
{
    /* V0.13.57: 挂载点前缀解析 (/cfg /data) */
    const char *rpath = ext2_64_resolve_path(path);

    if (ext2_use_shared_algo) {
        return ext2_algo_read_file_offset(&ext2_ctx, rpath, buffer, max_size, offset);
    }
    if (!ext2_mounted) return -1;

    uint32_t ino;
    if (ext2_lookup(rpath, &ino) != 0) return -1;

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) return -1;

    if (offset >= inode.i_size) return 0;

    uint32_t avail = inode.i_size - offset;
    uint32_t size = (avail < max_size) ? avail : max_size;

    uint32_t start_block = offset / ext2_block_size;
    uint32_t block_offset = offset % ext2_block_size;
    uint32_t blocks = (size + block_offset + ext2_block_size - 1) / ext2_block_size;

    uint8_t block_buf[EXT2_MAX_BLOCK_SIZE];
    uint32_t buf_offset = 0;

    for (uint32_t b = 0; b < blocks; b++) {
        if (ext2_read_data_block(&inode, start_block + b, block_buf) != 0) {
            /* V0.33: 同 ext2_64_read_file() —— 读块失败如实上报，
             * 不能返回「已读字节数」让调用方以为数据是完整的。 */
            return -1;
        }
        uint32_t src_offset = (b == 0) ? block_offset : 0;
        uint32_t copy_size = ext2_block_size - src_offset;
        if (buf_offset + copy_size > size) copy_size = size - buf_offset;
        memcpy(buffer + buf_offset, block_buf + src_offset, copy_size);
        buf_offset += copy_size;
    }

    return buf_offset;
}

/* ============================================================================
 * ext2写支持
 * ========================================================================== */

/* 写入一个块到磁盘 */
static int ext2_write_block(uint32_t block, const uint8_t *buffer)
{
    uint32_t lba = block * (ext2_block_size / EXT2_SECTOR_SIZE) + ext2_partition_offset;
    uint8_t count = ext2_block_size / EXT2_SECTOR_SIZE;
    /* V0.13.48: 写前失效对应块缓存（读一致性） */
    ext2_cache_invalidate(block);
    return storage_hal_write(lba, count, buffer);
}

/* 分配一个空闲块 */
static uint32_t ext2_alloc_block(void)
{
    if (ext2_use_shared_algo) {
        return ext2_algo_alloc_block(&ext2_ctx);
    }
    if (!ext2_mounted) return 0;

    for (uint32_t g = 0; g < ext2_groups_count; g++) {
        if (ext2_gdt[g].bg_free_blocks_count == 0) continue;

        uint8_t bitmap[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(ext2_gdt[g].bg_block_bitmap, bitmap) != 0) continue;

        for (uint32_t b = 0; b < ext2_sb.s_blocks_per_group; b++) {
            if (!(bitmap[b / 8] & (1 << (b % 8)))) {
                bitmap[b / 8] |= (1 << (b % 8));
                ext2_write_block(ext2_gdt[g].bg_block_bitmap, bitmap);
                ext2_gdt[g].bg_free_blocks_count--;
                ext2_sb.s_free_blocks_count--;

                uint32_t block_num = g * ext2_sb.s_blocks_per_group + b +
                                     ext2_sb.s_first_data_block;

                static uint8_t zero_blk[EXT2_MAX_BLOCK_SIZE];
                memset(zero_blk, 0, ext2_block_size);
                ext2_write_block(block_num, zero_blk);

                return block_num;
            }
        }
    }
    return 0;
}

/* 释放一个块 - V0.10.18 */
static int ext2_free_block(uint32_t block_num)
{
    if (ext2_use_shared_algo) {
        return ext2_algo_free_block(&ext2_ctx, block_num);
    }
    if (!ext2_mounted || block_num == 0) return -1;

    uint32_t g = (block_num - ext2_sb.s_first_data_block) / ext2_sb.s_blocks_per_group;
    uint32_t b = (block_num - ext2_sb.s_first_data_block) % ext2_sb.s_blocks_per_group;

    if (g >= ext2_groups_count) return -1;

    uint8_t bitmap[EXT2_MAX_BLOCK_SIZE];
    if (ext2_read_block(ext2_gdt[g].bg_block_bitmap, bitmap) != 0) return -1;

    if (!(bitmap[b / 8] & (1 << (b % 8)))) {
        return -1;  /* 块已经是空闲的 */
    }

    bitmap[b / 8] &= ~(1 << (b % 8));
    ext2_write_block(ext2_gdt[g].bg_block_bitmap, bitmap);
    ext2_gdt[g].bg_free_blocks_count++;
    ext2_sb.s_free_blocks_count++;

    return 0;
}

/* 写入inode到磁盘（公开接口：chmod/chown 等属性更新需要） */
int ext2_write_inode(uint32_t ino, const ext2_inode_t *inode)
{
    if (ext2_use_shared_algo) {
        return ext2_algo_write_inode(&ext2_ctx, ino, inode);
    }
    if (ino == 0) return -1;

    uint32_t group = (ino - 1) / ext2_sb.s_inodes_per_group;
    uint32_t index = (ino - 1) % ext2_sb.s_inodes_per_group;
    /* 安全修复：同 ext2_read_inode，组号必须落在 GDT 静态数组(32项)内 */
    if (group >= ext2_groups_count || group >= 32) return -1;
    uint32_t inode_table = ext2_gdt[group].bg_inode_table;
    uint32_t inode_size = ext2_sb.s_inode_size ? ext2_sb.s_inode_size : 128;
    uint32_t block_offset = (index * inode_size) / ext2_block_size;
    uint32_t byte_offset = (index * inode_size) % ext2_block_size;

    uint8_t block_buf[EXT2_MAX_BLOCK_SIZE];
    if (ext2_read_block(inode_table + block_offset, block_buf) != 0) {
        return -1;
    }

    memcpy(block_buf + byte_offset, inode, sizeof(ext2_inode_t));
    return ext2_write_block(inode_table + block_offset, block_buf);
}

/* 分配一个空闲inode */
static uint32_t ext2_alloc_inode(void)
{
    if (ext2_use_shared_algo) {
        return ext2_algo_alloc_inode(&ext2_ctx);
    }
    if (!ext2_mounted) return 0;

    for (uint32_t g = 0; g < ext2_groups_count; g++) {
        if (ext2_gdt[g].bg_free_inodes_count == 0) continue;

        uint8_t bitmap[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(ext2_gdt[g].bg_inode_bitmap, bitmap) != 0) continue;

        for (uint32_t i = 0; i < ext2_sb.s_inodes_per_group; i++) {
            if (!(bitmap[i / 8] & (1 << (i % 8)))) {
                bitmap[i / 8] |= (1 << (i % 8));
                ext2_write_block(ext2_gdt[g].bg_inode_bitmap, bitmap);
                ext2_gdt[g].bg_free_inodes_count--;
                ext2_sb.s_free_inodes_count--;

                uint32_t ino = g * ext2_sb.s_inodes_per_group + i + 1;

                ext2_inode_t inode;
                memset(&inode, 0, sizeof(inode));
                ext2_write_inode(ino, &inode);

                return ino;
            }
        }
    }
    return 0;
}

/* 在目录中添加条目 */
static int ext2_add_dir_entry(uint32_t dir_ino, const char *name, uint32_t ino, uint8_t file_type)
{
    if (ext2_use_shared_algo) {
        return ext2_algo_add_dir_entry(&ext2_ctx, dir_ino, name, ino, file_type);
    }
    /* 注意：暂不使用共享算法，原实现更稳定 */
    ext2_inode_t dir_inode;
    if (ext2_read_inode(dir_ino, &dir_inode) != 0) return -1;

    uint32_t name_len = strlen(name);
    uint32_t needed = 8 + ((name_len + 3) & ~3);

    uint8_t dir_buf[EXT2_MAX_BLOCK_SIZE];
    uint32_t dir_blocks = (dir_inode.i_size + ext2_block_size - 1) / ext2_block_size;

    for (uint32_t b = 0; b < dir_blocks && b < 12; b++) {
        if (dir_inode.i_block[b] == 0) continue;
        if (ext2_read_block(dir_inode.i_block[b], dir_buf) != 0) continue;

        uint32_t offset = 0;
        while (offset < ext2_block_size) {
            /* 安全修复：目录项来自磁盘，必须先校验再分割。
             * 旧实现：rec_len<actual 时 free_space 下溢为巨大值通过检查，
             * 或 rec_len 越块导致 new_entry 落到 dir_buf 之外 —— 栈写溢出 */
            if (offset + 8 > ext2_block_size) break;
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(dir_buf + offset);
            if (entry->rec_len < 8) break;
            if (entry->rec_len > ext2_block_size - offset) break;

            uint32_t actual = 8 + ((entry->name_len + 3) & ~3);
            if (entry->rec_len < actual) break;
            uint32_t free_space = entry->rec_len - actual;

            if (free_space >= needed) {
                /* 分割当前条目 */
                uint16_t old_rec_len = entry->rec_len;
                entry->rec_len = actual;

                ext2_dir_entry_t *new_entry = (ext2_dir_entry_t *)(dir_buf + offset + actual);
                new_entry->inode = ino;
                new_entry->rec_len = old_rec_len - actual;
                new_entry->name_len = name_len;
                new_entry->file_type = file_type;
                memcpy(new_entry->name, name, name_len);

                ext2_write_block(dir_inode.i_block[b], dir_buf);

                /* 更新目录inode */
                dir_inode.i_links_count++;
                dir_inode.i_size += needed;
                ext2_write_inode(dir_ino, &dir_inode);

                return 0;
            }
            offset += entry->rec_len;
        }
    }

    /* 需要分配新块（简化：暂不支持） */
    return -1;
}

/* 创建文件 */
static int ext2_create_file(const char *path, uint16_t mode)
{
    /* 分离目录路径和文件名 */
    const char *slash = strrchr(path, '/');
    if (!slash) return -1;

    char dir_path[256];
    char file_name[256];
    int dir_len = slash - path;
    if (dir_len == 0) dir_len = 1;
    memcpy(dir_path, path, dir_len);
    dir_path[dir_len] = '\0';
    strcpy(file_name, slash + 1);

    /* 查找目录inode */
    uint32_t dir_ino;
    if (ext2_lookup(dir_path, &dir_ino) != 0) return -1;

    /* 检查是否已存在 */
    uint32_t existing;
    ext2_inode_t dir_inode;
    if (ext2_read_inode(dir_ino, &dir_inode) == 0) {
        if (ext2_find_in_dir(&dir_inode, file_name, &existing) == 0) {
            return existing;  /* 已存在 */
        }
    }

    /* 分配新inode */
    uint32_t new_ino = ext2_alloc_inode();
    if (new_ino == 0) return -1;


    /* 设置inode */
    ext2_inode_t new_inode;
    memset(&new_inode, 0, sizeof(new_inode));
    new_inode.i_mode = mode;
    new_inode.i_uid = 0;
    new_inode.i_gid = 0;
    new_inode.i_size = 0;
    new_inode.i_links_count = 1;
    new_inode.i_blocks = 0;

    /* 如果是目录，初始化.和..条目 */
    uint32_t allocated_dir_block = 0;   /* 失败时回收 */
    if ((mode & 0xF000) == 0x4000) {
        uint32_t dir_block = ext2_alloc_block();
        if (dir_block != 0) {
            allocated_dir_block = dir_block;

            new_inode.i_block[0] = dir_block;
            new_inode.i_size = ext2_block_size;
            new_inode.i_blocks = ext2_block_size / 512;
            new_inode.i_links_count = 2;  /* 目录本身 + . */

            uint8_t block_buf[EXT2_MAX_BLOCK_SIZE];
            memset(block_buf, 0, ext2_block_size);

            /* . 条目 */
            ext2_dir_entry_t *dot = (ext2_dir_entry_t *)block_buf;
            dot->inode = new_ino;
            dot->rec_len = 12;  /* 4+2+1+1+1 + 3字节对齐 */
            dot->name_len = 1;
            dot->file_type = 2;  /* 目录 */
            dot->name[0] = '.';

            /* .. 条目 */
            ext2_dir_entry_t *dotdot = (ext2_dir_entry_t *)(block_buf + 12);
            dotdot->inode = dir_ino;
            dotdot->rec_len = ext2_block_size - 12;  /* 占据剩余空间 */
            dotdot->name_len = 2;
            dotdot->file_type = 2;  /* 目录 */
            dotdot->name[0] = '.';
            dotdot->name[1] = '.';

            ext2_write_block(dir_block, block_buf);
        }
    }

    ext2_write_inode(new_ino, &new_inode);

    /* 添加目录项 */
    uint8_t file_type = (mode & 0xF000) == 0x4000 ? 2 : 1;
    if (ext2_add_dir_entry(dir_ino, file_name, new_ino, file_type) != 0) {
        /* 目录项写入失败: 回收刚分配的 inode/目录块, 避免留下孤立资源 */
        if (allocated_dir_block) ext2_free_block(allocated_dir_block);
        if (ext2_use_shared_algo) {
            ext2_algo_free_inode(&ext2_ctx, new_ino);
            memset(&new_inode, 0, sizeof(new_inode));
            ext2_write_inode(new_ino, &new_inode);
        }
        return -1;
    }

    return new_ino;
}

/* 统计 inode 占用的间接元数据块数（数据块由 blocks_written 单独统计）。
 * e2fsck 要求 i_blocks = 数据块 + 所有间接块(含一/二/三级) 的 512 字节扇区数 */
static uint32_t ext2_count_meta_blocks(const ext2_inode_t *inode)
{
    static uint8_t mbuf[EXT2_MAX_BLOCK_SIZE];
    static uint8_t mbuf2[EXT2_MAX_BLOCK_SIZE];
    uint32_t n = 0;
    uint32_t ppb = ext2_block_size / 4;

    if (inode->i_block[12]) n++;                       /* 一级间接块本身 */

    if (inode->i_block[13]) {
        n++;                                           /* 二级间接块本身 */
        if (ext2_read_block(inode->i_block[13], mbuf) == 0) {
            uint32_t *e = (uint32_t *)mbuf;
            for (uint32_t i = 0; i < ppb; i++)
                if (e[i]) n++;                         /* 下属一级间接块 */
        }
    }

    if (inode->i_block[14]) {
        n++;                                           /* 三级间接块本身 */
        if (ext2_read_block(inode->i_block[14], mbuf) == 0) {
            uint32_t *t = (uint32_t *)mbuf;
            for (uint32_t i = 0; i < ppb; i++) {
                if (!t[i]) continue;
                n++;                                   /* 下属二级间接块 */
                if (ext2_read_block(t[i], mbuf2) != 0) continue;
                uint32_t *d = (uint32_t *)mbuf2;
                for (uint32_t j = 0; j < ppb; j++)
                    if (d[j]) n++;                 /* 下属一级间接块 */
            }
        }
    }
    return n;
}

/* 写入文件内容 - V0.10.22: 支持二级间接块（最大文件256MB+） */
int ext2_64_write_file(const char *path, const uint8_t *data, uint32_t size)
{
    /* V0.13.57: 挂载点前缀解析 (/cfg /data) */
    const char *rpath = ext2_64_resolve_path(path);

    if (!ext2_mounted) return -1;

    uint32_t ino;
    if (ext2_lookup(rpath, &ino) != 0) {
        ino = ext2_create_file(rpath, 0x81A4);
        if (ino <= 0) return -1;
    }

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) return -1;

    uint32_t blocks_needed = (size + ext2_block_size - 1) / ext2_block_size;
    uint32_t blocks_written = 0;
    uint32_t max_blocks = 12 + 256 + 65536 + 16777216;  /* 直接+一级+二级+三级间接块 */

    static uint8_t indirect_buf[EXT2_MAX_BLOCK_SIZE];
    static uint8_t dbl_indirect_buf[EXT2_MAX_BLOCK_SIZE];
    static uint8_t tri_indirect_buf[EXT2_MAX_BLOCK_SIZE];
    int indirect_loaded = 0;
    int dbl_indirect_loaded = 0;
    int tri_indirect_loaded = 0;
    uint32_t current_dbl_idx = 0xFFFFFFFF;
    uint32_t current_tri_idx = 0xFFFFFFFF;

    for (uint32_t b = 0; b < blocks_needed && b < max_blocks; b++) {
        uint32_t block_num = 0;

        if (b < 12) {
            /* 直接块 */
            if (inode.i_block[b] == 0) {
                inode.i_block[b] = ext2_alloc_block();
                if (inode.i_block[b] == 0) break;
            }
            block_num = inode.i_block[b];
        } else if (b < 12 + 256) {
            /* 一级间接块 */
            uint32_t idx = b - 12;
            if (inode.i_block[12] == 0) {
                inode.i_block[12] = ext2_alloc_block();
                if (inode.i_block[12] == 0) break;
                memset(indirect_buf, 0, ext2_block_size);
                ext2_write_block(inode.i_block[12], indirect_buf);
                indirect_loaded = 1;
            }
            if (!indirect_loaded) {
                if (ext2_read_block(inode.i_block[12], indirect_buf) != 0) break;
                indirect_loaded = 1;
            }
            uint32_t *entries = (uint32_t *)indirect_buf;
            if (entries[idx] == 0) {
                entries[idx] = ext2_alloc_block();
                if (entries[idx] == 0) break;
                ext2_write_block(inode.i_block[12], indirect_buf);
            }
            block_num = entries[idx];
        } else if (b < 12 + 256 + 65536) {
            /* 二级间接块 */
            uint32_t idx = b - (12 + 256);
            uint32_t dbl_idx = idx / 256;
            uint32_t sing_idx = idx % 256;

            /* 分配二级间接块 */
            if (inode.i_block[13] == 0) {
                inode.i_block[13] = ext2_alloc_block();
                if (inode.i_block[13] == 0) break;
                memset(dbl_indirect_buf, 0, ext2_block_size);
                ext2_write_block(inode.i_block[13], dbl_indirect_buf);
                dbl_indirect_loaded = 1;
            }
            if (!dbl_indirect_loaded) {
                if (ext2_read_block(inode.i_block[13], dbl_indirect_buf) != 0) break;
                dbl_indirect_loaded = 1;
            }
            uint32_t *dbl_entries = (uint32_t *)dbl_indirect_buf;

            /* 分配一级间接块 */
            if (dbl_entries[dbl_idx] == 0) {
                dbl_entries[dbl_idx] = ext2_alloc_block();
                if (dbl_entries[dbl_idx] == 0) break;
                ext2_write_block(inode.i_block[13], dbl_indirect_buf);
                memset(indirect_buf, 0, ext2_block_size);
                ext2_write_block(dbl_entries[dbl_idx], indirect_buf);
                current_dbl_idx = dbl_idx;
            } else if (current_dbl_idx != dbl_idx) {
                if (ext2_read_block(dbl_entries[dbl_idx], indirect_buf) != 0) break;
                current_dbl_idx = dbl_idx;
            }

            /* 分配数据块 */
            uint32_t *sing_entries = (uint32_t *)indirect_buf;
            if (sing_entries[sing_idx] == 0) {
                sing_entries[sing_idx] = ext2_alloc_block();
                if (sing_entries[sing_idx] == 0) break;
                ext2_write_block(dbl_entries[dbl_idx], indirect_buf);
            }
            block_num = sing_entries[sing_idx];
        } else {
            /* 三级间接块 */
            uint32_t idx = b - (12 + 256 + 65536);
            uint32_t tri_idx = idx / 65536;
            uint32_t dbl_idx = (idx % 65536) / 256;
            uint32_t sing_idx = idx % 256;

            /* 分配三级间接块 */
            if (inode.i_block[14] == 0) {
                inode.i_block[14] = ext2_alloc_block();
                if (inode.i_block[14] == 0) break;
                memset(tri_indirect_buf, 0, ext2_block_size);
                ext2_write_block(inode.i_block[14], tri_indirect_buf);
                tri_indirect_loaded = 1;
            }
            if (!tri_indirect_loaded) {
                if (ext2_read_block(inode.i_block[14], tri_indirect_buf) != 0) break;
                tri_indirect_loaded = 1;
            }
            uint32_t *tri_entries = (uint32_t *)tri_indirect_buf;

            /* 分配二级间接块 */
            if (tri_entries[tri_idx] == 0) {
                tri_entries[tri_idx] = ext2_alloc_block();
                if (tri_entries[tri_idx] == 0) break;
                ext2_write_block(inode.i_block[14], tri_indirect_buf);
                memset(dbl_indirect_buf, 0, ext2_block_size);
                ext2_write_block(tri_entries[tri_idx], dbl_indirect_buf);
                current_tri_idx = tri_idx;
                current_dbl_idx = 0xFFFFFFFF;
            } else if (current_tri_idx != tri_idx) {
                if (ext2_read_block(tri_entries[tri_idx], dbl_indirect_buf) != 0) break;
                current_tri_idx = tri_idx;
                current_dbl_idx = 0xFFFFFFFF;
            }
            uint32_t *dbl_entries = (uint32_t *)dbl_indirect_buf;

            /* 分配一级间接块 */
            if (dbl_entries[dbl_idx] == 0) {
                dbl_entries[dbl_idx] = ext2_alloc_block();
                if (dbl_entries[dbl_idx] == 0) break;
                ext2_write_block(tri_entries[tri_idx], dbl_indirect_buf);
                memset(indirect_buf, 0, ext2_block_size);
                ext2_write_block(dbl_entries[dbl_idx], indirect_buf);
                current_dbl_idx = dbl_idx;
            } else if (current_dbl_idx != dbl_idx) {
                if (ext2_read_block(dbl_entries[dbl_idx], indirect_buf) != 0) break;
                current_dbl_idx = dbl_idx;
            }

            /* 分配数据块 */
            uint32_t *sing_entries = (uint32_t *)indirect_buf;
            if (sing_entries[sing_idx] == 0) {
                sing_entries[sing_idx] = ext2_alloc_block();
                if (sing_entries[sing_idx] == 0) break;
                ext2_write_block(dbl_entries[dbl_idx], indirect_buf);
            }
            block_num = sing_entries[sing_idx];
        }

        if (block_num == 0) break;

        uint32_t offset = b * ext2_block_size;
        uint32_t write_size = ext2_block_size;
        if (b == blocks_needed - 1) {
            write_size = size - offset;
        }

        uint8_t block_buf[EXT2_MAX_BLOCK_SIZE];
        memset(block_buf, 0, ext2_block_size);
        memcpy(block_buf, data + offset, write_size);
        ext2_write_block(block_num, block_buf);
        blocks_written++;
    }

    inode.i_size = size;
    inode.i_blocks = (blocks_written + ext2_count_meta_blocks(&inode)) *
                     (ext2_block_size / 512);
    ext2_write_inode(ino, &inode);

    return (blocks_written == blocks_needed) ? size : blocks_written * ext2_block_size;
}

/* 从偏移写入文件 - V0.13.59 (swap 随机页写基础)
 * 复用 write_file 的三级间接块分配循环, 支持首/尾块读改写
 * 返回写入字节数, 失败返回-1 */
int ext2_64_write_file_offset(const char *path, const uint8_t *data,
                              uint32_t size, uint32_t offset)
{
    /* V0.13.57: 挂载点前缀解析 (/cfg /data) */
    const char *rpath = ext2_64_resolve_path(path);

    if (!ext2_mounted) return -1;
    if (!data || size == 0) return -1;

    uint32_t ino;
    if (ext2_lookup(rpath, &ino) != 0) {
        ino = ext2_create_file(rpath, 0x81A4);
        if (ino <= 0) return -1;
    }

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) return -1;

    uint32_t bs = ext2_block_size;
    uint32_t start_block = offset / bs;
    uint32_t start_off = offset % bs;
    /* V0.54.12(R4 FS-05): 结束偏移用 64 位计算，旧实现 offset+size 在
       uint32 内回绕，使 blocks_needed 变小，造成写截断/数据错位。 */
    uint64_t end_offset64 = (uint64_t)offset + size;
    uint32_t end_block = (uint32_t)((end_offset64 + bs - 1) / bs);
    uint32_t blocks_needed = end_block - start_block;
    uint32_t blocks_written = 0;
    uint32_t max_blocks = 12 + 256 + 65536 + 16777216;

    static uint8_t indirect_buf[EXT2_MAX_BLOCK_SIZE];
    static uint8_t dbl_indirect_buf[EXT2_MAX_BLOCK_SIZE];
    static uint8_t tri_indirect_buf[EXT2_MAX_BLOCK_SIZE];
    int indirect_loaded = 0;
    int dbl_indirect_loaded = 0;
    int tri_indirect_loaded = 0;
    uint32_t current_dbl_idx = 0xFFFFFFFF;
    uint32_t current_tri_idx = 0xFFFFFFFF;

    for (uint32_t i = 0; i < blocks_needed && (start_block + i) < max_blocks; i++) {
        uint32_t b = start_block + i;
        uint32_t block_num = 0;

        if (b < 12) {
            if (inode.i_block[b] == 0) {
                inode.i_block[b] = ext2_alloc_block();
                if (inode.i_block[b] == 0) break;
            }
            block_num = inode.i_block[b];
        } else if (b < 12 + 256) {
            uint32_t idx = b - 12;
            if (inode.i_block[12] == 0) {
                inode.i_block[12] = ext2_alloc_block();
                if (inode.i_block[12] == 0) break;
                memset(indirect_buf, 0, bs);
                ext2_write_block(inode.i_block[12], indirect_buf);
                indirect_loaded = 1;
            }
            if (!indirect_loaded) {
                if (ext2_read_block(inode.i_block[12], indirect_buf) != 0) break;
                indirect_loaded = 1;
            }
            uint32_t *entries = (uint32_t *)indirect_buf;
            if (entries[idx] == 0) {
                entries[idx] = ext2_alloc_block();
                if (entries[idx] == 0) break;
                ext2_write_block(inode.i_block[12], indirect_buf);
            }
            block_num = entries[idx];
        } else if (b < 12 + 256 + 65536) {
            uint32_t idx = b - (12 + 256);
            uint32_t dbl_idx = idx / 256;
            uint32_t sing_idx = idx % 256;

            if (inode.i_block[13] == 0) {
                inode.i_block[13] = ext2_alloc_block();
                if (inode.i_block[13] == 0) break;
                memset(dbl_indirect_buf, 0, bs);
                ext2_write_block(inode.i_block[13], dbl_indirect_buf);
                dbl_indirect_loaded = 1;
            }
            if (!dbl_indirect_loaded) {
                if (ext2_read_block(inode.i_block[13], dbl_indirect_buf) != 0) break;
                dbl_indirect_loaded = 1;
            }
            uint32_t *dbl_entries = (uint32_t *)dbl_indirect_buf;
            if (dbl_entries[dbl_idx] == 0) {
                dbl_entries[dbl_idx] = ext2_alloc_block();
                if (dbl_entries[dbl_idx] == 0) break;
                ext2_write_block(inode.i_block[13], dbl_indirect_buf);
                memset(indirect_buf, 0, bs);
                ext2_write_block(dbl_entries[dbl_idx], indirect_buf);
                current_dbl_idx = dbl_idx;
            } else if (current_dbl_idx != dbl_idx) {
                if (ext2_read_block(dbl_entries[dbl_idx], indirect_buf) != 0) break;
                current_dbl_idx = dbl_idx;
            }
            uint32_t *sing_entries = (uint32_t *)indirect_buf;
            if (sing_entries[sing_idx] == 0) {
                sing_entries[sing_idx] = ext2_alloc_block();
                if (sing_entries[sing_idx] == 0) break;
                ext2_write_block(dbl_entries[dbl_idx], indirect_buf);
            }
            block_num = sing_entries[sing_idx];
        } else {
            uint32_t idx = b - (12 + 256 + 65536);
            uint32_t tri_idx = idx / 65536;
            uint32_t dbl_idx = (idx % 65536) / 256;
            uint32_t sing_idx = idx % 256;

            if (inode.i_block[14] == 0) {
                inode.i_block[14] = ext2_alloc_block();
                if (inode.i_block[14] == 0) break;
                memset(tri_indirect_buf, 0, bs);
                ext2_write_block(inode.i_block[14], tri_indirect_buf);
                tri_indirect_loaded = 1;
            }
            if (!tri_indirect_loaded) {
                if (ext2_read_block(inode.i_block[14], tri_indirect_buf) != 0) break;
                tri_indirect_loaded = 1;
            }
            uint32_t *tri_entries = (uint32_t *)tri_indirect_buf;
            if (tri_entries[tri_idx] == 0) {
                tri_entries[tri_idx] = ext2_alloc_block();
                if (tri_entries[tri_idx] == 0) break;
                ext2_write_block(inode.i_block[14], tri_indirect_buf);
                memset(dbl_indirect_buf, 0, bs);
                ext2_write_block(tri_entries[tri_idx], dbl_indirect_buf);
                current_tri_idx = tri_idx;
                current_dbl_idx = 0xFFFFFFFF;
            } else if (current_tri_idx != tri_idx) {
                if (ext2_read_block(tri_entries[tri_idx], dbl_indirect_buf) != 0) break;
                current_tri_idx = tri_idx;
                current_dbl_idx = 0xFFFFFFFF;
            }
            uint32_t *dbl_entries = (uint32_t *)dbl_indirect_buf;
            if (dbl_entries[dbl_idx] == 0) {
                dbl_entries[dbl_idx] = ext2_alloc_block();
                if (dbl_entries[dbl_idx] == 0) break;
                ext2_write_block(tri_entries[tri_idx], dbl_indirect_buf);
                memset(indirect_buf, 0, bs);
                ext2_write_block(dbl_entries[dbl_idx], indirect_buf);
                current_dbl_idx = dbl_idx;
            } else if (current_dbl_idx != dbl_idx) {
                if (ext2_read_block(dbl_entries[dbl_idx], indirect_buf) != 0) break;
                current_dbl_idx = dbl_idx;
            }
            uint32_t *sing_entries = (uint32_t *)indirect_buf;
            if (sing_entries[sing_idx] == 0) {
                sing_entries[sing_idx] = ext2_alloc_block();
                if (sing_entries[sing_idx] == 0) break;
                ext2_write_block(dbl_entries[dbl_idx], indirect_buf);
            }
            block_num = sing_entries[sing_idx];
        }

        if (block_num == 0) break;

        /* 读改写: 首块/尾块保留现有内容 */
        uint8_t block_buf[EXT2_MAX_BLOCK_SIZE];
        int need_merge = (i == 0 && start_off != 0) ||
                         (i == blocks_needed - 1 && (end_offset64 % bs) != 0);
        if (need_merge) {
            memset(block_buf, 0, bs);
            ext2_read_data_block(&inode, b, block_buf);
        } else {
            memset(block_buf, 0, bs);
        }

        uint32_t dst_start = (i == 0) ? start_off : 0;
        uint32_t dst_end = (i == blocks_needed - 1 && (end_offset64 % bs) != 0)
                               ? (uint32_t)(end_offset64 % bs) : bs;
        uint32_t src = i * bs + dst_start - start_off;
        memcpy(block_buf + dst_start, data + src, dst_end - dst_start);

        ext2_write_block(block_num, block_buf);
        blocks_written++;
    }

    if (blocks_written != blocks_needed) {
        return (blocks_written == 0) ? -1 : blocks_written * bs;
    }

    if (end_offset64 > inode.i_size) inode.i_size = (uint32_t)end_offset64;
    inode.i_blocks = (inode.i_size + bs - 1) / bs * (bs / 512);
    ext2_write_inode(ino, &inode);

    return size;
}

/* 删除文件 - V0.10.18: 释放数据块+inode+目录项 */
int ext2_64_delete_file(const char *path)
{
    /* V0.13.57: 挂载点前缀解析 (/cfg /data) */
    const char *rpath = ext2_64_resolve_path(path);

    if (!ext2_mounted) return -1;

    /* 分离目录路径和文件名 */
    char dir_path[256];
    char file_name[256];
    int len = strlen(rpath);   /* 真实全长: 路径可达 512, 不能硬限 255 否则长名被截断 */

    int last_slash = -1;
    for (int i = 0; i < len; i++) {
        if (rpath[i] == '/') last_slash = i;
    }

    if (last_slash <= 0) {
        /* 根目录下的文件 */
        dir_path[0] = '/'; dir_path[1] = 0;
        for (int i = 1; i < len && i < 255; i++) file_name[i-1] = rpath[i];
        file_name[len-1 < 255 ? len-1 : 255] = 0;
    } else {
        for (int i = 0; i < last_slash && i < 255; i++) dir_path[i] = rpath[i];
        dir_path[last_slash < 255 ? last_slash : 255] = 0;
        int j = 0;
        for (int i = last_slash + 1; i < len && j < 255; i++, j++) file_name[j] = rpath[i];
        file_name[j] = 0;
    }

    /* 查找文件inode */
    uint32_t ino;
    if (ext2_lookup(rpath, &ino) != 0) return -1;

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) return -1;

    /* 释放数据块 + 间接元数据块
     * 共享算法按 block_size/4 递归释放 (旧本地实现硬编码 256 指针, 4K 块盘会泄漏块) */
    if (ext2_use_shared_algo) {
        ext2_algo_free_data_blocks(&ext2_ctx, &inode);
    } else {
    /* 释放直接块 */
    for (int b = 0; b < 12; b++) {
        if (inode.i_block[b] != 0) {
            ext2_free_block(inode.i_block[b]);
        }
    }

    /* 释放一级间接块 */
    if (inode.i_block[12] != 0) {
        uint8_t ibuf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(inode.i_block[12], ibuf) == 0) {
            uint32_t *entries = (uint32_t *)ibuf;
            for (int i = 0; i < 256; i++) {
                if (entries[i] != 0) {
                    ext2_free_block(entries[i]);
                }
            }
        }
        ext2_free_block(inode.i_block[12]);
    }

    /* 释放二级间接块 */
    if (inode.i_block[13] != 0) {
        uint8_t dbl_buf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(inode.i_block[13], dbl_buf) == 0) {
            uint32_t *dbl_entries = (uint32_t *)dbl_buf;
            for (int i = 0; i < 256; i++) {
                if (dbl_entries[i] != 0) {
                    uint8_t sing_buf[EXT2_MAX_BLOCK_SIZE];
                    if (ext2_read_block(dbl_entries[i], sing_buf) == 0) {
                        uint32_t *sing_entries = (uint32_t *)sing_buf;
                        for (int j = 0; j < 256; j++) {
                            if (sing_entries[j] != 0) {
                                ext2_free_block(sing_entries[j]);
                            }
                        }
                    }
                    ext2_free_block(dbl_entries[i]);
                }
            }
        }
        ext2_free_block(inode.i_block[13]);
    }

    /* 释放三级间接块 */
    if (inode.i_block[14] != 0) {
        uint8_t tri_buf[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(inode.i_block[14], tri_buf) == 0) {
            uint32_t *tri_entries = (uint32_t *)tri_buf;
            for (int i = 0; i < 256; i++) {
                if (tri_entries[i] != 0) {
                    uint8_t dbl_buf[EXT2_MAX_BLOCK_SIZE];
                    if (ext2_read_block(tri_entries[i], dbl_buf) == 0) {
                        uint32_t *dbl_entries = (uint32_t *)dbl_buf;
                        for (int j = 0; j < 256; j++) {
                            if (dbl_entries[j] != 0) {
                                uint8_t sing_buf[EXT2_MAX_BLOCK_SIZE];
                                if (ext2_read_block(dbl_entries[j], sing_buf) == 0) {
                                    uint32_t *sing_entries = (uint32_t *)sing_buf;
                                    for (int k = 0; k < 256; k++) {
                                        if (sing_entries[k] != 0) {
                                            ext2_free_block(sing_entries[k]);
                                        }
                                    }
                                }
                                ext2_free_block(dbl_entries[j]);
                            }
                        }
                    }
                    ext2_free_block(tri_entries[i]);
                }
            }
        }
        ext2_free_block(inode.i_block[14]);
    }
    }   /* ext2_use_shared_algo else */

    /* 释放inode */
    if (ext2_use_shared_algo) {
        /* 共享算法同时更新位图/GDT/超级块计数, 避免三处记账不一致 */
        ext2_algo_free_inode(&ext2_ctx, ino);
    } else {
    uint32_t g = (ino - 1) / ext2_sb.s_inodes_per_group;
    uint32_t idx = (ino - 1) % ext2_sb.s_inodes_per_group;
    if (g < ext2_groups_count && g < 32) {   /* 安全修复：GDT 静态数组仅32项 */
        uint8_t bitmap[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(ext2_gdt[g].bg_inode_bitmap, bitmap) == 0) {
            bitmap[idx / 8] &= ~(1 << (idx % 8));
            ext2_write_block(ext2_gdt[g].bg_inode_bitmap, bitmap);
            ext2_gdt[g].bg_free_inodes_count++;
            ext2_sb.s_free_inodes_count++;
        }
    }
    }

    /* 从目录中删除目录项（通过设置rec_len合并） */
    uint32_t dir_ino;
    int found = 0;
    if (ext2_lookup(dir_path, &dir_ino) == 0) {
        ext2_inode_t dir_inode;
        if (ext2_read_inode(dir_ino, &dir_inode) == 0) {
            uint32_t blocks = (dir_inode.i_size + ext2_block_size - 1) / ext2_block_size;
            for (uint32_t b = 0; b < blocks; b++) {
                uint8_t block_buf[EXT2_MAX_BLOCK_SIZE];
                if (ext2_read_data_block(&dir_inode, b, block_buf) != 0) continue;

                uint32_t offset = 0;
                ext2_dir_entry_t *prev = NULL;
                while (offset < ext2_block_size) {
                    ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(block_buf + offset);
                    if (entry->rec_len == 0) break;

                    int name_match = 1;
                    for (int i = 0; i < entry->name_len && i < 255; i++) {
                        if (entry->name[i] != file_name[i]) { name_match = 0; break; }
                    }
                    if (entry->name_len != (int)strlen(file_name)) name_match = 0;

                    if (name_match && entry->inode == ino) {
                        /* 删除：合并到前一个目录项 */
                        if (prev) {
                            prev->rec_len += entry->rec_len;
                        } else {
                            entry->inode = 0;  /* 第一个目录项，清空inode */
                        }
                        ext2_write_data_block(&dir_inode, b, block_buf);
                        found = 1;
                        break;
                    }

                    prev = entry;
                    offset += entry->rec_len;
                }
                if (found) break;
            }
        }
    }

    return 0;
}

/* 删除目录 - V0.10.21: 检查空目录+释放块+释放inode+删除目录项 */
int ext2_64_delete_dir(const char *path)
{
    /* V0.13.57: 挂载点前缀解析 (/cfg /data) */
    const char *rpath = ext2_64_resolve_path(path);

    if (!ext2_mounted) return -1;

    /* 查找目录inode */
    uint32_t ino;
    if (ext2_lookup(rpath, &ino) != 0) return -1;

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) return -1;

    /* 检查是否是目录 */
    if ((inode.i_mode & 0xF000) != 0x4000) return -1;

    /* 检查目录是否为空（只有.和..） */
    uint8_t dir_buf[EXT2_MAX_BLOCK_SIZE];
    uint32_t blocks = (inode.i_size + ext2_block_size - 1) / ext2_block_size;
    int entry_count = 0;
    for (uint32_t b = 0; b < blocks; b++) {
        if (ext2_read_data_block(&inode, b, dir_buf) != 0) continue;
        uint32_t offset = 0;
        while (offset < ext2_block_size) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(dir_buf + offset);
            if (entry->rec_len == 0) break;
            if (entry->inode != 0 && entry->name_len > 0) {
                /* 跳过.和.. */
                if (entry->name_len == 1 && entry->name[0] == '.') { offset += entry->rec_len; continue; }
                if (entry->name_len == 2 && entry->name[0] == '.' && entry->name[1] == '.') { offset += entry->rec_len; continue; }
                entry_count++;
            }
            offset += entry->rec_len;
        }
    }
    if (entry_count > 0) return -2;  /* 目录非空 */

    /* 分离目录路径和目录名 */
    char dir_path[256];
    char dir_name[256];
    int len = strlen(rpath);   /* 真实全长, 避免长目录名被截断 */
    int last_slash = -1;
    for (int i = 0; i < len; i++) {
        if (rpath[i] == '/') last_slash = i;
    }
    if (last_slash <= 0) {
        dir_path[0] = '/'; dir_path[1] = 0;
        for (int i = 1; i < len && i < 255; i++) dir_name[i-1] = rpath[i];
        dir_name[len-1 < 255 ? len-1 : 255] = 0;
    } else {
        for (int i = 0; i < last_slash && i < 255; i++) dir_path[i] = rpath[i];
        dir_path[last_slash < 255 ? last_slash : 255] = 0;
        int j = 0;
        for (int i = last_slash + 1; i < len && j < 255; i++, j++) dir_name[j] = rpath[i];
        dir_name[j] = 0;
    }

    /* 释放目录数据块 (含间接块) */
    if (ext2_use_shared_algo) {
        ext2_algo_free_data_blocks(&ext2_ctx, &inode);
    } else {
        for (int b = 0; b < 12; b++) {
            if (inode.i_block[b] != 0) ext2_free_block(inode.i_block[b]);
        }
    }

    /* 释放inode */
    if (ext2_use_shared_algo) {
        /* 共享算法同时更新位图/GDT/超级块计数, 避免三处记账不一致 */
        ext2_algo_free_inode(&ext2_ctx, ino);
    } else {
    uint32_t g = (ino - 1) / ext2_sb.s_inodes_per_group;
    uint32_t idx = (ino - 1) % ext2_sb.s_inodes_per_group;
    if (g < ext2_groups_count && g < 32) {   /* 安全修复：GDT 静态数组仅32项 */
        uint8_t bitmap[EXT2_MAX_BLOCK_SIZE];
        if (ext2_read_block(ext2_gdt[g].bg_inode_bitmap, bitmap) == 0) {
            bitmap[idx / 8] &= ~(1 << (idx % 8));
            ext2_write_block(ext2_gdt[g].bg_inode_bitmap, bitmap);
            ext2_gdt[g].bg_free_inodes_count++;
            ext2_sb.s_free_inodes_count++;
        }
    }
    }

    /* 从父目录中删除目录项 */
    uint32_t parent_ino;
    if (ext2_lookup(dir_path, &parent_ino) == 0) {
        ext2_inode_t parent_inode;
        if (ext2_read_inode(parent_ino, &parent_inode) == 0) {
            uint32_t pblocks = (parent_inode.i_size + ext2_block_size - 1) / ext2_block_size;
            for (uint32_t b = 0; b < pblocks; b++) {
                uint8_t block_buf[EXT2_MAX_BLOCK_SIZE];
                if (ext2_read_data_block(&parent_inode, b, block_buf) != 0) continue;
                uint32_t offset = 0;
                ext2_dir_entry_t *prev = NULL;
                while (offset < ext2_block_size) {
                    ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(block_buf + offset);
                    if (entry->rec_len == 0) break;
                    int name_match = 1;
                    for (int i = 0; i < entry->name_len && i < 255; i++) {
                        if (entry->name[i] != dir_name[i]) { name_match = 0; break; }
                    }
                    if (entry->name_len != (int)strlen(dir_name)) name_match = 0;
                    if (name_match && entry->inode == ino) {
                        if (prev) {
                            prev->rec_len += entry->rec_len;
                        } else {
                            entry->inode = 0;
                        }
                        ext2_write_data_block(&parent_inode, b, block_buf);
                        /* 子目录的 ".." 指向父目录, 删除子目录后父目录链接计数 -1 */
                        if (parent_inode.i_links_count > 0) parent_inode.i_links_count--;
                        ext2_write_inode(parent_ino, &parent_inode);
                        break;
                    }
                    prev = entry;
                    offset += entry->rec_len;
                }
            }
        }
    }

    return 0;
}

/* 路径查找（公开接口） */
uint32_t ext2_64_lookup(const char *path)
{
    /* V0.13.57: 挂载点前缀解析 (/cfg /data) */
    const char *rpath = ext2_64_resolve_path(path);
    uint32_t ino = 0;
    if (ext2_lookup(rpath, &ino) != 0) return 0;
    return ino;
}

/* 列出目录内容 */
int ext2_64_list_dir(const char *path)
{
    const char *rpath = ext2_64_resolve_path(path);
    uint32_t ino = 0;
    if (!ext2_mounted || ext2_lookup(rpath, &ino) != 0 || ino == 0) {
        vga_puts("ls: cannot access '");
        vga_puts(path);
        vga_puts("': No such file or directory\n");
        return -1;
    }

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) {
        return -1;
    }

    if (!(inode.i_mode & 0x4000)) {  /* S_IFDIR */
        vga_puts("ls: '");
        vga_puts(path);
        vga_puts("' is not a directory\n");
        return -1;
    }

    uint8_t dir_buf[EXT2_MAX_BLOCK_SIZE];
    uint32_t blocks = (inode.i_size + ext2_block_size - 1) / ext2_block_size;

    for (uint32_t b = 0; b < blocks; b++) {
        if (b < 12 && inode.i_block[b] == 0) continue;
        if (ext2_read_data_block(&inode, b, dir_buf) != 0) continue;
        uint32_t offset = 0;
        uint32_t guard = 0;
        while (offset < ext2_block_size && guard++ < 512) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(dir_buf + offset);
            uint32_t rec = entry->rec_len;
            if (rec < 8 || (rec & 3) || rec > ext2_block_size - offset) break;
            if ((uint32_t)entry->name_len + 8 > rec) break;
            if (entry->name_len > 0) {
                char namebuf[256];
                int nlen = entry->name_len;
                if (nlen > 255) nlen = 255;
                if (!(nlen == 1 && entry->name[0] == '.') &&
                    !(nlen == 2 && entry->name[0] == '.' && entry->name[1] == '.')) {
                    for (int i = 0; i < nlen; i++) namebuf[i] = entry->name[i];
                    namebuf[nlen] = '\0';
                    vga_puts(namebuf);
                    vga_puts(entry->file_type == 2 ? "/" : "");
                    vga_puts("\n");
                }
            }
            offset += rec;
        }
    }

    return 0;
}

/* UI 目录列举接口 (文件管理器): 填 name 数组 + 类型数组, 返回条目数 */
int ext2_64_list_dir_entries_ex(const char *path, char names[][256], uint8_t *types,
                                uint32_t *sizes, int max_entries);
int ext2_64_list_dir_entries(const char *path, char names[][256], uint8_t *types, int max_entries)
{
    return ext2_64_list_dir_entries_ex(path, names, types, 0, max_entries);
}

/* 带大小的列目录接口 (文件管理器): sizes 可为 NULL, 等价于旧接口 */
int ext2_64_list_dir_entries_ex(const char *path, char names[][256], uint8_t *types,
                                uint32_t *sizes, int max_entries)
{
    const char *rpath = ext2_64_resolve_path(path);

    uint32_t ino = 0;
    if (!ext2_mounted || ext2_lookup(rpath, &ino) != 0 || ino == 0) return -1;
    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) return -1;
    if (!(inode.i_mode & 0x4000)) return -1;  /* S_IFDIR */

    uint8_t dir_buf[EXT2_MAX_BLOCK_SIZE];
    uint32_t blocks = (inode.i_size + ext2_block_size - 1) / ext2_block_size;
    int n = 0;
    for (uint32_t b = 0; b < blocks && n < max_entries; b++) {
        /* 直接块可直接查 i_block; 间接块由 ext2_read_data_block 解析 */
        if (b < 12 && inode.i_block[b] == 0) continue;
        if (ext2_read_data_block(&inode, b, dir_buf) != 0) continue;
        uint32_t offset = 0;
        uint32_t guard = 0;
        while (offset < ext2_block_size && n < max_entries && guard++ < 512) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(dir_buf + offset);
            uint32_t rec = entry->rec_len;
            if (rec < 8 || (rec & 3) || rec > ext2_block_size - offset) break;
            if ((uint32_t)entry->name_len + 8 > rec) break;
            if (entry->inode != 0 && entry->name_len > 0) {
                int nlen = entry->name_len;
                if (nlen > 255) nlen = 255;
                if (!(nlen == 1 && entry->name[0] == '.') &&
                    !(nlen == 2 && entry->name[0] == '.' && entry->name[1] == '.')) {
                    for (int i = 0; i < nlen; i++) names[n][i] = entry->name[i];
                    names[n][nlen] = '\0';
                    if (sizes) {
                        ext2_inode_t fi;
                        if (ext2_read_inode(entry->inode, &fi) == 0) {
                            types[n] = ((fi.i_mode & 0xF000) == 0x4000) ? 1 : 0;
                            sizes[n] = types[n] ? 0 : fi.i_size;
                        } else {
                            types[n] = (entry->file_type == 2) ? 1 : 0;
                            sizes[n] = 0;
                        }
                    } else {
                        types[n] = (entry->file_type == 2) ? 1 : 0;
                    }
                    n++;
                }
            }
            offset += rec;
        }
    }
    return n;
}

/* 创建文件（公开接口） */
int ext2_64_create_file(const char *path)
{
    /* V0.13.57: 挂载点前缀解析 (/cfg /data) */
    const char *rpath = ext2_64_resolve_path(path);

    if (!rpath || !rpath[0]) return -1;
    return ext2_create_file(rpath, 0x81A4) > 0 ? 0 : -1;  /* 普通文件，rw-r--r-- */
}

/* 创建目录（公开接口） */
int ext2_64_create_dir(const char *path)
{
    /* V0.13.57: 挂载点前缀解析 (/cfg /data) */
    const char *rpath = ext2_64_resolve_path(path);

    if (!rpath || !rpath[0]) return -1;
    return ext2_create_file(rpath, 0x41ED) > 0 ? 0 : -1;  /* 目录，rwxr-xr-x */
}

/* V0.13.40: 新建文件夹 (文件管理器 New Folder 入口) */
int ext2_64_mkdir(const char *path)
{
    /* V0.13.57: 挂载点前缀解析 (/cfg /data) */
    const char *rpath = ext2_64_resolve_path(path);

    return ext2_64_create_dir(rpath);
}

/* V0.13.40: 重命名 (文件管理器 Rename 入口)
 * 返回: 0成功 / -2新名冲突 / -3旧条目未找到 / -4新增目录项失败 */
int ext2_64_rename(const char *oldpath, const char *newpath)
{
    /* V0.13.57: 挂载点前缀解析 (/cfg /data) */
    const char *rp_old = ext2_64_resolve_path(oldpath);
    const char *rp_new = ext2_64_resolve_path(newpath);

    if (!rp_old || !rp_new || !rp_old[0] || !rp_new[0]) return -1;

    const char *slash = strrchr(rp_old, '/');
    if (!slash) return -1;
    char old_dir[256], old_name[128];
    int dl = slash - rp_old;
    if (dl == 0) { old_dir[0] = '/'; old_dir[1] = 0; }
    else { memcpy(old_dir, rp_old, dl); old_dir[dl] = 0; }
    strcpy(old_name, slash + 1);

    uint32_t ino;
    if (ext2_lookup(rp_old, &ino) != 0) return -1;
    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) return -1;
    uint8_t ftype = ((inode.i_mode & 0xF000) == 0x4000) ? 2 : 1;

    const char *nslash = strrchr(rp_new, '/');
    if (!nslash) return -1;
    char new_dir[256], new_name[128];
    int ndl = nslash - rp_new;
    if (ndl == 0) { new_dir[0] = '/'; new_dir[1] = 0; }
    else { memcpy(new_dir, rp_new, ndl); new_dir[ndl] = 0; }
    strcpy(new_name, nslash + 1);

    uint32_t new_dir_ino;
    if (ext2_lookup(new_dir, &new_dir_ino) != 0) return -1;

    /* 新名冲突检查(同一inode重命名自己除外) */
    {
        ext2_inode_t ndi;
        uint32_t ex;
        if (ext2_read_inode(new_dir_ino, &ndi) == 0 &&
            ext2_find_in_dir(&ndi, new_name, &ex) == 0 && ex != ino) return -2;
    }

    /* 清除旧目录项(置 inode=0, 保留rec_len槽) */
    {
        uint32_t old_dir_ino;
        if (ext2_lookup(old_dir, &old_dir_ino) != 0) return -1;
        ext2_inode_t oi;
        if (ext2_read_inode(old_dir_ino, &oi) != 0) return -1;
        uint32_t blocks = (oi.i_size + ext2_block_size - 1) / ext2_block_size;
        uint8_t dbuf[EXT2_MAX_BLOCK_SIZE];
        uint32_t old_nlen = (uint32_t)strlen(old_name);
        int found = 0;
        for (uint32_t b = 0; b < blocks && b < 12 && !found; b++) {
            if (oi.i_block[b] == 0) continue;
            if (ext2_read_block(oi.i_block[b], dbuf) != 0) continue;
            uint32_t off = 0;
            while (off < ext2_block_size) {
                ext2_dir_entry_t *e = (ext2_dir_entry_t *)(dbuf + off);
                if (e->rec_len == 0) break;
                if (e->inode != 0 && (uint32_t)e->name_len == old_nlen) {
                    int eq = 1;
                    for (uint32_t k = 0; k < old_nlen; k++)
                        if (e->name[k] != old_name[k]) { eq = 0; break; }
                    if (eq) {
                        e->inode = 0;
                        ext2_write_block(oi.i_block[b], dbuf);
                        found = 1;
                        break;
                    }
                }
                off += e->rec_len;
            }
        }
        if (!found) return -3;
    }

    if (ext2_add_dir_entry(new_dir_ino, new_name, ino, ftype) != 0) return -4;
    return 0;
}

/* V0.13.8 P6: 目录条目结构 + 读取到数组 (供 GUI 文件管理器显示真实 ext2) */
typedef struct { char name[256]; int is_dir; uint32_t ino; } ext2_dir_item_t;

int ext2_64_read_dir(const char *path, ext2_dir_item_t *items, int max_items)
{
    /* V0.13.57: 挂载点前缀解析 (/cfg /data) */
    const char *rpath = ext2_64_resolve_path(path);

    if (!ext2_mounted) return -1;
    uint32_t ino = 0;
    if (ext2_lookup(rpath, &ino) != 0 || ino == 0) return -1;
    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) return -1;
    if (!(inode.i_mode & 0x4000)) return -1;  /* S_IFDIR */

    uint8_t dir_buf[EXT2_MAX_BLOCK_SIZE];
    uint32_t blocks = (inode.i_size + ext2_block_size - 1) / ext2_block_size;
    int count = 0;
    for (uint32_t b = 0; b < blocks && count < max_items; b++) {
        if (b < 12 && inode.i_block[b] == 0) continue;
        if (ext2_read_data_block(&inode, b, dir_buf) != 0) continue;
        uint32_t offset = 0;
        uint32_t guard = 0;
        while (offset < ext2_block_size && count < max_items && guard++ < 512) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(dir_buf + offset);
            uint32_t rec = entry->rec_len;
            if (rec < 8 || (rec & 3) || rec > ext2_block_size - offset) break;
            if ((uint32_t)entry->name_len + 8 > rec) break;
            if (entry->name_len > 0 && entry->inode != 0) {
                int nlen = entry->name_len;
                if (nlen > 255) nlen = 255;
                if (!(nlen == 1 && entry->name[0] == '.') &&
                    !(nlen == 2 && entry->name[0] == '.' && entry->name[1] == '.')) {
                    for (int i = 0; i < nlen; i++) items[count].name[i] = entry->name[i];
                    items[count].name[nlen] = '\0';
                    items[count].is_dir = (entry->file_type == 2);
                    items[count].ino = entry->inode;
                    count++;
                }
            }
            offset += rec;
        }
    }
    return count;
}

/* V0.13.21: 导出 superblock/GDT 指针（磁盘管理 fsck 用） */
int ext2_64_get_superblock(void **sb, void **gdt)
{
    if (!ext2_mounted) return -1;
    if (sb) *sb = &ext2_sb;
    if (gdt) *gdt = &ext2_gdt;
    return 0;
}

/* ============================================================================
 * V0.13.54: 根分区 ext2 自动扩展 (boot-time resize)
 * 场景: install64 把 P1 分区表写大(如1GB) 但 ext2 仍是出厂大小(224MB)
 * 内核启动时检测 分区大小 > 文件系统大小 → 扩展 ext2 到全分区
 * 支持 block_size=4096 (组布局: 块位图1 + inode位图1 + inode表 + 数据块)
 * 新组从文件系统末尾连续扩展(非 bpg 对齐, 自用内核定位正确)
 * 返回: 0=已扩展/无操作, 负=失败
 * ========================================================================== */
int ext2_64_expand_to_partition(uint32_t part_lba, uint32_t part_sectors)
{
    (void)part_lba;
    if (!ext2_mounted) return -1;
    if (ext2_block_size != 4096) return -2;   /* 仅支持 4096 块大小 */

    uint64_t part_bytes = (uint64_t)part_sectors * EXT2_SECTOR_SIZE;
    uint32_t target_blocks = (uint32_t)(part_bytes / ext2_block_size);
    uint32_t bpg = ext2_sb.s_blocks_per_group;
    uint32_t old_blocks = ext2_sb.s_blocks_count;
    if (bpg == 0) return -3;
    if (target_blocks <= old_blocks + bpg / 2) return 0;  /* 无显著盈余(留半组容差) */

    uint32_t inodes_per_group = ext2_sb.s_inodes_per_group;
    uint32_t inode_size = ext2_sb.s_inode_size ? ext2_sb.s_inode_size : 128;
    uint32_t it_blocks = (inodes_per_group * inode_size + ext2_block_size - 1) / ext2_block_size;
    uint32_t gdt_entries = ext2_block_size / sizeof(ext2_group_desc_t);
    if (gdt_entries > 32) gdt_entries = 32;

    uint32_t old_groups = (old_blocks + bpg - 1) / bpg;
    uint32_t new_groups = (target_blocks + bpg - 1) / bpg;
    if (new_groups > gdt_entries) return -4;   /* 组数超 GDT 容量 */

    uint8_t block_buf[EXT2_MAX_BLOCK_SIZE];
    uint32_t cur = old_blocks;                 /* 新组从文件系统末尾开始 */
    uint64_t added_free_blocks = 0;

    for (uint32_t g = old_groups; g < new_groups; g++) {
        uint32_t bb = cur;              /* 块位图块 */
        uint32_t ib = cur + 1;          /* inode 位图块 */
        uint32_t it = cur + 2;          /* inode 表起始块 */
        uint32_t group_total;
        if (g == new_groups - 1) {
            group_total = target_blocks - cur;
        } else {
            group_total = bpg;
        }
        uint32_t data_blocks = (group_total > 2 + it_blocks) ? (group_total - 2 - it_blocks) : 0;

        /* 块位图: 标记元数据块(位图1 + inode位图1 + inode表it_blocks)为已用 */
        memset(block_buf, 0, ext2_block_size);
        for (uint32_t b = 0; b < 2 + it_blocks; b++) {
            block_buf[b >> 3] |= (uint8_t)(1 << (b & 7));
        }
        if (ext2_write_block(bb, block_buf) != 0) return -5;

        /* inode 位图: 全空闲 */
        memset(block_buf, 0, ext2_block_size);
        if (ext2_write_block(ib, block_buf) != 0) return -6;

        /* inode 表: 清零 */
        for (uint32_t k = 0; k < it_blocks; k++) {
            memset(block_buf, 0, ext2_block_size);
            if (ext2_write_block(it + k, block_buf) != 0) return -7;
        }

        /* 组描述符 */
        if (g < 32) {
            ext2_gdt[g].bg_block_bitmap = bb;
            ext2_gdt[g].bg_inode_bitmap = ib;
            ext2_gdt[g].bg_inode_table = it;
            ext2_gdt[g].bg_free_blocks_count =
                (uint16_t)(data_blocks > 0xFFFF ? 0xFFFF : data_blocks);
            ext2_gdt[g].bg_free_inodes_count =
                (uint16_t)(inodes_per_group > 0xFFFF ? 0xFFFF : inodes_per_group);
            ext2_gdt[g].bg_used_dirs_count = 0;
        }
        added_free_blocks += data_blocks;
        cur += group_total;
    }

    /* 写回 GDT (块 s_first_data_block+1, 读改写保留旧组) */
    uint32_t gdt_block = ext2_sb.s_first_data_block + 1;
    if (ext2_read_block(gdt_block, block_buf) != 0) return -8;
    memcpy(block_buf, ext2_gdt, gdt_entries * sizeof(ext2_group_desc_t));
    if (ext2_write_block(gdt_block, block_buf) != 0) return -9;

    /* 更新超级块: 块数/空闲块/空闲inode */
    ext2_sb.s_blocks_count = target_blocks;
    ext2_sb.s_free_blocks_count += (uint32_t)added_free_blocks;
    ext2_sb.s_free_inodes_count += (uint32_t)((new_groups - old_groups) * inodes_per_group);

    /* 写回超级块 (块0 内偏移1024, 读改写保留0-1023引导区) */
    if (ext2_read_block(0, block_buf) != 0) return -10;
    memcpy(block_buf + 1024, (uint8_t *)&ext2_sb, sizeof(ext2_superblock_t));
    if (ext2_write_block(0, block_buf) != 0) return -11;

    vga_puts("[EXTRESIZE] ext2 expanded: ");
    vga_put_hex(old_blocks);
    vga_puts(" -> ");
    vga_put_hex(target_blocks);
    vga_puts(" blocks, ");
    vga_put_hex((uint64_t)(new_groups - old_groups));
    vga_puts(" new groups\n");
    return 0;
}

/* ============================================================================
 * V0.13.56: 简易 ext2 mkfs (内核版, 未格式化分区自动初始化)
 * 创建 4096 块大小的 ext2 文件系统: 超级块/GDT/块位图/inode位图/inode表/根目录
 * 用途: 三区制下 P2(/cfg) P3(/data) 出厂未格式化, 开机自动 mkfs 使其可用
 * 返回: 0=成功, 负=失败
 * ========================================================================== */
int ext2_64_mkfs(uint32_t part_lba, uint32_t part_sectors, const char *label)
{
    extern int ide64_write_sectors(uint32_t lba, uint8_t count, const uint8_t *buffer);
    const uint32_t block_size = 4096;
    const uint32_t bpg = 32768;
    const uint32_t inodes_per_group = 8192;
    const uint32_t inode_size = 128;
    const uint32_t it_blocks = (inodes_per_group * inode_size) / block_size;  /* 256 */

    uint32_t total_blocks = (uint32_t)((uint64_t)part_sectors * 512 / block_size);
    if (total_blocks < 64) return -1;
    uint32_t groups = (total_blocks + bpg - 1) / bpg;
    if (groups > 512) return -2;   /* GDT 单块容量 */

    uint8_t buf[block_size];
    uint32_t used_meta0 = 2 + it_blocks + 1;   /* 位图1+inode位图1+inode表+根目录1 = 259 */
    uint32_t used_meta_g = 2 + it_blocks;      /* 位图1+inode位图1+inode表 = 258 */

    /* 1. 块0: 引导区 + 超级块(偏移1024) */
    ext2_superblock_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.s_inodes_count = groups * inodes_per_group;
    sb.s_blocks_count = total_blocks;
    sb.s_r_blocks_count = 0;
    uint64_t free_blocks_total = 0;
    for (uint32_t g = 0; g < groups; g++) {
        uint32_t gtotal = (g == groups - 1) ? (total_blocks - g * bpg) : bpg;
        uint32_t used = (g == 0) ? used_meta0 : used_meta_g;
        free_blocks_total += (gtotal > used) ? (gtotal - used) : 0;
    }
    sb.s_free_blocks_count = (uint32_t)free_blocks_total;
    sb.s_free_inodes_count = groups * inodes_per_group - 1;   /* 根目录占1 */
    sb.s_first_data_block = 0;
    sb.s_log_block_size = 2;             /* 4096 */
    sb.s_log_frag_size = 2;
    sb.s_blocks_per_group = bpg;
    sb.s_frags_per_group = bpg;
    sb.s_inodes_per_group = inodes_per_group;
    sb.s_mtime = 0; sb.s_wtime = 0;
    sb.s_mnt_count = 0;
    sb.s_max_mnt_count = 20;
    sb.s_magic = 0xEF53;
    sb.s_state = 1;                      /* clean */
    sb.s_errors = 1;
    sb.s_minor_rev_level = 0;
    sb.s_lastcheck = 0; sb.s_checkinterval = 0;
    sb.s_creator_os = 0;
    sb.s_rev_level = 1;
    sb.s_def_resuid = 0; sb.s_def_resgid = 0;
    sb.s_first_ino = 11;
    sb.s_inode_size = inode_size;
    sb.s_block_group_nr = 0;
    sb.s_feature_compat = 0;
    sb.s_feature_incompat = 0;
    sb.s_feature_ro_compat = 0;
    if (label) {
        int li = 0;
        while (label[li] && li < 15) { sb.s_volume_name[li] = (uint8_t)label[li]; li++; }
    }
    memset(buf, 0, block_size);
    /* 写盘起点 = 分区LBA+2 即块0偏移1024, buf[0]即超级块起点 */
    memcpy(buf, &sb, sizeof(sb));
    if (ide64_write_sectors(part_lba + 2, block_size / 512, buf) != 0) return -3;
    /* V0.13.56: 写后读回自检 (超级块魔数, 统一 g_ext2_block_read/ide64 路径) */
    {
        uint8_t rbuf[4096];
        memset(rbuf, 0, sizeof(rbuf));
        if (g_ext2_block_read(part_lba + 2, 8, rbuf) != 0) return -10;
        uint16_t magic;
        memcpy(&magic, rbuf + 56, 2);   /* rbuf[0]=超级块起点, s_magic 偏移56 */
        if (magic != 0xEF53) return -11;
    }

    /* 2. 块1: GDT */
    ext2_group_desc_t gdt[512];
    memset(gdt, 0, sizeof(gdt));
    for (uint32_t g = 0; g < groups; g++) {
        uint32_t gstart = g * bpg;
        uint32_t gtotal = (g == groups - 1) ? (total_blocks - gstart) : bpg;
        uint32_t used = (g == 0) ? used_meta0 : used_meta_g;
        uint32_t it = (g == 0) ? 4 : gstart + 2;
        gdt[g].bg_block_bitmap = (g == 0) ? 2 : gstart;
        gdt[g].bg_inode_bitmap = (g == 0) ? 3 : gstart + 1;
        gdt[g].bg_inode_table = it;
        gdt[g].bg_free_blocks_count = (uint16_t)((gtotal > used) ? ((gtotal - used) > 0xFFFF ? 0xFFFF : (gtotal - used)) : 0);
        gdt[g].bg_free_inodes_count = (g == 0) ? (uint16_t)(inodes_per_group - 1) : (uint16_t)inodes_per_group;
        gdt[g].bg_used_dirs_count = (g == 0) ? 1 : 0;
    }
    memset(buf, 0, block_size);
    memcpy(buf, gdt, groups * sizeof(ext2_group_desc_t));
    if (ide64_write_sectors(part_lba + 8, block_size / 512, buf) != 0) return -4;

    /* 3. 各组: 块位图 / inode位图 / inode表 */
    for (uint32_t g = 0; g < groups; g++) {
        uint32_t gstart = g * bpg;
        uint32_t gtotal = (g == groups - 1) ? (total_blocks - gstart) : bpg;
        uint32_t used = (g == 0) ? used_meta0 : used_meta_g;
        uint32_t bb = (g == 0) ? 2 : gstart;
        uint32_t ib = (g == 0) ? 3 : gstart + 1;
        uint32_t it = (g == 0) ? 4 : gstart + 2;

        /* 块位图: 元数据块已用 */
        memset(buf, 0, block_size);
        for (uint32_t b = 0; b < used; b++) buf[b >> 3] |= (uint8_t)(1 << (b & 7));
        if (ide64_write_sectors(part_lba + bb * 8, block_size / 512, buf) != 0) return -5;

        /* inode位图: 组0标记inode2(根目录) */
        memset(buf, 0, block_size);
        if (g == 0) buf[0] |= 0x04;   /* inode 2 */
        if (ide64_write_sectors(part_lba + ib * 8, block_size / 512, buf) != 0) return -6;

        /* inode表: 清零; 组0写根目录inode(2) */
        for (uint32_t k = 0; k < it_blocks; k++) {
            memset(buf, 0, block_size);
            if (ide64_write_sectors(part_lba + (it + k) * 8, block_size / 512, buf) != 0) return -7;
        }
    }

    /* 4. 组0: 根目录inode + 根目录数据块 */
    {
        uint32_t root_block = used_meta0 - 1;   /* 260 */
        memset(buf, 0, block_size);
        /* inode 2 (偏移 2*128=256) */
        uint8_t *ino = buf + 2 * inode_size;
        uint16_t mode = EXT2_S_IFDIR | 0x1FF;   /* 040755 */
        memcpy(ino + 0, &mode, 2);
        uint32_t size = 1024;
        memcpy(ino + 4, &size, 4);
        uint16_t links = 2;
        memcpy(ino + 12, &links, 2);
        uint32_t blocks = 2;                    /* 512字节单位: 1024/512 */
        memcpy(ino + 16, &blocks, 4);
        uint32_t dblk = root_block;
        memcpy(ino + 40, &dblk, 4);            /* i_block[0] */
        /* 写inode表块(含inode2) */
        if (ide64_write_sectors(part_lba + (4 + 0) * 8, block_size / 512, buf) != 0) return -8;
        /* inode 2 位于 inode 表起始块(块4) 偏移256 —— 已包含在上面的写 */

        /* 根目录数据块 */
        memset(buf, 0, block_size);
        /* "." */
        uint32_t ino_self = 2;
        memcpy(buf + 0, &ino_self, 4);
        uint16_t rl = 12; uint8_t nl = 1, ft = 2;
        memcpy(buf + 4, &rl, 2); buf[6] = nl; buf[7] = ft;
        buf[8] = '.';
        /* ".." */
        memcpy(buf + 12, &ino_self, 4);
        rl = 1012; nl = 2;
        memcpy(buf + 16, &rl, 2); buf[18] = nl; buf[19] = ft;
        buf[20] = '.'; buf[21] = '.';
        if (ide64_write_sectors(part_lba + root_block * 8, block_size / 512, buf) != 0) return -9;
    }

    return 0;
}

/* ============================================================================
 * V0.13.57: 多实例挂载 (P2=/cfg 槽1, P3=/data 槽2)
 * /cfg、/data 分区文件级访问: 路径前缀解析 -> 切换分区实例 -> 子路径解析
 * 缓存槽已含物理LBA字段, 跨分区自动防误命中
 * ========================================================================== */

static int ext2_cur_mount = 0;

/* 登记分区到挂载槽 (校验 ext2 魔数) */
int ext2_64_mount_slot(int slot, uint32_t part_offset)
{
    if (slot < 0 || slot > 3) return -1;
    uint8_t sbuf[1024];
    if (g_ext2_block_read(part_offset + 2, 2, sbuf) != 0) return -1;
    ext2_superblock_t sb;
    memcpy(&sb, sbuf, sizeof(sb));
    if (sb.s_magic != EXT2_SUPER_MAGIC) return -2;
    ext2_mount_offsets[slot] = part_offset;
    ext2_mount_valid[slot] = 1;
    return 0;
}

/* 切换到挂载槽 (重挂载全局状态: 超级块+GDT+共享算法上下文; 不清缓存) */
int ext2_64_switch_mount(int slot)
{
    if (slot < 0 || slot > 3 || !ext2_mount_valid[slot]) return -1;
    uint32_t off = ext2_mount_offsets[slot];
    uint8_t sbuf[1024];
    if (g_ext2_block_read(off + 2, 2, sbuf) != 0) return -2;
    ext2_superblock_t sb;
    memcpy(&sb, sbuf, sizeof(sb));
    if (sb.s_magic != EXT2_SUPER_MAGIC) return -3;
    ext2_partition_offset = off;
    memcpy(&ext2_sb, &sb, sizeof(ext2_sb));
    ext2_block_size = 1024 << sb.s_log_block_size;
    ext2_groups_count = (sb.s_blocks_count + sb.s_blocks_per_group - 1) /
                        sb.s_blocks_per_group;
    uint32_t gdt_block = sb.s_first_data_block + 1;
    uint8_t gdt_buf[EXT2_MAX_BLOCK_SIZE];
    if (ext2_read_block(gdt_block, gdt_buf) != 0) return -4;
    uint32_t gdt_entries = ext2_block_size / sizeof(ext2_group_desc_t);
    if (gdt_entries > 32) gdt_entries = 32;
    memcpy(ext2_gdt, gdt_buf, gdt_entries * sizeof(ext2_group_desc_t));
    ext2_ctx.sb = ext2_sb;
    ext2_ctx.block_size = ext2_block_size;
    ext2_ctx.group_count = ext2_groups_count;
    ext2_cur_mount = slot;
    return 0;
}

/* 挂载点解析: /cfg -> 槽1, /data -> 槽2, 其余 -> 槽0(根)
 * 返回子路径(静态缓冲, 2个轮换供 rename 双路径使用) */
static char ext2_rp_buf[2][512];
static int ext2_rp_inner = 0;   /* 嵌套入口保护: 内层不再重复切换实例 */
const char *ext2_64_resolve_path(const char *path)
{
    static int rp_toggle = 0;
    char *dst = ext2_rp_buf[rp_toggle & 1];
    rp_toggle++;
    if (!path) return path;
    if (ext2_rp_inner) return path;   /* 已在外层解析, 沿用当前实例 */
    ext2_rp_inner = 1;

    if (path[0] == '/' && path[1] == 'c' && path[2] == 'f' && path[3] == 'g' &&
        (path[4] == '/' || path[4] == '\0')) {
        if (ext2_64_switch_mount(1) == 0) {
            int i = 0; const char *s = path + 4;
            if (*s == '/') { dst[i++] = '/'; s++; }
            while (*s && i < 510) dst[i++] = *s++;
            dst[i] = '\0';
            if (i == 0) { dst[0] = '/'; dst[1] = '\0'; }   /* 无子路径 -> 根目录 */
            ext2_rp_inner = 0;
            return dst;
        }
    }
    if (path[0] == '/' && path[1] == 'd' && path[2] == 'a' && path[3] == 't' &&
        path[4] == 'a' && (path[5] == '/' || path[5] == '\0')) {
        if (ext2_64_switch_mount(2) == 0) {
            int i = 0; const char *s = path + 5;
            if (*s == '/') { dst[i++] = '/'; s++; }
            while (*s && i < 510) dst[i++] = *s++;
            dst[i] = '\0';
            if (i == 0) { dst[0] = '/'; dst[1] = '\0'; }   /* 无子路径 -> 根目录 */
            ext2_rp_inner = 0;
            return dst;
        }
    }
    ext2_64_switch_mount(0);
    ext2_rp_inner = 0;
    return path;
}


/* 取文件/目录属性：size=输出大小，isdir=1 目录，ino_out=ext2 inode 号
 *
 * Linux 兼容层必须提供真实的 (st_dev, st_ino)：glibc 的 ld.so 在
 * _dl_map_object_from_fd 里用这对值判断「这个文件是不是已经加载过」。
 * 旧 fstat 两者恒为 0，于是加载 libc.so.6 时被误判为主程序已加载对象，
 * 拿到主程序的 link_map → 版本检查报 "no version information available"，
 * 符号查找报 undefined symbol: __libc_start_main。 */
int ext2_64_stat_ino(const char *path, uint32_t *size, uint8_t *isdir,
                     uint32_t *ino_out)
{
    const char *rpath = ext2_64_resolve_path(path);
    uint32_t ino = 0;
    if (!ext2_mounted || ext2_lookup(rpath, &ino) != 0 || ino == 0) return -1;
    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) return -1;
    if (size) *size = inode.i_size;
    if (isdir) *isdir = (inode.i_mode & 0x4000) ? 1 : 0;
    if (ino_out) *ino_out = ino;
    return 0;
}

/* 取文件/目录属性：size=输出大小，isdir=1 目录 */
int ext2_64_stat(const char *path, uint32_t *size, uint8_t *isdir)
{
    return ext2_64_stat_ino(path, size, isdir, 0);
}

/* 递归删除文件/目录 (文件管理器 Delete 用)
 * 非递归实现: 用路径栈式下降，避免内核栈上递归 + 大块缓冲叠加。
 * 返回 0=成功, -1=失败, -2=目录层级过深 */
int ext2_64_delete_tree(const char *path)
{
    if (!path || path[0] != '/') return -1;
    uint32_t sz = 0;
    uint8_t isdir = 0;
    if (ext2_64_stat(path, &sz, &isdir) != 0) return -1;
    if (!isdir) return ext2_64_delete_file(path);

    char cur[512];
    int plen = 0;
    while (path[plen] && plen < 510) { cur[plen] = path[plen]; plen++; }
    cur[plen] = 0;
    if (plen > 1 && cur[plen - 1] == '/') { plen--; cur[plen] = 0; }

    int depth = 0;
    while (depth >= 0) {
        char names[1][256];
        uint8_t types[1];
        int n = ext2_64_list_dir_entries(cur, names, types, 1);
        if (n < 0) return -1;

        if (n == 0) {
            /* 空目录: 删除自身，然后回到父目录继续 */
            int rc = ext2_64_delete_dir(cur);
            if (rc != 0) return rc;
            if (depth == 0) return 0;
            int l = strlen(cur);
            while (l > 1 && cur[l - 1] != '/') l--;
            if (l <= 1) { cur[0] = '/'; cur[1] = 0; }
            else cur[l - 1] = 0;
            depth--;
            continue;
        }

        /* 构造子路径 */
        char child[512];
        int cl = strlen(cur);
        int nl = strlen(names[0]);
        if (cl + 1 + nl >= 510) return -1;
        strcpy(child, cur);
        int cp = cl;
        if (cp > 0 && child[cp - 1] != '/') { child[cp++] = '/'; child[cp] = 0; }
        strcpy(child + cp, names[0]);

        uint32_t csz = 0;
        uint8_t cisdir = 0;
        if (ext2_64_stat(child, &csz, &cisdir) != 0) return -1;
        if (cisdir) {
            if (depth >= 8) return -2;
            strcpy(cur, child);
            depth++;
        } else {
            int rc = ext2_64_delete_file(child);
            if (rc != 0) return -1;
        }
    }
    return 0;
}
