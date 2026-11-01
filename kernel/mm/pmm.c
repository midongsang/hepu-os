/* ============================================================================
 * 赫普OS - 物理内存管理 (V0.95深化)
 * 实现：位图式物理页分配器 + 多页连续分配 + 内存区域保留
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "mm.h"
#include "vga.h"

#define PAGE_SIZE   4096
#define PAGE_ALIGN(addr) (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGN_DOWN(addr) ((addr) & ~(PAGE_SIZE - 1))

/* 物理内存位图：每一位代表一个4KB页是否被占用。
 * V3.89-E23: 位图存储改自属 static——原实现由 mm_init 传入固定物理地址
 * 0x00200000, 与内核镜像/BSS 清零区可能重叠(模块增多后 BSS 布局漂移,
 * 位图被清零覆盖 → mm_alloc_pages 扫描异常)。static 数组由链接器分配,
 * 彻底消除固定地址假设。
 * V6.76: 物理内存支持从256MB扩展到4GB (4GB/4KB=1M页, 1M/32=32K uint32=128KB) */
static uint32_t memory_bitmap_storage[(4096UL * 1024 * 1024 / 4096) / 32 + 8];
static uint32_t *memory_bitmap = NULL;
static uint32_t  total_pages = 0;
static uint32_t  used_pages = 0;
static uint32_t  memory_size_kb = 0;
static uint32_t  last_alloc_hint = 0;  /* 上次分配位置，加速搜索 */

/* V6.85: 页表页记录——paging_init登记所有页表物理页，mm_alloc_page必须跳过，
 * 否则slab/页表自身会被分配器返回并覆盖，导致页表项损坏、系统挂起。 */
#define MAX_PT_PAGES 1024
static uint32_t page_table_pages[MAX_PT_PAGES];
static int page_table_page_count = 0;

void mm_register_page_table_page(uint32_t phys)
{
    if (page_table_page_count < MAX_PT_PAGES) {
        page_table_pages[page_table_page_count++] = phys;
    }
}

static int is_page_table_page(uint32_t page)
{
    for (int i = 0; i < page_table_page_count; i++) {
        if (page_table_pages[i] / PAGE_SIZE == page) return 1;
    }
    return 0;
}

/* 内存区域类型 */
#define MEM_REGION_USABLE    0
#define MEM_REGION_RESERVED  1
#define MEM_REGION_KERNEL    2
#define MEM_REGION_MMIO      3
#define MEM_REGION_MAX       16

typedef struct {
    uint32_t start_page;
    uint32_t page_count;
    uint8_t  type;
} mem_region_t;

static mem_region_t mem_regions[MEM_REGION_MAX];
static uint32_t     mem_region_count = 0;

/* 检查页是否在保留区域内 */
int mm_is_page_reserved(uint32_t page)
{
    for (uint32_t i = 0; i < mem_region_count; i++) {
        if (page >= mem_regions[i].start_page &&
            page < mem_regions[i].start_page + mem_regions[i].page_count) {
            return 1;
        }
    }
    return 0;
}

/* 初始化内存管理 */
void mm_init(uint32_t mem_kb, uint32_t bitmap_addr)
{
    (void)bitmap_addr;   /* V3.89-E23: 忽略固定地址, 用自属 static 位图 */

    /* V7.77安全修复: 内存大小上限检查和溢出防护（漏洞Z）
     * 原代码 total_pages = (mem_kb * 1024) / PAGE_SIZE 可能溢出，
     * 且无上限检查，大内存机器可能越界写位图。
     * 位图大小为4GB (4GB/4KB/32 = 32768 uint32)。 */
    #define MAX_PHYS_MEM_KB (4096UL * 1024)  /* 4GB */
    if (mem_kb > MAX_PHYS_MEM_KB) {
        vga_printf("[MM] WARNING: mem %d KB exceeds 4GB limit, clamping\n", mem_kb);
        mem_kb = MAX_PHYS_MEM_KB;
    }
    memory_size_kb = mem_kb;

    /* 防止mem_kb * 1024溢出（4GB时=4194304*1024=4294967296，刚好溢出uint32） */
    uint64_t total_bytes = (uint64_t)mem_kb * 1024;
    total_pages = (uint32_t)(total_bytes / PAGE_SIZE);

    /* 确保total_pages不超过位图容量 */
    uint32_t max_bitmap_pages = (sizeof(memory_bitmap_storage) / sizeof(uint32_t)) * 32;
    if (total_pages > max_bitmap_pages) {
        vga_printf("[MM] WARNING: total_pages %d exceeds bitmap %d, clamping\n",
                   total_pages, max_bitmap_pages);
        total_pages = max_bitmap_pages;
    }

    memory_bitmap = memory_bitmap_storage;

    /* 初始全部标记为可用 */
    uint32_t bitmap_words = (total_pages + 31) / 32;
    for (uint32_t i = 0; i < bitmap_words; i++) {
        memory_bitmap[i] = 0;
    }
    used_pages = 0;
    last_alloc_hint = 0;
    mem_region_count = 0;

    vga_printf("[MM] physmem: %d KB, total pages count: %d, bitmap words: %d\n",
               mem_kb, total_pages, bitmap_words);
}

/* 注册内存区域（用于保留内核/MMIO区域） */
int mm_add_region(uint32_t start_addr, uint32_t size, uint8_t type)
{
    if (mem_region_count >= MEM_REGION_MAX) return -1;
    uint32_t start_page = start_addr / PAGE_SIZE;
    uint32_t page_count = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    mem_regions[mem_region_count].start_page = start_page;
    mem_regions[mem_region_count].page_count = page_count;
    mem_regions[mem_region_count].type = type;
    mem_region_count++;

    /* 如果位图已初始化，立即标记保留区域 */
    if (memory_bitmap && type != MEM_REGION_USABLE) {
        for (uint32_t p = 0; p < page_count; p++) {
            uint32_t page = start_page + p;
            if (page >= total_pages) break;
            uint32_t idx = page / 32;
            uint32_t bit = page % 32;
            if (!(memory_bitmap[idx] & (1 << bit))) {
                memory_bitmap[idx] |= (1 << bit);
                used_pages++;
            }
        }
    }

    vga_printf("[MM] region domains: 0x%x-0x%x type =%d pages count =%d\n",
               start_addr, start_addr + size, type, page_count);
    return 0;
}

/* 分配一个物理页，返回物理地址 */
uint32_t mm_alloc_page(void)
{
    /* 从上次位置开始搜索，加速分配 */
    for (uint32_t i = 0; i < total_pages; i++) {
        uint32_t page = (last_alloc_hint + i) % total_pages;
        /* V3.86: 双保险——跳过保留区(内核映像/内核堆)的页. */
        if (mm_is_page_reserved(page)) {
            last_alloc_hint = page + 1;
            continue;
        }
        /* V6.85: 跳过页表页，防止覆盖页表 */
        if (is_page_table_page(page)) {
            last_alloc_hint = page + 1;
            continue;
        }
        uint32_t idx = page / 32;
        uint32_t bit = page % 32;
        if (!(memory_bitmap[idx] & (1 << bit))) {
            memory_bitmap[idx] |= (1 << bit);
            used_pages++;
            last_alloc_hint = page + 1;
            return page * PAGE_SIZE;
        }
    }
    return 0; /* 内存不足 */
}

/* V6.85: 标记物理页为已占用（用于页表页等特殊页） */
void mm_mark_page_used(uint32_t phys)
{
    if (!memory_bitmap) return;
    uint32_t page = phys / PAGE_SIZE;
    if (page >= total_pages) return;
    uint32_t idx = page / 32;
    uint32_t bit = page % 32;
    if (!(memory_bitmap[idx] & (1 << bit))) {
        memory_bitmap[idx] |= (1 << bit);
        used_pages++;
    }
}

/* 分配连续的多页，返回起始物理地址 */
uint32_t mm_alloc_pages(uint32_t count)
{
    if (count == 0) return 0;
    if (count == 1) return mm_alloc_page();

    for (uint32_t start = 0; start <= total_pages - count; start++) {
        /* 检查连续count页是否都空闲 */
        uint32_t found = 1;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t page = start + i;
            if (mm_is_page_reserved(page)) {   /* V3.86: 跳过保留区 */
                found = 0;
                start += i;
                break;
            }
            uint32_t idx = page / 32;
            uint32_t bit = page % 32;
            if (memory_bitmap[idx] & (1 << bit)) {
                found = 0;
                start += i;  /* 跳过已检查的区域 */
                break;
            }
        }
        if (found) {
            /* 标记所有页为已占用 */
            for (uint32_t i = 0; i < count; i++) {
                uint32_t page = start + i;
                uint32_t idx = page / 32;
                uint32_t bit = page % 32;
                memory_bitmap[idx] |= (1 << bit);
                used_pages++;
            }
            last_alloc_hint = start + count;
            return start * PAGE_SIZE;
        }
    }
    return 0;
}

/* 在指定地址分配页（用于内存映射IO等） */
int mm_alloc_page_at(uint32_t phys_addr)
{
    uint32_t page = phys_addr / PAGE_SIZE;
    if (page >= total_pages) return -1;
    uint32_t idx = page / 32;
    uint32_t bit = page % 32;
    if (memory_bitmap[idx] & (1 << bit)) return -1;  /* 已占用 */
    memory_bitmap[idx] |= (1 << bit);
    used_pages++;
    return 0;
}

/* 释放物理页 */
void mm_free_page(uint32_t phys_addr)
{
    uint32_t page = phys_addr / PAGE_SIZE;
    if (page >= total_pages) return;
    if (mm_is_page_reserved(page)) return;  /* 保留区域不释放 */
    uint32_t idx = page / 32;
    uint32_t bit = page % 32;
    if (memory_bitmap[idx] & (1 << bit)) {
        memory_bitmap[idx] &= ~(1 << bit);
        used_pages--;
        if (page < last_alloc_hint) last_alloc_hint = page;
    }
}

/* 释放连续多页 */
void mm_free_pages(uint32_t phys_addr, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        mm_free_page(phys_addr + i * PAGE_SIZE);
    }
}

/* 保留一页（标记为已占用但不返回地址） */
int mm_reserve_page(uint32_t phys_addr)
{
    return mm_alloc_page_at(phys_addr);
}

/* 获取内存统计 */
void mm_stats(uint32_t *total, uint32_t *used, uint32_t *free)
{
    if (total) *total = total_pages;
    if (used)  *used = used_pages;
    if (free)  *free = total_pages - used_pages;
}

/* 获取可用内存字节数 */
uint32_t mm_free_bytes(void)
{
    return (total_pages - used_pages) * PAGE_SIZE;
}

/* 打印内存区域信息 */
void mm_dump_regions(void)
{
    vga_printf("[MM] mem region(%d):\n", mem_region_count);
    for (uint32_t i = 0; i < mem_region_count; i++) {
        const char *type_name = "unknown";
        switch (mem_regions[i].type) {
            case MEM_REGION_USABLE:   type_name = "usable"; break;
            case MEM_REGION_RESERVED: type_name = "reserved"; break;
            case MEM_REGION_KERNEL:   type_name = "kernel"; break;
            case MEM_REGION_MMIO:     type_name = "MMIO"; break;
        }
        vga_printf(" [%d] 0x%x-0x%x(%d pages) %s\n", i,
                   mem_regions[i].start_page * PAGE_SIZE,
                   (mem_regions[i].start_page + mem_regions[i].page_count) * PAGE_SIZE,
                   mem_regions[i].page_count, type_name);
    }
}
