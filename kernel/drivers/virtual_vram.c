/* ============================================================================
 * 赫普OS - Hepu虚拟显存（Hepu Virtual VRAM）
 *
 * 功能：
 * - 从存储分区划分出虚拟显存分区，作为显存使用
 * - 统一管理物理显存 + 虚拟显存，优先物理显存，不足时用虚拟显存
 * - 无显卡设备也能使用显存技术（Hepu虚拟显存）
 * - 显示显存信息：有显卡时显示物理显存+虚拟显存，无显卡时显示Hepu显存分区
 *
 * 设计：
 * - 虚拟显存分区：内存版实现（后续可持久化到磁盘）
 * - 统一显存管理器：整合物理显存区域和虚拟显存区域
 * - 显存分配策略：优先物理显存，不足时用虚拟显存
 * - 显存句柄：统一管理物理/虚拟显存分配
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "virtual_vram.h"
#include "vram.h"
#include "kheap.h"
#include "vga.h"
#include "storage_vram.h"  /* V6.78: 存储-backed显存驱动 */

/* 最大显存区域数 V6.76: 从8扩展到16, 句柄从256扩展到512 */
#define VVRAM_MAX_REGIONS  16
#define VVRAM_MAX_HANDLES  512

/* 显存区域 */
typedef struct {
    uint32_t type;            /* 技术类型 */
    uint32_t phys_addr;       /* 物理地址（虚拟显存为0） */
    uint32_t size;            /* 总大小 */
    uint32_t used;            /* 已使用 */
    char     name[VVRAM_NAME_LEN];
    int      is_virtual;      /* 是否虚拟显存 */
    int      is_storage;      /* V6.78: 是否存储-backed显存 */
    uint8_t  *data;           /* 虚拟显存数据指针（内存版） */
    uint8_t  *bitmap;         /* 分配位图（内存版） */
    int      initialized;
} vvram_region_t;

/* 显存句柄 */
typedef struct {
    int      used;
    uint32_t region_idx;      /* 所属区域 */
    uint32_t offset;          /* 区域内偏移 */
    uint32_t size;            /* 分配大小 */
} vvram_handle_t;

/* 全局状态 */
static vvram_region_t vvram_regions[VVRAM_MAX_REGIONS];
static vvram_handle_t vvram_handles[VVRAM_MAX_HANDLES];
static uint32_t vvram_region_count = 0;
static int vvram_initialized = 0;
static int vvram_has_gpu = 0;
static int vvram_storage_mounted = 0;  /* V6.80: 避免重复挂载 */

/* V6.80: 前向声明 */
static void vvram_auto_mount_storage_vram(void);

/* ============================================================================
 * 区域管理
 * ========================================================================== */

/* 注册显存区域 */
static int vvram_register_region(uint32_t type, uint32_t phys_addr, uint32_t size,
                                   const char *name, int is_virtual)
{
    if (vvram_region_count >= VVRAM_MAX_REGIONS) return -1;

    vvram_region_t *r = &vvram_regions[vvram_region_count];
    memset(r, 0, sizeof(vvram_region_t));

    r->type = type;
    r->phys_addr = phys_addr;
    r->size = size;
    r->used = 0;
    r->is_virtual = is_virtual;
    r->initialized = 1;

    if (name) {
        strncpy(r->name, name, VVRAM_NAME_LEN - 1);
        r->name[VVRAM_NAME_LEN - 1] = '\0';
    }

    /* 虚拟显存需要分配数据和位图 */
    if (is_virtual) {
        r->data = (uint8_t *)kmalloc(size);
        if (!r->data) return -2;
        memset(r->data, 0, size);

        uint32_t block_count = size / VVRAM_BLOCK_SIZE;
        r->bitmap = (uint8_t *)kmalloc(block_count / 8 + 1);
        if (!r->bitmap) {
            kfree(r->data);
            return -3;
        }
        memset(r->bitmap, 0, block_count / 8 + 1);
    }

    vvram_region_count++;
    return vvram_region_count - 1;
}

/* 从物理显存初始化区域 */
static void vvram_init_physical_regions(void)
{
    /* 检测物理显存（简化版：假设VGA显存存在） */
    uint32_t vga_text_size = 32 * 1024;   /* 32KB VGA文本显存 */
    uint32_t vga_gfx_size = 64 * 1024;    /* 64KB VGA图形显存 */

    /* 注册VGA文本显存 */
    vvram_register_region(VVRAM_TECH_VGA, 0xB8000, vga_text_size,
                           "VGA Text VRAM", 0);

    /* 注册VGA图形显存 */
    vvram_register_region(VVRAM_TECH_VGA, 0xA0000, vga_gfx_size,
                           "VGA Graphics VRAM", 0);

    vvram_has_gpu = 1;  /* 标记有物理显卡 */
}

/* ============================================================================
 * 公共接口
 * ========================================================================== */

int vvram_init(uint32_t virtual_size)
{
    if (vvram_initialized) return 0;

    memset(vvram_regions, 0, sizeof(vvram_regions));
    memset(vvram_handles, 0, sizeof(vvram_handles));
    vvram_region_count = 0;
    vvram_has_gpu = 0;

    /* 初始化物理显存区域 */
    vvram_init_physical_regions();

    /* 创建Hepu虚拟显存分区
     * V6.76: 动态降级——大内存分配失败时自动尝试更小的大小,
     * 避免小内存系统上VVRAM初始化完全失败
     * V6.99: 增加更小的降级选项(1MB/512KB/256KB)，确保小内存系统也能初始化 */
    if (virtual_size == 0) virtual_size = VVRAM_DEFAULT_SIZE;
    uint32_t try_sizes[] = {virtual_size, 4*1024*1024, 2*1024*1024, 1*1024*1024,
                            512*1024, 256*1024, 128*1024, 64*1024};
    int ret = -1;
    for (int i = 0; i < 8; i++) {
        if (try_sizes[i] > virtual_size) continue;
        ret = vvram_register_region(VVRAM_TECH_HEPU, 0, try_sizes[i],
                                      "Hepu Virtual VRAM", 1);
        if (ret >= 0) {
            if (try_sizes[i] != virtual_size) {
                vga_printf("[VVRAM] degraded to %dKB (requested %dKB)\n",
                           try_sizes[i] / 1024, virtual_size / 1024);
            }
            break;
        }
    }
    if (ret < 0) {
        vga_printf("[VVRAM] Failed to create Hepu virtual VRAM (all sizes failed)\n");
    }

    vvram_initialized = 1;

    /* V6.80: 尝试自动挂载存储显存（IDE可能未初始化，失败则延后） */
    vvram_auto_mount_storage_vram();

    return 0;
}

/* V6.80: 公共接口——在IDE初始化后重试自动挂载 */
void vvram_try_auto_mount(void)
{
    if (vvram_initialized) {
        vvram_auto_mount_storage_vram();
    }
}

/* V6.80: 自动检测MBR中的存储显存分区（类型0xDA）并挂载 */
static void vvram_auto_mount_storage_vram(void)
{
    extern void serial_puts(uint16_t port, const char *s);
    extern int ide_read_sectors(uint32_t lba, uint8_t *buffer, uint32_t count);
    extern int ide_is_initialized(void);
    static uint8_t mbr[512];

    /* 避免重复挂载 */
    if (vvram_storage_mounted) {
        return;
    }

    /* V6.99: IDE未初始化时静默返回，不报错 */
    if (!ide_is_initialized()) {
        return;
    }

    serial_puts(0x3F8, "[VVRAM] auto-mount: checking MBR...\n");

    /* 读取MBR */
    int ret = ide_read_sectors(0, mbr, 1);
    if (ret != 1) {
        serial_puts(0x3F8, "[VVRAM] auto-mount: failed to read MBR (IDE not ready)\n");
        return;
    }

    /* 验证MBR签名 */
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        char dbg[64];
        snprintf(dbg, sizeof(dbg), "[VVRAM] auto-mount: MBR sig=0x%x%x\n", mbr[510], mbr[511]);
        serial_puts(0x3F8, dbg);
        return;
    }

    /* 查找存储显存分区（类型0xDA） */
    int found = 0;
    for (int i = 0; i < 4; i++) {
        const uint8_t *e = mbr + 446 + i * 16;
        if (e[4] == 0xDA) {  /* 0xDA = 非文件系统数据分区 */
            uint32_t lba = (uint32_t)e[8] | ((uint32_t)e[9] << 8) |
                           ((uint32_t)e[10] << 16) | ((uint32_t)e[11] << 24);
            uint32_t secs = (uint32_t)e[12] | ((uint32_t)e[13] << 8) |
                            ((uint32_t)e[14] << 16) | ((uint32_t)e[15] << 24);

            serial_puts(0x3F8, "[VVRAM] auto-mount: found storage VRAM partition\n");

            if (secs > 0) {
                int mret = vvram_mount_storage_vram(0, lba, secs);
                if (mret == 0) {
                    serial_puts(0x3F8, "[VVRAM] auto-mount: storage VRAM mounted\n");
                    vvram_storage_mounted = 1;
                    found = 1;
                } else {
                    serial_puts(0x3F8, "[VVRAM] auto-mount: mount failed\n");
                }
            }
            break;
        }
    }

    if (!found) {
        serial_puts(0x3F8, "[VVRAM] auto-mount: no storage VRAM partition (type 0xDA)\n");
    }
}

int vvram_create_partition(uint32_t dev, uint32_t start_block, uint32_t size_blocks)
{
    /* 简化版：内存模拟，不实际写入磁盘 */
    /* 后续实现：在存储设备上格式化虚拟显存分区 */
    return 0;
}

int vvram_mount_partition(uint32_t dev, uint32_t start_block)
{
    /* 简化版：内存模拟 */
    return 0;
}

/* 在指定区域分配内存（位图分配） */
static int vvram_alloc_in_region(vvram_region_t *r, uint32_t size, uint32_t flags)
{
    if (!r || !r->initialized || !r->is_virtual) return -1;

    uint32_t block_count = r->size / VVRAM_BLOCK_SIZE;
    uint32_t need_blocks = (size + VVRAM_BLOCK_SIZE - 1) / VVRAM_BLOCK_SIZE;

    /* 4KB对齐分配 */
    for (uint32_t i = 0; i + need_blocks <= block_count; i++) {
        int free = 1;
        for (uint32_t j = 0; j < need_blocks; j++) {
            if (r->bitmap[(i + j) / 8] & (1 << ((i + j) % 8))) {
                free = 0;
                break;
            }
        }
        if (free) {
            /* 标记已用 */
            for (uint32_t j = 0; j < need_blocks; j++) {
                r->bitmap[(i + j) / 8] |= (1 << ((i + j) % 8));
            }
            r->used += need_blocks * VVRAM_BLOCK_SIZE;
            return i * VVRAM_BLOCK_SIZE;
        }
    }
    return -1;
}

int vvram_alloc(uint32_t size, uint32_t flags)
{
    if (!vvram_initialized || size == 0) return -1;

    /* 优先分配物理显存（通过vram_alloc） */
    /* 简化版：物理显存太小，直接用虚拟显存 */
    /* 实际实现：先尝试vram_alloc，失败再用虚拟显存 */

    /* 查找空闲句柄 */
    int handle = -1;
    for (int i = 0; i < VVRAM_MAX_HANDLES; i++) {
        if (!vvram_handles[i].used) {
            handle = i;
            break;
        }
    }
    if (handle < 0) return -1;

    /* 优先从虚拟显存区域分配 */
    for (uint32_t i = 0; i < vvram_region_count; i++) {
        vvram_region_t *r = &vvram_regions[i];
        if (r->is_virtual && r->initialized) {
            int offset = vvram_alloc_in_region(r, size, flags);
            if (offset >= 0) {
                vvram_handles[handle].used = 1;
                vvram_handles[handle].region_idx = i;
                vvram_handles[handle].offset = offset;
                vvram_handles[handle].size = size;
                return handle;
            }
        }
    }

    return -1;  /* 分配失败 */
}

void vvram_free(int handle)
{
    if (!vvram_initialized || handle < 0 || handle >= VVRAM_MAX_HANDLES) return;
    if (!vvram_handles[handle].used) return;

    vvram_handle_t *h = &vvram_handles[handle];
    vvram_region_t *r = &vvram_regions[h->region_idx];

    if (r->is_virtual && r->bitmap) {
        uint32_t start_block = h->offset / VVRAM_BLOCK_SIZE;
        uint32_t need_blocks = (h->size + VVRAM_BLOCK_SIZE - 1) / VVRAM_BLOCK_SIZE;
        for (uint32_t i = 0; i < need_blocks; i++) {
            r->bitmap[(start_block + i) / 8] &= ~(1 << ((start_block + i) % 8));
        }
        r->used -= need_blocks * VVRAM_BLOCK_SIZE;
    }

    memset(h, 0, sizeof(vvram_handle_t));
}

int vvram_read(int handle, uint32_t offset, void *buf, uint32_t len)
{
    if (!vvram_initialized || !buf) return -1;
    if (handle < 0 || handle >= VVRAM_MAX_HANDLES || !vvram_handles[handle].used) return -2;

    vvram_handle_t *h = &vvram_handles[handle];
    if (offset + len > h->size) return -3;

    vvram_region_t *r = &vvram_regions[h->region_idx];

    /* 存储-backed显存：通过sbvr驱动读取 */
    if (r->is_storage) {
        uint32_t total_offset = h->offset + offset;
        uint32_t block_idx = total_offset / VVRAM_BLOCK_SIZE;
        uint32_t block_off = total_offset % VVRAM_BLOCK_SIZE;
        uint32_t remaining = len;
        uint8_t *dst = (uint8_t *)buf;

        while (remaining > 0) {
            uint32_t chunk = VVRAM_BLOCK_SIZE - block_off;
            if (chunk > remaining) chunk = remaining;

            if (sbvr_read(block_idx, block_off, dst, chunk) != 0) return -4;

            dst += chunk;
            remaining -= chunk;
            block_idx++;
            block_off = 0;
        }
        return len;
    }

    if (r->is_virtual && r->data) {
        memcpy(buf, r->data + h->offset + offset, len);
        return len;
    }

    /* 物理显存：直接从物理地址读取 */
    if (r->phys_addr) {
        memcpy(buf, (void *)(r->phys_addr + h->offset + offset), len);
        return len;
    }

    return -4;
}

int vvram_write(int handle, uint32_t offset, const void *buf, uint32_t len)
{
    if (!vvram_initialized || !buf) return -1;
    if (handle < 0 || handle >= VVRAM_MAX_HANDLES || !vvram_handles[handle].used) return -2;

    vvram_handle_t *h = &vvram_handles[handle];
    if (offset + len > h->size) return -3;

    vvram_region_t *r = &vvram_regions[h->region_idx];

    /* 存储-backed显存：通过sbvr驱动写入 */
    if (r->is_storage) {
        uint32_t total_offset = h->offset + offset;
        uint32_t block_idx = total_offset / VVRAM_BLOCK_SIZE;
        uint32_t block_off = total_offset % VVRAM_BLOCK_SIZE;
        uint32_t remaining = len;
        const uint8_t *src = (const uint8_t *)buf;

        while (remaining > 0) {
            uint32_t chunk = VVRAM_BLOCK_SIZE - block_off;
            if (chunk > remaining) chunk = remaining;

            if (sbvr_write(block_idx, block_off, src, chunk) != 0) return -4;

            src += chunk;
            remaining -= chunk;
            block_idx++;
            block_off = 0;
        }
        return len;
    }

    if (r->is_virtual && r->data) {
        memcpy(r->data + h->offset + offset, buf, len);
        return len;
    }

    /* 物理显存：直接写入物理地址 */
    if (r->phys_addr) {
        memcpy((void *)(r->phys_addr + h->offset + offset), buf, len);
        return len;
    }

    return -4;
}

int vvram_get_region_info(uint32_t index, vvram_region_info_t *info)
{
    if (!info || index >= vvram_region_count) return -1;

    vvram_region_t *r = &vvram_regions[index];
    info->type = r->type;
    info->phys_addr = r->phys_addr;
    info->size = r->size;
    info->used = r->used;
    info->is_virtual = r->is_virtual;
    strncpy(info->name, r->name, VVRAM_NAME_LEN - 1);
    info->name[VVRAM_NAME_LEN - 1] = '\0';

    return 0;
}

void vvram_get_stats(vvram_stats_t *stats)
{
    if (!stats) return;
    memset(stats, 0, sizeof(vvram_stats_t));

    stats->region_count = vvram_region_count;
    stats->has_gpu = vvram_has_gpu;

    for (uint32_t i = 0; i < vvram_region_count; i++) {
        vvram_region_t *r = &vvram_regions[i];
        stats->total_vram += r->size;
        stats->used_vram += r->used;

        if (r->is_virtual) {
            stats->virtual_total += r->size;
            stats->virtual_used += r->used;
        } else {
            stats->physical_total += r->size;
            stats->physical_used += r->used;
        }
    }
}

void vvram_dump(void)
{
    vvram_stats_t stats;
    vvram_get_stats(&stats);

    vga_printf("=== Hepu VRAM Information ===\n");

    if (stats.has_gpu) {
        vga_printf("[GPU] Physical GPU detected\n");
    } else {
        vga_printf("[GPU] No physical GPU, using Hepu Virtual VRAM\n");
    }

    vga_printf("[Total] Total VRAM: %d KB, Used: %d KB\n",
                stats.total_vram / 1024, stats.used_vram / 1024);

    if (stats.physical_total > 0) {
        vga_printf("[Physical] Physical VRAM: %d KB, Used: %d KB\n",
                    stats.physical_total / 1024, stats.physical_used / 1024);
    }

    if (stats.virtual_total > 0) {
        vga_printf("[Hepu] Hepu Virtual VRAM: %d KB, Used: %d KB (Hepu VRAM Technology)\n",
                    stats.virtual_total / 1024, stats.virtual_used / 1024);
    }

    vga_printf("[Regions] %d VRAM regions:\n", stats.region_count);
    for (uint32_t i = 0; i < stats.region_count; i++) {
        vvram_region_info_t info;
        vvram_get_region_info(i, &info);
        vga_printf("  [%d] %s: %d KB, Used: %d KB%s\n",
                    i, info.name, info.size / 1024, info.used / 1024,
                    info.is_virtual ? " [Virtual]" : "");
    }
    vga_printf("==============================\n");
}

/* ============================================================================
 * 自测
 * ========================================================================== */
void vvram_selftest(void)
{
    extern void serial_puts(uint16_t, const char *);
    extern void serial_puthex(uint16_t, uint32_t);
    #define VVLOG(s) serial_puts(0x3F8, s)

    VVLOG("[VVRAM] selftest: start\n");
    int pass = 0, fail = 0;

    /* 测试1：初始化（8MB虚拟显存） */
    int ret = vvram_init(8 * 1024 * 1024);
    if (ret == 0) {
        vvram_stats_t stats;
        vvram_get_stats(&stats);
        if (stats.region_count >= 2 && stats.virtual_total > 0) {
            VVLOG("[VVRAM] test1 init: PASS (regions=");
            serial_puthex(0x3F8, stats.region_count);
            VVLOG(", virtual=");
            serial_puthex(0x3F8, stats.virtual_total / 1024);
            VVLOG("KB)\n");
            pass++;
        } else {
            VVLOG("[VVRAM] test1 init: FAIL\n");
            fail++;
        }
    } else {
        VVLOG("[VVRAM] test1 init: FAIL\n");
        fail++;
    }

    /* 测试2：分配虚拟显存 */
    int h1 = vvram_alloc(4096, 0);
    if (h1 >= 0) {
        VVLOG("[VVRAM] test2 alloc 4KB: PASS (handle=");
        serial_puthex(0x3F8, h1);
        VVLOG(")\n");
        pass++;
    } else {
        VVLOG("[VVRAM] test2 alloc 4KB: FAIL\n");
        fail++;
    }

    /* 测试3：写入/读取虚拟显存 */
    static uint8_t write_buf[4096];
    static uint8_t read_buf[4096];
    for (int i = 0; i < 4096; i++) write_buf[i] = i & 0xFF;

    ret = vvram_write(h1, 0, write_buf, 4096);
    if (ret == 4096) {
        memset(read_buf, 0, 4096);
        ret = vvram_read(h1, 0, read_buf, 4096);
        if (ret == 4096 && memcmp(write_buf, read_buf, 4096) == 0) {
            VVLOG("[VVRAM] test3 write/read: PASS\n");
            pass++;
        } else {
            VVLOG("[VVRAM] test3 read: FAIL\n");
            fail++;
        }
    } else {
        VVLOG("[VVRAM] test3 write: FAIL\n");
        fail++;
    }

    /* 测试4：部分读取 */
    memset(read_buf, 0, 4096);
    ret = vvram_read(h1, 1024, read_buf, 512);
    if (ret == 512 && memcmp(write_buf + 1024, read_buf, 512) == 0) {
        VVLOG("[VVRAM] test4 partial read: PASS\n");
        pass++;
    } else {
        VVLOG("[VVRAM] test4 partial read: FAIL\n");
        fail++;
    }

    /* 测试5：分配多个显存块 */
    int h2 = vvram_alloc(64 * 1024, 0);  /* 64KB */
    int h3 = vvram_alloc(256 * 1024, 0); /* 256KB */
    if (h2 >= 0 && h3 >= 0) {
        VVLOG("[VVRAM] test5 multi alloc: PASS (64KB+256KB)\n");
        pass++;
    } else {
        VVLOG("[VVRAM] test5 multi alloc: FAIL\n");
        fail++;
    }

    /* 测试6：释放显存 */
    vvram_free(h1);
    vvram_free(h2);
    vvram_free(h3);
    vvram_stats_t stats;
    vvram_get_stats(&stats);
    if (stats.virtual_used == 0) {
        VVLOG("[VVRAM] test6 free all: PASS (virtual_used=0)\n");
        pass++;
    } else {
        VVLOG("[VVRAM] test6 free all: FAIL (used=");
        serial_puthex(0x3F8, stats.virtual_used);
        VVLOG(")\n");
        fail++;
    }

    /* 测试7：显存信息和区域数 */
    vvram_get_stats(&stats);
    if (stats.has_gpu && stats.region_count >= 2 && stats.virtual_total > 0) {
        VVLOG("[VVRAM] test7 info: PASS (gpu=1, regions=");
        serial_puthex(0x3F8, stats.region_count);
        VVLOG(", virtual=");
        serial_puthex(0x3F8, stats.virtual_total / 1024);
        VVLOG("KB)\n");
        pass++;
    } else {
        VVLOG("[VVRAM] test7 info: FAIL\n");
        fail++;
    }

    VVLOG("[VVRAM] selftest: done (pass=");
    serial_puthex(0x3F8, pass);
    VVLOG(", fail=");
    serial_puthex(0x3F8, fail);
    VVLOG(")\n");

    #undef VVLOG
}

/* ============================================================================
 * V6.78: 存储-backed显存分区支持
 * ========================================================================== */

/* 挂载存储-backed显存分区 */
int vvram_mount_storage_vram(uint32_t dev_id, uint64_t start_lba, uint64_t total_sectors)
{
    if (!vvram_initialized) {
        vga_puts("[VVRAM] system not initialized\n");
        return -1;
    }

    if (vvram_region_count >= VVRAM_MAX_REGIONS) {
        vga_puts("[VVRAM] no free region slot\n");
        return -2;
    }

    /* 初始化sbvr驱动 */
    int ret = sbvr_init(dev_id, start_lba, total_sectors);
    if (ret != 0) {
        vga_printf("[VVRAM] sbvr_init failed: %d\n", ret);
        return -3;
    }

    /* 获取存储显存统计 */
    uint32_t total_mb, used_mb, free_mb;
    sbvr_stats(&total_mb, &used_mb, &free_mb);

    /* 注册为VVRAM区域 */
    vvram_region_t *r = &vvram_regions[vvram_region_count];
    memset(r, 0, sizeof(vvram_region_t));

    r->type = VVRAM_TECH_STORAGE;
    r->phys_addr = 0;  /* 存储-backed无物理地址 */
    r->size = total_mb * 1024 * 1024;
    r->used = used_mb * 1024 * 1024;
    r->is_virtual = 1;  /* 视为虚拟显存的一种 */
    r->is_storage = 1;  /* 标记为存储-backed */
    r->initialized = 1;

    /* 分配内存位图用于快速分配（不同步到存储，仅作缓存） */
    uint32_t block_count = r->size / VVRAM_BLOCK_SIZE;
    r->bitmap = (uint8_t *)kmalloc(block_count / 8 + 1);
    if (r->bitmap) {
        memset(r->bitmap, 0, block_count / 8 + 1);
    }

    strncpy(r->name, "Hepu Storage VRAM", VVRAM_NAME_LEN - 1);
    r->name[VVRAM_NAME_LEN - 1] = '\0';

    vvram_region_count++;

    vga_printf("[VVRAM] storage VRAM mounted: %dMB (Hepu显存技术)\n", total_mb);
    return 0;
}


