/* ============================================================================
 * 赫普OS 64位 - 交换区 (Swap) V0.13.59
 * - 槽位位图管理(页粒度 4096B)
 * - 后端: /rootfs/swap.hepu ext2 文件(懒扩展, 只写实际使用的页)
 * - 配合 vmm64 页换出原语: 内存压力时页面落盘换出
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "swap64.h"

#define SWAP_FILE "/swap.hepu"

static int g_swap_inited = 0;
static uint32_t g_swap_map[(SWAP_MAX_PAGES + 31) / 32];  /* 槽位图: 1=已用 */
static uint32_t g_swap_used = 0;

extern void *memset(void *dst, int val, unsigned long n);
extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t v);

extern int ext2_64_write_file_offset(const char *path, const uint8_t *data,
                                     uint32_t size, uint32_t offset);
extern int ext2_64_read_file_offset(const char *path, uint8_t *buffer,
                                    uint32_t max_size, uint32_t offset);
extern uint32_t ext2_64_lookup(const char *path);

/* 初始化(懒): 建 swap 文件(若不存在) */
int swap64_init(void)
{
    if (g_swap_inited) return 0;

    memset(g_swap_map, 0, sizeof(g_swap_map));
    g_swap_used = 0;

    /* 检查文件是否存在 */
    if (ext2_64_lookup(SWAP_FILE) == 0) {
        extern int ext2_64_create_file(const char *p);
        int rc = ext2_64_create_file(SWAP_FILE);
        if (rc != 0) {
            vga_puts("[SWAP] create file failed\n");
            return -1;
        }
        vga_puts("[SWAP] swap file created\n");
    }

    g_swap_inited = 1;
    vga_puts("[SWAP] ready (2MB, 512 slots)\n");
    return 0;
}

/* 分配槽 */
int swap64_alloc_slot(void)
{
    if (!g_swap_inited && swap64_init() != 0) return -1;

    for (uint32_t i = 0; i < SWAP_MAX_PAGES; i++) {
        if (!(g_swap_map[i / 32] & (1u << (i % 32)))) {
            g_swap_map[i / 32] |= (1u << (i % 32));
            g_swap_used++;
            return (int)i;
        }
    }
    return -1;  /* swap 满 */
}

/* 释放槽 */
int swap64_free_slot(int slot)
{
    if (!g_swap_inited || slot < 0 || slot >= SWAP_MAX_PAGES) return -1;
    if (!(g_swap_map[slot / 32] & (1u << (slot % 32)))) return -1;
    g_swap_map[slot / 32] &= ~(1u << (slot % 32));
    g_swap_used--;
    return 0;
}

/* 写一页到槽 */
int swap64_write(int slot, const uint8_t *page)
{
    if (!g_swap_inited || slot < 0 || slot >= SWAP_MAX_PAGES) return -1;
    if (!page) return -1;

    uint32_t file_off = (uint32_t)slot * SWAP_PAGE_SIZE;
    int r = ext2_64_write_file_offset(SWAP_FILE, page, SWAP_PAGE_SIZE, file_off);
    return (r == SWAP_PAGE_SIZE) ? 0 : -1;   /* 归一化为0=成功 */
}

/* 从槽读一页 */
int swap64_read(int slot, uint8_t *page)
{
    if (!g_swap_inited || slot < 0 || slot >= SWAP_MAX_PAGES) return -1;
    if (!page) return -1;

    uint32_t file_off = (uint32_t)slot * SWAP_PAGE_SIZE;
    int r = ext2_64_read_file_offset(SWAP_FILE, page, SWAP_PAGE_SIZE, file_off);
    return (r < 0) ? -1 : 0;
}

/* 统计 */
int swap64_get_stats(uint32_t *used, uint32_t *total)
{
    if (!g_swap_inited && swap64_init() != 0) return -1;
    if (used) *used = g_swap_used;
    if (total) *total = SWAP_MAX_PAGES;
    return 0;
}
