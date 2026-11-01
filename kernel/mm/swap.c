/* ============================================================================
 * 赫普OS - Swap交换子系统 V2 (V6.77增强)
 * 支持: 内存模拟swap + 磁盘swap分区接口
 *
 * V6.77: 槽位从512扩展到2048 (8MB), 新增磁盘swap分区接口
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "swap.h"
#include "mm.h"
#include "vga.h"
#include "string.h"
#include "reclaim.h"
#include "kheap.h"

#define SWAP_MAGIC      0x53575031
#define SWAP_SLOT_MAX   2048       /* V6.77: 2048槽 = 8MB */
#define SWAP_HEADER_MAGIC 0x50415753

/* Swap槽位 */
typedef struct {
    uint8_t  used;
    uint8_t  dirty;
    uint32_t page_phys;
    uint32_t age;
} swap_slot_t;

/* 磁盘swap头部 */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t total_slots;
    uint32_t used_slots;
    uint32_t page_size;
    uint32_t reserved[123];
} swap_disk_header_t;

/* Swap管理器 */
typedef struct {
    uint8_t         initialized;
    uint8_t         enabled;
    uint8_t         disk_mode;
    uint32_t        total_slots;
    uint32_t        used_slots;
    uint32_t        swap_in_count;
    uint32_t        swap_out_count;
    uint32_t        swap_area_addr;
    uint32_t        swap_area_size;
    uint32_t        disk_dev;
    uint64_t        disk_start_lba;
    uint64_t        disk_total_lba;
    swap_slot_t     slots[SWAP_SLOT_MAX];
} swap_manager_t;

static swap_manager_t swapm;

extern int ide_read_sectors(uint32_t lba, uint8_t *buffer, uint32_t count);
extern int ide_write_sectors(uint32_t lba, const uint8_t *buffer, uint32_t count);

void swap_init(void)
{
    memset(&swapm, 0, sizeof(swapm));
    swapm.initialized = 1;
    swapm.enabled = 0;
    swapm.total_slots = SWAP_SLOT_MAX;
    swapm.used_slots = 0;
    swapm.disk_mode = 0;

    /* V6.77: 分配8MB swap区域 */
    swapm.swap_area_addr = mm_alloc_pages(2048);
    swapm.swap_area_size = 2048 * PAGE_SIZE;

    if (swapm.swap_area_addr) {
        swapm.enabled = 1;
        vga_printf("[SWAP] init: %dKB memory-backed (%d slots)\n",
                   swapm.swap_area_size / 1024, SWAP_SLOT_MAX);
        extern void serial_puts(uint16_t, const char *);
        serial_puts(0x3F8, "[SWAP] init OK (8MB)\n");
    } else {
        /* 降级到4MB */
        swapm.swap_area_addr = mm_alloc_pages(1024);
        swapm.swap_area_size = 1024 * PAGE_SIZE;
        if (swapm.swap_area_addr) {
            swapm.enabled = 1;
            swapm.total_slots = 1024;
            vga_printf("[SWAP] init: %dKB (degraded, %d slots)\n",
                       swapm.swap_area_size / 1024, 1024);
            extern void serial_puts(uint16_t, const char *);
            serial_puts(0x3F8, "[SWAP] init degraded (4MB)\n");
        } else {
            vga_puts("[SWAP] alloc failed, swap disabled\n");
            extern void serial_puts(uint16_t, const char *);
            serial_puts(0x3F8, "[SWAP] init FAILED\n");
        }
    }
}

int swap_init_disk(uint32_t dev_id, uint64_t start_lba, uint64_t total_lba)
{
    if (!swapm.initialized) return -1;
    swap_disk_header_t hdr;
    if (ide_read_sectors((uint32_t)start_lba, (uint8_t *)&hdr, 8) < 8) return -2;
    if (hdr.magic != SWAP_HEADER_MAGIC) return -3;

    if (swapm.swap_area_addr) {
        mm_free_pages(swapm.swap_area_addr, swapm.swap_area_size / PAGE_SIZE);
        swapm.swap_area_addr = 0;
    }
    swapm.disk_mode = 1;
    swapm.disk_dev = dev_id;
    swapm.disk_start_lba = start_lba;
    swapm.disk_total_lba = total_lba;
    swapm.total_slots = hdr.total_slots;
    swapm.used_slots = hdr.used_slots;
    swapm.enabled = 1;
    vga_printf("[SWAP] disk swap: %d slots (%dMB)\n",
               swapm.total_slots, swapm.total_slots * 4 / 1024);
    return 0;
}

int swap_format_disk(uint32_t dev_id, uint64_t start_lba, uint64_t total_lba)
{
    swap_disk_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = SWAP_HEADER_MAGIC;
    hdr.version = 2;
    hdr.page_size = PAGE_SIZE;
    uint64_t available_lba = total_lba - 8;
    hdr.total_slots = (uint32_t)(available_lba / 8);
    if (hdr.total_slots > SWAP_SLOT_MAX) hdr.total_slots = SWAP_SLOT_MAX;
    hdr.used_slots = 0;
    if (ide_write_sectors((uint32_t)start_lba, (const uint8_t *)&hdr, 8) < 8) return -1;
    vga_printf("[SWAP] disk formatted: %d slots\n", hdr.total_slots);
    return 0;
}

int swap_set_enabled(uint8_t enable)
{
    if (!swapm.initialized) return -1;
    swapm.enabled = enable ? 1 : 0;
    return 0;
}

static int swap_find_free_slot(void)
{
    for (int i = 0; i < SWAP_SLOT_MAX; i++) {
        if (!swapm.slots[i].used) return i;
    }
    return -1;
}

int swap_out(uint32_t phys_addr)
{
    if (!swapm.initialized || !swapm.enabled) return -1;
    if (swapm.used_slots >= swapm.total_slots) return -2;

    int slot = swap_find_free_slot();
    if (slot < 0) return -2;

    if (swapm.disk_mode) {
        uint64_t data_lba = swapm.disk_start_lba + 8;
        uint64_t slot_lba = data_lba + (uint64_t)slot * 8;
        if (ide_write_sectors((uint32_t)slot_lba, (const uint8_t *)phys_addr, 8) < 8) return -4;
    } else {
        uint32_t swap_offset = slot * PAGE_SIZE;
        if (swap_offset + PAGE_SIZE > swapm.swap_area_size) return -3;
        memcpy((void *)(swapm.swap_area_addr + swap_offset),
               (void *)phys_addr, PAGE_SIZE);
    }

    swapm.slots[slot].used = 1;
    swapm.slots[slot].dirty = 1;
    swapm.slots[slot].page_phys = phys_addr;
    swapm.slots[slot].age = swapm.swap_out_count;
    swapm.used_slots++;
    swapm.swap_out_count++;
    mm_free_page(phys_addr);
    return slot;
}

uint32_t swap_in(int slot)
{
    if (!swapm.initialized || !swapm.enabled) return 0;
    if (slot < 0 || slot >= SWAP_SLOT_MAX) return 0;
    if (!swapm.slots[slot].used) return 0;

    uint32_t new_phys = mm_alloc_page();
    if (!new_phys) return 0;

    if (swapm.disk_mode) {
        uint64_t data_lba = swapm.disk_start_lba + 8;
        uint64_t slot_lba = data_lba + (uint64_t)slot * 8;
        if (ide_read_sectors((uint32_t)slot_lba, (uint8_t *)new_phys, 8) < 8) {
            mm_free_page(new_phys);
            return 0;
        }
    } else {
        uint32_t swap_offset = slot * PAGE_SIZE;
        memcpy((void *)new_phys,
               (void *)(swapm.swap_area_addr + swap_offset), PAGE_SIZE);
    }

    swapm.slots[slot].used = 0;
    swapm.slots[slot].dirty = 0;
    swapm.used_slots--;
    swapm.swap_in_count++;
    return new_phys;
}

int swap_is_enabled(void)
{
    return swapm.initialized && swapm.enabled;
}

void swap_stats(uint32_t *total_slots, uint32_t *used_slots,
                uint32_t *swap_in, uint32_t *swap_out)
{
    if (total_slots) *total_slots = swapm.total_slots;
    if (used_slots) *used_slots = swapm.used_slots;
    if (swap_in) *swap_in = swapm.swap_in_count;
    if (swap_out) *swap_out = swapm.swap_out_count;
}

uint32_t swap_total_kb(void)
{
    return swapm.total_slots * PAGE_SIZE / 1024;
}

uint32_t swap_used_kb(void)
{
    return swapm.used_slots * PAGE_SIZE / 1024;
}

int swap_is_disk_mode(void)
{
    return swapm.disk_mode;
}

int swap_swapout_until(uint32_t target_free_pages)
{
    int swapped = 0;
    while (free_pages_now() < target_free_pages) {
        uint32_t page = reclaim_reclaim_page();
        if (!page) break;
        if (swap_out(page) < 0) {
            mm_free_page(page);
            break;
        }
        swapped++;
        if ((uint32_t)swapped > swapm.total_slots) break;
    }
    return swapped;
}

/* ============================================================================
 * V6.78: 磁盘swap分区自动检测
 * ========================================================================== */

/* MBR分区类型: Linux swap = 0x82 */
#define MBR_TYPE_LINUX_SWAP 0x82

/* 自动检测并挂载磁盘swap分区 */
int swap_auto_detect(void)
{
    if (!swapm.initialized) return -1;

    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[SWAP] auto-detecting disk swap...\n");

    /* 读取MBR */
    uint8_t mbr_buf[512];
    if (ide_read_sectors(0, mbr_buf, 1) < 1) {
        serial_puts(0x3F8, "[SWAP] failed to read MBR\n");
        return -2;
    }

    /* 检查MBR签名 */
    if (mbr_buf[510] != 0x55 || mbr_buf[511] != 0xAA) {
        serial_puts(0x3F8, "[SWAP] no MBR signature, skip\n");
        return -3;
    }

    /* 解析MBR分区表 */
    for (int i = 0; i < 4; i++) {
        uint32_t offset = 446 + i * 16;
        uint8_t part_type = mbr_buf[offset + 4];
        uint32_t start_lba = *(uint32_t *)&mbr_buf[offset + 8];
        uint32_t size_sectors = *(uint32_t *)&mbr_buf[offset + 12];

        if (part_type == 0 || size_sectors == 0) continue;

        /* 检测Linux swap分区类型 */
        if (part_type == MBR_TYPE_LINUX_SWAP) {
            serial_puts(0x3F8, "[SWAP] found Linux swap partition (type 0x82)\n");
            /* 尝试挂载 */
            if (swap_init_disk(0, start_lba, size_sectors) == 0) {
                serial_puts(0x3F8, "[SWAP] disk swap mounted successfully\n");
                return 0;
            }
        }

        /* 检测Hepu swap分区（通过魔数） */
        uint8_t header_buf[512];
        if (ide_read_sectors(start_lba, header_buf, 1) >= 1) {
            uint32_t magic = *(uint32_t *)header_buf;
            if (magic == SWAP_HEADER_MAGIC) {
                serial_puts(0x3F8, "[SWAP] found Hepu swap partition (magic)\n");
                if (swap_init_disk(0, start_lba, size_sectors) == 0) {
                    serial_puts(0x3F8, "[SWAP] Hepu disk swap mounted\n");
                    return 0;
                }
            }
        }
    }

    serial_puts(0x3F8, "[SWAP] no disk swap partition found, using memory swap\n");
    return -4;  /* 未找到swap分区，继续使用内存swap */
}
