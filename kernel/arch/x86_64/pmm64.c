/* ============================================================================
 * 赫普OS - 64位物理内存管理（增强版）
 * V0.10.14: 支持1GB内存、多区域管理、内存统计
 * V0.10.99: P4-3大内存支持，扩展到2GB
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "pmm64.h"

/* 页面大小 */
#define PAGE_SIZE 4096
#define PAGE_SIZE_2M (2 * 1024 * 1024)

/* 最大支持内存：2GB = 524288页，位图需要65536字节（64KB） */
#define MAX_MEMORY_PAGES 524288
/* 安全：强制 uint64 运算。旧写法按 int 计算 524288*4096=2^31 溢出为
 * 负数，使 pmm64_init 的内存上限钳制失效（大内存下固定位图越界），
 * pmm64_get_max_memory_mb 也会返回错误的巨大值。 */
#define MAX_MEMORY_SIZE ((uint64_t)MAX_MEMORY_PAGES * PAGE_SIZE)

/* 内存区域类型 */
typedef enum {
    MEM_REGION_FREE = 1,
    MEM_REGION_RESERVED = 2,
    MEM_REGION_ACPI = 3,
    MEM_REGION_KERNEL = 4,
} mem_region_type_t;

/* 内存区域 */
typedef struct {
    uint64_t base;
    uint64_t length;
    mem_region_type_t type;
} mem_region_t;

#define MAX_MEM_REGIONS 16

/* 物理内存管理结构 */
typedef struct {
    uint64_t memory_size;      /* 总内存大小（字节） */
    uint64_t total_pages;      /* 总页面数 */
    uint64_t used_pages;       /* 已使用页面数 */
    uint64_t free_pages;       /* 空闲页面数 */
    uint64_t reserved_pages;   /* 保留页面数 */
    uint8_t *bitmap;           /* 页面位图（1=已使用，0=空闲） */
    uint64_t bitmap_pages;     /* 位图占用的页面数 */
    uint64_t next_free;        /* 下一个空闲页面索引 */
    uint64_t alloc_count;      /* 分配次数统计 */
    uint64_t free_count;       /* 释放次数统计 */
    mem_region_t regions[MAX_MEM_REGIONS];
    int region_count;
} pmm64_t;

static pmm64_t pmm64;

/* COW引用计数：简单哈希表（物理页号 -> count） */
#define COW_HASH_SIZE 1024
typedef struct { uint64_t pfn; int count; } cow_entry_t;
static cow_entry_t cow_table[COW_HASH_SIZE];

void cow_ref(uint64_t phys) {
    uint64_t pfn = phys >> 12;
    uint32_t h = pfn % COW_HASH_SIZE;
    for (int i = 0; i < COW_HASH_SIZE; i++) {
        uint32_t idx = (h + i) % COW_HASH_SIZE;
        if (cow_table[idx].count == 0) { cow_table[idx].pfn = pfn; cow_table[idx].count = 1; return; }
        if (cow_table[idx].pfn == pfn) { cow_table[idx].count++; return; }
    }
}
void cow_unref(uint64_t phys) {
    uint64_t pfn = phys >> 12;
    uint32_t h = pfn % COW_HASH_SIZE;
    for (int i = 0; i < COW_HASH_SIZE; i++) {
        uint32_t idx = (h + i) % COW_HASH_SIZE;
        if (cow_table[idx].pfn == pfn && cow_table[idx].count > 0) { cow_table[idx].count--; if (cow_table[idx].count == 0) cow_table[idx].pfn = 0; return; }
    }
}
int cow_count(uint64_t phys) {
    uint64_t pfn = phys >> 12;
    uint32_t h = pfn % COW_HASH_SIZE;
    for (int i = 0; i < COW_HASH_SIZE; i++) {
        uint32_t idx = (h + i) % COW_HASH_SIZE;
        if (cow_table[idx].pfn == pfn && cow_table[idx].count > 0) return cow_table[idx].count;
    }
    return 0;
}

/* COW页错误处理：复制物理页，恢复W位 */
int cow_handle_fault(uint64_t cr2) {
    extern uint64_t vmm64_phys_to_virt(uint64_t);
    extern uint64_t pmm64_alloc_page(void);
    uint64_t cr3; __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *pml4 = (uint64_t*)vmm64_phys_to_virt(cr3 & 0xFFFFFFFFF000ULL);
    int pi = cr2 >> 39 & 0x1FF;
    if (!(pml4[pi] & 1)) return -1;
    uint64_t *pdpt = (uint64_t*)vmm64_phys_to_virt(pml4[pi] & 0xFFFFFFFFF000ULL);
    int di = cr2 >> 30 & 0x1FF;
    if (!(pdpt[di] & 1)) return -1;
    uint64_t *pd = (uint64_t*)vmm64_phys_to_virt(pdpt[di] & 0xFFFFFFFFF000ULL);
    int ti = cr2 >> 21 & 0x1FF;
    if (!(pd[ti] & 1) || (pd[ti] & 0x80)) return -1;
    uint64_t *pt = (uint64_t*)vmm64_phys_to_virt(pd[ti] & 0xFFFFFFFFF000ULL);
    int si = cr2 >> 12 & 0x1FF;
    uint64_t old_pte = pt[si];
    if (!(old_pte & 1)) return -1;
    /* 分配新页，复制内容 */
    uint64_t new_page = pmm64_alloc_page();
    if (!new_page) return -1;
    uint64_t old_phys = old_pte & 0xFFFFFFFFF000ULL;
    __builtin_memcpy((void*)vmm64_phys_to_virt(new_page),
                     (void*)vmm64_phys_to_virt(old_phys), 4096);
    /* 更新PTE：新物理页 + W位。
       V0.54.12(R4 MM-01): 掩码补保留 bit63 NX——旧实现 (old_pte&0xFFF) 丢弃
       NX，COW 后用户页变可执行，构成 DEP 绕过；与 vmm64_try_cow 口径对齐。 */
    pt[si] = (old_pte & 0xFFF) | (old_pte & (1ULL << 63)) | new_page | 0x2;
    cow_unref(old_phys);
    return 0;
}

/* 静态位图数组（管理最多2GB = 524288页，需要65536字节 = 16页） */
static uint8_t pmm64_static_bitmap[65536] __attribute__((aligned(4096)));

/* 添加内存区域 */
static void pmm64_add_region(uint64_t base, uint64_t length, mem_region_type_t type)
{
    if (pmm64.region_count >= MAX_MEM_REGIONS) return;
    pmm64.regions[pmm64.region_count].base = base;
    pmm64.regions[pmm64.region_count].length = length;
    pmm64.regions[pmm64.region_count].type = type;
    pmm64.region_count++;
}

/* 初始化物理内存管理
 * 参数：
 *   memory_size - 总内存大小（字节）
 *   bitmap_addr - 位图存放地址（已忽略，使用静态数组）
 */
int pmm64_init(uint64_t memory_size, uint64_t bitmap_addr)
{
    (void)bitmap_addr;  /* 使用静态数组，忽略此参数 */

    /* 限制最大内存 */
    if (memory_size > MAX_MEMORY_SIZE) {
        memory_size = MAX_MEMORY_SIZE;
    }

    pmm64.memory_size = memory_size;
    pmm64.total_pages = memory_size / PAGE_SIZE;
    pmm64.used_pages = 0;
    pmm64.free_pages = 0;
    pmm64.reserved_pages = 0;
    pmm64.next_free = 0;
    pmm64.alloc_count = 0;
    pmm64.free_count = 0;
    pmm64.region_count = 0;

    /* 使用静态位图数组 */
    pmm64.bitmap = pmm64_static_bitmap;
    pmm64.bitmap_pages = (sizeof(pmm64_static_bitmap) + PAGE_SIZE - 1) / PAGE_SIZE;

    /* 清零位图 */
    for (uint64_t i = 0; i < sizeof(pmm64_static_bitmap); i++) {
        pmm64.bitmap[i] = 0;
    }

    /* 标记所有页面为保留（先全部保留，再标记可用区域） */
    for (uint64_t i = 0; i < pmm64.total_pages; i++) {
        pmm64.bitmap[i / 8] |= (1 << (i % 8));
        pmm64.reserved_pages++;
    }

    /* 外部符号：内核结束地址 */
    extern char _end;

    /* 标记可用内存区域 */
    /* 区域1: 0x100000 (1MB) 到内核结束 - 内核占用 */
    uint64_t kernel_start = 0x100000;
    uint64_t kernel_end = (uint64_t)&_end;
    if (kernel_end < 0x100000 + 0x20000) kernel_end = 0x100000 + 0x20000; /* 至少128KB */

    /* 区域2: 内核结束到内存结束 - 可用
       FIX: free_start 必须向上对齐到页边界，否则内核 _end 所在页（可能包含
       stage2 页表/PML4 等引导数据结构）会被整页误标空闲，随后被分配清零，
       造成内核页表被覆盖（TLB 缓存掩盖到首个未缓存地址才崩溃）。 */
    uint64_t free_start = (kernel_end + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t free_end = memory_size;
    if (free_end > 0x207000) free_end = free_end; /* stage2页表在0x200000-0x206FFF */

    /* 标记可用区域 */
    uint64_t free_start_page = free_start / PAGE_SIZE;
    uint64_t free_end_page = free_end / PAGE_SIZE;
    for (uint64_t i = free_start_page; i < free_end_page && i < pmm64.total_pages; i++) {
        /* 跳过stage2页表区域 */
        uint64_t addr = i * PAGE_SIZE;
        if (addr >= 0x200000 && addr < 0x207000) continue;
        pmm64.bitmap[i / 8] &= ~(1 << (i % 8));
        pmm64.reserved_pages--;
        pmm64.free_pages++;
    }

    /* 记录内存区域 */
    pmm64_add_region(0, 0x100000, MEM_REGION_RESERVED); /* 低1MB */
    pmm64_add_region(kernel_start, kernel_end - kernel_start, MEM_REGION_KERNEL);
    pmm64_add_region(0x200000, 0x7000, MEM_REGION_RESERVED); /* stage2页表 */
    pmm64_add_region(free_start, free_end - free_start, MEM_REGION_FREE);

    /* 查找第一个空闲页面 */
    pmm64.next_free = free_start_page;
    while (pmm64.next_free < pmm64.total_pages) {
        if (!(pmm64.bitmap[pmm64.next_free / 8] & (1 << (pmm64.next_free % 8)))) {
            break;
        }
        pmm64.next_free++;
    }

    return 0;
}

/* 分配一个物理页面
 * 返回：物理地址，0表示失败
 */
uint64_t pmm64_alloc_page(void)
{
    if (pmm64.free_pages == 0) {
        return 0;
    }

    /* 从next_free开始查找空闲页面 */
    for (uint64_t i = pmm64.next_free; i < pmm64.total_pages; i++) {
        if (!(pmm64.bitmap[i / 8] & (1 << (i % 8)))) {
            pmm64.bitmap[i / 8] |= (1 << (i % 8));
            pmm64.used_pages++;
            pmm64.free_pages--;
            pmm64.alloc_count++;
            pmm64.next_free = i + 1;
            return i * PAGE_SIZE;
        }
    }

    /* 从头开始查找 */
    for (uint64_t i = 0; i < pmm64.next_free; i++) {
        if (!(pmm64.bitmap[i / 8] & (1 << (i % 8)))) {
            pmm64.bitmap[i / 8] |= (1 << (i % 8));
            pmm64.used_pages++;
            pmm64.free_pages--;
            pmm64.alloc_count++;
            pmm64.next_free = i + 1;
            return i * PAGE_SIZE;
        }
    }

    return 0;
}

/* 保留一段物理地址（标记为已用，分配器不会再发出去）。
 * V0.33：内核栈是 .bss 里的静态数组，必须显式保留 ——
 * 否则它会被当成普通空闲页分配给用户进程栈，
 * 用户栈一写就覆盖内核栈（表现为 auxv 里出现内核地址）。 */
void pmm64_reserve_range(uint64_t base, uint64_t length)
{
    if (!length) return;
    uint64_t start = base / PAGE_SIZE;
    uint64_t end = (base + length + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t p = start; p < end && p < pmm64.total_pages; p++) {
        if (pmm64.bitmap[p / 8] & (1 << (p % 8))) continue;  /* 已经占着 */
        pmm64.bitmap[p / 8] |= (1 << (p % 8));
        pmm64.reserved_pages--;
        if (pmm64.free_pages) pmm64.free_pages--;
    }
}

/* 释放一个物理页面 */
void pmm64_free_page(uint64_t phys_addr)
{
    uint64_t page = phys_addr / PAGE_SIZE;
    if (page >= pmm64.total_pages) {
        return;
    }

    /* 检查是否已经是空闲的（避免重复释放） */
    if (!(pmm64.bitmap[page / 8] & (1 << (page % 8)))) {
        return; /* 已经是空闲的 */
    }

    pmm64.bitmap[page / 8] &= ~(1 << (page % 8));
    pmm64.used_pages--;
    pmm64.free_pages++;
    pmm64.free_count++;

    if (page < pmm64.next_free) {
        pmm64.next_free = page;
    }
}

/* 分配连续物理页面 */
uint64_t pmm64_alloc_pages(size_t count)
{
    if (count == 0) return 0;
    if (count == 1) return pmm64_alloc_page();

    if (count > pmm64.free_pages) return 0;

    uint64_t first = 0;
    size_t contiguous = 0;

    for (uint64_t i = 0; i < pmm64.total_pages; i++) {
        if (!(pmm64.bitmap[i / 8] & (1 << (i % 8)))) {
            if (contiguous == 0) first = i;
            contiguous++;
            if (contiguous == count) {
                for (size_t j = 0; j < count; j++) {
                    pmm64.bitmap[(first + j) / 8] |= (1 << ((first + j) % 8));
                    pmm64.used_pages++;
                    pmm64.free_pages--;
                }
                pmm64.alloc_count++;
                return first * PAGE_SIZE;
            }
        } else {
            contiguous = 0;
        }
    }

    return 0;
}

/* 释放连续物理页面 */
void pmm64_free_pages(uint64_t phys_addr, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        pmm64_free_page(phys_addr + i * PAGE_SIZE);
    }
}

/* 获取内存统计信息 */
void pmm64_get_stats(uint64_t *total, uint64_t *used, uint64_t *free)
{
    if (total) *total = pmm64.total_pages * PAGE_SIZE;
    if (used) *used = pmm64.used_pages * PAGE_SIZE;
    if (free) *free = pmm64.free_pages * PAGE_SIZE;
}

/* 获取详细内存统计 */
void pmm64_get_detailed_stats(uint64_t *total, uint64_t *used,
                               uint64_t *free, uint64_t *reserved,
                               uint64_t *alloc_count, uint64_t *free_count)
{
    if (total) *total = pmm64.total_pages * PAGE_SIZE;
    if (used) *used = pmm64.used_pages * PAGE_SIZE;
    if (free) *free = pmm64.free_pages * PAGE_SIZE;
    if (reserved) *reserved = pmm64.reserved_pages * PAGE_SIZE;
    if (alloc_count) *alloc_count = pmm64.alloc_count;
    if (free_count) *free_count = pmm64.free_count;
}

/* 获取内存大小（MB） */
uint64_t pmm64_get_memory_size_mb(void)
{
    return pmm64.memory_size / (1024 * 1024);
}

/* 获取最大支持内存（MB） */
uint64_t pmm64_get_max_memory_mb(void)
{
    return MAX_MEMORY_SIZE / (1024 * 1024);
}

/* 获取内存区域数量 */
int pmm64_get_region_count(void)
{
    return pmm64.region_count;
}

/* 获取内存区域信息 */
int pmm64_get_region(int index, uint64_t *base, uint64_t *length, int *type)
{
    if (index < 0 || index >= pmm64.region_count) return -1;
    if (base) *base = pmm64.regions[index].base;
    if (length) *length = pmm64.regions[index].length;
    if (type) *type = (int)pmm64.regions[index].type;
    return 0;
}

/* 检查地址是否在可用内存范围内 */
int pmm64_is_valid_address(uint64_t addr)
{
    uint64_t page = addr / PAGE_SIZE;
    if (page >= pmm64.total_pages) return 0;
    return 1;
}

/* V0.54.6: hepu_pmm_dump_regions 转发实现（64 位无 dump 接口，stub 空实现） */
void pmm64_dump_regions_impl(void) {
    /* stub: 将来扩展时在次打印内存区域信息 */
}

/* ============================================================================
 * 接口（操作表）
 * ========================================================================== */

/* 物理内存管理操作表 */
static const struct pmm_ops pmm64_ops = {
    .init = pmm64_init,
    .alloc_page = pmm64_alloc_page,
    .free_page = pmm64_free_page,
    .get_stats = pmm64_get_stats,
    .alloc_pages = pmm64_alloc_pages,
    .free_pages = pmm64_free_pages,
};

/* 获取默认的物理内存管理操作表 */
const struct pmm_ops *pmm64_get_ops(void)
{
    return &pmm64_ops;
}
