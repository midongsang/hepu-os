/* ============================================================================
 * 赫普OS - IDE磁盘 ext2 文件系统挂载 (V3.87-D6 按需读写架构)
 * 设计: superblock+GDT 常驻 sb_area(32KB, 覆盖块0-31);
 *       位图/inode表/间接表/数据块 经 4x4KB 块缓存 按需 IDE 直读直写。
 * 数据盘容量上限: IDE 28位LBA 硬件上限 128GB (此前架构上限 128MB 已突破)。
 * ========================================================================== */
#include <stdint.h>
#include <string.h>
#include "ide.h"
#include "ext2.h"
#include "i18n.h"
#include "ide_ext2.h"
#include "kheap.h"
#include "vga.h"
#include "serial.h"
#include "page_cache.h"

/* V6.49: 页缓存（Page Cache）二级缓存
 * 基于《赫普OS内存、显存与存储资源管理详解》3.4节实现
 * 4槽块缓存作为一级缓存（接口兼容），页缓存作为二级缓存（扩大容量、LRU替换）
 * 最大缓存4096页（16MB），哈希表O(1)查找，LRU回收，脏页写回 */
static page_cache_t *g_block_cache = NULL;

static uint8_t sb_area[32768];          /* superblock+GDT 常驻区 (块0-31) */
int fs_mounted = 0;
/* V3.80: 数据区定位 (MBR 0x83 分区或整盘) */
static uint32_t data_lba = 0;
static uint32_t data_sectors = 0;

/* V3.87-D6: 块缓存 4x4KB (位图/inode表/间接表/数据块共用) */
static uint8_t  cache_blk[4][4096];
static uint32_t cache_num[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
static int      cache_dirty[4] = {0, 0, 0, 0};
static uint32_t cache_bs = 1024;

/* 读IDE磁盘指定扇区区间到内存 (逐扇区, QEMU PIO 稳定) */
static int ide_read_range(uint8_t *mem, uint32_t lba0, uint32_t total)
{
    uint32_t lba = 0;
    while (lba < total) {
        if (ide_read_sectors(lba0 + lba, mem + lba * 512, 1) <= 0) return -1;
        lba++;
    }
    return 0;
}

/* 写回IDE磁盘指定扇区区间 */
static int ide_write_range(uint8_t *mem, uint32_t lba0, uint32_t total)
{
    uint32_t lba = 0;
    while (lba < total) {
        if (ide_write_sectors(lba0 + lba, mem + lba * 512, 1) <= 0) return -1;
        lba++;
    }
    return 0;
}

/* V6.49: 页缓存写回函数：脏页写回IDE磁盘 */
static int block_cache_writeback(page_cache_t *cache, page_cache_entry_t *entry)
{
    (void)cache;
    if (!entry || !fs_mounted)
        return -1;
    uint32_t blk = entry->index;
    uint8_t *buf = (uint8_t *)(uintptr_t)entry->phys_addr;
    return ide_write_range(buf, data_lba + blk * cache_bs / 512, cache_bs / 512);
}

/* === 块缓存核心: 取块指针(未命中则逐出脏块+IDE读入) ===
 * V6.49: 集成页缓存作为二级缓存
 * - 一级缓存：4槽静态缓冲区（接口兼容，调用方可直接修改）
 * - 二级缓存：页缓存（4096页/LRU/哈希，扩大容量、减少磁盘IO）
 * 查找顺序：一级缓存 → 二级缓存 → IDE磁盘
 * IDE读取后同时写入一级和二级缓存 */
static uint8_t *meta_get(uint32_t blk)
{
    /* 一级缓存查找（4槽静态缓冲区） */
    for (int i = 0; i < 4; i++)
        if (cache_num[i] == blk) return cache_blk[i];

    /* 二级缓存查找（页缓存） */
    if (g_block_cache) {
        page_cache_entry_t *entry = page_cache_lookup(g_block_cache, blk);
        if (entry) {
            /* 二级缓存命中：拷贝到一级缓存槽 */
            static int next = 0;
            int slot = next; next = (next + 1) & 3;
            if (cache_dirty[slot]) {
                if (ide_write_range(cache_blk[slot], data_lba + cache_num[slot] * cache_bs / 512,
                                    cache_bs / 512) != 0) {
                    page_cache_release(g_block_cache, entry);
                    return NULL;
                }
                cache_dirty[slot] = 0;
            }
            memcpy(cache_blk[slot], (const void *)(uintptr_t)entry->phys_addr, cache_bs);
            cache_num[slot] = blk;
            page_cache_release(g_block_cache, entry);
            return cache_blk[slot];
        }
    }

    /* 一级和二级缓存都未命中：从IDE读取 */
    static int next = 0;
    int slot = next; next = (next + 1) & 3;
    if (cache_dirty[slot]) {
        if (ide_write_range(cache_blk[slot], data_lba + cache_num[slot] * cache_bs / 512,
                            cache_bs / 512) != 0) return NULL;
        cache_dirty[slot] = 0;
    }
    if (ide_read_range(cache_blk[slot], data_lba + blk * cache_bs / 512, cache_bs / 512) != 0)
        return NULL;
    cache_num[slot] = blk;

    /* 同时写入二级缓存（页缓存） */
    if (g_block_cache) {
        extern uint32_t mm_alloc_page(void);
        uint32_t phys = mm_alloc_page();
        if (phys) {
            memcpy((void *)(uintptr_t)phys, cache_blk[slot], cache_bs);
            page_cache_entry_t *entry = page_cache_add(g_block_cache, blk, phys, PC_FLAG_UPTODATE);
            if (entry)
                page_cache_release(g_block_cache, entry);
        }
    }

    return cache_blk[slot];
}

/* 标记块为脏(调用方修改 meta_get 返回指针后必须调用)
 * V6.49: 同时更新页缓存（二级缓存），保持数据一致性
 * 从一级缓存拷贝最新数据到页缓存，并标记脏 */
void ext2_block_dirty(uint32_t blk)
{
    uint8_t *src = NULL;
    for (int i = 0; i < 4; i++) {
        if (cache_num[i] == blk) {
            cache_dirty[i] = 1;
            src = cache_blk[i];
            break;
        }
    }
    if (!src || !g_block_cache) return;

    /* 更新页缓存：如果已存在则更新数据并标记脏，否则添加新条目 */
    page_cache_entry_t *entry = page_cache_lookup(g_block_cache, blk);
    if (entry) {
        memcpy((void *)(uintptr_t)entry->phys_addr, src, cache_bs);
        page_cache_mark_dirty(g_block_cache, entry);
        page_cache_release(g_block_cache, entry);
    } else {
        extern uint32_t mm_alloc_page(void);
        uint32_t phys = mm_alloc_page();
        if (phys) {
            memcpy((void *)(uintptr_t)phys, src, cache_bs);
            entry = page_cache_add(g_block_cache, blk, phys, PC_FLAG_DIRTY | PC_FLAG_UPTODATE);
            if (entry)
                page_cache_release(g_block_cache, entry);
        }
    }
}

/* 按需访问: 返回块缓存指针(可读可写), 写后须 ext2_block_dirty */
uint8_t *ext2_block_access(uint32_t blk)
{
    return meta_get(blk);
}

/* 按需读: 拷贝一块到 out */
int ext2_read_block(uint32_t blk, uint8_t *out)
{
    if (!fs_mounted || !out) return -1;
    uint8_t *p = meta_get(blk);
    if (!p) return -1;
    memcpy(out, p, cache_bs);
    return 0;
}

/* 按需写: 整块拷入缓存并标记脏
 * V6.49: 同时更新页缓存（二级缓存），保持数据一致性 */
int ext2_write_block(uint32_t blk, const uint8_t *in)
{
    if (!fs_mounted || !in) return -1;
    uint8_t *p = meta_get(blk);
    if (!p) return -1;
    memcpy(p, in, cache_bs);
    ext2_block_dirty(blk);

    /* 同时更新页缓存（如果该块在缓存中） */
    if (g_block_cache) {
        page_cache_entry_t *entry = page_cache_lookup(g_block_cache, blk);
        if (entry) {
            memcpy((void *)(uintptr_t)entry->phys_addr, in, cache_bs);
            page_cache_mark_dirty(g_block_cache, entry);
            page_cache_release(g_block_cache, entry);
        }
    }

    return 0;
}

/* 写回全部脏缓存块
 * V6.49: 同时写回页缓存的脏页 */
int ext2_block_flush(void)
{
    if (!fs_mounted) return -1;
    /* 写回一级缓存（4槽）的脏块 */
    for (int i = 0; i < 4; i++) {
        if (cache_dirty[i]) {
            if (ide_write_range(cache_blk[i], data_lba + cache_num[i] * cache_bs / 512,
                                cache_bs / 512) != 0) return -1;
            cache_dirty[i] = 0;
        }
    }
    /* 写回二级缓存（页缓存）的脏页 */
    if (g_block_cache)
        page_cache_flush(g_block_cache);
    return 0;
}

/* 挂载: 读 sb+gdt 常驻区(块0-31) → ext2_init → 就绪
 * V6.49: 自动检测ext2数据盘所在的IDE驱动器（支持系统盘+数据盘多硬盘场景） */
int ide_ext2_mount(void)
{
    if (fs_mounted) return 0;

    /* V0.10.5: 改进探测逻辑——先解析MBR找0x83分区，再在分区内校验ext2超级块
     * 兼容：整盘ext2（无分区表）和分区内ext2（BIOS/UEFI混合镜像） */
    int ext2_drive = -1;
    uint32_t found_data_lba = 0;
    uint32_t found_data_sectors = 0;
    static uint8_t mbr[512];
    static uint8_t probe_buf[2048];

    for (int i = 0; i < 4; i++) {
        ide_drive_t di;
        if (ide_get_drive_info(i, &di) != 0) continue;
        if (!di.present) continue;
        ide_set_current_drive(i);

        uint32_t part_start = 0;
        uint32_t part_count = di.total_sectors;

        /* 先解析MBR分区表 */
        if (ide_read_sectors(0, mbr, 1) > 0 &&
            mbr[510] == 0x55 && mbr[511] == 0xAA) {
            for (int p = 0; p < 4; p++) {
                uint8_t *pe = mbr + 446 + p * 16;
                if (pe[4] == 0x83 && (pe[8] | pe[9] | pe[10] | pe[11]) != 0) {
                    part_start = pe[8] | (pe[9] << 8) | (pe[10] << 16) |
                                 ((uint32_t)pe[11] << 24);
                    part_count = pe[12] | (pe[13] << 8) | (pe[14] << 16) |
                                 ((uint32_t)pe[15] << 24);
                    break;
                }
            }
        }

        /* 在分区起始位置+2扇区（块1，1024字节偏移）查找ext2超级块 */
        uint32_t sb_lba = part_start + 2;  /* 块0=引导块(2扇区)，块1=超级块 */
        if (sb_lba + 4 <= di.total_sectors &&
            ide_read_sectors(sb_lba, probe_buf, 4) > 0) {
            if (*(uint16_t *)(probe_buf + 56) == 0xEF53) {
                ext2_drive = i;
                found_data_lba = part_start;
                found_data_sectors = part_count;
                serial_puts(0x3F8, "[FS] ext2 filesystem found on IDE drive ");
                extern void serial_puthex(uint16_t, uint32_t);
                serial_puthex(0x3F8, i);
                serial_puts(0x3F8, " partition LBA=");
                serial_puthex(0x3F8, part_start);
                serial_puts(0x3F8, "\n");
                break;
            }
        }
    }
    if (ext2_drive < 0) {
        ide_set_current_drive(-1);
        return -1;
    }

    ide_drive_t d;
    if (ide_get_drive_info(ext2_drive, &d) != 0) {
        ide_set_current_drive(-1);
        return -1;
    }

    data_lba = found_data_lba;
    data_sectors = found_data_sectors;

    /* V3.87-D6: 数据盘容量上限 = IDE 28位LBA 硬件上限 128GB (不再受内存镜像限制) */
    if (data_sectors == 0 || (uint64_t)data_sectors * 512 > (uint64_t)128ULL * 1024 * 1024 * 1024)
        return -1;

    /* 读块0-31 常驻区: superblock(块1) + GDT(块2起) */
    if (ide_read_range(sb_area, data_lba, 64) != 0) return -1;

    /* 校验 superblock */
    if (*(uint16_t *)(sb_area + 1024 + 56) != 0xEF53) return -1;

    /* 检查 GDT 是否在常驻区内 (gdt_bytes <= 32KB - gdt偏移) */
    {
        uint32_t log_bs = *(uint32_t *)(sb_area + 1024 + 24);
        uint32_t blocks = *(uint32_t *)(sb_area + 1024 + 4);
        uint32_t bpg    = *(uint32_t *)(sb_area + 1024 + 32);
        /* V7.68安全修复: 校验块大小，防止缓冲区溢出
         * 缓冲区固定4096字节，支持1024/2048/4096 */
        if (log_bs > 2) {
            serial_puts(0x3F8, "[FS] block size too large (max 4096)\n");
            return -1;
        }
        uint32_t bs = 1024 << log_bs;
        /* V7.68安全修复: 校验bpg非零，防止除零 */
        if (bpg == 0) {
            serial_puts(0x3F8, "[FS] blocks_per_group is zero\n");
            return -1;
        }
        uint32_t groups = (blocks + bpg - 1) / bpg;
        uint32_t gdt_block = (bs == 1024) ? 2 : 1;
        uint32_t gdt_bytes = groups * 32;
        if ((uint32_t)gdt_block * bs + gdt_bytes > sizeof(sb_area)) {
            serial_puts(0x3F8, "[FS] GDT too large for resident area\n");
            return -1;
        }
        /* V7.68安全修复: 最终确认cache_bs不超过缓冲区大小 */
        if (bs > 4096) {
            serial_puts(0x3F8, "[FS] cache_bs > 4096, rejected\n");
            return -1;
        }
        cache_bs = bs;
    }

    if (ext2_init(sb_area, sizeof(sb_area)) != 0) return -1;
    fs_mounted = 1;

    /* V6.49: 初始化页缓存（二级缓存）
     * 基于《赫普OS内存、显存与存储资源管理详解》3.4节实现
     * 最大缓存4096页（16MB），LRU替换，脏页写回 */
    g_block_cache = page_cache_create("ext2-block-cache", block_cache_writeback);
    if (g_block_cache)
        serial_puts(0x3F8, "[FS] page cache initialized (4096 pages, LRU)\n");
    else
        serial_puts(0x3F8, "[FS] page cache init FAILED, using 4-slot cache only\n");

    return 0;
}

/* 同步回写: superblock+GDT 常驻区 + 全部脏缓存块 */
int ide_ext2_sync(void)
{
    if (!fs_mounted) return -1;
    /* 常驻区(块0-31)全量写回: superblock 计数/GDT 空闲计数等 */
    if (ide_write_range(sb_area, data_lba, sizeof(sb_area) / 512) != 0) return -1;
    return ext2_block_flush();
}

void ide_ext2_list_dir(void)
{
    if (!fs_mounted) {
        if (ide_ext2_mount() != 0) { vga_puts(" fs: no ext2 disk mounted\n"); return; }
    }
    ext2_inode_t root;
    if (ext2_read_inode(EXT2_ROOT_INODE, &root) != 0) { vga_puts(" fs: read root inode FAIL\n"); return; }
    vga_puts(" /:\n");
    uint32_t offset = 0;
    static uint8_t block[4096];
    while (offset < root.i_size) {
        uint32_t to_read = root.i_size - offset;
        if (to_read > 4096) to_read = 4096;
        uint32_t n = ext2_read_data(&root, offset, block, to_read);
        if (n <= 0) break;
        uint32_t pos = 0;
        while (pos + 8 <= n) {
            ext2_dir_entry_t *entry = (ext2_dir_entry_t *)(block + pos);
            if (entry->rec_len < 8 || entry->name_len >= 255) break;
            if (pos + entry->rec_len > n) break;
            if (entry->inode != 0) {
                char nm[256];
                uint32_t nl = entry->name_len;
                if (nl > 255) nl = 255;
                memcpy(nm, entry->name, nl);
                nm[nl] = 0;
                vga_printf("   %d  %s\n", entry->inode, nm);
            }
            pos += entry->rec_len;
        }
        offset += to_read;
    }
}

/* 读取文件并打印内容（支持 /path 多级） */
int ide_ext2_cat(const char *name)
{
    if (!fs_mounted) {
        if (ide_ext2_mount() != 0) return -1;
    }
    static char path[300];
    int n = 0;
    path[n++] = '/';
    for (int i = 0; name[i] && n < 298; i++) path[n++] = name[i];
    path[n] = 0;
    char filename[256];
    uint32_t dir_ino;
    ext2_inode_t inode;
    uint32_t ino = 0;
    if (ext2_resolve_path(path, &dir_ino, filename, 256) != 0) return -1;
    ext2_inode_t dir;
    if (ext2_read_inode(dir_ino, &dir) != 0) return -1;
    if (ext2_lookup(&dir, filename, &ino) != 0) return -1;
    if (ext2_read_inode(ino, &inode) != 0) return -1;
    if (inode.i_size > 0) {
        static char rbuf[512];
        uint32_t n = ext2_read_data(&inode, 0, rbuf, inode.i_size > 511 ? 511 : inode.i_size);
        rbuf[n] = 0;
        vga_puts(rbuf);
        vga_putc('\n');
    }
    return 0;
}

/* 创建/覆盖文件并写入内容（支持 /path 多级） */
int ide_ext2_writefile(const char *name, const char *data)
{
    if (!fs_mounted) {
        if (ide_ext2_mount() != 0) return -1;
    }
    ext2_inode_t inode;
    uint32_t ino = 0;
    int have = 0;
    static char path[300];
    int n = 0;
    path[n++] = '/';
    for (int i = 0; name[i] && n < 298; i++) path[n++] = name[i];
    path[n] = 0;
    char filename[256];
    uint32_t dir_ino;
    ext2_inode_t dir;
    if (ext2_resolve_path(path, &dir_ino, filename, 256) != 0) return -1;
    if (ext2_read_inode(dir_ino, &dir) != 0) return -1;
    have = (ext2_lookup(&dir, filename, &ino) == 0);
    if (!have) {
        int pc = ext2_path_create(path, 0x81A4);
        if (pc <= 0) return -1;
        if (ext2_resolve_path(path, &dir_ino, filename, 256) != 0) return -1;
        if (ext2_read_inode(dir_ino, &dir) != 0) return -1;
        if (ext2_lookup(&dir, filename, &ino) != 0) return -1;
    }
    if (ext2_read_inode(ino, &inode) != 0) return -1;
    uint32_t dl = 0;
    while (data[dl] && dl < 4095) dl++;
    if (ext2_write_data(&inode, 0, data, dl) > 0) {
        inode.i_size = dl;
        ext2_write_inode(ino, &inode);
        extern int ide_ext2_sync(void);
        ide_ext2_sync();   /* V3.87-E15: 脏缓存落盘——否则写入只留在内存缓存 */
        return (int)dl;
    }
    return -1;
}

/* V2.57: 构造 /name 根路径（一级，手机系统场景够用） */
static int make_root_path(const char *name, char *out, int cap)
{
    if (!name || !name[0] || !out) return -1;
    int n = 0;
    out[n++] = '/';
    for (int i = 0; name[i] && n < cap - 1; i++) out[n++] = name[i];
    out[n] = 0;
    return n;
}

/* V2.57: 删除根目录文件 */
int ide_ext2_rm(const char *name)
{
    if (!fs_mounted) {
        if (ide_ext2_mount() != 0) return -1;
    }
    static char path[300];
    if (make_root_path(name, path, 300) < 0) return -1;
    int r = ext2_path_unlink_v2(path);
    if (r == 0) {
        extern int ide_ext2_sync(void);
        ide_ext2_sync();
        vga_printf(" deleted /%s\n", name);
    } else vga_printf(" rm /%s FAIL (not found?)\n", name);
    return r;
}

/* V2.57: 重命名根目录文件 */
int ide_ext2_mv(const char *oldn, const char *newn)
{
    if (!fs_mounted) {
        if (ide_ext2_mount() != 0) return -1;
    }
    static char oldp[300], newp[300];
    if (make_root_path(oldn, oldp, 300) < 0) return -1;
    if (make_root_path(newn, newp, 300) < 0) return -1;
    int r = ext2_path_rename(oldp, newp);
    if (r == 0) {
        extern int ide_ext2_sync(void);
        ide_ext2_sync();
        vga_printf(" renamed /%s -> /%s\n", oldn, newn);
    } else vga_printf(" mv /%s -> /%s FAIL\n", oldn, newn);
    return r;
}

/* V2.57: 根目录下建子目录 */
int ide_ext2_mkdir(const char *name)
{
    if (!fs_mounted) {
        if (ide_ext2_mount() != 0) return -1;
    }
    static char path[300];
    if (make_root_path(name, path, 300) < 0) return -1;
    int r = ext2_path_mkdir_v2(path, 0x1ED);
    if (r > 0) vga_printf(" created dir /%s (inode=%d)\n", name, r);
    else vga_printf(" mkdir /%s FAIL (exists?)\n", name);
    return r > 0 ? 0 : -1;
}

/* V2.58: 解析目录 inode（"/"->根; "/docs"->docs 目录 inode） */
int ide_ext2_free_bytes(void)
{
    if (!fs_mounted) {
        if (ide_ext2_mount() != 0) return -1;
    }
    ext2_superblock_t *sb = ext2_get_sb();
    if (!sb) return -1;
    uint64_t free_bytes = (uint64_t)sb->s_free_blocks_count * ext2_block_size;
    if (free_bytes > 0x7FFFFFFF) return 0x7FFFFFFF;
    return (int)free_bytes;
}

/* ============================================================================
 * V4.03-E48: fs stat / fs df —— 数据盘管理与写入增强
 * - fs stat <name>: inode/类型/大小/块数/时间
 * - fs df        : 空间统计(总块/空闲块/空闲inode/占用%)
 * ========================================================================== */
int ide_ext2_stat(const char *name)
{
    if (!fs_mounted) {
        if (ide_ext2_mount() != 0) return -1;
    }
    static char path[300];
    int n = 0;
    path[n++] = '/';
    for (int i = 0; name[i] && n < 298; i++) path[n++] = name[i];
    path[n] = 0;

    char filename[256];
    uint32_t dir_ino;
    ext2_inode_t dir, ino;
    if (ext2_resolve_path(path, &dir_ino, filename, 256) != 0) return -1;
    if (ext2_read_inode(dir_ino, &dir) != 0) return -1;
    uint32_t ino_num = 0;
    if (ext2_lookup(&dir, filename, &ino_num) != 0) return -1;
    if (ext2_read_inode(ino_num, &ino) != 0) return -1;

    vga_printf(" %s: inode=%u size=%u blocks=%u\n", name,
               ino_num, ino.i_size, ino.i_blocks / 2);
    vga_printf("  mode=0x%x uid=%u gid=%u links=%u\n",
               ino.i_mode, ino.i_uid, ino.i_gid, ino.i_links_count);
    if (ino.i_mode & 0x4000)      vga_puts("  type: dir\n");
    else if (ino.i_mode & 0x8000) vga_puts("  type: regular file\n");
    else                          vga_puts("  type: other\n");
    vga_printf("  mtime=%u\n", ino.i_mtime);
    return 0;
}

int ide_ext2_df(void)
{
    if (!fs_mounted) {
        if (ide_ext2_mount() != 0) return -1;
    }
    ext2_superblock_t *sb = ext2_get_sb();
    if (!sb) return -1;
    /* 32 位内核无 __udivdi3: 全程 uint32 运算(32MB 盘 blocks*bs=32MB, KB 不溢出) */
    uint32_t total_kb = (uint32_t)(sb->s_blocks_count * ext2_block_size / 1024);
    uint32_t free_kb  = (uint32_t)(sb->s_free_blocks_count * ext2_block_size / 1024);
    uint32_t used_kb  = total_kb - free_kb;
    uint32_t pct = (total_kb > 0) ? (used_kb * 100 / total_kb) : 0;
    extern const char *i18n_get(int);
    vga_puts(i18n_get(I18N_STORAGE_FREE));
    vga_printf(": total=%uKB used=%uKB free=%uKB (%u%% used)\n",
               total_kb, used_kb, free_kb, pct);
    vga_printf("     inodes: free=%u/%u blocks: free=%u/%u\n",
               sb->s_free_inodes_count, sb->s_inodes_count,
               sb->s_free_blocks_count, sb->s_blocks_count);
    return 0;
}
